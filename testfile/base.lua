
local my_onConnect=nil
local my_onDisConnect=nil
local my_onRecv=nil

function reg_callback(onConnect,onDisConnect,onRecv)
        my_onConnect=onConnect
        my_onDisConnect=onDisConnect
        my_onRecv=onRecv
end 


function put(fd,...)
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
        if my_onConnect then
                my_onConnect(fd,addr,port)
        end 
end

local function onDisConnect(fd)
        if my_onDisConnect then
                my_onDisConnect(fd)
        end 
end
local function onRecv(fd,str)
        if my_onRecv then
                my_onRecv(fd,str)
        end
end 
function loop()
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
end 
