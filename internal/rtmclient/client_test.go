package rtmclient

import (
	"context"
	"encoding/hex"
	"fmt"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strings"
	"testing"
	"time"
)

func testClient(server *httptest.Server) *Client {
	client := New()
	parsed, _ := url.Parse(server.URL)
	client.endpointOverride = parsed.Host
	client.HTTP = server.Client()
	return client
}

func TestReadMemoryScopesProcessAndParsesHexOutput(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/getmem.ps3mapi" {
			t.Fatalf("unexpected path %s", r.URL.Path)
		}
		if got := r.URL.Query().Get("proc"); got != DefaultProcessID {
			t.Fatalf("process = %q", got)
		}
		if got := r.URL.Query().Get("addr"); got != "0030D230" {
			t.Fatalf("address = %q", got)
		}
		if got := r.URL.Query().Get("len"); got != "4" {
			t.Fatalf("length = %q", got)
		}
		fmt.Fprint(w, `<textarea id="output">3DCCCCCD</textarea>`)
	}))
	defer server.Close()

	data, err := testClient(server).ReadMemory(context.Background(), "192.168.0.62", "0x0030D230", 4)
	if err != nil {
		t.Fatal(err)
	}
	if got := strings.ToUpper(hex.EncodeToString(data)); got != "3DCCCCCD" {
		t.Fatalf("read = %s", got)
	}
}

func TestWriteMemoryRequiresMatchingReadback(t *testing.T) {
	var value = []byte{0x3F, 0x80, 0x00, 0x00}
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/setmem.ps3mapi":
			if got := r.URL.Query().Get("val"); got != "3F800000" {
				t.Fatalf("write hex = %s", got)
			}
			if got := r.URL.Query().Get("proc"); got != DefaultProcessID {
				t.Fatalf("process = %s", got)
			}
			fmt.Fprint(w, "OK")
		case "/getmem.ps3mapi":
			fmt.Fprintf(w, `<textarea id="output">%s</textarea>`, strings.ToUpper(hex.EncodeToString(value)))
		default:
			t.Fatalf("unexpected path %s", r.URL.Path)
		}
	}))
	defer server.Close()

	client := testClient(server)
	if err := client.WriteMemory(context.Background(), "192.168.0.62", "0030D230", []byte{0x3F, 0x80, 0, 0}); err != nil {
		t.Fatal(err)
	}
	value[3] = 0x01
	if err := client.WriteMemory(context.Background(), "192.168.0.62", "0030D230", []byte{0x3F, 0x80, 0, 0}); err == nil || !strings.Contains(err.Error(), "not verified") {
		t.Fatalf("expected a readback mismatch, got %v", err)
	}
}

func TestRejectsPublicTargetsAndInvalidWriteWidths(t *testing.T) {
	client := New()
	if _, err := client.ReadMemory(context.Background(), "8.8.8.8", "0030D230", 4); err == nil {
		t.Fatal("public target accepted")
	}
	if _, err := ParseAddress("100000000"); err == nil {
		t.Fatal("overflow address accepted")
	}
	if err := client.WriteMemory(context.Background(), "192.168.0.62", "0030D230", make([]byte, 9)); err == nil {
		t.Fatal("oversized write accepted")
	}
}

func TestNotifyEncodesMessageAndRejectsOverlongInput(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/notify.ps3mapi" {
			t.Fatalf("path = %s", r.URL.Path)
		}
		if got := r.URL.Query().Get("msg"); got != "hello & guardian" {
			t.Fatalf("message = %q", got)
		}
		fmt.Fprint(w, "OK")
	}))
	defer server.Close()
	client := testClient(server)
	if err := client.Notify(context.Background(), "192.168.0.62", "hello & guardian"); err != nil {
		t.Fatal(err)
	}
	if err := client.Notify(context.Background(), "192.168.0.62", strings.Repeat("x", 129)); err == nil {
		t.Fatal("overlong notification accepted")
	}
}

func TestLaunchGameUsesRecoveredStageOrderAndReportsOnlyHTTPAcknowledgement(t *testing.T) {
	var paths []string
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		paths = append(paths, r.URL.EscapedPath())
		w.WriteHeader(http.StatusNoContent)
	}))
	defer server.Close()
	client := testClient(server)
	var pauses []time.Duration
	client.sleep = func(_ context.Context, delay time.Duration) error {
		pauses = append(pauses, delay)
		return nil
	}

	result, err := client.LaunchGame(context.Background(), "192.168.0.62", DefaultDestinyISOPath)
	if err != nil {
		t.Fatal(err)
	}
	wantPaths := []string{
		"/mount.ps3/dev_hdd0/PS3ISO/Destiny.iso&mode=silent",
		"/wmnav.ps3/game",
		"/play.ps3",
	}
	if strings.Join(paths, "|") != strings.Join(wantPaths, "|") {
		t.Fatalf("launch path order = %#v", paths)
	}
	if len(result.Stages) != 3 || !result.Stages[0].Acknowledged || !result.Stages[1].Acknowledged || !result.Stages[2].Acknowledged || result.GameStartConfirmed {
		t.Fatalf("unexpected launch result: %#v", result)
	}
	if len(pauses) != 2 || pauses[0] != 3*time.Second || pauses[1] != time.Second {
		t.Fatalf("launch delays = %#v", pauses)
	}
}

func TestLaunchGameValidatesPathAndPreservesPartialProgressOnCancellation(t *testing.T) {
	for _, path := range []string{
		"/dev_hdd0/PS3ISO/subfolder/Destiny.iso",
		"/dev_hdd0/PS3ISO/../Destiny.iso",
		"/dev_hdd0/PS3ISO/Destiny.iso?x=/play.ps3",
		"/dev_hdd0/PS3ISO/Destiny.iso#fragment",
		"/dev_hdd0/GAMES/Destiny.iso",
	} {
		if _, err := ValidateDestinyISOPath(path); err == nil {
			t.Errorf("unsafe ISO path accepted: %q", path)
		}
	}

	calls := 0
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		calls++
	}))
	defer server.Close()
	client := testClient(server)
	client.sleep = func(context.Context, time.Duration) error { return context.Canceled }
	result, err := client.LaunchGame(context.Background(), "192.168.0.62", DefaultDestinyISOPath)
	if err == nil || !strings.Contains(err.Error(), "waiting before return_to_xmb") {
		t.Fatalf("expected cancellation after mount, got %v", err)
	}
	if calls != 1 || len(result.Stages) != 1 || !result.Stages[0].Acknowledged || result.GameStartConfirmed {
		t.Fatalf("partial launch progress lost: calls=%d result=%#v", calls, result)
	}
}

func TestParseMemoryResponseSupportsAddressDumpAndRejectsShortOutput(t *testing.T) {
	got, err := parseMemoryResponse("<pre>0030D230  3D CC CC CD</pre>", 0x0030D230, 4)
	if err != nil {
		t.Fatal(err)
	}
	if hex.EncodeToString(got) != "3dcccccd" {
		t.Fatalf("bytes = %x", got)
	}
	if _, err := parseMemoryResponse("no memory row", 0x0030D230, 4); err == nil {
		t.Fatal("unparseable response accepted")
	}
}
