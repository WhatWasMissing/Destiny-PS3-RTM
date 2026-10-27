package main

import (
	"context"
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
	"strconv"
	"strings"
	"time"

	"destiny-rtm/internal/rtmclient"
)

//go:embed source
var assets embed.FS

var endpoints = map[string]bool{
	"/index.ps3": true, "/beep.ps3": true, "/getmem.ps3mapi": true,
	"/setmem.ps3mapi": true, "/notify.ps3mapi": true,
	"/shutdown.ps3mapi": true, "/restart.ps3mapi": true,
	"/getcid.ps3mapi": true, "/setcid.ps3mapi": true,
	"/wmnav.ps3/game": true, "/play.ps3": true,
}

type host struct {
	token       string
	origin      string
	client      *http.Client
	validTarget func(*url.URL) bool
	verifyBuild func(context.Context, string) error
}

func consoleTarget(target *url.URL) bool {
	ip := net.ParseIP(target.Hostname())
	return target.Scheme == "http" && target.User == nil && ip != nil && ip.To4() != nil &&
		ip.IsPrivate() && (target.Port() == "" || target.Port() == "80") && target.Fragment == ""
}

func supportedConsoleEndpoint(target *url.URL) bool {
	if target.Path == "/wmnav.ps3/game" || target.Path == "/play.ps3" {
		return target.RawQuery == "" && !target.ForceQuery
	}
	if endpoints[target.Path] {
		return true
	}
	if strings.HasPrefix(target.Path, "/popup.ps3/") {
		return true
	}
	if !strings.HasPrefix(target.Path, "/mount.ps3/") || target.RawQuery != "" || target.ForceQuery {
		return false
	}
	const prefix = "/mount.ps3"
	const suffix = "&mode=silent"
	if !strings.HasSuffix(target.Path, suffix) {
		return false
	}
	isoPath := strings.TrimSuffix(strings.TrimPrefix(target.Path, prefix), suffix)
	validatedPath, err := rtmclient.ValidateDestinyISOPath(isoPath)
	if err != nil {
		return false
	}
	canonical := prefix + validatedPath + suffix
	return target.Path == canonical && target.EscapedPath() == (&url.URL{Path: canonical}).EscapedPath()
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
		if err != nil || !h.validTarget(target) || !supportedConsoleEndpoint(target) {
			http.Error(w, "Unsupported console target or endpoint", http.StatusBadRequest)
			return
		}
		if target.Path == "/setmem.ps3mapi" {
			query := target.Query()
			processValues := query["proc"]
			process := ""
			if len(processValues) == 1 {
				process = processValues[0]
			} else if len(processValues) > 1 {
				http.Error(w, "Memory writes require one Destiny process target", http.StatusBadRequest)
				return
			}
			if process == "" {
				query.Set("proc", rtmclient.DefaultProcessID)
				target.RawQuery = query.Encode()
				process = rtmclient.DefaultProcessID
			}
			query = target.Query()
			if len(query["addr"]) != 1 || len(query["val"]) != 1 {
				http.Error(w, "Memory writes require one address and one value", http.StatusBadRequest)
				return
			}
			if !strings.EqualFold(process, rtmclient.DefaultProcessID) {
				http.Error(w, "Memory writes must target the Destiny process", http.StatusBadRequest)
				return
			}
			address, addressErr := rtmclient.ParseAddress(target.Query().Get("addr"))
			data, dataErr := hex.DecodeString(target.Query().Get("val"))
			if addressErr != nil || dataErr != nil || len(data) < 1 || len(data) > 1<<16 || uint64(address)+uint64(len(data))-1 > 0xFFFFFFFF {
				http.Error(w, "Memory write address or value is invalid", http.StatusBadRequest)
				return
			}
			if err := rtmclient.ValidateDestinyWriteRange(address, len(data)); err != nil {
				http.Error(w, err.Error(), http.StatusBadRequest)
				return
			}
			if h.verifyBuild == nil {
				http.Error(w, "Destiny 01.24 build verification is unavailable; memory write refused", http.StatusPreconditionFailed)
				return
			}
			if err := h.verifyBuild(r.Context(), target.Hostname()); err != nil {
				http.Error(w, err.Error(), http.StatusPreconditionFailed)
				return
			}
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
	mux.HandleFunc("/module-load", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost || r.Header.Get("X-RTM-Token") != h.token ||
			(r.Header.Get("Origin") != "" && r.Header.Get("Origin") != h.origin) {
			http.Error(w, "Forbidden", http.StatusForbidden)
			return
		}
		defer r.Body.Close()
		var input struct {
			Target string `json:"target"`
			PID    string `json:"pid"`
			Path   string `json:"path"`
		}
		decoder := json.NewDecoder(io.LimitReader(r.Body, 2048))
		decoder.DisallowUnknownFields()
		if err := decoder.Decode(&input); err != nil || input.Path != "/dev_hdd0/tmp/Destiny_Flyhack.sprx" ||
			strings.ContainsAny(input.Path, "\r\n\x00") {
			http.Error(w, "Invalid module request", http.StatusBadRequest)
			return
		}
		pid, err := strconv.ParseUint(input.PID, 10, 32)
		destinyPID, _ := strconv.ParseUint(rtmclient.DefaultProcessID, 0, 32)
		if err != nil || pid != destinyPID {
			http.Error(w, "Module load must target the Destiny process", http.StatusBadRequest)
			return
		}
		targetIP := net.ParseIP(input.Target)
		target := &url.URL{Scheme: "http", Host: input.Target}
		if targetIP == nil || targetIP.To4() == nil || !consoleTarget(target) || !h.validTarget(target) {
			http.Error(w, "Unsupported console target", http.StatusBadRequest)
			return
		}
		if h.verifyBuild == nil {
			http.Error(w, "Destiny 01.24 build verification is unavailable; module load refused", http.StatusPreconditionFailed)
			return
		}
		if err := h.verifyBuild(r.Context(), input.Target); err != nil {
			http.Error(w, err.Error(), http.StatusPreconditionFailed)
			return
		}
		command := fmt.Sprintf("MODULE LOAD %d %s", pid, input.Path)
		moduleURL := &url.URL{
			Scheme:   "http",
			Host:     net.JoinHostPort(targetIP.String(), "80"),
			Path:     "/ps3mapi.ps3",
			RawQuery: url.QueryEscape(command),
		}
		request, err := http.NewRequestWithContext(r.Context(), http.MethodGet, moduleURL.String(), nil)
		if err != nil {
			http.Error(w, "Invalid module request", http.StatusBadRequest)
			return
		}
		response, err := h.client.Do(request)
		if err != nil {
			http.Error(w, "Console module-load request failed", http.StatusBadGateway)
			return
		}
		defer response.Body.Close()
		body, err := io.ReadAll(io.LimitReader(response.Body, 64*1024+1))
		if err != nil || len(body) > 64*1024 {
			http.Error(w, "Module-load response exceeded limit or was interrupted", http.StatusBadGateway)
			return
		}
		w.Header().Set("Cache-Control", "no-store")
		w.Header().Set("Content-Type", "text/plain; charset=utf-8")
		if response.StatusCode < 200 || response.StatusCode >= 300 {
			http.Error(w, "Console rejected module-load request", http.StatusBadGateway)
			return
		}
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write(body)
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
	buildVerifier := rtmclient.New()
	h := &host{token: hex.EncodeToString(token), origin: origin, validTarget: consoleTarget,
		verifyBuild: buildVerifier.VerifyDestiny0124Profile,
		client:      &http.Client{Transport: &http.Transport{Proxy: nil}, Timeout: 10 * time.Second, CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}}
	server := &http.Server{Handler: h.handler(), ReadHeaderTimeout: 5 * time.Second, IdleTimeout: 60 * time.Second}
	fmt.Println("Destiny RTM:", origin)
	if !*noOpen {
		if err := launch(origin); err != nil {
			log.Printf("Open %s in your browser: %v", origin, err)
		}
	}
	log.Fatal(server.Serve(listener))
}
