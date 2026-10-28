package rtmclient

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"sort"
	"strings"
)

const (
	actorPoolDescriptorAddress     uint32 = 0x01A1E160
	actorPoolDescriptorBytes              = 20
	actorPoolFullDescriptorBytes          = 0x34
	actorPoolExpectedStride        uint32 = 0x28D0
	actorPoolExpectedCapacity      uint32 = 40
	actorPoolCodeAddress           uint32 = 0x00255FA8
	actorPoolCodeSignature                = "7C0802A648B42A5DF821FF21F80100F0607E00003C6001A23FE001A23063E16033FF1138809E01743BA00001548404FE80A3000C7C8429D6806300087F432014"
	actorDatumCodeAddress          uint32 = 0x00108960
	actorDatumCodeSignature               = "54A704FE80A4001C7CA539D681040018812400083CC040007D08281480A40030"
	actorAllocateCodeAddress       uint32 = 0x00108A50
	actorAllocateCodeSignature            = "57A3E8FA8108000857A406FE392000017D2420307D28182E7D2423787C88192E"
	actorAllocatorHeaderBytes             = 0x20
	actorBitmapDescriptorBytes            = 0x0C
	actorRecordPreviewBytes               = 0x48
	actorMaxPoolSlots                     = 8192
	actorMaxBitmapBytes                   = 0x1000
	actorMaxReadBytes                     = 0x10000
	actorMaxTargetReadBytes               = int(actorPoolExpectedCapacity) * actorRecordPreviewBytes
	actorMaxCandidateCallReadBytes        = 0x10000
)

// ActorPoolDescriptorSnapshot describes only the fixed-address pool descriptor.
// It does not follow storage or establish which records represent live enemies.
type ActorPoolDescriptorSnapshot struct {
	Profile           string `json:"profile"`
	DescriptorAddress string `json:"descriptor_address"`
	Word0             string `json:"word_0"`
	Word4             string `json:"word_4"`
	StoragePointer    string `json:"storage_pointer"`
	Stride            uint32 `json:"stride"`
	Capacity          uint32 `json:"capacity"`
	BackingBytes      uint64 `json:"backing_bytes"`
	Initialized       bool   `json:"initialized"`
	RecordsRead       int    `json:"records_read"`
	Atomic            bool   `json:"atomic"`
	Note              string `json:"note"`
}

// AllocatedActorRecordPreview contains only fields Blackrock reads from the
// first 0x48 bytes of an actor record. Most are intentionally raw; +0x30 is a
// unit-datum candidate, not proof that the record is a live enemy.
type AllocatedActorRecordPreview struct {
	Word24             string `json:"word_24"`
	Word28             string `json:"word_28"`
	UnitDatumCandidate string `json:"unit_datum_candidate"`
	MemberOwnerHandle  string `json:"member_owner_handle_word"`
	MemberOwnerOffset  string `json:"member_owner_offset_word"`
}

// AllocatedActorCandidate is a slot whose allocator bitmap bit was set during
// this sample. The full datum distinguishes slot reuse; it is not a spawn tag.
type AllocatedActorCandidate struct {
	Slot                     uint32                      `json:"slot"`
	IdentityAddress          string                      `json:"identity_address"`
	IdentityWord             string                      `json:"identity_word"`
	ActorDatum               string                      `json:"actor_datum"`
	ActorDatumAcceptedByHost bool                        `json:"actor_datum_accepted_by_blackrock"`
	RecordAddress            string                      `json:"record_address"`
	RecordPreview            AllocatedActorRecordPreview `json:"record_preview"`
}

// ActorPoolCandidatesSnapshot reports allocated pool candidates only. Reads
// are sequential and cannot prove process-instance continuity or enemy status.
type ActorPoolCandidatesSnapshot struct {
	Profile                 string                    `json:"profile"`
	DescriptorAddress       string                    `json:"descriptor_address"`
	StoragePointer          string                    `json:"storage_pointer"`
	AllocatorHeaderPointer  string                    `json:"allocator_header_pointer"`
	BitmapDescriptorPointer string                    `json:"bitmap_descriptor_pointer"`
	BitmapWordsPointer      string                    `json:"bitmap_words_pointer"`
	Stride                  uint32                    `json:"stride"`
	Capacity                uint32                    `json:"capacity"`
	AllocatorIndexLimit     uint16                    `json:"allocator_index_limit"`
	BitmapByteLength        uint32                    `json:"bitmap_byte_length"`
	BitmapBitCount          uint32                    `json:"bitmap_bit_count"`
	PlayerPointer           string                    `json:"player_pointer"`
	AllocatedCount          int                       `json:"allocated_count"`
	IdentityWordsRead       int                       `json:"identity_words_read"`
	ActorRecordFieldBytes   int                       `json:"actor_record_field_bytes"`
	Atomic                  bool                      `json:"atomic"`
	Candidates              []AllocatedActorCandidate `json:"candidates"`
	Note                    string                    `json:"note"`
}

