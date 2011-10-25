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

int server_port=3000; //"-p"
char server_ip[100]="0.0.0.0"; //"-h"
//options
bool bDebug=false;   //"-d"
bool bLimitOne=false;//"-s"
bool bCorrect=false; //"-f"

int max_client_num=0; //"-c 100"

int admin_server_port=0; //"-P"
char admin_server_ip[100]="0.0.0.0"; //"-H"
//
struct evarg;

//need to free
int serverFd=0;
map<int,evarg*> arg_map;
int in[2];
int out[2];

int p_argcount=0;
char* p_name=NULL;
char** p_argv=NULL;

pid_t catPid=0;
//need to free

bool catIsRunning=false;
bool dogIsLooping=false;

void _onRead(struct bufferevent *bev, void * _arg);
void _onError(struct bufferevent *bev, short events, void *_arg);
void _onAccept(int fd,short event,void *arg);
void _onReadOutput(struct bufferevent *bev, void * _arg);
void sendStr(int fd,const char*buff,int len);

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

struct evarg
{
	int fd;
	evbuffer* buf;
	bufferevent* ev;
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
void _onSignal(int sig,short event,void* arg)
{
	if(bDebug)
		cout<<"catch signal"<<sig<<endl;
#ifndef __APPLE__
	if(catPid&&sig!=SIGCHLD)
		kill(catPid,SIGKILL);
#endif
	catIsRunning=false;
	if(dogIsLooping)
		event_loopbreak();
}
bool Init(int argc,const char **argv)
{
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
				cout<<"cannot understand:"<<str<<endl;
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
						cout<<"-exec arg not enough\n";
						quit(-1);
					}
				}
				else if(!strcmp(str,"-p"))
				{
					++i;
					if(i>=argc)
					{
						cout<<"please enter your port"<<endl;
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
						cout<<"please enter your ip"<<endl;
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
	//free clients
	int size=arg_map.size();
	if(size)
	{
		map<int,evarg*>::iterator it=arg_map.begin();
		for(it;it!=arg_map.end();++it)
		{
			int fd=it->first;
			evarg *_arg=it->second;
			if(_arg)
				_onError(_arg->ev,-1,_arg);
		}
		arg_map.clear();
	}

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
	//close server
	if(serverFd)
	{
		close(serverFd);
		serverFd=0;
	}

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
				cout<<"execute dog error:"<<errno<<endl;
			}
			evbuffer_free(cmd);
			close(in[0]);
			close(out[1]);
			close(out[1]);
		}
		else if(pid>0)
		{
			catPid=pid;
			close(in[0]);
			close(out[1]);
			catIsRunning=true;
			InitEvent();
		}
		else
		{
			cout<<"fork failed"<<endl;
		}
	}
	else
	{
		cout<<"create pipe failed!"<<endl;
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
	if(!catIsRunning)return;
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

		if(!bLimitOne&&catIsRunning) //multi-mode && cat progress is running
		{
			sprintf(_buf,"d%d:1:\n",arg->fd);
			write(in[1],_buf,strlen(_buf));
		}

		if(events>0)
		{
			map<int,evarg*>::iterator it=arg_map.find(arg->fd);
			if(it!=arg_map.end())
			{
				arg_map.erase(it);
			}
		}
		if(bDebug)
		{
			if(arg->fd==out[0])
			{
				cout<<"cat quit"<<endl;
			}
			else
			{
				cout<<arg->fd<<" client quit"<<endl;
			}
		}
		evbuffer_free(arg->buf);
		close(arg->fd);
		free(arg);
		//bufferevent_free(bev);
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
		cout<<"server accept error"<<endl;
		quit(-1);
	}
	noblock(s);
	evarg *_arg=(evarg*)malloc(sizeof(evarg));
	_arg->buf=evbuffer_new();
	_arg->fd=s;
	struct bufferevent* client=bufferevent_new(s,_onRead,NULL,_onError,(void*)_arg);
	_arg->ev=client;
	arg_map.insert(pair<int,evarg*>(s,_arg));
	bufferevent_enable(client, EV_READ);
	if(!bLimitOne&&catIsRunning)
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
	if(fd==0&&arg_map.size())
	{
		map<int,evarg*>::iterator it=arg_map.begin();
		for(it;it!=arg_map.end();++it)
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
								map<int,evarg*>::iterator it=arg_map.find(fd);
								if(it!=arg_map.end())
								{
									evarg *_arg=it->second;

									if(_arg)
									{
										_onError(_arg->ev,0,_arg);
										cout<<"force close fd:"<<fd<<endl;
									}
									arg_map.erase(it);
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
						cout<<"fetch ouput format wrong:"<<data+newBase<<endl;
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
				if(bDebug)
					cout<<"fetch fd format wrong:"<<i<<" "<<data[i]<<endl;
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
		cout<<"init socket error"<<endl;
		quit(-1);
	}
	memset(&addr,0,sizeof(addr));
	addr.sin_family=AF_INET;
	addr.sin_port=htons(server_port);
	addr.sin_addr.s_addr=(inet_addr(server_ip));
	if(bind(fd,(sockaddr*)&addr,sizeof(addr))==-1)
	{
		cout<<"cannot bind port:"<<server_ip<<":"<<server_port<<endl;
		quit(-1);
	}
	if(listen(fd,5)==-1)
	{
		cout<<"cannot listen on port:"<<server_ip<<":"<<server_port<<endl;
		quit(-1);
	}
	else
	{
		cout<<"server is listenning on "<<server_ip<<":"<<server_port<<endl;
	}
	event_init();

	evarg *_arg=(evarg*)malloc(sizeof(evarg));
	_arg->buf=evbuffer_new();
	_arg->fd=out[0];
	struct bufferevent* pipe_buf=bufferevent_new(out[0],_onReadOutput,NULL,_onError,(void*)_arg);
	bufferevent_enable(pipe_buf, EV_READ);

	event server,signal_int,signal_chld;
	event_set(&server,fd,EV_READ,_onAccept,&server);
	event_add(&server, NULL);
	//int flags=fcntl(out[0],F_GETFL,0);
	//fcntl(out[0],F_SETFL,flags|O_NONBLOCK);
	event_set(&signal_int, SIGINT, EV_SIGNAL, _onSignal,NULL);
	event_set(&signal_chld, SIGCHLD, EV_SIGNAL, _onSignal,NULL);
	event_add(&signal_int, NULL);
	event_add(&signal_chld, NULL);
	cout<<"Init server OK"<<endl;
	serverFd=fd;
	dogIsLooping=true;
	event_dispatch();
	dogIsLooping=false;
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
