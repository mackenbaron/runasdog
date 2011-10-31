
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
