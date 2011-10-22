#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
//=====================
#include <arpa/inet.h>
#include <sys/socket.h>
#include <event.h>
//=====================
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <iostream>
#include <map>
//=====================
using namespace std;

int server_port=2012;
char server_ip[100]="0.0.0.0";

pid_t childPid=0;
bool bDebug=false;
int p_argcount=0;
char* p_name=NULL;
char** p_argv=NULL;
int in[2];
int out[2];
bool bLimitOne=false;
bool bCorrect=false;

int serverFd=0;

struct evarg
{
	int fd;
	evbuffer* buf;
	bufferevent* ev;
};
void _onRead(struct bufferevent *bev, void * _arg);
void _onError(struct bufferevent *bev, short events, void *_arg);
void _onAccept(int fd,short event,void *arg);
void _onReadOutput(struct bufferevent *bev, void * _arg);
void sendStr(int fd,const char*buff,int len);

map<int,evarg*> argMap;
const char* command[]=
{
	"-help","show help",
	"-e","-e program arg1 arg2 ...",
	"-p","-p 1000 (server port,default 2012)",
	"-h","-h 127.0.0.1 (server ip)",
	"-d","-d (debug mode)",
	"-s","-s (shell mode,just redirect stdin/stdout)",
	"-f","-f (change \\r\\n into \\n from client)",
};

void DeInit();
void InitEvent();

