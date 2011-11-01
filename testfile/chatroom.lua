
local function put(fd,...)
	local s=table.concat({...}," ").."\r\n"
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

local map={}
local id=0
local function genId()
	id=id+1
	return id
end
local function onConnect(fd,addr,port)
	map[fd]={ip=addr,port=port,id=genId(),fd=fd}
	put(fd,"--客户端",addr,port,"接入,id为",map[fd].id)
	put(fd,"--请输入help回车")
end

local function onDisConnect(fd)
	put(1,"client",fd,map[fd].ip,map[fd].port,"leave")
	map[fd]=nil
end
local function onRecv(fd,str)
	if str=='quit' then
		put(-fd,"byebye")
	elseif str=='shutdown' then
		put(fd,"system shutdown")
		os.exit()
	elseif string.find(str,'^send') then
		local id,s=string.match(str,"^send (%d+) (.*)")
		id=tonumber(id)
		if not id then
			put(fd,"the playerId is unvalid")
			return
		end 
		if id==0 then
			put(0,"player id ",map[fd].id," from ",map[fd].ip,"send msg to you:",s)
			return
		end 
		for a,b in pairs(map)do
			if b.id==id then
				put(b.fd,"player id ",map[fd].id," from ",map[fd].ip,"send msg to you:",s)
				return
			end 
		end
		put(fd,"cannot find player")
	elseif str=='time' then
		os.execute("./testfile/test.sh "..fd.." &")
	elseif str=="list" then
		local c=0
		for a,b in pairs(map)do
			c=c+1
			put(fd,"player",b.ip..":"..tostring(b.port),",id:",b.id)
			if fd==b.fd then
				put(fd,"^this is you")
			end
		end 
		put(fd,"total online",c)
	else
		--put(fd,"shutdown\t--donnot do this!\r\nquit\t--quit\r\nsend\t--send onlineId msg (send msg to other)\r\nlist\t--show online players\r\ntime\t--show time every one seconds\r\n")
		put(fd,"本程序由纯lua编写，没有任何网络功能，通过runAsDog将普通程序转换为服务器，你可以将任意语言的程序变成服务器!\r\nquit\t--quit\r\nsend\t--send onlineId msg (send msg to other)\r\nlist\t--show online players\r\ntime\t--show time every one seconds\r\n")
	end
end 
for line in io.stdin:lines()do
	line=line:gsub("\r","")
	--local f=io.open("a.log","a")
	--f:write(line,"\n")
	--f:close()
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
