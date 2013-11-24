package main

import (
	"net"
	"bufio"
	"io"
	"os/exec"
	"flag"
	"strconv"
)

var g_Addr2Child map[string] *exec.Cmd
var g_Addr2Conn map[string] net.Conn
var g_Address = flag.String("h", "127.0.0.1", "ip address,default 127.0.0.1")
var g_Port = flag.Int("p", 8000, "port,default 8000")
var g_Cmd = flag.String("c", "bash", "cmd to run,default bash")
var g_Cmd_Args = flag.String("a", "", "cmd args")
func main() {
	flag.Parse()
	g_Addr2Child = make(map[string] *exec.Cmd)
	g_Addr2Conn = make(map[string] net.Conn)
	ip := (*g_Address) + ":" + strconv.Itoa(*g_Port)
	println("start server on", ip)
	server, err := net.Listen("tcp", ip)
	if err!= nil {
		println(err.Error())
		return
	}
	for {
		conn, err := server.Accept()
		if err!= nil {
			continue
		}
		go handleClient(conn)
	}
}

func forkChild(addr string, name string, arg ...string) (*exec.Cmd, bool) {
	oldChild, ok := g_Addr2Child[addr]
	if ok {
		return oldChild, false
	}
	child := exec.Command(name, arg...)
	g_Addr2Child[addr] = child
	return child, true
}

func childResponse(addr string, input io.Reader, conn net.Conn) {
	write := bufio.NewWriter(conn)
	write.ReadFrom(input)
	conn.Close()
}

func handleClient(conn net.Conn) {
	addr := conn.RemoteAddr().String()
	g_Addr2Conn[addr] = conn
	var child *exec.Cmd
	var  bNeedStart bool
	if (*g_Cmd_Args == "") {
		child, bNeedStart = forkChild(addr, *g_Cmd)
	} else {
		child, bNeedStart = forkChild(addr, *g_Cmd, *g_Cmd_Args)
	}
	println("run sub process:", *g_Cmd, *g_Cmd_Args)
	childin, _:= child.StdinPipe()
	childerr, err:= child.StderrPipe()
	childout, _:= child.StdoutPipe()
	if bNeedStart {
		err = child.Start()
		if err!= nil {
			println(err.Error())
			return
		}
	}
	
	go childResponse(addr, childerr, conn)
	go childResponse(addr, childout, conn)
	input := bufio.NewScanner(conn)
	/*input.Split(func(data []byte, atEOF bool)(adv int, token []byte, err error) {
		
			})*/
	for input.Scan() {
		content := input.Text()
		childin.Write([]byte(content + "\n"))
	}
	println(addr, "disconnected")
	delete(g_Addr2Conn, addr)
	child.Process.Kill()
	delete(g_Addr2Child, addr)
	conn.Close()
}
