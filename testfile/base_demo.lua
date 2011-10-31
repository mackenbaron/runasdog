require "testfile/base"


function onConnect(fd,addr,port)
        put(fd,"hello")
end 
function onDisConnect()
        os.exit()
end 
put(1,"This is a demo written in lua")
reg_callback(onConnect,onDisConnect,nil)
loop()
