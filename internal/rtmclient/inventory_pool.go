package rtmclient

import (
	"context"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"strings"
)

const (
	profileManagerCell       uint32 = 0x01AD5FA4
	profileManagerBound      uint32 = 0x50000
	itemPoolDescriptorOffset uint32 = 0x49548
	itemPoolStorageOffset    uint32 = 0x40A0
	itemPoolRelativeLimit    uint32 = 0x45341
	itemPoolTypeHash         uint32 = 0x80801732
	itemPoolRecordStride     uint32 = 0xA8
	itemPoolCapacityLimit           = 646
	itemPoolReadChunkRecords        = 9
	itemPoolReadGroupChunks         = 18
)

type InventoryPoolItem struct {
	Slot      int `json:"slot"`
	ItemIndex int `json:"item_index"`
}

type InventoryPoolSnapshot struct {
	Profile       string              `json:"profile"`
	SlotCount     int                 `json:"slot_count"`
	OccupiedCount int                 `json:"occupied_count"`
	Stride        int                 `json:"stride"`
	TypeHash      string              `json:"type_hash"`
	Atomic        bool                `json:"atomic"`
	Items         []InventoryPoolItem `json:"items"`
	Note          string              `json:"note"`
}

type inventoryPoolDescriptor struct {
	manager        uint32
	descriptorAddr uint32
	descriptorRaw  [16]byte
	dataOffset     uint32
	count          int
	stride         uint32
	typeHash       uint32
	poolAddress    uint32
	byteLength     uint32
}

var liveInventoryCodeSignatures = [...]memorySignature{
	{address: 0x0047C1A0, hex: "3C6001AD30635FA0806300044E800020"},
	{address: 0x0047E3CC, hex: "78842EA478A526E47CE420147C8438143CE000047C83201460E793987C8428143CA00004788A00203C80000460A9939C"},
}

