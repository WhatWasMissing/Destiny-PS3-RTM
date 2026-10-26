package rtmclient

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"math"
	"strings"
)

const (
	PlayerNamePointerAddress       uint32 = 0x01897C48
	PlayerObjectsListAddress       uint32 = 0x01B29450
	PlayerScaleObjectsAddress      uint32 = 0x01A1D9E8
	PlayerScaleLookupAddress       uint32 = 0x01A21CB0
	PlayerScaleResolverAddress     uint32 = 0x002FF2D0
	PlayerScaleLookupGetterAddress uint32 = 0x001FB3C8
	PlayerScaleObjectsBlockSize           = 311600
	PlayerScaleObjectsCount               = 24
	PlayerScaleObjectsStride              = 10544
	PlayerScaleNameOffset                 = 1920
	PlayerScaleHandleOffset               = 1604
	PlayerScaleSlotStride                 = 0xC0
	PlayerScaleValueOffset                = 0x9C
	maxUserAddress                 uint32 = 0x7FFFFFFF
)

const (
	playerScaleResolverSignature     = "7C0802A648A99751F821FF81F801009038A0FFFF607F00002C04FFFF418200483C6001A2548404FE30C31CB08066000C7C8419D680E600083061007080C600247C87201490A100707885002080A500087CA530387C852010788400204BEFC09D80A1007090BF00003821008048A9974C"
	playerScaleLookupGetterSignature = "8084001C908300004E800020"
)

type PlayerScaleState struct {
	PlayerName             string  `json:"player_name"`
	Handle                 uint32  `json:"handle"`
	HandleTableAddress     uint32  `json:"handle_table_entry_address"`
	ResolvedObject         uint32  `json:"resolved_object"`
	ObjectIndex            uint32  `json:"object_index"`
	Slot                   uint32  `json:"slot"`
	ScaleAddress           uint32  `json:"scale_address"`
	ScaleRawHex            string  `json:"scale_raw_hex"`
	Scale                  float32 `json:"scale"`
	ResolverSignatureMatch bool    `json:"resolver_signature_match"`
	SnapshotAtomic         bool    `json:"snapshot_atomic"`

	namePointer    uint32
	listPointer    uint32
	objectsAddress uint32
	objectRecord   uint32
	mapBase        uint32
	mapStride      uint32
	mapMask        uint32
	objectsBase    uint32
}

type PlayerScaleWrite struct {
	PlayerScaleState
	NewScale       float32 `json:"new_scale"`
	ReadbackRawHex string  `json:"readback_raw_hex"`
	ReadbackExact  bool    `json:"readback_exact"`
}

// InspectPlayerScale follows the local 1.24 C# player's name and object-list
// chain. It emulates the small resolver at 0x002FF2D0 using its verified PPC
// instructions and the leaf getter at 0x001FB3C8, so it does not execute game
// code or write the scratch address used by the source RTM.
func (c *Client) InspectPlayerScale(ctx context.Context, ip string) (PlayerScaleState, error) {
	return c.resolvePlayerScale(ctx, ip)
}

// SetPlayerScale applies a source-range Float32 scale to the current player.
// It rechecks the player, record handle, lookup mapping and destination before
// writing, then requires exact memory readback. This modifies process memory;
// it does not establish visual effect or server persistence.
func (c *Client) SetPlayerScale(ctx context.Context, ip string, scale float32) (PlayerScaleWrite, error) {
	var result PlayerScaleWrite
	if math.IsNaN(float64(scale)) || math.IsInf(float64(scale), 0) || scale < 0.3 || scale > 20 {
		return result, errors.New("player scale must be a finite value from 0.3 through 20")
	}
	state, err := c.resolvePlayerScale(ctx, ip)
	if err != nil {
		return result, err
	}
	if err := c.verifyPlayerScaleTarget(ctx, ip, state); err != nil {
		return result, err
	}
	data := make([]byte, 4)
	binary.BigEndian.PutUint32(data, math.Float32bits(scale))
	if err := c.WriteMemory(ctx, ip, fmt.Sprintf("%08X", state.ScaleAddress), data); err != nil {
		return result, err
	}
	readback, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", state.ScaleAddress), 4)
	if err != nil {
		return result, fmt.Errorf("player scale write was acknowledged but final readback failed: %w", err)
	}
	if !bytes.Equal(data, readback) {
		return result, fmt.Errorf("player scale was not verified: sent %X, read back %X", data, readback)
	}
	result = PlayerScaleWrite{
		PlayerScaleState: state,
		NewScale:         scale,
		ReadbackRawHex:   strings.ToUpper(hex.EncodeToString(readback)),
		ReadbackExact:    true,
	}
	result.ScaleRawHex = result.ReadbackRawHex
	result.Scale = scale
	return result, nil
}