// ReadActorPoolDescriptor reads and rechecks the BLES 01.24 actor descriptor
// between fresh build/code checks. No backing-storage read or memory write occurs.
func (c *Client) ReadActorPoolDescriptor(ctx context.Context, ip string) (ActorPoolDescriptorSnapshot, error) {
	var empty ActorPoolDescriptorSnapshot
	if c == nil {
		return empty, errors.New("console client is unavailable")
	}
	process := c.ProcessID
	if process == "" {
		process = DefaultProcessID
	}
	if !strings.EqualFold(process, DefaultProcessID) {
		return empty, errors.New("actor-pool inspection must target Destiny process 0x1010200")
	}
	if err := c.inspectActorPoolProfile(ctx, ip); err != nil {
		return empty, err
	}
	before, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", actorPoolDescriptorAddress), actorPoolDescriptorBytes)
	if err != nil {
		return empty, fmt.Errorf("read actor-pool descriptor: %w", err)
	}
	if len(before) != actorPoolDescriptorBytes {
		return empty, errors.New("short actor-pool descriptor read")
	}
	storage := binary.BigEndian.Uint32(before[8:12])
	stride := binary.BigEndian.Uint32(before[12:16])
	capacity := binary.BigEndian.Uint32(before[16:20])
	backingBytes := uint64(stride) * uint64(capacity)
	if storage != 0 && (storage&3 != 0 || !actorPoolMemorySpanAllowed(storage, 1)) {
		return empty, fmt.Errorf("actor-pool storage pointer 0x%08X is outside Blackrock's bounded aligned memory spans", storage)
	}
	if stride != 0 && stride != actorPoolExpectedStride {
		return empty, fmt.Errorf("actor-pool stride 0x%X does not match BLES 01.24 stride 0x28D0", stride)
	}
	if capacity != 0 && capacity != actorPoolExpectedCapacity {
		return empty, fmt.Errorf("actor-pool capacity %d does not match BLES 01.24 capacity 40", capacity)
	}
	if storage != 0 && (stride != actorPoolExpectedStride || capacity != actorPoolExpectedCapacity) {
		return empty, errors.New("initialized actor-pool storage requires stride 0x28D0 and capacity 40")
	}
	if storage != 0 && !actorPoolMemorySpanAllowed(storage, backingBytes) {
		return empty, errors.New("actor-pool backing span exceeds Blackrock's bounded memory spans; storage was not followed")
	}
	if err := c.inspectActorPoolProfile(ctx, ip); err != nil {
		return empty, fmt.Errorf("actor-pool final profile check failed: %w", err)
	}
	after, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", actorPoolDescriptorAddress), actorPoolDescriptorBytes)
	if err != nil {
		return empty, fmt.Errorf("recheck actor-pool descriptor: %w", err)
	}
	if !bytes.Equal(before, after) {
		return empty, errors.New("actor-pool descriptor changed during the sample; result discarded")
	}
	return ActorPoolDescriptorSnapshot{
		Profile: "BLES01857 01.24 signature profile", DescriptorAddress: fmt.Sprintf("0x%08X", actorPoolDescriptorAddress),
		Word0: fmt.Sprintf("0x%08X", binary.BigEndian.Uint32(before[0:4])), Word4: fmt.Sprintf("0x%08X", binary.BigEndian.Uint32(before[4:8])),
		StoragePointer: fmt.Sprintf("0x%08X", storage), Stride: stride, Capacity: capacity, BackingBytes: backingBytes,
		Initialized: storage != 0, RecordsRead: 0, Atomic: false,
		Note: "Sequential read-only descriptor sample; backing storage was not followed. Record occupancy, loaded-enemy correlation, and spawn behavior remain unverified.",
	}, nil
}