// ReadLiveItemPool returns occupied item-object slots from the BLES 01.24
// self-family pool. It is read-only, bounded to 646 0xA8-byte records, and
// suppresses instance and owner IDs from its result.
func (c *Client) ReadLiveItemPool(ctx context.Context, ip string) (InventoryPoolSnapshot, error) {
	result := InventoryPoolSnapshot{
		Profile:  "BLES01857 01.24 signature profile",
		Stride:   int(itemPoolRecordStride),
		TypeHash: "0x80801732",
		Atomic:   false,
		Note:     "Sequential, non-atomic live-memory sample. Instance and owner IDs are only used transiently and are not returned.",
		Items:    make([]InventoryPoolItem, 0, itemPoolCapacityLimit),
	}
	if c == nil {
		return result, errors.New("console client is unavailable")
	}
	process := c.ProcessID
	if process == "" {
		process = DefaultProcessID
	}
	if !strings.EqualFold(process, DefaultProcessID) {
		return result, errors.New("inventory inspection must target Destiny process 0x1010200")
	}

	if err := c.inspectLiveInventoryProfile(ctx, ip); err != nil {
		return result, err
	}
	descriptor, err := c.readInventoryPoolDescriptor(ctx, ip)
	if err != nil {
		return result, err
	}
	if descriptor.count < 1 || descriptor.count > itemPoolCapacityLimit {
		return result, fmt.Errorf("item-pool count %d is outside the verified 1–%d bound", descriptor.count, itemPoolCapacityLimit)
	}
	result.SlotCount = descriptor.count
	firstIdentity, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", descriptor.poolAddress), 8)
	if err != nil {
		return InventoryPoolSnapshot{}, fmt.Errorf("sample first item-pool boundary: %w", err)
	}
	lastAddress := uint32(uint64(descriptor.poolAddress) + uint64(descriptor.count-1)*uint64(itemPoolRecordStride))
	lastIdentity, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", lastAddress), 8)
	if err != nil {
		return InventoryPoolSnapshot{}, fmt.Errorf("sample last item-pool boundary: %w", err)
	}

	const recordsPerChunk = itemPoolReadChunkRecords
	chunksPerGroup := itemPoolReadGroupChunks
	for firstSlot := 0; firstSlot < descriptor.count; {
		groupEnd := firstSlot + recordsPerChunk*chunksPerGroup
		if groupEnd > descriptor.count {
			groupEnd = descriptor.count
		}
		if firstSlot != 0 {
			if err := c.inspectLiveInventoryProfile(ctx, ip); err != nil {
				return InventoryPoolSnapshot{}, err
			}
			current, err := c.readInventoryPoolDescriptor(ctx, ip)
			if err != nil {
				return InventoryPoolSnapshot{}, err
			}
			if !sameInventoryPoolDescriptor(descriptor, current) {
				return InventoryPoolSnapshot{}, errors.New("profile manager or item descriptor changed during the snapshot")
			}
		}

		for slot := firstSlot; slot < groupEnd; {
			records := recordsPerChunk
			if remaining := groupEnd - slot; remaining < records {
				records = remaining
			}
			readBytes := records * int(itemPoolRecordStride)
			address := uint64(descriptor.poolAddress) + uint64(slot)*uint64(itemPoolRecordStride)
			if address+uint64(readBytes) > 0x40000000 {
				return InventoryPoolSnapshot{}, errors.New("item-pool chunk exceeds the bounded PS3 user-memory range")
			}
			data, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", uint32(address)), readBytes)
			if err != nil {
				return InventoryPoolSnapshot{}, fmt.Errorf("read item-pool slots %d–%d: %w", slot, slot+records-1, err)
			}
			for index := 0; index < records; index++ {
				offset := index * int(itemPoolRecordStride)
				// The 64-bit item instance ID is intentionally reduced to a
				// zero/nonzero predicate; only the item index reaches the result.
				if binary.BigEndian.Uint64(data[offset:offset+8]) == 0 {
					continue
				}
				itemIndex := int(int16(binary.BigEndian.Uint16(data[offset+0x24 : offset+0x26])))
				result.Items = append(result.Items, InventoryPoolItem{Slot: slot + index, ItemIndex: itemIndex})
			}
			slot += records
		}
		firstSlot = groupEnd
	}

	if err := c.inspectLiveInventoryProfile(ctx, ip); err != nil {
		return InventoryPoolSnapshot{}, err
	}
	finalDescriptor, err := c.readInventoryPoolDescriptor(ctx, ip)
	if err != nil {
		return InventoryPoolSnapshot{}, err
	}
	if !sameInventoryPoolDescriptor(descriptor, finalDescriptor) {
		return InventoryPoolSnapshot{}, errors.New("final item-pool manager or descriptor recheck failed")
	}
	firstAgain, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", descriptor.poolAddress), 8)
	if err != nil {
		return InventoryPoolSnapshot{}, fmt.Errorf("recheck first item-pool boundary: %w", err)
	}
	lastAgain, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", lastAddress), 8)
	if err != nil {
		return InventoryPoolSnapshot{}, fmt.Errorf("recheck last item-pool boundary: %w", err)
	}
	if !equalBytes(firstAgain, firstIdentity) || !equalBytes(lastAgain, lastIdentity) {
		return InventoryPoolSnapshot{}, errors.New("item-pool boundary changed during the snapshot; partial result discarded")
	}

	result.OccupiedCount = len(result.Items)
	return result, nil
}

