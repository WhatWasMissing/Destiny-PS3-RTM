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

func TestInspectAndSetPlayerScaleResolvesSourcePointerChainWithoutRPC(t *testing.T) {
	memory := newPlayerScaleTestMemory()
	server := httptest.NewServer(http.HandlerFunc(memory.serveHTTP))
	defer server.Close()
	client := testClient(server)

	state, err := client.InspectPlayerScale(context.Background(), "192.168.0.62")
	if err != nil {
		t.Fatal(err)
	}
	if state.PlayerName != "TestGuardian" || state.Handle != 0x12345 || state.HandleTableAddress != 0x100768A0 || state.ResolvedObject != 0x100668A0 {
		t.Fatalf("unexpected player-scale resolver state: %#v", state)
	}
	if state.ObjectIndex != 42 || state.Slot != 42 || state.ScaleAddress != 0x1008201C || state.Scale != 1 || state.ScaleRawHex != "3F800000" {
		t.Fatalf("unexpected player-scale target: %#v", state)
	}
	if !state.ResolverSignatureMatch || state.SnapshotAtomic || memory.writeRequests != 0 {
		t.Fatalf("inspection flags or side effects are wrong: %#v; writes=%d", state, memory.writeRequests)
	}

	updated, err := client.SetPlayerScale(context.Background(), "192.168.0.62", 5)
	if err != nil {
		t.Fatal(err)
	}
	if updated.NewScale != 5 || !updated.ReadbackExact || updated.ReadbackRawHex != "40A00000" || updated.Scale != 5 || memory.writeRequests != 1 {
		t.Fatalf("scale write was not verified: %#v; writes=%d", updated, memory.writeRequests)
	}
	if got := hex.EncodeToString(memory.read(0x1008201C, 4)); got != "40a00000" {
		t.Fatalf("memory scale = %s, want 40a00000", got)
	}

	minimumScale, err := client.SetPlayerScale(context.Background(), "192.168.0.62", 0.3)
	if err != nil {
		t.Fatal(err)
	}
	if minimumScale.ReadbackRawHex != "3E99999A" || memory.writeRequests != 2 {
		t.Fatalf("source 0.3 scale preset was not verified: %#v; writes=%d", minimumScale, memory.writeRequests)
	}
}

func TestSetPlayerScaleRejectsUnsupportedCodeAndOutOfRangeValuesBeforeWriting(t *testing.T) {
	for _, scale := range []float32{0.29, 20.01} {
		t.Run(fmt.Sprintf("out of range %v", scale), func(t *testing.T) {
			memory := newPlayerScaleTestMemory()
			server := httptest.NewServer(http.HandlerFunc(memory.serveHTTP))
			defer server.Close()
			if _, err := testClient(server).SetPlayerScale(context.Background(), "192.168.0.62", scale); err == nil {
				t.Fatalf("scale %v outside the source range was accepted", scale)
			}
			if memory.writeRequests != 0 || memory.readRequests != 0 {
				t.Fatalf("invalid scale reached memory: reads=%d writes=%d", memory.readRequests, memory.writeRequests)
			}
		})
	}

	t.Run("resolver mismatch", func(t *testing.T) {
		memory := newPlayerScaleTestMemory()
		memory.set(PlayerScaleResolverAddress, make([]byte, len(playerScaleResolverSignature)/2))
		server := httptest.NewServer(http.HandlerFunc(memory.serveHTTP))
		defer server.Close()
		_, err := testClient(server).SetPlayerScale(context.Background(), "192.168.0.62", 2)
		if err == nil || !strings.Contains(err.Error(), "resolver bytes do not match") {
			t.Fatalf("expected source-code mismatch, got %v", err)
		}
		if memory.writeRequests != 0 {
			t.Fatalf("unsupported resolver reached memory write: %d", memory.writeRequests)
		}
	})
	t.Run("lookup helper mismatch", func(t *testing.T) {
		memory := newPlayerScaleTestMemory()
		memory.set(PlayerScaleLookupGetterAddress, make([]byte, len(playerScaleLookupGetterSignature)/2))
		server := httptest.NewServer(http.HandlerFunc(memory.serveHTTP))
		defer server.Close()
		_, err := testClient(server).SetPlayerScale(context.Background(), "192.168.0.62", 2)
		if err == nil || !strings.Contains(err.Error(), "lookup helper bytes do not match") {
			t.Fatalf("expected helper-code mismatch, got %v", err)
		}
		if memory.writeRequests != 0 {
			t.Fatalf("unsupported helper reached memory write: %d", memory.writeRequests)
		}
	})
}

