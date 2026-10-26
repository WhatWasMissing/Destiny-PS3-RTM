package main

import (
	"crypto/rand"
	"embed"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

//go:embed source
var assets embed.FS

var endpoints = map[string]bool{
	"/index.ps3": true, "/beep.ps3": true, "/getmem.ps3mapi": true,
	"/setmem.ps3mapi": true, "/call.ps3mapi": true, "/notify.ps3mapi": true,
	"/shutdown.ps3mapi": true, "/restart.ps3mapi": true, "/rpc.ps3": true,
	"/enablerpc.ps3mapi": true, "/getcid.ps3mapi": true, "/setcid.ps3mapi": true,
}

type host struct {
	token       string
	origin      string
	client      *http.Client
	validTarget func(*url.URL) bool
}

func consoleTarget(target *url.URL) bool {
	ip := net.ParseIP(target.Hostname())
	return target.Scheme == "http" && target.User == nil && ip != nil && ip.To4() != nil &&
		ip.IsPrivate() && (target.Port() == "" || target.Port() == "80") && target.Fragment == ""
}

func (h *host) handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/host-config", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "GET" || (r.Header.Get("Origin") != "" && r.Header.Get("Origin") != h.origin) {
			http.Error(w, "Forbidden", http.StatusForbidden)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.Header().Set("Cache-Control", "no-store")
		json.NewEncoder(w).Encode(map[string]string{"host": "destiny-rtm", "token": h.token})
	})
	mux.HandleFunc("/console", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "GET" || r.Header.Get("X-RTM-Token") != h.token ||
			(r.Header.Get("Origin") != "" && r.Header.Get("Origin") != h.origin) {
			http.Error(w, "Forbidden", http.StatusForbidden)
			return
		}
		target, err := url.Parse(r.URL.Query().Get("url"))
		if err != nil || !h.validTarget(target) || (!endpoints[target.Path] && !strings.HasPrefix(target.Path, "/popup.ps3/")) {
			http.Error(w, "Unsupported console target or endpoint", http.StatusBadRequest)
			return
		}
		request, err := http.NewRequestWithContext(r.Context(), "GET", target.String(), nil)
		if err != nil {
			http.Error(w, "Invalid console request", http.StatusBadRequest)
			return
		}
		response, err := h.client.Do(request)
		if err != nil {
			http.Error(w, "Console unavailable or request timed out", http.StatusBadGateway)
			return
		}
		defer response.Body.Close()
		body, err := io.ReadAll(io.LimitReader(response.Body, 8*1024*1024+1))
		if err != nil || len(body) > 8*1024*1024 {
			http.Error(w, "Console response exceeded limit or was interrupted", http.StatusBadGateway)
			return
		}
		w.Header().Set("Cache-Control", "no-store")
		w.Header().Set("Content-Type", "text/plain; charset=utf-8")
		w.WriteHeader(response.StatusCode)
		w.Write(body)
	})
	files, _ := fs.Sub(assets, "source")
	mux.Handle("/", http.FileServer(http.FS(files)))
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if "http://"+r.Host != h.origin {
			http.Error(w, "Invalid host", http.StatusForbidden)
			return
		}
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("X-Frame-Options", "DENY")
		w.Header().Set("Cache-Control", "no-store")
		mux.ServeHTTP(w, r)
	})
}

func launch(url string) error {
	for _, base := range []string{os.Getenv("ProgramFiles(x86)"), os.Getenv("ProgramFiles")} {
		edge := filepath.Join(base, "Microsoft", "Edge", "Application", "msedge.exe")
		if _, err := os.Stat(edge); err == nil {
			return exec.Command(edge, "--app="+url).Start()
		}
	}
	return exec.Command("rundll32", "url.dll,FileProtocolHandler", url).Start()
}

func main() {
	listen := flag.String("listen", "127.0.0.1:8767", "Loopback listen address; keep stable to retain browser settings")
	noOpen := flag.Bool("no-open", false, "Run without opening the app window")
	flag.Parse()
	hostname, _, err := net.SplitHostPort(*listen)
	if err != nil || net.ParseIP(hostname) == nil || !net.ParseIP(hostname).IsLoopback() {
		log.Fatal("listen address must be a loopback IP")
	}
	listener, err := net.Listen("tcp", *listen)
	if err != nil {
		log.Fatal(err)
	}
	token := make([]byte, 32)
	if _, err := rand.Read(token); err != nil {
		log.Fatal(err)
	}
	origin := "http://" + listener.Addr().String()
	h := &host{token: hex.EncodeToString(token), origin: origin, validTarget: consoleTarget,
		client: &http.Client{Transport: &http.Transport{Proxy: nil}, Timeout: 10 * time.Second, CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}}
	server := &http.Server{Handler: h.handler(), ReadHeaderTimeout: 5 * time.Second, IdleTimeout: 60 * time.Second}
	fmt.Println("Destiny RTM:", origin)
	if !*noOpen {
		if err := launch(origin); err != nil {
			log.Printf("Open %s in your browser: %v", origin, err)
		}
	}
	log.Fatal(server.Serve(listener))
}