func (c *Client) resolvePlayerScale(ctx context.Context, ip string) (PlayerScaleState, error) {
	var result PlayerScaleState
	if err := c.verifyPlayerScaleCode(ctx, ip); err != nil {
		return result, err
	}
	namePointer, err := c.readPlayerScaleU32(ctx, ip, PlayerNamePointerAddress)
	if err != nil {
		return result, fmt.Errorf("read player-name root: %w", err)
	}
	if !validPlayerScaleRange(namePointer+0x90, 32) {
		return result, errors.New("player-name pointer is outside the supported user-memory range")
	}
	playerNameBytes, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", namePointer+0x90), 32)
	if err != nil {
		return result, fmt.Errorf("read player name: %w", err)
	}
	playerName := playerScaleASCII(playerNameBytes)
	if playerName == "" {
		return result, errors.New("player name is empty; Destiny may not be in a loaded character state")
	}

	listPointer, err := c.readPlayerScaleU32(ctx, ip, PlayerObjectsListAddress)
	if err != nil {
		return result, fmt.Errorf("read player-object list root: %w", err)
	}
	if !validPlayerScaleRange(listPointer+4, 4) {
		return result, errors.New("player-object list pointer is outside the supported user-memory range")
	}
	objectsAddress, err := c.readPlayerScaleU32(ctx, ip, listPointer+4)
	if err != nil {
		return result, fmt.Errorf("read player-object records pointer: %w", err)
	}
	if !validPlayerScaleRange(objectsAddress, PlayerScaleObjectsBlockSize) {
		return result, errors.New("player-object records block is outside the supported user-memory range")
	}
	objects, err := c.readPlayerScaleRange(ctx, ip, objectsAddress, PlayerScaleObjectsBlockSize)
	if err != nil {
		return result, fmt.Errorf("read player-object records: %w", err)
	}

	var foundOffset uint32
	var handle uint32
	matches := 0
	for index := uint32(0); index < PlayerScaleObjectsCount; index++ {
		recordOffset := index * PlayerScaleObjectsStride
		nameStart := recordOffset + PlayerScaleNameOffset
		if uint64(nameStart)+32 > uint64(len(objects)) {
			return result, errors.New("player-object name offset exceeds the source block")
		}
		if playerScaleASCII(objects[nameStart:nameStart+32]) != playerName {
			continue
		}
		matches++
		foundOffset = recordOffset
		handleOffset := recordOffset + PlayerScaleHandleOffset
		if uint64(handleOffset)+4 > uint64(len(objects)) {
			return result, errors.New("player-object handle offset exceeds the source block")
		}
		handle = binary.BigEndian.Uint32(objects[handleOffset : handleOffset+4])
	}
	if matches == 0 {
		return result, fmt.Errorf("player %q was not found in the 24 source-mapped object records", playerName)
	}
	if matches != 1 {
		return result, fmt.Errorf("player name %q matches %d object records; refusing an ambiguous scale write", playerName, matches)
	}
	if handle == 0 || handle == ^uint32(0) {
		return result, errors.New("player object has a null or sentinel handle")
	}

	mapFields, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", PlayerScaleLookupAddress+8), 0x20)
	if err != nil {
		return result, fmt.Errorf("read player-handle lookup table: %w", err)
	}
	if len(mapFields) != 0x20 {
		return result, errors.New("player-handle lookup table read returned an invalid length")
	}
	mapBase := binary.BigEndian.Uint32(mapFields[0:4])
	mapStride := binary.BigEndian.Uint32(mapFields[4:8])
	mapMask := binary.BigEndian.Uint32(mapFields[0x1C:0x20])
	if mapBase == 0 || mapStride < 12 || mapStride > 0x10000 {
		return result, errors.New("player-handle lookup table has an invalid base or stride")
	}
	handleTableAddress, err := playerScaleTableAddress(mapBase, mapStride, handle)
	if err != nil {
		return result, err
	}
	entryTag, err := c.readPlayerScaleU32(ctx, ip, handleTableAddress+8)
	if err != nil {
		return result, fmt.Errorf("read player-handle lookup entry: %w", err)
	}
	maskedTag := entryTag & mapMask
	if uint64(maskedTag) > uint64(handleTableAddress) {
		return result, errors.New("player-handle lookup resolved below the supported address range")
	}
	resolvedObject := handleTableAddress - maskedTag
	if !validPlayerScaleRange(resolvedObject+0x1C, 4) {
		return result, errors.New("resolved player object is outside the supported user-memory range")
	}
	objectIndex, err := c.readPlayerScaleU32(ctx, ip, resolvedObject+0x1C)
	if err != nil {
		return result, fmt.Errorf("read resolved player-object index: %w", err)
	}
	if objectIndex == ^uint32(0) {
		return result, errors.New("player-handle resolver returned the invalid -1 sentinel")
	}

	objectsBase, err := c.readPlayerScaleU32(ctx, ip, PlayerScaleObjectsAddress)
	if err != nil {
		return result, fmt.Errorf("read scale-object base: %w", err)
	}
	if objectsBase == 0 {
		return result, errors.New("scale-object base is null")
	}
	slot := objectIndex & 0x1FFF
	scaleAddress64 := uint64(objectsBase) + uint64(slot)*PlayerScaleSlotStride + PlayerScaleValueOffset
	if scaleAddress64 > uint64(maxUserAddress)-3 {
		return result, errors.New("resolved player scale field is outside the supported user-memory range")
	}
	scaleAddress := uint32(scaleAddress64)
	scaleBytes, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", scaleAddress), 4)
	if err != nil {
		return result, fmt.Errorf("read player scale field: %w", err)
	}
	scale := math.Float32frombits(binary.BigEndian.Uint32(scaleBytes))
	if !finiteFloat32(scale) {
		return result, errors.New("resolved player scale field is not a finite Float32")
	}
	result = PlayerScaleState{
		PlayerName:             playerName,
		Handle:                 handle,
		HandleTableAddress:     handleTableAddress,
		ResolvedObject:         resolvedObject,
		ObjectIndex:            objectIndex,
		Slot:                   slot,
		ScaleAddress:           scaleAddress,
		ScaleRawHex:            strings.ToUpper(hex.EncodeToString(scaleBytes)),
		Scale:                  scale,
		ResolverSignatureMatch: true,
		SnapshotAtomic:         false,
		namePointer:            namePointer,
		listPointer:            listPointer,
		objectsAddress:         objectsAddress,
		objectRecord:           objectsAddress + foundOffset,
		mapBase:                mapBase,
		mapStride:              mapStride,
		mapMask:                mapMask,
		objectsBase:            objectsBase,
	}
	return result, nil
}

