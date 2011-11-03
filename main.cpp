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
bool b_debug=false;   //"-d"
bool b_broadcast=true;//"-m"
bool b_fix=false; //"-f"
//
struct evarg;

//need to free
int server_fd=0;
map<int,evarg*> arg_map;
int in[2];
int out[2];

int p_argcount=0;
char* p_name=NULL;
char** p_argv=NULL;

pid_t cat_pid=0;
//need to free

bool cat_running=false;
bool dog_looping=false;

void _on_read(struct bufferevent *bev, void * _arg);
void _on_error(struct bufferevent *bev, short events, void *_arg);
void _on_connected(int fd,short event,void *arg);
void _on_read_output(struct bufferevent *bev, void * _arg);
void _send_str(int fd,const char*buff,int len);


const char* command[]=
{
	"-help","show help",
	"-e","-e program arg1 arg2 ...",
	"-p","-p 3000 (server port,default 3000)",
	"-h","-h 127.0.0.1 (server ip)",
	"-d","-d (debug mode,with more output)",
	"-m","-m (multi-client mode,not just redirect stdin/stdout,but also identify clients with id)",
	"-f","-f (change \\r\\n into \\n from client)",
};

struct evarg
{
	int fd;
	evbuffer* buf;
	bufferevent* ev;
};

void de_init();
void init_server_event();

