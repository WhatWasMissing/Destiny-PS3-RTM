package main

import (
	"encoding/hex"
	"fmt"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"os/exec"
	"strconv"
	"sync"
	"testing"
	"time"
)

type roundTripFunc func(*http.Request) (*http.Response,error)
func (fn roundTripFunc) RoundTrip(r *http.Request) (*http.Response,error) { return fn(r) }

func TestDesktopFrontendAgainstFakeConsole(t *testing.T) {
	var lock sync.Mutex
	memory:=map[uint64]byte{}
	console:=httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter,r *http.Request){
		lock.Lock();defer lock.Unlock()
		q:=r.URL.Query()
		switch r.URL.Path {
		case "/index.ps3":fmt.Fprint(w,"fake console ready")
		case "/setmem.ps3mapi":
			address,err:=strconv.ParseUint(q.Get("addr"),16,32)
			if err!=nil||address==0xBADBAD {http.Error(w,"simulated write failure",503);return}
			if q.Get("proc")!="0x1010200" {http.Error(w,"missing process scope",400);return}
			data,err:=hex.DecodeString(q.Get("val"));if err!=nil {http.Error(w,"invalid bytes",400);return}
			for i,value:=range data {memory[address+uint64(i)]=value};fmt.Fprint(w,"accepted")
		case "/getmem.ps3mapi":
			address,_:=strconv.ParseUint(q.Get("addr"),16,32);length,_:=strconv.Atoi(q.Get("len"))
			if length<1||length>1024 {http.Error(w,"invalid length",400);return}
			data:=make([]byte,length);for i:=range data {if address!=0x1238 {data[i]=memory[address+uint64(i)]}}
			fmt.Fprintf(w,"<textarea id=\"output\">%X</textarea>",data)
		default:http.Error(w,"unexpected fake console action",400)
		}
	}));defer console.Close()
	backend,_:=url.Parse(console.URL)
	transport:=roundTripFunc(func(r *http.Request)(*http.Response,error){
		copy:=r.Clone(r.Context());target:=*r.URL;target.Scheme=backend.Scheme;target.Host=backend.Host;copy.URL=&target
		return http.DefaultTransport.RoundTrip(copy)
	})
	h:=&host{token:"isolated-test-token",validTarget:consoleTarget,client:&http.Client{Transport:transport,Timeout:time.Second}}
	server:=httptest.NewServer(h.handler());defer server.Close();h.origin=server.URL
	command:=exec.Command("node","tests/desktop-integration.cjs")
	command.Env=append(os.Environ(),"RTM_TEST_ORIGIN="+server.URL)
	output,err:=command.CombinedOutput();if err!=nil {t.Fatalf("frontend integration failed: %v\n%s",err,output)}
	t.Log(string(output))
}