void noblock(int fd)
{
	int flags=fcntl(fd,F_GETFL,0);
	fcntl(fd,F_SETFL,flags|O_NONBLOCK);
}
void quit(int code)
{
	DeInit();
	exit(code);
}
void showHelp()
{
	cout<<"show help"<<endl;
	cout<<"author:vzex"<<endl;
	cout<<"mail:vzex@163.com"<<endl;
	int argsize=sizeof(command)/sizeof(char*);
	for(int i=0;i<argsize;i+=2)
	{
		cout<<command[i]<<"\t"<<command[i+1]<<endl;
	}
	quit(0);
}
void doNothing(int sig)
{
}
void onChildQuit(int sig)
{
	if(bDebug)
		cout<<"catch signal"<<sig<<endl;
#ifndef __APPLE__
	if(childPid&&sig!=SIGCHLD)
		kill(childPid,SIGKILL);
#endif
	int size=argMap.size();
	if(size)
	{
		map<int,evarg*>::iterator it=argMap.begin();
		for(it;it!=argMap.end();++it)
		{
			int fd=it->first;
			evarg *_arg=it->second;
			if(_arg)
				_onError(_arg->ev,-1,_arg);
		}
		argMap.clear();
	}
	event_loopbreak();
}
bool Init(int argc,const char **argv)
{
	signal(SIGCHLD,onChildQuit);
	signal(SIGINT,onChildQuit);
	//signal(SIGPIPE,SIG_IGN);
	int argsize=sizeof(command)/sizeof(char*);
	bool canRun=false;
	for(int i=1;i<argc;++i)
	{
		const char*str=(char*)argv[i];
		if(str[0]&&str[0]=='-')
		{
			bool valid=false;
			for(int k=0;k<argsize;k+=2)
			{
				if(!strcmp(command[k],str))
				{
					valid=true;
				}
			}
			if(!valid)
			{
				cerr<<"cannot understand:"<<str<<endl;
				showHelp();
			}
			else
			{
				if(!strcmp(str,"-e"))
				{
					if(i+1<argc)
					{
						int len=strlen(argv[i+1]);
						p_name=new char[len+1];
						strcpy(p_name,argv[i+1]);
						int _i=i+2;
						int count=argc-_i+1;
						if(count>0)
						{
							p_argv=new char*[count];
							p_argv[0]=new char[len+1];
							strcpy(p_argv[0],p_name);

							for(_i;_i<argc;++_i)
							{
								const char*arg=argv[_i];
								int _len=strlen(arg);
								p_argv[_i-i-1]=new char[_len+1];
								strcpy(p_argv[_i-i-1],arg);
							}
						}
						p_argcount=count;
						canRun=true;
						break;
					}
					else
					{
						cerr<<"-exec arg not enough\n";
						quit(-1);
					}
				}
				else if(!strcmp(str,"-p"))
				{
					++i;
					if(i>=argc)
					{
						cerr<<"please enter your port"<<endl;
						showHelp();
					}
					const char*arg=argv[i];
					server_port=atoi(arg);
				}
				else if(!strcmp(str,"-d"))
				{
					bDebug=true;
				}

				else if(!strcmp(str,"-h"))
				{
					++i;
					if(i>=argc)
					{
						cerr<<"please enter your ip"<<endl;
						showHelp();
					}
					const char*arg=argv[i];
					strcpy(server_ip,arg);

				}
				else if(!strcmp(str,"-s"))
				{
					bLimitOne=true;
				}
				else if(!strcmp(str,"-f"))
				{
					bCorrect=true;
				}
				else if(!strcmp(str,"-help"))
				{
					showHelp();
				}
			}
		}
		else
		{
			showHelp();
		}
	}
	return canRun;
}
void DeInit()
{
	if(p_name)
	{
		delete[] p_name;
		p_name=NULL;
	}
	if(p_argv)
	{
		for(int i=0;i<p_argcount;++i)
		{
			delete[] p_argv[i];
			p_argv[i]=NULL;
		}
		delete[] p_argv;
	}
	if(serverFd)
	{
		close(serverFd);
		serverFd=0;
	}
	argMap.clear();
	cout<<"free all"<<endl;
}
void ForkDog()
{
	if(pipe(in)==0&&pipe(out)==0)
	{
		pid_t pid=fork();
		if(pid==0)
		{
			if(bDebug)
				cout<<"execute\t"<<p_name<<endl;
			if(p_argv&&bDebug)
			{
				cout<<"args\t";
				for(int i=1;i<p_argcount;++i)
				{
					cout<<p_argv[i]<<"\t";
				}
				cout<<endl;
			}
			close(in[1]);
			close(out[0]);
			dup2(in[0],0);
			dup2(out[1],1);
			dup2(out[1],2);
			close(in[0]);
			close(out[1]);
			//if(execvp(p_name,p_argv)==-1)
			evbuffer *cmd=evbuffer_new();
			evbuffer_add(cmd,p_name,strlen(p_name));
			if(p_argv)
			{
				for(int i=1;i<p_argcount;++i)
				{
					evbuffer_add(cmd," ",1);
					evbuffer_add(cmd,p_argv[i],strlen(p_argv[i]));
				}
			}

			char *command=(char*)EVBUFFER_DATA(cmd);
			if(system(command))
			{
				cerr<<"execute dog error:"<<errno<<endl;
			}
			evbuffer_free(cmd);
			close(in[0]);
			close(out[1]);
			close(out[1]);
		}
		else if(pid>0)
		{
			childPid=pid;
			close(in[0]);
			close(out[1]);
			InitEvent();
		}
		else
		{
			cerr<<"fork failed"<<endl;
		}
	}
	else
	{
		cerr<<"create pipe failed!"<<endl;
	}
}

void fix(char*str,int& len)
{
	int offset=0;
	int i=0;
	for(i;i<len;++i)
	{
		if(str[i]=='\r')
		{
			offset=1;
		}
		if(offset)
		{

			if(i+offset<len)
			{

				int j;
				for(j=i+offset;j<len&&str[j]=='\r';++j){str[j]=0;};
				if(j>=len)
				{
					str[i]=0;
					break;
				}
				str[i]=str[j];
				offset=j-i;
			}
			else
			{
				str[i]=0;
				break;
			}
		}
	}
	len=i;
}