// ReadAllocatedActorCandidates walks only the allocator's bounded bitmap and
// the identity/first-0x48-byte fields for its set slots. It uses the BLES 01.24
// actor stride (0x28D0), not Blackrock's Xbox/host reader stride (0x28E0).
func (c *Client) ReadAllocatedActorCandidates(ctx context.Context, ip string) (ActorPoolCandidatesSnapshot, error) {
	var empty ActorPoolCandidatesSnapshot
	if c == nil {
		return empty, errors.New("console client is unavailable")
	}
	process := c.ProcessID
	if process == "" {
		process = DefaultProcessID
	}
	if !strings.EqualFold(process, DefaultProcessID) {
		return empty, errors.New("actor-pool inspection must target Destiny process 0x1010200")
	}
	if err := c.inspectActorPoolAllocationProfile(ctx, ip); err != nil {
		return empty, err
	}
	playerPointer, err := c.readActorPoolPlayerAnchor(ctx, ip)
	if err != nil {
		return empty, err
	}
	descriptorBefore, err := c.readFullActorPoolDescriptor(ctx, ip)
	if err != nil {
		return empty, err
	}
	result := ActorPoolCandidatesSnapshot{
		Profile:           "BLES01857 01.24 signature profile",
		DescriptorAddress: fmt.Sprintf("0x%08X", actorPoolDescriptorAddress),
		PlayerPointer:     fmt.Sprintf("0x%08X", playerPointer),
		Atomic:            false,
		Candidates:        make([]AllocatedActorCandidate, 0, actorPoolExpectedCapacity),
		Note:              "Sequential read-only allocated-slot snapshot. Allocation is not live-enemy status or spawnability. Actor data may change between reads; PID/profile checks cannot identify a process restart at the same addresses or defeat ABA reuse.",
	}
	storage := binary.BigEndian.Uint32(descriptorBefore[8:12])
	stride := binary.BigEndian.Uint32(descriptorBefore[12:16])
	capacity := binary.BigEndian.Uint32(descriptorBefore[16:20])
	result.StoragePointer = fmt.Sprintf("0x%08X", storage)
	result.Stride = stride
	result.Capacity = capacity
	if storage == 0 {
		if (stride != 0 && stride != actorPoolExpectedStride) || (capacity != 0 && capacity != actorPoolExpectedCapacity) {
			return empty, errors.New("uninitialized actor-pool descriptor has unexpected BLES 01.24 dimensions")
		}
		if err := c.recheckActorPoolCandidateSample(ctx, ip, playerPointer, descriptorBefore, nil, nil, nil, nil, nil); err != nil {
			return empty, err
		}
		result.Note = "The BLES actor pool is not initialized in this sample. No allocator, bitmap, identity, or actor-record data was read."
		return result, nil
	}
	if stride != actorPoolExpectedStride || capacity != actorPoolExpectedCapacity {
		return empty, errors.New("initialized BLES actor pool does not match the verified 40-slot, 0x28D0 layout")
	}
	backingBytes := uint64(stride) * uint64(capacity)
	if storage&3 != 0 || !actorPoolMemorySpanAllowed(storage, backingBytes) {
		return empty, errors.New("actor-pool backing span is outside Blackrock's bounded memory spans")
	}
	allocatorPointer := binary.BigEndian.Uint32(descriptorBefore[0:4])
	if allocatorPointer&3 != 0 || !actorPoolMemorySpanAllowed(allocatorPointer, actorAllocatorHeaderBytes) {
		return empty, errors.New("actor allocator header pointer is null, unaligned, or outside Blackrock's bounded memory spans")
	}
	headerBefore, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", allocatorPointer), actorAllocatorHeaderBytes)
	if err != nil {
		return empty, fmt.Errorf("read actor allocator header: %w", err)
	}
	indexLimit := binary.BigEndian.Uint16(headerBefore[0x1C:0x1E])
	if indexLimit == 0 || indexLimit > actorMaxPoolSlots {
		return empty, fmt.Errorf("actor allocator index limit %d is outside 1..%d", indexLimit, actorMaxPoolSlots)
	}
	if uint32(indexLimit) < capacity {
		return empty, errors.New("actor allocator index limit is smaller than the BLES descriptor capacity; refusing a partial candidate list")
	}
	bitmapDescriptorPointer := binary.BigEndian.Uint32(headerBefore[8:12])
	if bitmapDescriptorPointer&3 != 0 || !actorPoolMemorySpanAllowed(bitmapDescriptorPointer, actorBitmapDescriptorBytes) {
		return empty, errors.New("actor allocator bitmap-descriptor pointer is null, unaligned, or outside Blackrock's bounded memory spans")
	}
	bitmapDescriptorBefore, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", bitmapDescriptorPointer), actorBitmapDescriptorBytes)
	if err != nil {
		return empty, fmt.Errorf("read actor allocator bitmap descriptor: %w", err)
	}
	bitmapByteLength := binary.BigEndian.Uint32(bitmapDescriptorBefore[0:4])
	bitmapBitCount := binary.BigEndian.Uint32(bitmapDescriptorBefore[4:8])
	bitmapWordsPointer := binary.BigEndian.Uint32(bitmapDescriptorBefore[8:12])
	if bitmapBitCount < capacity || bitmapBitCount > actorMaxPoolSlots {
		return empty, fmt.Errorf("actor bitmap bit count %d cannot cover all %d descriptor slots", bitmapBitCount, capacity)
	}
	requiredBitmapBytes := ((bitmapBitCount + 31) / 32) * 4
	if bitmapByteLength < requiredBitmapBytes || bitmapByteLength > actorMaxBitmapBytes {
		return empty, fmt.Errorf("actor bitmap byte length 0x%X is inconsistent with %d bits", bitmapByteLength, bitmapBitCount)
	}
	if bitmapWordsPointer&3 != 0 || !actorPoolMemorySpanAllowed(bitmapWordsPointer, uint64(bitmapByteLength)) {
		return empty, errors.New("actor bitmap words pointer is unaligned or its declared span is outside Blackrock's bounded memory spans")
	}
	bitmapWordsBytes := int(((capacity + 31) / 32) * 4)
	bitmapWordsBefore, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", bitmapWordsPointer), bitmapWordsBytes)
	if err != nil {
		return empty, fmt.Errorf("read actor allocation words: %w", err)
	}
	slots := make([]uint32, 0, capacity)
	for slot := uint32(0); slot < capacity; slot++ {
		word := binary.BigEndian.Uint32(bitmapWordsBefore[(slot/32)*4 : (slot/32+1)*4])
		if word&(uint32(1)<<(slot&31)) != 0 {
			slots = append(slots, slot)
		}
	}
	result.AllocatorHeaderPointer = fmt.Sprintf("0x%08X", allocatorPointer)
	result.BitmapDescriptorPointer = fmt.Sprintf("0x%08X", bitmapDescriptorPointer)
	result.BitmapWordsPointer = fmt.Sprintf("0x%08X", bitmapWordsPointer)
	result.AllocatorIndexLimit = indexLimit
	result.BitmapByteLength = bitmapByteLength
	result.BitmapBitCount = bitmapBitCount
	result.AllocatedCount = len(slots)
	identityOffset := binary.BigEndian.Uint32(descriptorBefore[0x18:0x1C])
	identityStride := binary.BigEndian.Uint32(descriptorBefore[0x1C:0x20])
	identityMask := binary.BigEndian.Uint32(descriptorBefore[0x20:0x24])
	namespace := binary.BigEndian.Uint32(descriptorBefore[0x30:0x34])
	if identityOffset > 0x100000 || identityStride == 0 || identityStride > 0x10000 {
		return empty, errors.New("actor identity offset or stride is outside Blackrock's verified metadata bounds")
	}
	identityTargets := make([]actorPoolReadTarget, 0, len(slots))
	recordTargets := make([]actorPoolReadTarget, 0, len(slots))
	for _, slot := range slots {
		identityAddress := uint64(storage) + uint64(slot)*uint64(identityStride) + uint64(identityOffset)
		if identityAddress > uint64(^uint32(0)) || identityAddress&3 != 0 || !actorPoolMemorySpanAllowed(uint32(identityAddress), 4) {
			return empty, fmt.Errorf("actor identity word for slot %d is unaligned or outside Blackrock's bounded memory spans", slot)
		}
		identityTargets = append(identityTargets, actorPoolReadTarget{slot: slot, address: uint32(identityAddress), length: 4})
		recordAddress := uint64(storage) + uint64(slot)*uint64(stride)
		if recordAddress > uint64(^uint32(0)) || recordAddress&3 != 0 || !actorPoolMemorySpanAllowed(uint32(recordAddress), actorRecordPreviewBytes) {
			return empty, fmt.Errorf("actor record preview for slot %d is outside Blackrock's bounded memory spans", slot)
		}
		recordTargets = append(recordTargets, actorPoolReadTarget{slot: slot, address: uint32(recordAddress), length: actorRecordPreviewBytes})
	}
	identityBefore, err := c.readActorPoolTargets(ctx, ip, identityTargets)
	if err != nil {
		return empty, fmt.Errorf("read actor identity words: %w", err)
	}
	recordPreviews, err := c.readActorPoolTargets(ctx, ip, recordTargets)
	if err != nil {
		return empty, fmt.Errorf("read allocated actor record previews: %w", err)
	}
	identityAfter, err := c.readActorPoolTargets(ctx, ip, identityTargets)
	if err != nil {
		return empty, fmt.Errorf("recheck actor identity words: %w", err)
	}
	if err := compareActorPoolTargets(identityBefore, identityAfter, slots); err != nil {
		return empty, err
	}
	for _, slot := range slots {
		identityWord := binary.BigEndian.Uint32(identityAfter[slot])
		identity := identityWord & identityMask
		var datum uint32
		if namespace&0x40000000 == 0 {
			datum = ((identity & 0xFF) << 23) | ((namespace & 0x3FF) << 13) | slot
		} else {
			datum = 0x80000000 | ((identity & 7) << 27) | ((namespace & 0x3FFF) << 13) | slot
		}
		preview := recordPreviews[slot]
		recordAddress := uint32(uint64(storage) + uint64(slot)*uint64(stride))
		result.Candidates = append(result.Candidates, AllocatedActorCandidate{
			Slot:                     slot,
			IdentityAddress:          fmt.Sprintf("0x%08X", identityTargetsForSlot(identityTargets, slot)),
			IdentityWord:             fmt.Sprintf("0x%08X", identityWord),
			ActorDatum:               fmt.Sprintf("0x%08X", datum),
			ActorDatumAcceptedByHost: datum != 0 && datum != ^uint32(0),
			RecordAddress:            fmt.Sprintf("0x%08X", recordAddress),
			RecordPreview: AllocatedActorRecordPreview{
				Word24:             fmt.Sprintf("0x%08X", binary.BigEndian.Uint32(preview[0x24:0x28])),
				Word28:             fmt.Sprintf("0x%08X", binary.BigEndian.Uint32(preview[0x28:0x2C])),
				UnitDatumCandidate: fmt.Sprintf("0x%08X", binary.BigEndian.Uint32(preview[0x30:0x34])),
				MemberOwnerHandle:  fmt.Sprintf("0x%08X", binary.BigEndian.Uint32(preview[0x40:0x44])),
				MemberOwnerOffset:  fmt.Sprintf("0x%08X", binary.BigEndian.Uint32(preview[0x44:0x48])),
			},
		})
	}
	result.IdentityWordsRead = len(slots) * 3
	result.ActorRecordFieldBytes = len(slots) * actorRecordPreviewBytes
	if err := c.recheckActorPoolCandidateSample(ctx, ip, playerPointer, descriptorBefore, headerBefore, bitmapDescriptorBefore, bitmapWordsBefore, identityTargets, identityAfter); err != nil {
		return empty, err
	}
	return result, nil
}

