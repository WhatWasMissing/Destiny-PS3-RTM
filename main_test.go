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

func TestHostRejectsUndocumentedFunctionCallRoutes(t *testing.T) {
	h := &host{token: "test-token", origin: "http://127.0.0.1:8767", client: http.DefaultClient, validTarget: consoleTarget}
	for _, path := range []string{"/call.ps3mapi", "/rpc.ps3", "/enablerpc.ps3mapi"} {
		r := httptest.NewRequest("GET", h.origin+"/console?url="+url.QueryEscape("http://192.168.1.62"+path), nil)
		r.Header.Set("X-RTM-Token", h.token)
		w := httptest.NewRecorder()
		h.handler().ServeHTTP(w, r)
		if w.Code != http.StatusBadRequest {
			t.Errorf("route %s returned %d", path, w.Code)
		}
	}
}

func TestHostRejectsMemoryWriteWithoutBuildVerifier(t *testing.T) {
	calls := 0
	console := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls++
		io.WriteString(w, "unexpected write")
	}))
	defer console.Close()
	h := &host{token: "test-token", origin: "http://127.0.0.1:8767", client: console.Client(), validTarget: func(u *url.URL) bool { return u.Scheme+"://"+u.Host == console.URL }}
	r := httptest.NewRequest(http.MethodGet, h.origin+"/console?url="+url.QueryEscape(console.URL+"/setmem.ps3mapi?addr=1234&val=00&proc=0x1010200"), nil)
	r.Header.Set("X-RTM-Token", h.token)
	w := httptest.NewRecorder()
	h.handler().ServeHTTP(w, r)
	if w.Code != http.StatusPreconditionFailed || calls != 0 {
		t.Fatalf("unverified write was forwarded: status=%d calls=%d", w.Code, calls)
	}
}

func TestHostProtectsBuildSignatureRanges(t *testing.T) {
	calls := 0
	console := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { calls++ }))
	defer console.Close()
	h := &host{token: "test-token", origin: "http://127.0.0.1:8767", client: console.Client(), validTarget: func(u *url.URL) bool { return u.Scheme+"://"+u.Host == console.URL }}
	r := httptest.NewRequest(http.MethodGet, h.origin+"/console?url="+url.QueryEscape(console.URL+"/setmem.ps3mapi?addr=00444B64&val=00&proc=0x1010200"), nil)
	r.Header.Set("X-RTM-Token", h.token)
	w := httptest.NewRecorder()
	h.handler().ServeHTTP(w, r)
	if w.Code != http.StatusBadRequest || calls != 0 || !strings.Contains(w.Body.String(), "protected 01.24 code signature") {
		t.Fatalf("protected signature write was not blocked: status=%d calls=%d body=%q", w.Code, calls, w.Body.String())
	}
}

func TestHostProxyAllowsOnlyConstrainedLaunchTargets(t *testing.T) {
	var forwarded []string
	client := &http.Client{Transport: roundTripFunc(func(request *http.Request) (*http.Response, error) {
		forwarded = append(forwarded, request.URL.EscapedPath())
		return &http.Response{StatusCode: http.StatusOK, Header: make(http.Header), Body: io.NopCloser(strings.NewReader("OK")), Request: request}, nil
	})}
	h := &host{token: "test-token", origin: "http://127.0.0.1:8767", client: client, validTarget: consoleTarget}
	valid := []string{
		"http://192.168.1.62/mount.ps3/dev_hdd0/PS3ISO/Destiny.iso&mode=silent",
		"http://192.168.1.62/mount.ps3/dev_hdd0/PS3ISO/Destiny%20One.iso&mode=silent",
		"http://192.168.1.62/wmnav.ps3/game",
		"http://192.168.1.62/play.ps3",
	}
	for _, target := range valid {
		r := httptest.NewRequest(http.MethodGet, h.origin+"/console?url="+url.QueryEscape(target), nil)
		r.Header.Set("X-RTM-Token", h.token)
		w := httptest.NewRecorder()
		h.handler().ServeHTTP(w, r)
		if w.Code != http.StatusOK {
			t.Errorf("valid launch target %q returned %d: %s", target, w.Code, w.Body.String())
		}
	}
	for _, target := range []string{
		"http://192.168.1.62/mount.ps3/dev_hdd0/PS3ISO/sub/Destiny.iso&mode=silent",
		"http://192.168.1.62/mount.ps3/dev_hdd0/PS3ISO/../Destiny.iso&mode=silent",
		"http://192.168.1.62/mount.ps3/dev_hdd0/PS3ISO/Destiny.iso&mode=silent?next=/play.ps3",
		"http://192.168.1.62/mount.ps3/dev_hdd0/PS3ISO/Destiny.iso",
		"http://192.168.1.62/play.ps3?next=/mount.ps3/dev_hdd0/PS3ISO/Destiny.iso",
		"http://192.168.1.62/wmnav.ps3/game?x=1",
	} {
		r := httptest.NewRequest(http.MethodGet, h.origin+"/console?url="+url.QueryEscape(target), nil)
		r.Header.Set("X-RTM-Token", h.token)
		w := httptest.NewRecorder()
		h.handler().ServeHTTP(w, r)
		if w.Code != http.StatusBadRequest {
			t.Errorf("invalid launch target %q returned %d", target, w.Code)
		}
	}
	if len(forwarded) != len(valid) {
		t.Fatalf("forwarded %d requests, want %d: %#v", len(forwarded), len(valid), forwarded)
	}
}