char _buf[255];
void _onRead(struct bufferevent *bev, void * _arg)
{
	evarg *arg=(evarg*)_arg;
	evbuffer* buf=arg->buf;
	int len;
	while((len=bufferevent_read(bev,_buf,sizeof(_buf)-1))>0)
	{
		_buf[len]=0;
		evbuffer_add(buf,_buf,len);
	}
	len=buf->off;
	int orilen=len;
	if(bCorrect)
	{
		fix((char*)EVBUFFER_DATA(buf),len);
		buf->off=len;
	}
	if(bDebug)
	{
		cout<<"recv from port:"<<len<<" "<<EVBUFFER_DATA(buf)<<endl;
	}
	if(bLimitOne)
	{
		write(in[1],(char*)EVBUFFER_DATA(buf),len);
		evbuffer_drain(buf,orilen);
	}
	else
	{
		sprintf(_buf,"r%d:%d:",arg->fd,len);
		write(in[1],_buf,strlen(_buf));
		evbuffer_write(buf,in[1]);
	}
}
void _onError(struct bufferevent *bev, short events, void *_arg)
{
	if(bDebug)
		cout<<"on error cb "<<events<<endl;
	if(events&EVBUFFER_EOF||events<=0){
		evarg *arg=(evarg*)_arg;

		map<int,evarg*>::iterator it=argMap.find(arg->fd);
		if(it!=argMap.end())
		{
			if(events>=0)
				argMap.erase(it);
		}
		else
		{
			return ;
		}
		if(!bLimitOne&&events>0)
		{
			sprintf(_buf,"d%d:1:\n",arg->fd);
			write(in[1],_buf,strlen(_buf));
		}

		evbuffer_free(arg->buf);
		close(arg->fd);
		free(arg);
		bufferevent_free(bev);
		cout<<"client quit"<<endl;
	}
}
void _onAccept(int fd,short event,void *arg)
{
	struct event *ev=(struct event*)arg;
	sockaddr addr;
	socklen_t len=sizeof(addr);

	int s=accept(fd,&addr,&len);
	if(s==-1)
	{
		cerr<<"server accept error"<<endl;
		quit(-1);
	}
	noblock(s);
	evarg *_arg=(evarg*)malloc(sizeof(evarg));
	_arg->buf=evbuffer_new();
	_arg->fd=s;
	struct bufferevent* client=bufferevent_new(s,_onRead,NULL,_onError,(void*)_arg);
	_arg->ev=client;
	argMap.insert(pair<int,evarg*>(s,_arg));
	//bufferevent_settimeout(client,0,1);
	bufferevent_enable(client, EV_READ);
	//cout<<"accept client from "<<inet_ntoa(((sockaddr_in*)&addr)->sin_addr)<<":"<<((sockaddr_in*)&addr)->sin_port<<endl;
	if(!bLimitOne)
	{
		char tmp[30];
		sprintf(tmp,"%s:%d\n",inet_ntoa(((sockaddr_in*)&addr)->sin_addr),((sockaddr_in*)&addr)->sin_port);
		sprintf(_buf,"c%d:%d:%s",_arg->fd,(int)strlen(tmp),tmp);
		write(in[1],_buf,strlen(_buf));
	}
	event_add(ev,NULL);
}
void _sendStr(int fd,const char*buff,int len)
{
	int n=0;
	while((n=write(fd,buff,len))>0||len)
	{
		if(n>0)
		{
			len-=n;
			if(len<=0)
			{
				break;
			}
		}
		if(n==-1)break;
	}
}
void sendStr(int fd,const char*buff,int len)
{
	if(fd==0&&argMap.size())
	{
		map<int,evarg*>::iterator it=argMap.begin();
		for(it;it!=argMap.end();++it)
		{
			evarg *_arg=it->second;
			int fd=_arg->fd;
			_sendStr(fd,buff,len);
		}
	}
	else
		_sendStr(fd,buff,len);

}
void _onReadOutput(struct bufferevent *bev, void * _arg)
{

	evarg *arg=(evarg*)_arg;
	evbuffer* buf=arg->buf;
	int len;
	while((len=bufferevent_read(bev,_buf,sizeof(_buf)-1))>0)
	{
		_buf[len]=0;
		evbuffer_add(buf,_buf,len);
	}
	len=buf->off;
	if(len)
	{
		char*data=(char*)EVBUFFER_DATA(buf);
		if(bDebug)
		{
			cout<<"return "<<data<<endl;
		}
		if(bLimitOne)
		{
			sendStr(0,data,len);
			evbuffer_drain(buf,len);
			return;
		}
		int base=0;
		int base_off=0;
		for(int i=base_off;i<len;++i)
		{
			if(data[i]==':')
			{
				data[i]=0;
				int fd=atoi(data+base);
				if(bDebug)
					cout<<"fectch fd "<<fd<<endl;
				int newBase=i+1;
				for(int j=newBase;j<len;++j)
				{
					if(data[j]==':')
					{
						const char *tail=data+j+1;
						data[j]=0;
						int datalen=atoi(data+newBase);
						int taillen=len-1-j;
						if(bDebug)
							cout<<"fetch len "<<datalen<<" "<<taillen<<" "<<tail<<endl;
						if(taillen>=datalen)
						{
							sendStr(fd,tail,datalen);
							evbuffer_drain(buf,j+1+datalen);
							if(base==base_off+1)
							{
								map<int,evarg*>::iterator it=argMap.find(fd);
								if(it!=argMap.end())
								{
									evarg *_arg=it->second;

									if(_arg)
									{
										_onError(_arg->ev,0,_arg);
										cout<<"force close fd:"<<fd<<endl;
									}
								}
							}
							len=buf->off;
							i=-1;
							base_off=i+1;
							data=(char*)EVBUFFER_DATA(buf);
							break;
						}
						else
						{
							return;
						}
					}
					else if(data[j]>'9'||data[j]<'0')
					{
						cerr<<"fetch ouput format wrong:"<<data+newBase<<endl;
						evbuffer_drain(buf,len);
						return;
					}
				}
			}
			else if(i==base_off&&data[i]=='-')
			{
				base=base_off+1;
			}
			else if(data[i]>'9'||data[i]<'0')
			{
				cerr<<"fetch fd format wrong:"<<i<<" "<<data[i]<<endl;
				evbuffer_drain(buf,len);
				break;
			}
		}
	}

	//evbuffer_write(buf,1);
}
void InitEvent()
{
	int fd;
	sockaddr_in addr;
	fd=socket(AF_INET,SOCK_STREAM,0);
	if(fd==-1)
	{
		cerr<<"init socket error"<<endl;
		quit(-1);
	}
	memset(&addr,0,sizeof(addr));
	addr.sin_family=AF_INET;
	addr.sin_port=htons(server_port);
	addr.sin_addr.s_addr=(inet_addr(server_ip));
	if(bind(fd,(sockaddr*)&addr,sizeof(addr))==-1)
	{
		cerr<<"cannot bind port:"<<server_ip<<":"<<server_port<<endl;
		quit(-1);
	}
	if(listen(fd,5)==-1)
	{
		cerr<<"cannot listen on port:"<<server_ip<<":"<<server_port<<endl;
		quit(-1);
	}
	event_init();

	evarg *_arg=(evarg*)malloc(sizeof(evarg));
	_arg->buf=evbuffer_new();
	_arg->fd=out[0];
	struct bufferevent* output=bufferevent_new(out[0],_onReadOutput,NULL,_onError,(void*)_arg);
	bufferevent_enable(output, EV_READ);

	event server;
	event_set(&server,fd,EV_READ,_onAccept,&server);
	event_add(&server, NULL);
	//int flags=fcntl(out[0],F_GETFL,0);
	//fcntl(out[0],F_SETFL,flags|O_NONBLOCK);
	cout<<"Init server OK"<<endl;
	serverFd=fd;
	event_dispatch();
	if(serverFd)
	{
		close(serverFd);
		serverFd=0;
		bufferevent_free(output);
	}
}
int main(int argc,char **argv)
{
	bool canRun=Init(argc,(const char**)argv);
	if(canRun)
	{
		ForkDog();
	}
	DeInit();
}
