
Author:vzex
----
Mail:<vzex@163.com>
----

Support System:
====
###### Linux,mac os ######
###### all files are tested on debian,archlinux and mac os. ######

setup
=====
1.Check out svn :svn checkout svn://svn.code.sf.net/p/runasdog/code/trunk runasdog-code
or download release files above.
2.cd runasdog-code,and make(be sure you have libevent-1.4 installed)
3.run test***.sh files for testing.There are two samples,one is remove bash server,another is chatroom.
4.run telnet 127.0.0.1 3000 for testing.

demos
====

remote bash server
----
This is a very simple demo,just execute ./runasdog -p port -f -e bash,'-f' will remove '\r' from the input buffer

chatroom server
----
This program is totally written in lua.It shows all of these functions that server can support

function|description
--------| -----------
multi-client|look at the pic(chatroom.png)
broadcast|server send msg to all clients
server disconnect client directly|look at the pic(chatroom.png)
not just send one line a time|it is a really tcp server!you can do anything as a normal server do
server kick clients when the program is done|nothing to explain

How Does It Work
====
![](https://sourceforge.net/p/runasdog/screenshot/runasdog-how%20it%20works.png)

1.dog create a child progress 'cat',cat where run the command from the arguments,like 'bash'.
2.dog chat with cat by pipe A and pipe B.
3.cat input stdout and stderr into pipe A,and read from pipe B.
4.dog start a server,watch for clients join at server port
5.dog redirect the message from client to pipe B,receive from pipe A,and redirect them to the client.

What is different between '-m' and without? (v0.2 replace "-s" with "-m")
========
If not with '-m',The server runs simple ,just redirect the whole things.But when there are more than one clients join,server must be able to identify who is who there,and which words of the messages got from cat's stdout/stderr should send where. So,the mode with '-m' comes into being.
In this mode,cat recv buffers as this format:

|
-|-
c5:127.0.0.1:1344|c:connected,5:client pipe id,127.0.0.1:1344 is the client address
r5:7:hello u|r:received,5:client pipe id,7:content len,hello u:real content from client
d5:|d:disconnected,5:client pipe id

cat send buff as this format:

|
-|-
5:7:abcdefg|5:client pipe id,7:content length,abcdefg:content for send to client
-5:7:abcdefg|5:client pipe id,-5 means after send this content,server will close the client
0:7:abcdefg|0:broadcast to all the clients

In lua ,there is a demo to handle this:

    local function put(fd,...)
    	local s=table.concat({...},"\t").."\n"
    	local len=string.len(s)
    	local format=string.format("%d:%d:%s",fd,len,s)
    	io.stdout:write(format)
    	io.stdout:flush()
    end
    local function get(line)
    	local result=""
    	local t,fd,count,other
    	while true do
    		t,fd,count,other=string.match(line,"([cdr])(%d+):(%d+):(.*)")
    		if t then
    			fd=tonumber(fd)
    			count=tonumber(count)
    			result=result..string.sub(other,1,count)
    			line=string.sub(other,count+1,-1)
    			if line=="" then
    				break
    			end
    		else
    			break
    		end 
    	end
    	return t,fd,result
    end 
    
    local function onConnect(fd,addr,port)
    end
    
    local function onDisConnect(fd)
    end
    local function onRecv(fd,str)
    end 
    for line in io.stdin:lines()do
    	line=line:gsub("\r","")
    	local t,fd,other=get(line)
    	if t then
    		if t=='c' then
    			onConnect(fd,string.match(other,"(.-):(.+)"))
    		elseif t=='d' then
    			onDisConnect(fd)
    		elseif t=='r' then
    			onRecv(fd,other)
    		end 
    	end 
    end
