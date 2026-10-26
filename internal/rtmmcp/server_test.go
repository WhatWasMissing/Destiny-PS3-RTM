package rtmmcp

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"io"
	"strconv"
	"strings"
	"sync"
	"testing"

	"destiny-rtm/internal/rtmclient"
)

type fakeClient struct {
	mu sync.Mutex

	statusCode    int
	statusErr     error
	readData      []byte
	readErr       error
	writeErr      error
	notifyErr     error
	reads         int
	writes        [][3]string
	notifies      []string
	launches      []string
	launchErr     error
	position      rtmclient.PlayerPosition
	positionErr   error
	positionReads int
	blockRead     bool
	readStarted   chan struct{}
}

func (f *fakeClient) Status(context.Context, string) (int, error) {
	return f.statusCode, f.statusErr
}

func (f *fakeClient) ReadMemory(ctx context.Context, _, address string, length int) ([]byte, error) {
	f.mu.Lock()
	f.reads++
	block := f.blockRead
	started := f.readStarted
	data := append([]byte(nil), f.readData...)
	err := f.readErr
	f.mu.Unlock()
	if block {
		close(started)
		<-ctx.Done()
		return nil, ctx.Err()
	}
	if err != nil {
		return nil, err
	}
	if len(data) == 0 {
		data = make([]byte, length)
	}
	return data, nil
}

func (f *fakeClient) ReadPlayerPosition(context.Context, string) (rtmclient.PlayerPosition, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.positionReads++
	return f.position, f.positionErr
}

func (f *fakeClient) WriteMemory(_ context.Context, _, address string, data []byte) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.writes = append(f.writes, [3]string{address, string(data), strings.ToUpper(hexString(data))})
	return f.writeErr
}

func (f *fakeClient) Notify(_ context.Context, _, message string) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.notifies = append(f.notifies, message)
	return f.notifyErr
}

func (f *fakeClient) LaunchGame(_ context.Context, target, isoPath string) (rtmclient.LaunchResult, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.launches = append(f.launches, target+" "+isoPath)
	return rtmclient.LaunchResult{
		TargetIPv4: target, ISOPath: isoPath, GameStartConfirmed: false,
		Stages: []rtmclient.LaunchStage{{Name: "mount_iso", Acknowledged: true, HTTPStatus: 200}},
	}, f.launchErr
}

func hexString(data []byte) string {
	const digits = "0123456789ABCDEF"
	var out strings.Builder
	for _, b := range data {
		out.WriteByte(digits[b>>4])
		out.WriteByte(digits[b&0x0f])
	}
	return out.String()
}

func runServer(t *testing.T, client Client, input string) []map[string]any {
	t.Helper()
	var output bytes.Buffer
	if err := New(client).Serve(strings.NewReader(input), &output); err != nil && !errors.Is(err, io.EOF) {
		t.Fatalf("Serve() error: %v", err)
	}
	var responses []map[string]any
	decoder := json.NewDecoder(&output)
	for decoder.More() {
		var response map[string]any
		if err := decoder.Decode(&response); err != nil {
			t.Fatalf("decode MCP response: %v\n%s", err, output.String())
		}
		responses = append(responses, response)
	}
	return responses
}

func responseByID(t *testing.T, responses []map[string]any, id float64) map[string]any {
	t.Helper()
	for _, response := range responses {
		if response["id"] == id {
			return response
		}
	}
	t.Fatalf("response id %v not found: %#v", id, responses)
	return nil
}

func resultObject(t *testing.T, response map[string]any) map[string]any {
	t.Helper()
	result, ok := response["result"].(map[string]any)
	if !ok {
		t.Fatalf("response result is not an object: %#v", response)
	}
	return result
}

func structuredObject(t *testing.T, response map[string]any) map[string]any {
	t.Helper()
	result := resultObject(t, response)
	structured, ok := result["structuredContent"].(map[string]any)
	if !ok {
		t.Fatalf("structuredContent is not an object: %#v", result)
	}
	return structured
}

func toolRequest(id int, name, args string) string {
	return `{"jsonrpc":"2.0","id":` + strconv.Itoa(id) + `,"method":"tools/call","params":{"name":"` + name + `","arguments":` + args + `}}` + "\n"
}

func TestToolsListAdvertisesBoundedRTMTools(t *testing.T) {
	responses := runServer(t, &fakeClient{}, `{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"test","version":"1"}}}`+"\n"+`{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}`+"\n")
	initialize := resultObject(t, responseByID(t, responses, 1))
	if initialize["protocolVersion"] != protocolVersion {
		t.Fatalf("protocol version = %v", initialize["protocolVersion"])
	}
	list := resultObject(t, responseByID(t, responses, 2))
	tools, ok := list["tools"].([]any)
	if !ok || len(tools) != 7 {
		t.Fatalf("expected seven tools, got %#v", list["tools"])
	}
	for _, item := range tools {
		descriptor := item.(map[string]any)
		if descriptor["inputSchema"].(map[string]any)["additionalProperties"] != false {
			t.Fatalf("tool schema accepts unknown arguments: %#v", descriptor)
		}
	}
}