type actorPoolReadTarget struct {
	slot    uint32
	address uint32
	length  int
}

func (c *Client) readFullActorPoolDescriptor(ctx context.Context, ip string) ([]byte, error) {
	descriptor, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", actorPoolDescriptorAddress), actorPoolFullDescriptorBytes)
	if err != nil {
		return nil, fmt.Errorf("read full actor-pool descriptor: %w", err)
	}
	if len(descriptor) != actorPoolFullDescriptorBytes {
		return nil, errors.New("short full actor-pool descriptor read")
	}
	storage := binary.BigEndian.Uint32(descriptor[8:12])
	stride := binary.BigEndian.Uint32(descriptor[12:16])
	capacity := binary.BigEndian.Uint32(descriptor[16:20])
	if storage != 0 && (storage&3 != 0 || !actorPoolMemorySpanAllowed(storage, uint64(stride)*uint64(capacity))) {
		return nil, errors.New("actor-pool descriptor storage span is unaligned or outside Blackrock's bounded memory spans")
	}
	return descriptor, nil
}

func (c *Client) readActorPoolPlayerAnchor(ctx context.Context, ip string) (uint32, error) {
	data, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", PlayerPointerAddress), 4)
	if err != nil {
		return 0, fmt.Errorf("read active-player session anchor: %w", err)
	}
	if len(data) != 4 {
		return 0, errors.New("active-player session anchor read returned an invalid length")
	}
	pointer := binary.BigEndian.Uint32(data)
	if pointer < 0x10000 || pointer&3 != 0 || !actorPoolMemorySpanAllowed(pointer, uint64(PlayerPositionOffset+12)) {
		return 0, errors.New("an aligned active-player pointer in a loaded activity is required for actor-candidate enumeration")
	}
	return pointer, nil
}

