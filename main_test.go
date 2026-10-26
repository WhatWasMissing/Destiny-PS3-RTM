package main

import (
	"io"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strings"
	"testing"
)

func TestConsoleTarget(t *testing.T) {
	for _, raw := range []string{"http://8.8.8.8/index.ps3", "http://localhost/index.ps3", "http://192.168.1.1:3000/index.ps3", "http://user@192.168.1.1/index.ps3", "https://192.168.1.1/index.ps3"} {
		u, _ := url.Parse(raw)
		if consoleTarget(u) {
			t.Errorf("accepted %s", raw)
		}
	}
	u, _ := url.Parse("http://192.168.1.62/getmem.ps3mapi?addr=1234")
	if !consoleTarget(u) {
		t.Fatal("private console rejected")
	}
}

func TestHostProxy(t *testing.T) {
	calls := 0
	console := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls++
		if r.URL.RawQuery != "addr=1234&proc=77" {
			t.Error("query changed")
		}
		w.WriteHeader(503)
		io.WriteString(w, "console error")
	}))
	defer console.Close()
	h := &host{token: "test-token", origin: "http://127.0.0.1:8767", client: console.Client(), validTarget: func(u *url.URL) bool { return u.Scheme+"://"+u.Host == console.URL }}
	for _, tc := range []struct {
		token, path string
		status      int
	}{
		{"", "/getmem.ps3mapi", 403}, {"test-token", "/unknown", 400}, {"test-token", "/getmem.ps3mapi", 503},
	} {
		r := httptest.NewRequest("GET", h.origin+"/console?url="+url.QueryEscape(console.URL+tc.path+"?addr=1234&proc=77"), nil)
		r.Header.Set("X-RTM-Token", tc.token)
		w := httptest.NewRecorder()
		h.handler().ServeHTTP(w, r)
		if w.Code != tc.status {
			t.Errorf("status %d want %d", w.Code, tc.status)
		}
	}
	if calls != 1 {
		t.Fatalf("unexpected console requests: %d", calls)
	}
	r := httptest.NewRequest("GET", h.origin+"/", nil)
	w := httptest.NewRecorder()
	h.handler().ServeHTTP(w, r)
	if w.Code != 200 || !strings.Contains(w.Body.String(), "Destiny RTM") {
		t.Fatal("embedded frontend missing")
	}
	r = httptest.NewRequest("GET", "http://evil.example/host-config", nil)
	w = httptest.NewRecorder()
	h.handler().ServeHTTP(w, r)
	if w.Code != 403 {
		t.Fatal("host validation missing")
	}
}
