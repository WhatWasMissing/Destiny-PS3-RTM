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

const (
	testActorStorage       uint32 = 0x30010000
	testActorAllocator     uint32 = 0x30001000
	testActorBitmapDesc    uint32 = 0x30002000
	testActorBitmapWords   uint32 = 0x30003000
	testActorActivePlayer  uint32 = 0x30080000
	testActorAlternateRoot uint32 = 0x30081000
)

type actorPoolCandidatesConsole struct {
	memory               map[uint32][]byte
	bitmapWordReads      int
	activePlayerReads    int
	changeBitmapOnRead   int
	changePlayerOnRead   int
	changeIdentity       uint32
	identityReadsByBlock map[uint32]int
	mutateIdentityAfter  int
	requestedBytes       int
	maxRequestLength     int
	unexpectedRequests   int
}

func newActorPoolCandidatesConsole(allSlots bool) *actorPoolCandidatesConsole {
	console := &actorPoolCandidatesConsole{
		memory:               make(map[uint32][]byte),
		identityReadsByBlock: make(map[uint32]int),
		changeIdentity:       ^uint32(0),
		mutateIdentityAfter:  1,
	}
	descriptor := make([]byte, actorPoolFullDescriptorBytes)
	putActorWord(descriptor, 0x00, testActorAllocator)
	putActorWord(descriptor, 0x08, testActorStorage)
	putActorWord(descriptor, 0x0C, actorPoolExpectedStride)
	putActorWord(descriptor, 0x10, actorPoolExpectedCapacity)
	putActorWord(descriptor, 0x18, 0x10)
	putActorWord(descriptor, 0x1C, actorPoolExpectedStride)
	putActorWord(descriptor, 0x20, 0x000000FF)
	putActorWord(descriptor, 0x30, 0x00001234)
	console.memory[actorPoolDescriptorAddress] = descriptor

	header := make([]byte, actorAllocatorHeaderBytes)
	putActorWord(header, 0x08, testActorBitmapDesc)
	binary.BigEndian.PutUint16(header[0x1C:0x1E], uint16(actorPoolExpectedCapacity))
	console.memory[testActorAllocator] = header

	bitmapDesc := make([]byte, actorBitmapDescriptorBytes)
	putActorWord(bitmapDesc, 0x00, 0x1000)
	putActorWord(bitmapDesc, 0x04, 8192)
	putActorWord(bitmapDesc, 0x08, testActorBitmapWords)
	console.memory[testActorBitmapDesc] = bitmapDesc

	bitmapWords := make([]byte, 0x1000)
	if allSlots {
		binary.BigEndian.PutUint32(bitmapWords[0:4], ^uint32(0))
		binary.BigEndian.PutUint32(bitmapWords[4:8], 0x000000FF)
	} else {
		binary.BigEndian.PutUint32(bitmapWords[0:4], 0x80000009) // slots 0, 3, and 31
		binary.BigEndian.PutUint32(bitmapWords[4:8], 0x00000081) // slots 32 and 39
	}
	console.memory[testActorBitmapWords] = bitmapWords

	backing := make([]byte, int(actorPoolExpectedStride*actorPoolExpectedCapacity))
	for slot := uint32(0); slot < actorPoolExpectedCapacity; slot++ {
		offset := int(slot * actorPoolExpectedStride)
		putActorWord(backing, offset+0x10, 0x20+slot)
		putActorWord(backing, offset+0x24, 0x24000000|slot)
		putActorWord(backing, offset+0x28, 0x28000000|slot)
		putActorWord(backing, offset+0x30, 0x80000000|slot)
		owner := ^uint32(0)
		if slot == 32 {
			owner = 0
		}
		putActorWord(backing, offset+0x40, owner)
		putActorWord(backing, offset+0x44, 0x44000000|slot)
	}
	console.memory[testActorStorage] = backing
	console.memory[PlayerPointerAddress] = actorBytes(testActorActivePlayer)

	for _, signature := range destiny0124Signatures {
		console.memory[signature.address] = mustHexBytes(signature.hex)
	}
	console.memory[actorPoolCodeAddress] = mustHexBytes(actorPoolCodeSignature)
	console.memory[actorDatumCodeAddress] = mustHexBytes(actorDatumCodeSignature)
	console.memory[actorAllocateCodeAddress] = mustHexBytes(actorAllocateCodeSignature)
	return console
}

