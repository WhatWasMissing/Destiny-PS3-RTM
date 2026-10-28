package rtmclient

import (
	"context"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strconv"
	"strings"
	"testing"
)

type actorPoolTestConsole struct {
	descriptor         []byte
	finalDescriptor    []byte
	profileFailurePass int
	codeFailurePass    int
	profilePasses      int
	codeReads          int
	descriptorReads    int
	requests           int
	unexpectedRequests int
}

func actorPoolTestDescriptor(storage, stride, capacity uint32) []byte {
	data := make([]byte, actorPoolDescriptorBytes)
	binary.BigEndian.PutUint32(data[0:4], 0x11223344)
	binary.BigEndian.PutUint32(data[4:8], 0x55667788)
	binary.BigEndian.PutUint32(data[8:12], storage)
	binary.BigEndian.PutUint32(data[12:16], stride)
	binary.BigEndian.PutUint32(data[16:20], capacity)
	return data
}

func (console *actorPoolTestConsole) serveHTTP(w http.ResponseWriter, r *http.Request) {
	console.requests++
	if r.Method != http.MethodGet || r.URL.Path != "/getmem.ps3mapi" || r.URL.Query().Get("proc") != DefaultProcessID {
		console.unexpectedRequests++
		http.Error(w, "unexpected request", http.StatusBadRequest)
		return
	}
	address := r.URL.Query().Get("addr")
	length, _ := strconv.Atoi(r.URL.Query().Get("len"))
	if address == fmt.Sprintf("%08X", actorPoolDescriptorAddress) && length == actorPoolDescriptorBytes {
		console.descriptorReads++
		data := console.descriptor
		if console.descriptorReads > 1 && console.finalDescriptor != nil {
			data = console.finalDescriptor
		}
		fmt.Fprintf(w, `<textarea id="output">%s</textarea>`, hex.EncodeToString(data))
		return
	}
	if address == fmt.Sprintf("%08X", actorPoolCodeAddress) && length == len(actorPoolCodeSignature)/2 {
		console.codeReads++
		code := actorPoolCodeSignature
		if console.codeReads == console.codeFailurePass {
			code = code[:len(code)-2] + "00"
		}
		fmt.Fprintf(w, `<textarea id="output">%s</textarea>`, code)
		return
	}
	for index, signature := range destiny0124Signatures {
		if address == fmt.Sprintf("%08X", signature.address) && length == len(signature.hex)/2 {
			if index == 0 {
				console.profilePasses++
			}
			code := signature.hex
			if index == 0 && console.profilePasses == console.profileFailurePass {
				code = strings.Repeat("00", length)
			}
			fmt.Fprintf(w, `<textarea id="output">%s</textarea>`, code)
			return
		}
	}
	console.unexpectedRequests++
	http.Error(w, "backing-storage or unexpected read refused", http.StatusBadRequest)
}

func TestReadActorPoolDescriptorRechecksProfileAndDescriptorWithoutFollowingStorage(t *testing.T) {
	console := &actorPoolTestConsole{descriptor: actorPoolTestDescriptor(0x30010000, 0x28D0, 40)}
	server := httptest.NewServer(http.HandlerFunc(console.serveHTTP))
	defer server.Close()
	snapshot, err := testClient(server).ReadActorPoolDescriptor(context.Background(), "192.168.1.100")
	if err != nil {
		t.Fatal(err)
	}
	if snapshot.Profile != "BLES01857 01.24 signature profile" || snapshot.DescriptorAddress != "0x01A1E160" || snapshot.StoragePointer != "0x30010000" || snapshot.Word0 != "0x11223344" || snapshot.Word4 != "0x55667788" {
		t.Fatalf("unexpected descriptor identity: %#v", snapshot)
	}
	if snapshot.Stride != 0x28D0 || snapshot.Capacity != 40 || snapshot.BackingBytes != 0x66080 || !snapshot.Initialized || snapshot.RecordsRead != 0 || snapshot.Atomic {
		t.Fatalf("unexpected descriptor fields or claims: %#v", snapshot)
	}
	if console.profilePasses != 2 || console.codeReads != 2 || console.descriptorReads != 2 || console.requests != 14 || console.unexpectedRequests != 0 {
		t.Fatalf("inspection did not use only bounded profile/code/descriptor reads: %#v", console)
	}
}