func TestReadAndWritesReturnStructuredVerifiedResults(t *testing.T) {
	client := &fakeClient{statusCode: 200, readData: []byte{0x3f, 0x80, 0, 0}, position: rtmclient.PlayerPosition{PlayerPointer: 0x10000000, CoordinatesAddress: 0x10000140, X: 12.5, Y: -3.25, Z: 128, RawHex: "41480000C050000043000000"}}
	input := toolRequest(1, "rtm_status", `{"target_ipv4":"192.168.0.62"}`) +
		toolRequest(2, "rtm_read_memory", `{"target_ipv4":"192.168.0.62","address":"006101C0"}`) +
		toolRequest(3, "rtm_write_memory", `{"target_ipv4":"192.168.0.62","address":"006101C0","hex":"3F800000"}`) +
		toolRequest(4, "rtm_set_brightness", `{"target_ipv4":"192.168.0.62","preset":"inverted"}`) +
		toolRequest(5, "rtm_read_player_position", `{"target_ipv4":"192.168.0.62"}`) +
		toolRequest(6, "rtm_launch_game", `{"target_ipv4":"192.168.0.62"}`)
	responses := runServer(t, client, input)
	if got := structuredObject(t, responseByID(t, responses, 1))["destiny_process_confirmed"]; got != false {
		t.Fatalf("status incorrectly claims Destiny is confirmed: %v", got)
	}
	if got := structuredObject(t, responseByID(t, responses, 2))["hex"]; got != "3F800000" {
		t.Fatalf("read hex = %v", got)
	}
	if got := structuredObject(t, responseByID(t, responses, 3))["verified"]; got != true {
		t.Fatalf("write was not marked verified: %v; response=%#v", got, responseByID(t, responses, 3))
	}
	brightness := structuredObject(t, responseByID(t, responses, 4))
	if brightness["hex"] != "C1A00000" || brightness["effect_confirmed"] != false {
		t.Fatalf("unexpected brightness result: %#v", brightness)
	}
	position := structuredObject(t, responseByID(t, responses, 5))
	if position["player_pointer"] != "0x10000000" || position["coordinates_address"] != "0x10000140" || position["x"] != 12.5 || position["y"] != -3.25 || position["z"] != float64(128) || position["read_only"] != true || client.positionReads != 1 {
		t.Fatalf("unexpected player position result: %#v", position)
	}
	launch := structuredObject(t, responseByID(t, responses, 6))
	if launch["iso_path"] != rtmclient.DefaultDestinyISOPath || launch["game_start_confirmed"] != false || launch["request_dispatched"] != true {
		t.Fatalf("unexpected launch result: %#v", launch)
	}
	if len(client.launches) != 1 || client.launches[0] != "192.168.0.62 "+rtmclient.DefaultDestinyISOPath {
		t.Fatalf("unexpected launch call: %#v", client.launches)
	}
	if len(client.writes) != 2 {
		t.Fatalf("unexpected writes: %#v", client.writes)
	}
	var sawGeneric, sawBrightness bool
	for _, write := range client.writes {
		if write[0] == "006101C0" && write[2] == "3F800000" {
			sawGeneric = true
		}
		if write[0] == "006101C0" && write[2] == "C1A00000" {
			sawBrightness = true
		}
	}
	if !sawGeneric || !sawBrightness {
		t.Fatalf("expected both verified values, got %#v", client.writes)
	}
}

func TestInvalidTargetArgumentsAndOversizedWritesFailBeforeBackend(t *testing.T) {
	client := &fakeClient{}
	responses := runServer(t, client,
		toolRequest(1, "rtm_status", `{"target_ipv4":"8.8.8.8"}`)+
			toolRequest(2, "rtm_write_memory", `{"target_ipv4":"192.168.0.62","address":"1000","hex":"001122334455667788"}`)+
			toolRequest(3, "rtm_read_memory", `{"target_ipv4":"192.168.0.62","address":"1000","length":65537}`)+
			toolRequest(4, "rtm_notify", `{"target_ipv4":"192.168.0.62","message":"hello","extra":true}`)+
			toolRequest(5, "rtm_read_memory", `{"target_ipv4":"192.168.0.62","address":"FFFFFFFF","length":2}`)+
			toolRequest(6, "rtm_write_memory", `{"target_ipv4":"192.168.0.62","address":"FFFFFFFF","hex":"0102"}`)+
			toolRequest(7, "rtm_launch_game", `{"target_ipv4":"192.168.0.62","iso_path":"/dev_hdd0/PS3ISO/../../play.ps3"}`)+
			toolRequest(8, "rtm_read_player_position", `{"target_ipv4":"8.8.8.8"}`),
	)
	for id := float64(1); id <= 8; id++ {
		result := resultObject(t, responseByID(t, responses, id))
		if result["isError"] != true {
			t.Fatalf("invalid request %v was accepted: %#v", id, result)
		}
	}
	if len(client.writes) != 0 || client.reads != 0 || len(client.notifies) != 0 || len(client.launches) != 0 || client.positionReads != 0 {
		t.Fatalf("invalid inputs reached backend: %#v", client)
	}
}

func TestNotificationDispatchIsNotReportedAsDelivery(t *testing.T) {
	client := &fakeClient{}
	responses := runServer(t, client, toolRequest(1, "rtm_notify", `{"target_ipv4":"192.168.0.62","message":"hello"}`))
	result := structuredObject(t, responseByID(t, responses, 1))
	if result["request_dispatched"] != true || result["delivery_confirmed"] != false || len(client.notifies) != 1 {
		t.Fatalf("unexpected notification result: %#v", result)
	}
}

func TestCancellationNotificationCancelsOutstandingToolCall(t *testing.T) {
	client := &fakeClient{blockRead: true, readStarted: make(chan struct{})}
	input := toolRequest(1, "rtm_read_memory", `{"target_ipv4":"192.168.0.62","address":"1000"}`) +
		`{"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":1,"reason":"test"}}` + "\n"
	responses := runServer(t, client, input)
	<-client.readStarted
	result := resultObject(t, responseByID(t, responses, 1))
	if result["isError"] != true {
		t.Fatalf("cancelled call should return an error result: %#v", result)
	}
	if structured := result["structuredContent"].(map[string]any); !strings.Contains(structured["error"].(string), "canceled") {
		t.Fatalf("call did not report cancellation: %#v", structured)
	}
}
