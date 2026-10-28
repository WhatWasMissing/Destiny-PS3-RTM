package rtmclient

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"strings"
)

const (
	actorPoolDescriptorAddress uint32 = 0x01A1E160
	actorPoolDescriptorBytes          = 20
	actorPoolExpectedStride    uint32 = 0x28D0
	actorPoolExpectedCapacity  uint32 = 40
	actorPoolCodeAddress       uint32 = 0x00255FA8
	actorPoolCodeSignature            = "7C0802A648B42A5DF821FF21F80100F0607E00003C6001A23FE001A23063E16033FF1138809E01743BA00001548404FE80A3000C7C8429D6806300087F432014"
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
	if storage != 0 && (storage < 0x10000 || storage >= 0x40000000 || storage&3 != 0) {
		return empty, fmt.Errorf("actor-pool storage pointer 0x%08X is outside the bounded aligned user-memory range", storage)
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
	if storage != 0 && uint64(storage)+backingBytes > 0x40000000 {
		return empty, errors.New("actor-pool backing span exceeds the bounded user-memory range; storage was not followed")
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