func (c *Client) verifyPlayerScaleCode(ctx context.Context, ip string) error {
	resolver, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", PlayerScaleResolverAddress), len(playerScaleResolverSignature)/2)
	if err != nil {
		return fmt.Errorf("read the 1.24 player-scale resolver signature: %w", err)
	}
	if strings.ToUpper(hex.EncodeToString(resolver)) != playerScaleResolverSignature {
		return errors.New("player-scale resolver bytes do not match the statically verified 1.24 source; no write is permitted")
	}
	getter, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", PlayerScaleLookupGetterAddress), len(playerScaleLookupGetterSignature)/2)
	if err != nil {
		return fmt.Errorf("read the 1.24 player-scale lookup helper signature: %w", err)
	}
	if strings.ToUpper(hex.EncodeToString(getter)) != playerScaleLookupGetterSignature {
		return errors.New("player-scale lookup helper bytes do not match the statically verified 1.24 source; no write is permitted")
	}
	return nil
}

func (c *Client) verifyPlayerScaleTarget(ctx context.Context, ip string, state PlayerScaleState) error {
	if err := c.verifyPlayerScaleCode(ctx, ip); err != nil {
		return err
	}
	namePointer, err := c.readPlayerScaleU32(ctx, ip, PlayerNamePointerAddress)
	if err != nil || namePointer != state.namePointer {
		return errors.New("player-name pointer changed during scale update; no write was made")
	}
	listPointer, err := c.readPlayerScaleU32(ctx, ip, PlayerObjectsListAddress)
	if err != nil || listPointer != state.listPointer {
		return errors.New("player-object list changed during scale update; no write was made")
	}
	objectsAddress, err := c.readPlayerScaleU32(ctx, ip, listPointer+4)
	if err != nil || objectsAddress != state.objectsAddress {
		return errors.New("player-object records moved during scale update; no write was made")
	}
	name, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", namePointer+0x90), 32)
	if err != nil || playerScaleASCII(name) != state.PlayerName {
		return errors.New("current player name changed during scale update; no write was made")
	}
	nameRecord, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", state.objectRecord+PlayerScaleNameOffset), 32)
	if err != nil || playerScaleASCII(nameRecord) != state.PlayerName {
		return errors.New("player record changed during scale update; no write was made")
	}
	handle, err := c.readPlayerScaleU32(ctx, ip, state.objectRecord+PlayerScaleHandleOffset)
	if err != nil || handle != state.Handle {
		return errors.New("player handle changed during scale update; no write was made")
	}
	mapFields, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", PlayerScaleLookupAddress+8), 0x20)
	if err != nil || len(mapFields) != 0x20 || binary.BigEndian.Uint32(mapFields[0:4]) != state.mapBase || binary.BigEndian.Uint32(mapFields[4:8]) != state.mapStride || binary.BigEndian.Uint32(mapFields[0x1C:0x20]) != state.mapMask {
		return errors.New("player-handle lookup table changed during scale update; no write was made")
	}
	objectsBase, err := c.readPlayerScaleU32(ctx, ip, PlayerScaleObjectsAddress)
	if err != nil || objectsBase != state.objectsBase {
		return errors.New("scale-object base changed during scale update; no write was made")
	}
	entryAddress, err := playerScaleTableAddress(state.mapBase, state.mapStride, state.Handle)
	if err != nil || entryAddress != state.HandleTableAddress {
		return errors.New("player-handle lookup address changed during scale update; no write was made")
	}
	entryTag, err := c.readPlayerScaleU32(ctx, ip, entryAddress+8)
	if err != nil {
		return errors.New("player-handle lookup entry changed during scale update; no write was made")
	}
	maskedTag := entryTag & state.mapMask
	if maskedTag > entryAddress {
		return errors.New("player-handle lookup entry changed to an invalid address; no write was made")
	}
	resolved := entryAddress - maskedTag
	if !validPlayerScaleRange(resolved+0x1C, 4) {
		return errors.New("resolved player object changed to an invalid address; no write was made")
	}
	objectIndex, err := c.readPlayerScaleU32(ctx, ip, resolved+0x1C)
	if err != nil || objectIndex != state.ObjectIndex {
		return errors.New("resolved player object changed during scale update; no write was made")
	}
	address := uint64(objectsBase) + uint64(objectIndex&0x1FFF)*PlayerScaleSlotStride + PlayerScaleValueOffset
	if address != uint64(state.ScaleAddress) {
		return errors.New("player scale address changed during scale update; no write was made")
	}
	return nil
}