func (console *actorPoolCandidatesConsole) serveHTTP(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet || r.URL.Path != "/getmem.ps3mapi" || r.URL.Query().Get("proc") != DefaultProcessID {
		console.unexpectedRequests++
		http.Error(w, "unexpected or non-read request", http.StatusBadRequest)
		return
	}
	address64, err := strconv.ParseUint(r.URL.Query().Get("addr"), 16, 32)
	length, lengthErr := strconv.Atoi(r.URL.Query().Get("len"))
	if err != nil || lengthErr != nil || length < 1 || length > actorMaxReadBytes {
		console.unexpectedRequests++
		http.Error(w, "invalid bounded read", http.StatusBadRequest)
		return
	}
	address := uint32(address64)
	console.requestedBytes += length
	if length > console.maxRequestLength {
		console.maxRequestLength = length
	}
	if address == PlayerPointerAddress && length == 4 {
		console.activePlayerReads++
		pointer := testActorActivePlayer
		if console.changePlayerOnRead > 0 && console.activePlayerReads >= console.changePlayerOnRead {
			pointer = testActorAlternateRoot
		}
		fmt.Fprintf(w, `<textarea id="output">%X</textarea>`, pointer)
		return
	}
	if address == testActorBitmapWords && length == 8 {
		console.bitmapWordReads++
		data := append([]byte(nil), console.memory[testActorBitmapWords][:8]...)
		if console.changeBitmapOnRead > 0 && console.bitmapWordReads >= console.changeBitmapOnRead {
			data[0] ^= 0x01
		}
		fmt.Fprintf(w, `<textarea id="output">%s</textarea>`, hex.EncodeToString(data))
		return
	}
	var data []byte
	for start, region := range console.memory {
		end := uint64(start) + uint64(len(region))
		requestedEnd := address64 + uint64(length)
		if address64 >= uint64(start) && requestedEnd <= end {
			offset := int(address64 - uint64(start))
			data = append([]byte(nil), region[offset:offset+length]...)
			if console.changeIdentity != ^uint32(0) && address64 <= uint64(console.changeIdentity) && uint64(console.changeIdentity)+4 <= requestedEnd {
				console.identityReadsByBlock[address]++
				if console.identityReadsByBlock[address] > console.mutateIdentityAfter {
					identityOffset := int(uint64(console.changeIdentity) - address64)
					data[identityOffset+3] ^= 0x01
				}
			}
			break
		}
	}
	if len(data) != length {
		console.unexpectedRequests++
		http.Error(w, "unmapped or out-of-bounds read", http.StatusBadRequest)
		return
	}
	fmt.Fprintf(w, `<textarea id="output">%s</textarea>`, hex.EncodeToString(data))
}

func TestReadAllocatedActorCandidatesUsesAllocatorBitmapAndFullDatum(t *testing.T) {
	console := newActorPoolCandidatesConsole(false)
	server := httptest.NewServer(http.HandlerFunc(console.serveHTTP))
	defer server.Close()
	snapshot, err := testClient(server).ReadAllocatedActorCandidates(context.Background(), "192.168.1.100")
	if err != nil {
		t.Fatal(err)
	}
	wantSlots := []uint32{0, 3, 31, 32, 39}
	if snapshot.AllocatedCount != len(wantSlots) || len(snapshot.Candidates) != len(wantSlots) {
		t.Fatalf("wrong allocated candidate count: %#v", snapshot)
	}
	for i, slot := range wantSlots {
		candidate := snapshot.Candidates[i]
		if candidate.Slot != slot || candidate.IdentityAddress != fmt.Sprintf("0x%08X", testActorStorage+slot*actorPoolExpectedStride+0x10) {
			t.Fatalf("candidate ordering or identity address mismatch: %#v", candidate)
		}
		identity := uint32(0x20 + slot)
		wantDatum := ((identity & 0xFF) << 23) | ((0x1234 & 0x3FF) << 13) | slot
		if candidate.ActorDatum != fmt.Sprintf("0x%08X", wantDatum) || !candidate.ActorDatumAcceptedByHost {
			t.Fatalf("full datum mismatch for slot %d: %#v want 0x%08X", slot, candidate, wantDatum)
		}
		if candidate.RecordPreview.UnitDatumCandidate != fmt.Sprintf("0x%08X", 0x80000000|slot) {
			t.Fatalf("unit-datum candidate mismatch: %#v", candidate.RecordPreview)
		}
		if candidate.RecordPreview.MemberOwnerHandle != "0xFFFFFFFF" && slot != 32 {
			t.Fatalf("unexpected member-owner preview for slot %d: %#v", slot, candidate.RecordPreview)
		}
	}
	if snapshot.Atomic || snapshot.ActorRecordFieldBytes != len(wantSlots)*actorRecordPreviewBytes {
		t.Fatalf("snapshot consistency or read counts wrong: %#v", snapshot)
	}
	if console.unexpectedRequests != 0 || console.maxRequestLength > actorMaxReadBytes {
		t.Fatalf("reader escaped its bounded read profile: %#v", console)
	}
}