func (c *Client) inspectLiveInventoryProfile(ctx context.Context, ip string) error {
	profile := c.InspectDestiny0124Profile(ctx, ip)
	if !profile.Matches {
		for _, signature := range profile.Signatures {
			if signature.Error != "" {
				return fmt.Errorf("cannot inspect live inventory: BLES profile read at %s failed: %s", signature.Address, signature.Error)
			}
			if !signature.Matches {
				return fmt.Errorf("cannot inspect live inventory: BLES01857 01.24 signature mismatch at %s", signature.Address)
			}
		}
		return errors.New("cannot inspect live inventory: BLES01857 01.24 profile did not verify")
	}
	for _, signature := range liveInventoryCodeSignatures {
		data, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", signature.address), len(signature.hex)/2)
		if err != nil {
			return fmt.Errorf("read BLES inventory code signature at 0x%08X: %w", signature.address, err)
		}
		if strings.ToUpper(hex.EncodeToString(data)) != signature.hex {
			return fmt.Errorf("BLES inventory code signature mismatch at 0x%08X", signature.address)
		}
	}
	return nil
}

func (c *Client) readInventoryPoolDescriptor(ctx context.Context, ip string) (inventoryPoolDescriptor, error) {
	var result inventoryPoolDescriptor
	managerBytes, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", profileManagerCell), 4)
	if err != nil {
		return result, fmt.Errorf("read profile-manager pointer: %w", err)
	}
	manager := binary.BigEndian.Uint32(managerBytes)
	managerEnd := uint64(manager) + uint64(profileManagerBound)
	if manager < 0x10000 || manager&3 != 0 || managerEnd > 0x40000000 {
		return result, fmt.Errorf("profile-manager pointer 0x%08X is outside the bounded region", manager)
	}
	descriptorAddress := uint32(uint64(manager) + uint64(itemPoolDescriptorOffset))
	descriptorBytes, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", descriptorAddress), 16)
	if err != nil {
		return result, fmt.Errorf("read self-family item descriptor: %w", err)
	}
	copy(result.descriptorRaw[:], descriptorBytes)
	dataOffset := binary.BigEndian.Uint32(descriptorBytes[0:4])
	// lwz at descriptor +4 followed by extsh sign-extends the low
	// halfword of that word, which occupies descriptor bytes +6..+7.
	count := int(int16(binary.BigEndian.Uint16(descriptorBytes[6:8])))
	stride := binary.BigEndian.Uint32(descriptorBytes[8:12])
	typeHash := binary.BigEndian.Uint32(descriptorBytes[12:16])
	if stride != itemPoolRecordStride {
		return result, fmt.Errorf("item-pool descriptor stride 0x%X does not match 0xA8", stride)
	}
	if typeHash != itemPoolTypeHash {
		return result, fmt.Errorf("item-pool descriptor type 0x%08X does not match 0x%08X", typeHash, itemPoolTypeHash)
	}
	if count < 1 || count > itemPoolCapacityLimit {
		return result, fmt.Errorf("item-pool descriptor count %d is outside the verified 1–%d bound", count, itemPoolCapacityLimit)
	}
	byteLength := uint64(count) * uint64(stride)
	if uint64(dataOffset)+byteLength >= uint64(itemPoolRelativeLimit) {
		return result, errors.New("item-pool descriptor exceeds the conservative same-build storage ceiling")
	}
	poolAddress64 := uint64(manager) + uint64(itemPoolStorageOffset) + uint64(dataOffset)
	if poolAddress64&7 != 0 || poolAddress64+byteLength > managerEnd || poolAddress64+byteLength > 0x40000000 {
		return result, errors.New("item-pool data range exceeds the bounded, aligned manager region")
	}
	result.manager = manager
	result.descriptorAddr = descriptorAddress
	result.dataOffset = dataOffset
	result.count = count
	result.stride = stride
	result.typeHash = typeHash
	result.poolAddress = uint32(poolAddress64)
	result.byteLength = uint32(byteLength)
	return result, nil
}

func sameInventoryPoolDescriptor(a, b inventoryPoolDescriptor) bool {
	return a.manager == b.manager && a.descriptorAddr == b.descriptorAddr &&
		a.descriptorRaw == b.descriptorRaw && a.poolAddress == b.poolAddress &&
		a.count == b.count && a.byteLength == b.byteLength
}