func (c *Client) readPlayerScaleU32(ctx context.Context, ip string, address uint32) (uint32, error) {
	if !validPlayerScaleRange(address, 4) {
		return 0, fmt.Errorf("address 0x%08X is outside the supported user-memory range", address)
	}
	data, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", address), 4)
	if err != nil {
		return 0, err
	}
	if len(data) != 4 {
		return 0, errors.New("memory read returned an invalid length")
	}
	return binary.BigEndian.Uint32(data), nil
}

func (c *Client) readPlayerScaleRange(ctx context.Context, ip string, address uint32, length int) ([]byte, error) {
	if length < 1 || !validPlayerScaleRange(address, uint32(length)) {
		return nil, errors.New("player-object records range is invalid")
	}
	data := make([]byte, 0, length)
	for offset := 0; offset < length; {
		chunkLength := length - offset
		if chunkLength > 0x10000 {
			chunkLength = 0x10000
		}
		chunk, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", address+uint32(offset)), chunkLength)
		if err != nil {
			return nil, err
		}
		if len(chunk) != chunkLength {
			return nil, errors.New("player-object records read returned an invalid chunk length")
		}
		data = append(data, chunk...)
		offset += chunkLength
	}
	return data, nil
}

func playerScaleASCII(data []byte) string {
	if end := bytes.IndexByte(data, 0); end >= 0 {
		data = data[:end]
	}
	return string(data)
}

func playerScaleTableAddress(base, stride, handle uint32) (uint32, error) {
	index := handle & 0x1FFF
	address := uint64(base) + uint64(index)*uint64(stride)
	if address > uint64(maxUserAddress)-0x0B || !validPlayerScaleRange(uint32(address), 12) {
		return 0, errors.New("player-handle table entry is outside the supported user-memory range")
	}
	return uint32(address), nil
}

func validPlayerScaleRange(address, length uint32) bool {
	if address < 0x10000 || length == 0 {
		return false
	}
	return uint64(address)+uint64(length)-1 <= uint64(maxUserAddress)
}