func TestReadAllocatedActorCandidatesCapsAtFortyAndBoundsReadBudget(t *testing.T) {
	console := newActorPoolCandidatesConsole(true)
	server := httptest.NewServer(http.HandlerFunc(console.serveHTTP))
	defer server.Close()
	snapshot, err := testClient(server).ReadAllocatedActorCandidates(context.Background(), "192.168.1.100")
	if err != nil {
		t.Fatal(err)
	}
	if snapshot.AllocatedCount != 40 || len(snapshot.Candidates) != 40 || snapshot.Candidates[39].Slot != 39 {
		t.Fatalf("candidate walk did not stop at descriptor capacity: count=%d rows=%d last=%#v", snapshot.AllocatedCount, len(snapshot.Candidates), snapshot.Candidates[len(snapshot.Candidates)-1])
	}
	if console.requestedBytes > 2*1024*1024 || console.maxRequestLength > actorMaxReadBytes || console.unexpectedRequests != 0 {
		t.Fatalf("actor-candidate read budget exceeded: requested=%d max-read=%d unexpected=%d", console.requestedBytes, console.maxRequestLength, console.unexpectedRequests)
	}
}

func TestReadAllocatedActorCandidatesReconstructsAlternateNamespaceAndKeepsInvalidSentinel(t *testing.T) {
	t.Run("alternate namespace encoding", func(t *testing.T) {
		console := newActorPoolCandidatesConsole(false)
		putActorWord(console.memory[actorPoolDescriptorAddress], 0x30, 0x40001234)
		server := httptest.NewServer(http.HandlerFunc(console.serveHTTP))
		defer server.Close()
		snapshot, err := testClient(server).ReadAllocatedActorCandidates(context.Background(), "192.168.1.100")
		if err != nil {
			t.Fatal(err)
		}
		candidate := snapshot.Candidates[0]
		identity := uint32(0x20) & 7
		wantDatum := uint32(0x80000000) | (identity << 27) | ((uint32(0x1234) & 0x3FFF) << 13)
		if candidate.ActorDatum != fmt.Sprintf("0x%08X", wantDatum) || !candidate.ActorDatumAcceptedByHost {
			t.Fatalf("alternate namespace datum mismatch: got %#v want 0x%08X", candidate, wantDatum)
		}
	})
	t.Run("allocated slot with host-invalid zero datum remains visible", func(t *testing.T) {
		console := newActorPoolCandidatesConsole(false)
		binary.BigEndian.PutUint32(console.memory[testActorBitmapWords][0:4], 1)
		binary.BigEndian.PutUint32(console.memory[testActorBitmapWords][4:8], 0)
		putActorWord(console.memory[actorPoolDescriptorAddress], 0x30, 0)
		putActorWord(console.memory[testActorStorage], 0x10, 0)
		server := httptest.NewServer(http.HandlerFunc(console.serveHTTP))
		defer server.Close()
		snapshot, err := testClient(server).ReadAllocatedActorCandidates(context.Background(), "192.168.1.100")
		if err != nil {
			t.Fatal(err)
		}
		if len(snapshot.Candidates) != 1 || snapshot.Candidates[0].Slot != 0 || snapshot.Candidates[0].ActorDatum != "0x00000000" || snapshot.Candidates[0].ActorDatumAcceptedByHost {
			t.Fatalf("allocated slot with invalid datum was hidden or mislabeled: %#v", snapshot)
		}
	})
}

