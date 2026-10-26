package rtmclient

import (
	"context"
	"encoding/hex"
	"encoding/json"
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

func TestReadPlayerPositionFollowsOverwallsPointerAndDecodesBigEndianFloats(t *testing.T) {
	var addresses []string
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/getmem.ps3mapi" || r.URL.Query().Get("proc") != DefaultProcessID {
			t.Fatalf("unexpected request: %s?%s", r.URL.Path, r.URL.RawQuery)
		}
		address := r.URL.Query().Get("addr")
		addresses = append(addresses, address)
		switch address {
		case "0190EBB0":
			if r.URL.Query().Get("len") != "4" {
				t.Fatalf("pointer read length = %q", r.URL.Query().Get("len"))
			}
			fmt.Fprint(w, `<textarea id="output">10000000</textarea>`)
		case "10000140":
			if r.URL.Query().Get("len") != "12" {
				t.Fatalf("coordinate read length = %q", r.URL.Query().Get("len"))
			}
			fmt.Fprint(w, `<textarea id="output">41480000C050000043000000</textarea>`)
		default:
			t.Fatalf("unexpected address %s", address)
		}
	}))
	defer server.Close()

	position, err := testClient(server).ReadPlayerPosition(context.Background(), "192.168.0.62")
	if err != nil {
		t.Fatal(err)
	}
	if position.PlayerPointer != 0x10000000 || position.CoordinatesAddress != 0x10000140 {
		t.Fatalf("unexpected resolved addresses: %#v", position)
	}
	if position.X != 12.5 || position.Y != -3.25 || position.Z != 128 || position.RawHex != "41480000C050000043000000" {
		t.Fatalf("unexpected coordinates: %#v", position)
	}
	if strings.Join(addresses, "|") != "0190EBB0|10000140" {
		t.Fatalf("reads did not follow the source pointer map: %#v", addresses)
	}
}

func TestReadPlayerPositionRejectsInvalidPointersAndNonFiniteCoordinates(t *testing.T) {
	for _, pointer := range []string{"00000000", "00010001", "80000000", "7FFFFEC0"} {
		t.Run("pointer_"+pointer, func(t *testing.T) {
			calls := 0
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
				calls++
				fmt.Fprintf(w, `<textarea id="output">%s</textarea>`, pointer)
			}))
			defer server.Close()
			if _, err := testClient(server).ReadPlayerPosition(context.Background(), "192.168.0.62"); err == nil {
				t.Fatal("invalid player pointer accepted")
			}
			if calls != 1 {
				t.Fatalf("invalid pointer caused %d reads; expected only the pointer read", calls)
			}
		})
	}

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Query().Get("addr") == "0190EBB0" {
			fmt.Fprint(w, `<textarea id="output">10000000</textarea>`)
			return
		}
		fmt.Fprint(w, `<textarea id="output">7FC000003F8000003F800000</textarea>`)
	}))
	defer server.Close()
	if _, err := testClient(server).ReadPlayerPosition(context.Background(), "192.168.0.62"); err == nil || !strings.Contains(err.Error(), "non-finite") {
		t.Fatalf("expected non-finite coordinate rejection, got %v", err)
	}
}

func TestInspectEntityPointersReadsPlayerTargetCoordinatesAndActivation(t *testing.T) {
	var reads []string
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/getmem.ps3mapi" || r.URL.Query().Get("proc") != DefaultProcessID {
			t.Fatalf("unexpected request: %s?%s", r.URL.Path, r.URL.RawQuery)
		}
		address := r.URL.Query().Get("addr")
		length := r.URL.Query().Get("len")
		reads = append(reads, address+":"+length)
		values := map[string]string{
			"0190EBB0": "10000000",
			"0190EBB4": "10001000",
			"10000140": "41480000C050000043000000",
			"10001140": "3F8000004000000040400000",
			"100011B8": "3F800000",
		}
		if value, ok := values[address]; ok {
			fmt.Fprintf(w, `<textarea id="output">%s</textarea>`, value)
			return
		}
		t.Fatalf("unexpected address %s", address)
	}))
	defer server.Close()

	inspection, err := testClient(server).InspectEntityPointers(context.Background(), "192.168.0.62")
	if err != nil {
		t.Fatal(err)
	}
	if inspection.Player.RootAddress != PlayerPointerAddress || inspection.Player.Pointer != 0x10000000 || inspection.Player.PointerRawHex != "10000000" || inspection.Player.PointerState != PointerStateValid {
		t.Fatalf("unexpected player pointer: %#v", inspection.Player)
	}
	if inspection.Player.CoordinatesAddress != 0x10000140 || inspection.Player.CoordinatesRawHex != "41480000C050000043000000" || inspection.Player.CoordinatesState != "valid" {
		t.Fatalf("unexpected player coordinates: %#v", inspection.Player)
	}
	if inspection.Player.X == nil || *inspection.Player.X != 12.5 || inspection.Player.Y == nil || *inspection.Player.Y != -3.25 || inspection.Player.Z == nil || *inspection.Player.Z != 128 {
		t.Fatalf("unexpected decoded player coordinates: %#v", inspection.Player)
	}
	if inspection.Target.RootAddress != TargetPointerAddress || inspection.Target.Pointer != 0x10001000 || inspection.Target.PointerRawHex != "10001000" || inspection.Target.PointerState != PointerStateValid {
		t.Fatalf("unexpected target pointer: %#v", inspection.Target)
	}
	if inspection.Target.CoordinatesAddress != 0x10001140 || inspection.Target.CoordinatesRawHex != "3F8000004000000040400000" || inspection.Target.CoordinatesState != "valid" {
		t.Fatalf("unexpected target coordinates: %#v", inspection.Target)
	}
	if inspection.Target.X == nil || *inspection.Target.X != 1 || inspection.Target.Y == nil || *inspection.Target.Y != 2 || inspection.Target.Z == nil || *inspection.Target.Z != 3 {
		t.Fatalf("unexpected decoded target coordinates: %#v", inspection.Target)
	}
	if inspection.Target.ActivationAddress != 0x100011B8 || inspection.Target.ActivationRawHex != "3F800000" || inspection.Target.ActivationState != "valid" || inspection.Target.ActivationValue == nil || *inspection.Target.ActivationValue != 1 {
		t.Fatalf("unexpected target activation field: %#v", inspection.Target)
	}
	wantReads := "0190EBB0:4|0190EBB4:4|10000140:12|10001140:12|100011B8:4"
	if strings.Join(reads, "|") != wantReads {
		t.Fatalf("memory reads = %#v, want %s", reads, wantReads)
	}
}