func TestReadActorPoolDescriptorAllowsUninitializedExpectedDimensions(t *testing.T) {
	for _, dimensions := range [][2]uint32{{0, 0}, {0x28D0, 40}, {0, 40}, {0x28D0, 0}} {
		t.Run(fmt.Sprintf("%X_%d", dimensions[0], dimensions[1]), func(t *testing.T) {
			console := &actorPoolTestConsole{descriptor: actorPoolTestDescriptor(0, dimensions[0], dimensions[1])}
			server := httptest.NewServer(http.HandlerFunc(console.serveHTTP))
			defer server.Close()
			snapshot, err := testClient(server).ReadActorPoolDescriptor(context.Background(), "192.168.1.100")
			if err != nil || snapshot.Initialized || snapshot.RecordsRead != 0 {
				t.Fatalf("uninitialized descriptor: %#v, %v", snapshot, err)
			}
		})
	}
}

func TestReadActorPoolDescriptorRejectsUnexpectedDimensionsAndBounds(t *testing.T) {
	cases := []struct {
		name                      string
		storage, stride, capacity uint32
	}{
		{"unaligned", 0x10000001, 0x28D0, 40},
		{"below user memory", 0xFFFC, 0x28D0, 40},
		{"above Blackrock spans", 0x80000000, 0x28D0, 40},
		{"span exceeds low Blackrock span", 0x01C7FFFC, 0x28D0, 40},
		{"stride mismatch", 0x30010000, 0x28E0, 40},
		{"capacity mismatch", 0x30010000, 0x28D0, 41},
		{"initialized zero stride", 0x30010000, 0, 40},
		{"initialized zero capacity", 0x30010000, 0x28D0, 0},
		{"null unexpected stride", 0, 0x28E0, 40},
	}
	for _, item := range cases {
		t.Run(item.name, func(t *testing.T) {
			console := &actorPoolTestConsole{descriptor: actorPoolTestDescriptor(item.storage, item.stride, item.capacity)}
			server := httptest.NewServer(http.HandlerFunc(console.serveHTTP))
			defer server.Close()
			snapshot, err := testClient(server).ReadActorPoolDescriptor(context.Background(), "192.168.1.100")
			if err == nil || snapshot.StoragePointer != "" || console.descriptorReads != 1 || console.unexpectedRequests != 0 {
				t.Fatalf("unexpected descriptor accepted or dereferenced: %#v, err=%v, console=%#v", snapshot, err, console)
			}
		})
	}
}

func TestReadActorPoolDescriptorDiscardsChangedDescriptor(t *testing.T) {
	console := &actorPoolTestConsole{
		descriptor:      actorPoolTestDescriptor(0x30010000, 0x28D0, 40),
		finalDescriptor: actorPoolTestDescriptor(0x30011000, 0x28D0, 40),
	}
	server := httptest.NewServer(http.HandlerFunc(console.serveHTTP))
	defer server.Close()
	snapshot, err := testClient(server).ReadActorPoolDescriptor(context.Background(), "192.168.1.100")
	if err == nil || !strings.Contains(err.Error(), "changed during") || snapshot.StoragePointer != "" || console.unexpectedRequests != 0 {
		t.Fatalf("changed descriptor result was not discarded: %#v, %v", snapshot, err)
	}
}

func TestReadActorPoolDescriptorRejectsInitialAndFinalBuildOrFullCodeMismatch(t *testing.T) {
	for _, item := range []struct {
		name                  string
		profilePass, codePass int
		descriptorReads       int
	}{
		{"initial build", 1, 0, 0}, {"final build", 2, 0, 1},
		{"initial full code", 0, 1, 0}, {"final full code", 0, 2, 1},
	} {
		t.Run(item.name, func(t *testing.T) {
			console := &actorPoolTestConsole{descriptor: actorPoolTestDescriptor(0x30010000, 0x28D0, 40), profileFailurePass: item.profilePass, codeFailurePass: item.codePass}
			server := httptest.NewServer(http.HandlerFunc(console.serveHTTP))
			defer server.Close()
			snapshot, err := testClient(server).ReadActorPoolDescriptor(context.Background(), "192.168.1.100")
			if err == nil || snapshot.StoragePointer != "" || console.descriptorReads != item.descriptorReads || console.unexpectedRequests != 0 {
				t.Fatalf("code mismatch did not fail closed: %#v, err=%v, console=%#v", snapshot, err, console)
			}
		})
	}
}

func TestReadActorPoolDescriptorRejectsWrongProcessBeforeRequests(t *testing.T) {
	console := &actorPoolTestConsole{}
	server := httptest.NewServer(http.HandlerFunc(console.serveHTTP))
	defer server.Close()
	client := testClient(server)
	client.ProcessID = "1234"
	if _, err := client.ReadActorPoolDescriptor(context.Background(), "192.168.1.100"); err == nil || console.requests != 0 {
		t.Fatalf("wrong process reached console: err=%v requests=%d", err, console.requests)
	}
}