func (c *Client) recheckActorPoolCandidateSample(ctx context.Context, ip string, playerPointer uint32, descriptor, header, bitmapDescriptor, bitmapWords []byte, identityTargets []actorPoolReadTarget, identitySnapshot map[uint32][]byte) error {
	if err := c.inspectActorPoolAllocationProfile(ctx, ip); err != nil {
		return fmt.Errorf("final actor-pool code/profile check failed: %w", err)
	}
	playerAfter, err := c.readActorPoolPlayerAnchor(ctx, ip)
	if err != nil {
		return err
	}
	if playerAfter != playerPointer {
		return errors.New("active-player pointer changed during the actor-candidate sample; result discarded")
	}
	descriptorAfter, err := c.readFullActorPoolDescriptor(ctx, ip)
	if err != nil {
		return err
	}
	if !bytes.Equal(descriptor, descriptorAfter) {
		return errors.New("actor-pool descriptor changed during the sample; result discarded")
	}
	if len(header) == 0 {
		return nil
	}
	allocatorPointer := binary.BigEndian.Uint32(descriptor[0:4])
	headerAfter, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", allocatorPointer), actorAllocatorHeaderBytes)
	if err != nil {
		return fmt.Errorf("recheck actor allocator header: %w", err)
	}
	if !bytes.Equal(header, headerAfter) {
		return errors.New("actor allocator header changed during the sample; result discarded")
	}
	bitmapPointer := binary.BigEndian.Uint32(header[8:12])
	bitmapDescriptorAfter, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", bitmapPointer), actorBitmapDescriptorBytes)
	if err != nil {
		return fmt.Errorf("recheck actor allocator bitmap descriptor: %w", err)
	}
	if !bytes.Equal(bitmapDescriptor, bitmapDescriptorAfter) {
		return errors.New("actor allocator bitmap descriptor changed during the sample; result discarded")
	}
	wordsPointer := binary.BigEndian.Uint32(bitmapDescriptor[8:12])
	if len(bitmapWords) != 0 {
		wordsAfter, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", wordsPointer), len(bitmapWords))
		if err != nil {
			return fmt.Errorf("recheck actor allocation words: %w", err)
		}
		if !bytes.Equal(bitmapWords, wordsAfter) {
			return errors.New("actor allocation bitmap changed during the sample; result discarded")
		}
	}
	if len(identityTargets) != 0 {
		identitiesAfter, err := c.readActorPoolTargets(ctx, ip, identityTargets)
		if err != nil {
			return fmt.Errorf("final actor identity recheck failed: %w", err)
		}
		if err := compareActorPoolTargets(identitySnapshot, identitiesAfter, targetSlots(identityTargets)); err != nil {
			return err
		}
	}
	return nil
}