func TestReadAllocatedActorCandidatesDiscardsChangingBitmapIdentityOrPlayer(t *testing.T) {
	tests := []struct {
		name       string
		configure  func(*actorPoolCandidatesConsole)
		wantSubstr string
	}{
		{"bitmap changed", func(c *actorPoolCandidatesConsole) { c.changeBitmapOnRead = 2 }, "bitmap changed"},
		{"identity changed", func(c *actorPoolCandidatesConsole) {
			c.changeIdentity = testActorStorage + 3*actorPoolExpectedStride + 0x10
		}, "identity for allocated slot 3 changed"},
		{"player session anchor changed", func(c *actorPoolCandidatesConsole) { c.changePlayerOnRead = 2 }, "active-player pointer changed"},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			console := newActorPoolCandidatesConsole(false)
			test.configure(console)
			server := httptest.NewServer(http.HandlerFunc(console.serveHTTP))
			defer server.Close()
			snapshot, err := testClient(server).ReadAllocatedActorCandidates(context.Background(), "192.168.1.100")
			if err == nil || !strings.Contains(err.Error(), test.wantSubstr) || len(snapshot.Candidates) != 0 {
				t.Fatalf("changed sample was not discarded: snapshot=%#v err=%v", snapshot, err)
			}
		})
	}
}

func TestReadAllocatedActorCandidatesRejectsInvalidMetadata(t *testing.T) {
	tests := []struct {
		name      string
		configure func(*actorPoolCandidatesConsole)
	}{
		{"allocator index limit", func(c *actorPoolCandidatesConsole) {
			binary.BigEndian.PutUint16(c.memory[testActorAllocator][0x1C:0x1E], 39)
		}},
		{"bitmap bit count", func(c *actorPoolCandidatesConsole) { putActorWord(c.memory[testActorBitmapDesc], 4, 39) }},
		{"bitmap byte length", func(c *actorPoolCandidatesConsole) { putActorWord(c.memory[testActorBitmapDesc], 0, 1) }},
		{"identity offset", func(c *actorPoolCandidatesConsole) {
			putActorWord(c.memory[actorPoolDescriptorAddress], 0x18, 0x100004)
		}},
		{"identity stride", func(c *actorPoolCandidatesConsole) { putActorWord(c.memory[actorPoolDescriptorAddress], 0x1C, 0x10001) }},
		{"bitmap pointer", func(c *actorPoolCandidatesConsole) { putActorWord(c.memory[testActorBitmapDesc], 8, 0x80000000) }},
		{"allocator signature", func(c *actorPoolCandidatesConsole) { c.memory[actorAllocateCodeAddress][0] ^= 0x01 }},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			console := newActorPoolCandidatesConsole(false)
			test.configure(console)
			server := httptest.NewServer(http.HandlerFunc(console.serveHTTP))
			defer server.Close()
			snapshot, err := testClient(server).ReadAllocatedActorCandidates(context.Background(), "192.168.1.100")
			if err == nil || len(snapshot.Candidates) != 0 {
				t.Fatalf("invalid allocator metadata was accepted: %#v err=%v", snapshot, err)
			}
		})
	}
}

func TestActorPoolMemorySpanMatchesBlackrockBounds(t *testing.T) {
	for _, test := range []struct {
		address uint32
		length  uint64
		want    bool
	}{
		{0x00010000, 4, true},
		{0x01C7FFFC, 4, true},
		{0x01C7FFFC, 5, false},
		{0x30000000, 0x50000000, true},
		{0x30000000, 0x50000001, false},
		{0x40000000, 4, true},
		{0x20000000, 4, false},
		{0x80000000, 4, false},
	} {
		if got := actorPoolMemorySpanAllowed(test.address, test.length); got != test.want {
			t.Errorf("actorPoolMemorySpanAllowed(%08X,%X)=%t want %t", test.address, test.length, got, test.want)
		}
	}
}

func putActorWord(data []byte, offset int, value uint32) {
	binary.BigEndian.PutUint32(data[offset:offset+4], value)
}

func actorBytes(value uint32) []byte {
	data := make([]byte, 4)
	putActorWord(data, 0, value)
	return data
}

func mustHexBytes(value string) []byte {
	data, err := hex.DecodeString(value)
	if err != nil {
		panic(err)
	}
	return data
}