void _noblock(int fd)
{
	int flags=fcntl(fd,F_GETFL,0);
	fcntl(fd,F_SETFL,flags|O_NONBLOCK);
}
void quit(int code)
{
	de_init();
	exit(code);
}
void show_manual()
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
void _on_signal(int sig,short event,void* arg)
{
	if(b_debug)
		cout<<"catch signal"<<sig<<endl;
#ifndef __APPLE__
	if(cat_pid&&sig!=SIGCHLD)
		kill(cat_pid,SIGKILL);
#endif
	cat_running=false;
	if(dog_looping)
		event_loopbreak();
}
bool init(int argc,const char **argv)
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
				show_manual();
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
						show_manual();
					}
					const char*arg=argv[i];
					server_port=atoi(arg);
				}
				else if(!strcmp(str,"-d"))
				{
					b_debug=true;
				}

				else if(!strcmp(str,"-h"))
				{
					++i;
					if(i>=argc)
					{
						cout<<"please enter your ip"<<endl;
						show_manual();
					}
					const char*arg=argv[i];
					strcpy(server_ip,arg);

				}
				else if(!strcmp(str,"-m"))
				{
					b_broadcast=false;
				}
				else if(!strcmp(str,"-f"))
				{
					b_fix=true;
				}
				else if(!strcmp(str,"-help"))
				{
					show_manual();
				}
			}
		}
		else
		{
			show_manual();
		}
	}
	return canRun;
}
void de_init()
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
				_on_error(_arg->ev,-1,_arg);
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
	if(server_fd)
	{
		close(server_fd);
		server_fd=0;
	}

	cout<<"free all"<<endl;
}
void fork_cat()
{
	if(pipe(in)==0&&pipe(out)==0)
	{
		pid_t pid=fork();
		if(pid==0)
		{
			if(b_debug)
				cout<<"execute\t"<<p_name<<endl;
			if(p_argv&&b_debug)
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
			cat_pid=pid;
			close(in[0]);
			close(out[1]);
			cat_running=true;
                        evarg *_arg=(evarg*)malloc(sizeof(evarg));
                        _arg->buf=evbuffer_new();
                        _arg->fd=out[0];
                        struct bufferevent* pipe_buf=bufferevent_new(out[0],_on_read_output,NULL,_on_error,(void*)_arg);
                        bufferevent_enable(pipe_buf, EV_READ);
                        init_server_event();

			close(in[1]);
			close(out[0]);
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
void _on_read(struct bufferevent *bev, void * _arg)
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
	if(b_fix)
	{
		fix((char*)EVBUFFER_DATA(buf),len);
		buf->off=len;
	}
	if(b_debug)
	{
		cout<<"recv from port:"<<len<<" "<<EVBUFFER_DATA(buf)<<endl;
	}
	if(!cat_running)return;
	if(b_broadcast)
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
void _on_error(struct bufferevent *bev, short events, void *_arg)
{
	if(b_debug)
		cout<<"on error cb "<<events<<endl;
	if(events&EVBUFFER_EOF||events<=0){
		evarg *arg=(evarg*)_arg;

		if(!b_broadcast&&cat_running&&arg->fd!=out[0]) //multi-mode && cat progress is running
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
		if(b_debug)
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
                if(dog_looping)
        		bufferevent_free(bev);
		evbuffer_free(arg->buf);
		close(arg->fd);
		free(arg);
	}
}
void _on_connected(int fd,short event,void *arg)
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
	_noblock(s);
	evarg *_arg=(evarg*)malloc(sizeof(evarg));
	_arg->buf=evbuffer_new();
	_arg->fd=s;
	struct bufferevent* client=bufferevent_new(s,_on_read,NULL,_on_error,(void*)_arg);
	_arg->ev=client;
	arg_map.insert(pair<int,evarg*>(s,_arg));
	bufferevent_enable(client, EV_READ);
	if(!b_broadcast&&cat_running)
	{
		char tmp[30];
		sprintf(tmp,"%s:%d\n",inet_ntoa(((sockaddr_in*)&addr)->sin_addr),((sockaddr_in*)&addr)->sin_port);
		sprintf(_buf,"c%d:%d:%s",_arg->fd,(int)strlen(tmp),tmp);
		write(in[1],_buf,strlen(_buf));
	}
	event_add(ev,NULL);
}
void __send_str(int fd,const char*buff,int len)
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
void _send_str(int fd,const char*buff,int len)
{
	if(fd==0&&arg_map.size())
	{
		map<int,evarg*>::iterator it=arg_map.begin();
		for(it;it!=arg_map.end();++it)
		{
			evarg *_arg=it->second;
			int fd=_arg->fd;
			__send_str(fd,buff,len);
		}
	}
	else
		__send_str(fd,buff,len);

}
void _on_read_output(struct bufferevent *bev, void * _arg)
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
		if(b_broadcast)
		{
			_send_str(0,data,len);
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
				if(b_debug)
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
						if(b_debug)
							cout<<"fetch len "<<datalen<<" "<<taillen<<" "<<tail<<endl;
						if(taillen>=datalen)
						{
							_send_str(fd,tail,datalen);
							evbuffer_drain(buf,j+1+datalen);
							if(base==base_off+1)
							{
								map<int,evarg*>::iterator it=arg_map.find(fd);
								if(it!=arg_map.end())
								{
									evarg *_arg=it->second;

									if(_arg)
									{
										_on_error(_arg->ev,0,_arg);
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
				if(b_debug)
					cout<<"fetch fd format wrong:"<<i<<" "<<data[i]<<endl;
				evbuffer_drain(buf,len);
				break;
			}
		}
	}

	//evbuffer_write(buf,1);
}

int _gen_server(const char*ip,const int &port)
{
	int fd;
	sockaddr_in addr;
	fd=socket(AF_INET,SOCK_STREAM,0);
	if(fd==-1)
	{
		cout<<"init socket error"<<endl;
		quit(-1);
	}
	int val=1;
	setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,(const char*)(&val),sizeof(val));
	memset(&addr,0,sizeof(addr));
	addr.sin_family=AF_INET;
	addr.sin_port=htons(port);
	addr.sin_addr.s_addr=(inet_addr(ip));
	if(bind(fd,(sockaddr*)&addr,sizeof(addr))==-1)
	{
		cout<<"cannot bind port:"<<ip<<":"<<port<<endl;
		quit(-1);
	}
	if(listen(fd,5)==-1)
	{
		cout<<"cannot listen on port:"<<ip<<":"<<port<<endl;
		quit(-1);
	}
	else
	{
		cout<<"server is listenning on "<<ip<<":"<<port<<endl;
	}
	return fd;
}
void init_server_event()
{
	int fd=_gen_server(server_ip,server_port);

	event server,signal_int,signal_chld;
	event_set(&server,fd,EV_READ,_on_connected,&server);
	event_add(&server, NULL);
	//int flags=fcntl(out[0],F_GETFL,0);
	//fcntl(out[0],F_SETFL,flags|O_NONBLOCK);
	event_set(&signal_int, SIGINT, EV_SIGNAL, _on_signal,NULL);
	event_set(&signal_chld, SIGCHLD, EV_SIGNAL, _on_signal,NULL);
	event_add(&signal_int, NULL);
	event_add(&signal_chld, NULL);
	cout<<"init server OK"<<endl;
	server_fd=fd;

	dog_looping=true;
	event_dispatch();
	dog_looping=false;
}
int main(int argc,char **argv)
{
	bool canRun=init(argc,(const char**)argv);
	if(canRun)
	{
                event_init();
		fork_cat();
	}
	de_init();
}