func (c *Client) readActorPoolTargets(ctx context.Context, ip string, targets []actorPoolReadTarget) (map[uint32][]byte, error) {
	result := make(map[uint32][]byte, len(targets))
	if len(targets) == 0 {
		return result, nil
	}
	if len(targets) > int(actorPoolExpectedCapacity) {
		return nil, errors.New("actor-pool target count exceeds the BLES descriptor capacity")
	}
	ordered := append([]actorPoolReadTarget(nil), targets...)
	sort.Slice(ordered, func(i, j int) bool { return ordered[i].address < ordered[j].address })
	for _, target := range ordered {
		if target.length < 1 || target.length > actorRecordPreviewBytes || !actorPoolMemorySpanAllowed(target.address, uint64(target.length)) {
			return nil, errors.New("actor-pool target is outside its bounded read span")
		}
	}
	var totalReadBytes uint64
	for first := 0; first < len(ordered); {
		start := uint64(ordered[first].address)
		end := start + uint64(ordered[first].length)
		last := first + 1
		for last < len(ordered) {
			nextStart := uint64(ordered[last].address)
			nextEnd := uint64(ordered[last].address) + uint64(ordered[last].length)
			// Coalesce only overlapping or adjacent targets. Bridging a gap can
			// read unallocated actor slots or arbitrary memory between identities.
			if nextStart > end || nextEnd-start > actorMaxReadBytes {
				break
			}
			if nextEnd > end {
				end = nextEnd
			}
			last++
		}
		if end <= start || end-start > actorMaxReadBytes || end > 0x100000000 || !actorPoolMemorySpanAllowed(uint32(start), end-start) {
			return nil, errors.New("actor-pool grouped read exceeds its bounded span")
		}
		requestBytes := end - start
		if requestBytes > uint64(actorMaxTargetReadBytes) || totalReadBytes > uint64(actorMaxTargetReadBytes)-requestBytes {
			return nil, errors.New("actor-pool target reads exceed the aggregate candidate-byte limit")
		}
		block, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", uint32(start)), int(end-start))
		if err != nil {
			return nil, err
		}
		if len(block) != int(requestBytes) {
			return nil, errors.New("short actor-pool target read")
		}
		for _, target := range ordered[first:last] {
			offset := uint64(target.address) - start
			chunk := append([]byte(nil), block[offset:offset+uint64(target.length)]...)
			result[target.slot] = chunk
		}
		totalReadBytes += requestBytes
		first = last
	}
	return result, nil
}