func TestInspectEntityPointersRepresentsNullInvalidSelfAndNonFiniteFields(t *testing.T) {
	t.Run("null and invalid roots are not dereferenced", func(t *testing.T) {
		calls := 0
		server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			calls++
			value := map[string]string{"0190EBB0": "00000000", "0190EBB4": "10000001"}[r.URL.Query().Get("addr")]
			fmt.Fprintf(w, `<textarea id="output">%s</textarea>`, value)
		}))
		defer server.Close()
		inspection, err := testClient(server).InspectEntityPointers(context.Background(), "192.168.0.62")
		if err != nil {
			t.Fatal(err)
		}
		if calls != 2 || inspection.Player.PointerState != PointerStateNull || inspection.Player.PointerRawHex != "00000000" || inspection.Player.CoordinatesAddress != 0 {
			t.Fatalf("unexpected null pointer result/calls: %#v, %d", inspection.Player, calls)
		}
		if calls != 2 || inspection.Target.PointerState != PointerStateInvalid || inspection.Target.PointerRawHex != "10000001" || inspection.Target.PointerError == "" || inspection.Target.ActivationAddress != 0 {
			t.Fatalf("unexpected invalid pointer result/calls: %#v, %d", inspection.Target, calls)
		}
	})

	t.Run("self target is marked and remains read-only", func(t *testing.T) {
		var reads []string
		server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			address := r.URL.Query().Get("addr")
			reads = append(reads, address)
			value := map[string]string{
				"0190EBB0": "10000000", "0190EBB4": "10000000",
				"10000140": "3F8000004000000040400000", "100001B8": "3F800000",
			}[address]
			fmt.Fprintf(w, `<textarea id="output">%s</textarea>`, value)
		}))
		defer server.Close()
		inspection, err := testClient(server).InspectEntityPointers(context.Background(), "192.168.0.62")
		if err != nil {
			t.Fatal(err)
		}
		if inspection.Target.PointerState != PointerStateSameAsPlayer || inspection.Target.PointerNote == "" || inspection.Target.CoordinatesAddress != 0x10000140 || inspection.Target.ActivationAddress != 0x100001B8 {
			t.Fatalf("self target was not identified/read-only inspected: %#v", inspection.Target)
		}
		if strings.Join(reads, "|") != "0190EBB0|0190EBB4|10000140|10000140|100001B8" {
			t.Fatalf("unexpected self-target reads: %#v", reads)
		}
	})

	t.Run("non-finite values retain raw bytes and are JSON-safe", func(t *testing.T) {
		server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			value := map[string]string{
				"0190EBB0": "10000000", "0190EBB4": "10001000",
				"10000140": "7FC000003F80000040000000",
				"10001140": "3F8000004000000040400000",
				"100011B8": "7F800000",
			}[r.URL.Query().Get("addr")]
			fmt.Fprintf(w, `<textarea id="output">%s</textarea>`, value)
		}))
		defer server.Close()
		inspection, err := testClient(server).InspectEntityPointers(context.Background(), "192.168.0.62")
		if err != nil {
			t.Fatal(err)
		}
		if inspection.Player.CoordinatesState != "non_finite" || inspection.Player.X != nil || inspection.Player.Y == nil || *inspection.Player.Y != 1 || inspection.Player.CoordinatesRawHex != "7FC000003F80000040000000" {
			t.Fatalf("unexpected non-finite coordinate result: %#v", inspection.Player)
		}
		if inspection.Target.ActivationState != "non_finite" || inspection.Target.ActivationValue != nil || inspection.Target.ActivationRawHex != "7F800000" {
			t.Fatalf("unexpected non-finite activation result: %#v", inspection.Target)
		}
		if _, err := json.Marshal(inspection); err != nil {
			t.Fatalf("non-finite inspection cannot be JSON encoded: %v", err)
		}
	})
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