type playerScaleTestMemory struct {
	data          map[uint32]byte
	readRequests  int
	writeRequests int
}

func newPlayerScaleTestMemory() *playerScaleTestMemory {
	memory := &playerScaleTestMemory{data: make(map[uint32]byte)}
	resolver, _ := hex.DecodeString(playerScaleResolverSignature)
	getter, _ := hex.DecodeString(playerScaleLookupGetterSignature)
	memory.set(PlayerScaleResolverAddress, resolver)
	memory.set(PlayerScaleLookupGetterAddress, getter)
	consumer, _ := hex.DecodeString("3C600044C03601283CC03F803B4349EC3061034462C4000063850000C05A0000")
	plateHook, _ := hex.DecodeString("3063FFF44BFFFB15")
	memory.set(0x00444B64, consumer)
	memory.set(0x00A015AC, plateHook)
	memory.u32(PlayerNamePointerAddress, 0x10000000)
	memory.u32(0x10000000+0x90, 0)
	memory.ascii(0x10000000+0x90, 32, "TestGuardian")
	memory.u32(PlayerObjectsListAddress, 0x10010000)
	memory.u32(0x10010004, 0x10020000)
	objectRecord := uint32(0x10020000 + 2*PlayerScaleObjectsStride)
	memory.ascii(objectRecord+PlayerScaleNameOffset, 32, "TestGuardian")
	memory.u32(objectRecord+PlayerScaleHandleOffset, 0x12345)
	memory.u32(PlayerScaleLookupAddress+8, 0x10070000)
	memory.u32(PlayerScaleLookupAddress+12, 0x20)
	memory.u32(PlayerScaleLookupAddress+0x24, 0xFFFF0000)
	lookup := uint32(0x10070000 + 0x345*0x20)
	memory.u32(lookup+8, 0x00010000)
	memory.u32(lookup-0x10000+0x1C, 42)
	memory.u32(PlayerScaleObjectsAddress, 0x10080000)
	memory.u32(0x1008201C, 0x3F800000)
	return memory
}

func (m *playerScaleTestMemory) set(address uint32, data []byte) {
	for offset, value := range data {
		m.data[address+uint32(offset)] = value
	}
}

func (m *playerScaleTestMemory) read(address uint32, length int) []byte {
	data := make([]byte, length)
	for offset := range data {
		data[offset] = m.data[address+uint32(offset)]
	}
	return data
}

func (m *playerScaleTestMemory) u32(address uint32, value uint32) {
	data := make([]byte, 4)
	binary.BigEndian.PutUint32(data, value)
	m.set(address, data)
}

func (m *playerScaleTestMemory) ascii(address uint32, maxLength int, value string) {
	data := make([]byte, maxLength)
	copy(data, value)
	m.set(address, data)
}

func (m *playerScaleTestMemory) serveHTTP(w http.ResponseWriter, r *http.Request) {
	address64, err := strconv.ParseUint(r.URL.Query().Get("addr"), 16, 32)
	if err != nil {
		http.Error(w, "bad address", http.StatusBadRequest)
		return
	}
	address := uint32(address64)
	switch r.URL.Path {
	case "/getmem.ps3mapi":
		m.readRequests++
		length, err := strconv.Atoi(r.URL.Query().Get("len"))
		if err != nil || length < 1 || length > 0x10000 {
			http.Error(w, "bad length", http.StatusBadRequest)
			return
		}
		fmt.Fprintf(w, `<textarea id="output">%s</textarea>`, strings.ToUpper(hex.EncodeToString(m.read(address, length))))
	case "/setmem.ps3mapi":
		m.writeRequests++
		data, err := hex.DecodeString(r.URL.Query().Get("val"))
		if err != nil || len(data) < 1 || len(data) > 8 {
			http.Error(w, "bad value", http.StatusBadRequest)
			return
		}
		m.set(address, data)
		fmt.Fprint(w, "OK")
	default:
		http.Error(w, "unexpected path", http.StatusNotFound)
	}
}