func compareActorPoolTargets(before, after map[uint32][]byte, slots []uint32) error {
	for _, slot := range slots {
		if !bytes.Equal(before[slot], after[slot]) {
			return fmt.Errorf("actor identity for allocated slot %d changed during the sample; result discarded", slot)
		}
	}
	return nil
}

func targetSlots(targets []actorPoolReadTarget) []uint32 {
	slots := make([]uint32, 0, len(targets))
	for _, target := range targets {
		slots = append(slots, target.slot)
	}
	return slots
}

func identityTargetsForSlot(targets []actorPoolReadTarget, slot uint32) uint32 {
	for _, target := range targets {
		if target.slot == slot {
			return target.address
		}
	}
	return 0
}

func (c *Client) inspectActorPoolAllocationProfile(ctx context.Context, ip string) error {
	if err := c.inspectActorPoolProfile(ctx, ip); err != nil {
		return err
	}
	for _, signature := range []memorySignature{
		{address: actorDatumCodeAddress, hex: actorDatumCodeSignature},
		{address: actorAllocateCodeAddress, hex: actorAllocateCodeSignature},
	} {
		code, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", signature.address), len(signature.hex)/2)
		if err != nil {
			return fmt.Errorf("read actor allocation/datum signature at 0x%08X: %w", signature.address, err)
		}
		if strings.ToUpper(hex.EncodeToString(code)) != signature.hex {
			return fmt.Errorf("actor allocation/datum code signature mismatch at 0x%08X", signature.address)
		}
	}
	return nil
}

func actorPoolMemorySpanAllowed(address uint32, length uint64) bool {
	if length == 0 {
		return false
	}
	end := uint64(address) + length
	if end <= uint64(address) || end > 0x100000000 {
		return false
	}
	return (address >= 0x00010000 && end <= 0x01C80000) ||
		(address >= 0x30000000 && end <= 0x80000000)
}

func (c *Client) inspectActorPoolProfile(ctx context.Context, ip string) error {
	profile := c.InspectDestiny0124Profile(ctx, ip)
	if !profile.Matches {
		for _, signature := range profile.Signatures {
			if signature.Error != "" {
				return fmt.Errorf("cannot inspect actor pool: BLES profile read at %s failed: %s", signature.Address, signature.Error)
			}
			if !signature.Matches {
				return fmt.Errorf("cannot inspect actor pool: BLES01857 01.24 signature mismatch at %s", signature.Address)
			}
		}
		return errors.New("cannot inspect actor pool: BLES01857 01.24 profile did not verify")
	}
	code, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", actorPoolCodeAddress), len(actorPoolCodeSignature)/2)
	if err != nil {
		return fmt.Errorf("read actor-pool code signature: %w", err)
	}
	if strings.ToUpper(hex.EncodeToString(code)) != actorPoolCodeSignature {
		return errors.New("actor-pool 64-byte code signature does not match BLES 01.24")
	}
	return nil
}
