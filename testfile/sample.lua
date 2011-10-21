
local function put(fd,...)
	local s=table.concat({...},"\t").."\n"
	local len=string.len(s)
	local format=string.format("%d:%d:%s",fd,len,s)
	io.stdout:write(format)
	io.stdout:flush()
end
local function get(line)
	local t,fd,other=string.match(line,"([cdr])(%d+):(.*)")
	if t then
		fd=tonumber(fd)
		return t,fd,other
	else
		return nil,nil,nil
	end 
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
