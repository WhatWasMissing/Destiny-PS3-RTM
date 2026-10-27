package rtmmcp

import (
	"bufio"
	"bytes"
	"context"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"strconv"
	"strings"
	"sync"
	"time"

	"destiny-rtm/internal/rtmclient"
)

const protocolVersion = "2025-11-25"
const maxMessageBytes = 1 << 20

type Client interface {
	Status(context.Context, string) (int, error)
	ReadMemory(context.Context, string, string, int) ([]byte, error)
	ReadPlayerPosition(context.Context, string) (rtmclient.PlayerPosition, error)
	InspectEntityPointers(context.Context, string) (rtmclient.EntityPointerInspection, error)
	InspectPlayerScale(context.Context, string) (rtmclient.PlayerScaleState, error)
	SetPlayerScale(context.Context, string, float32) (rtmclient.PlayerScaleWrite, error)
	WriteMemory(context.Context, string, string, []byte) error
	Notify(context.Context, string, string) error
	LaunchGame(context.Context, string, string) (rtmclient.LaunchResult, error)
}

type Server struct {
	client Client
	out    io.Writer

	writeMu          sync.Mutex
	memoryWriteMu    sync.Mutex
	beepSequenceSlot chan struct{}
	callsMu          sync.Mutex
	calls            map[string]context.CancelFunc
	wg               sync.WaitGroup
}

func New(client Client) *Server {
	return &Server{client: client, calls: make(map[string]context.CancelFunc), beepSequenceSlot: make(chan struct{}, 1)}
}

type request struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      json.RawMessage `json:"id"`
	Method  string          `json:"method"`
	Params  json.RawMessage `json:"params"`
}

type rpcResponse struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      json.RawMessage `json:"id"`
	Result  *any            `json:"result,omitempty"`
	Error   *rpcError       `json:"error,omitempty"`
}

type rpcError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

type tool struct {
	Name        string         `json:"name"`
	Title       string         `json:"title"`
	Description string         `json:"description"`
	InputSchema map[string]any `json:"inputSchema"`
	Annotations map[string]any `json:"annotations,omitempty"`
}

type beepNote struct {
	tone    int
	pauseMS int
}

type beepSequence struct {
	name  string
	notes []beepNote
}

// These fixed sequences mirror source/js/jukebox.js. Keep notes and post-tone
// pauses in sync with the desktop Jukebox; callers cannot supply tone arrays.
var beepSequences = map[string]beepSequence{
	"fanfare":  {name: "Victory Fanfare", notes: []beepNote{{1, 180}, {1, 180}, {1, 180}, {2, 400}, {1, 180}, {2, 120}, {1, 400}, {1, 180}, {1, 180}, {1, 180}, {2, 400}, {1, 180}, {2, 120}, {1, 600}}},
	"imperial": {name: "Imperial March", notes: []beepNote{{2, 350}, {2, 350}, {2, 350}, {3, 500}, {1, 200}, {2, 500}, {2, 350}, {2, 350}, {2, 350}, {3, 500}, {1, 200}, {2, 600}}},
	"mario":    {name: "Super Mario", notes: []beepNote{{1, 150}, {1, 300}, {1, 150}, {1, 150}, {1, 150}, {2, 150}, {1, 150}, {2, 300}, {1, 150}, {1, 300}, {1, 150}, {1, 150}, {1, 150}, {2, 150}, {1, 150}, {2, 400}}},
	"nokia":    {name: "Nokia Tune", notes: []beepNote{{1, 150}, {2, 150}, {1, 150}, {2, 300}, {1, 150}, {2, 150}, {1, 300}, {2, 150}, {1, 150}, {2, 150}, {1, 300}, {2, 150}, {1, 150}, {2, 150}, {1, 400}}},
	"sos":      {name: "SOS", notes: []beepNote{{1, 120}, {1, 120}, {1, 300}, {3, 300}, {3, 300}, {3, 500}, {1, 120}, {1, 120}, {1, 400}}},
	"shave":    {name: "Shave & Haircut", notes: []beepNote{{1, 180}, {2, 180}, {2, 180}, {2, 180}, {1, 350}, {2, 180}, {1, 500}}},
}

var toolList = []tool{
	{
		Name:        "rtm_status",
		Title:       "Check RTM connection",
		Description: "Check whether webMAN responds at a private IPv4 address. This does not confirm that Destiny is running.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
		}, "target_ipv4"),
		Annotations: map[string]any{"readOnlyHint": true, "destructiveHint": false, "idempotentHint": true, "openWorldHint": true},
	},
	{
		Name:        "rtm_verify_build",
		Title:       "Check Destiny build profile",
		Description: "Read three code signatures and compare them with the audited BLES01857 01.24 profile. This is a read-only compatibility check, not a full EBOOT hash or region identification; a mismatch blocks memory writes but does not block reads.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
		}, "target_ipv4"),
		Annotations: map[string]any{"readOnlyHint": true, "destructiveHint": false, "idempotentHint": true, "openWorldHint": true},
	},
	{
		Name:        "rtm_read_memory",
		Title:       "Read Destiny memory",
		Description: "Read 1–65536 bytes from the Destiny process (PID 0x1010200) on a private IPv4 PS3. Address is hexadecimal.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
			"address":     map[string]any{"type": "string", "description": "32-bit hexadecimal memory address, optionally prefixed with 0x."},
			"length":      map[string]any{"type": "integer", "minimum": 1, "maximum": 65536, "default": 4},
		}, "target_ipv4", "address"),
		Annotations: map[string]any{"readOnlyHint": true, "destructiveHint": false, "idempotentHint": true, "openWorldHint": true},
	},
	{
		Name:        "rtm_read_live_inventory_pool",
		Title:       "Read live Destiny item-pool slots",
		Description: "Read the BLES01857 01.24 self-family item-object pool after verifying the three-site build profile, exact getter/lookup code signatures, and live descriptor. The bounded sequential snapshot returns only pool slot and signed item index (at most 646 rows); instance and owner IDs are suppressed. Read-only and non-atomic; does not map items to an active character or vault.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
		}, "target_ipv4"),
		Annotations: map[string]any{"readOnlyHint": true, "destructiveHint": false, "idempotentHint": true, "openWorldHint": true},
	},
	{
		Name:        "rtm_read_player_position",
		Title:       "Read Destiny player position",
		Description: "Read the Overwalls source-mapped player pointer at 0x0190EBB0 and decode the three big-endian Float32 coordinates at pointer +0x140. This performs memory reads only; the in-game position and build compatibility are not independently confirmed.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
		}, "target_ipv4"),
		Annotations: map[string]any{"readOnlyHint": true, "destructiveHint": false, "idempotentHint": true, "openWorldHint": true},
	},
	{
		Name:        "rtm_inspect_entity_pointers",
		Title:       "Inspect Destiny entity pointers",
		Description: "Read the Overwalls player and OPK-target roots at 0x0190EBB0 and 0x0190EBB4. For valid pointers, decode XYZ Float32 values at +0x140/+0x144/+0x148 and the target activation Float32 at +0x1B8. Null or invalid pointers are reported without dereferencing them; self-targets are marked. This is read-only; live semantics and build compatibility remain unconfirmed.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
		}, "target_ipv4"),
		Annotations: map[string]any{"readOnlyHint": true, "destructiveHint": false, "idempotentHint": true, "openWorldHint": true},
	},
	{
		Name:        "rtm_inspect_player_scale",
		Title:       "Inspect Destiny player scale",
		Description: "Follow the local 1.24 player-name and object-list chain, verify the PPC64 code signatures at 0x002FF2D0 and 0x001FB3C8, and emulate that small resolver using read-only memory requests. Returns the current player scale field and address. It does not execute game code; the snapshot is non-atomic and visual effect is unconfirmed.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
		}, "target_ipv4"),
		Annotations: map[string]any{"readOnlyHint": true, "destructiveHint": false, "idempotentHint": true, "openWorldHint": true},
	},
	{
		Name:        "rtm_set_player_scale",
		Title:       "Set Destiny player scale",
		Description: "Set the current local player scale to a finite source-range multiplier from 0.3 through 20 (the source UI offers 0.3, 1, 2 and 20; use 1 to reset). Requires the three-site BLES01857 01.24 runtime signature profile plus a stable player-name/object-handle chain. The tool rechecks the destination before a verified four-byte Float32 write; it emulates the source resolver rather than executing a game function. Game visual effect and server persistence are not confirmed.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
			"scale":       map[string]any{"type": "number", "minimum": 0.3, "maximum": 20, "description": "Float32 player scale multiplier; source buttons include 0.3, 1, 2 and 20; 1 resets the source value."},
		}, "target_ipv4", "scale"),
		Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": true, "idempotentHint": true, "openWorldHint": true},
	},
	{
		Name:        "rtm_write_memory",
		Title:       "Write Destiny memory",
		Description: "Require the three-site BLES01857 01.24 runtime signature profile, then write 1–8 bytes to the Destiny process and report success only after exact fresh readback. The address is hexadecimal; this does not call game functions.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
			"address":     map[string]any{"type": "string", "description": "32-bit hexadecimal memory address, optionally prefixed with 0x."},
			"hex":         map[string]any{"type": "string", "description": "1–8 bytes encoded as hexadecimal, optionally prefixed with 0x."},
		}, "target_ipv4", "address", "hex"),
		Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": true, "idempotentHint": true, "openWorldHint": true},
	},
	{
		Name:        "rtm_set_brightness",
		Title:       "Set a source-backed brightness preset",
		Description: "Require the three-site BLES01857 01.24 runtime signature profile, then write Bright (50), Normal (1), or Inverted (-20) from the local 1.24 C# source to 0x006101C0 and verify the bytes. Readback confirms bytes only; the in-game visual effect has not been observed.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
			"preset":      map[string]any{"type": "string", "enum": []string{"bright", "normal", "inverted"}},
		}, "target_ipv4", "preset"),
		Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": true, "idempotentHint": true, "openWorldHint": true},
	},
	{
		Name:        "rtm_set_legacy_scalar_preset",
		Title:       "Set an experimental RTM scalar preset",
		Description: "Select Normal (Float32 1), or a recovered RTM-labeled Max Damage (0x3FFFFFFF, about 2) / 1 Hit Kill 2 (Float32 100) preset at 0x004449EC. These gameplay labels are unverified: 01.24 code uses the word as a fallback coefficient in an unidentified ratio calculation. Requires the three-site 01.24 signature profile, the exact consumer-code signature at 0x00444B64, and a known current preset before writing; verifies exact readback.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
			"mode":        map[string]any{"type": "string", "enum": []string{"normal", "max_damage", "one_hit_kill_2"}},
		}, "target_ipv4", "mode"),
		Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": true, "idempotentHint": true, "openWorldHint": true},
	},
	{
		Name:        "rtm_notify",
		Title:       "Send a PS3 notification",
		Description: "Dispatch a webMAN system notification of up to 128 characters. The tool cannot confirm that the PS3 displayed it.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
			"message":     map[string]any{"type": "string", "minLength": 1, "maxLength": 128},
		}, "target_ipv4", "message"),
		Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": false, "idempotentHint": false, "openWorldHint": true},
	},
	{
		Name:        "rtm_play_beep_sequence",
		Title:       "Play a PS3 beep sequence",
		Description: "Play one of the six fixed PS3 system-tone sequences mirrored from the RTM Jukebox. This sends only webMAN /beep.ps3 requests and does not read or write Destiny memory. Playback can be cancelled between tones; the result counts HTTP-acknowledged requests and cannot confirm that audio was heard.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
			"sequence":    map[string]any{"type": "string", "enum": []string{"fanfare", "imperial", "mario", "nokia", "sos", "shave"}},
		}, "target_ipv4", "sequence"),
		Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": false, "idempotentHint": false, "openWorldHint": true},
	},
	{
		Name:        "rtm_launch_game",
		Title:       "Launch Destiny from ISO",
		Description: "Run the recovered Destiny RTM launch sequence: mount one ISO from /dev_hdd0/PS3ISO/, return to the game list, and start the mounted game. Each HTTP step must be acknowledged; Destiny boot cannot be confirmed.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
			"iso_path": map[string]any{
				"type": "string", "description": "One .iso filename directly under /dev_hdd0/PS3ISO/.",
				"default": rtmclient.DefaultDestinyISOPath, "minLength": 22, "maxLength": 160,
				"pattern": `^/dev_hdd0/PS3ISO/[A-Za-z0-9][A-Za-z0-9 _().-]{0,124}\.iso$`,
			},
		}, "target_ipv4"),
		Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": true, "idempotentHint": false, "openWorldHint": true},
	},
}

const instructions = "Use only the user's private PS3 IPv4 address. Memory reads and writes target Destiny process 0x1010200 through webMAN MOD on port 80. Reads are capped at 64 KiB; writes are capped at 8 bytes and require exact readback. rtm_status checks webMAN reachability, not whether Destiny is running. Player-position and entity-pointer tools follow the local Overwalls pointer map using read-only requests; coordinates, pointer semantics, and build compatibility remain unconfirmed. Player-scale tools require the verified 1.24 resolver/helper byte signatures and emulate the source lookup without calling game functions; writes recheck the pointer chain and verify the Float32 bytes, but visual effect and server persistence remain unconfirmed. Brightness and experimental scalar-preset effects are unverified. The scalar setter requires the exact 01.24 consumer signature at 0x00444B64 and transitions only between the three known values at 0x004449EC. Notifications report dispatch, not display. The fixed beep-sequence tool sends webMAN system-tone requests only; it reports HTTP-acknowledged tones and cannot confirm that audio was heard. Game-launch steps report HTTP acknowledgement and do not confirm that Destiny started. No generic function-call endpoint is exposed."

func objectSchema(properties map[string]any, required ...string) map[string]any {
	return map[string]any{"type": "object", "properties": properties, "required": required, "additionalProperties": false}
}

func (s *Server) Serve(input io.Reader, output io.Writer) error {
	if s.client == nil {
		return errors.New("RTM client is required")
	}
	s.out = output
	scanner := bufio.NewScanner(input)
	scanner.Buffer(make([]byte, 4096), maxMessageBytes)
	for scanner.Scan() {
		line := append([]byte(nil), scanner.Bytes()...)
		var req request
		if err := json.Unmarshal(line, &req); err != nil {
			if writeErr := s.writeResponse(rpcResponse{JSONRPC: "2.0", ID: json.RawMessage("null"), Error: &rpcError{Code: -32700, Message: "Parse error"}}); writeErr != nil {
				return writeErr
			}
			continue
		}
		if req.JSONRPC != "2.0" || req.Method == "" || !validID(req.ID) && len(req.ID) != 0 {
			if len(req.ID) == 0 {
				continue
			}
			if err := s.writeResponse(rpcResponse{JSONRPC: "2.0", ID: json.RawMessage("null"), Error: &rpcError{Code: -32600, Message: "Invalid Request"}}); err != nil {
				return err
			}
			continue
		}
		if len(req.ID) == 0 {
			s.handleNotification(req)
			if req.Method == "exit" {
				s.cancelAll()
				break
			}
			continue
		}
		if !validID(req.ID) {
			_ = s.writeResponse(rpcResponse{JSONRPC: "2.0", ID: json.RawMessage("null"), Error: &rpcError{Code: -32600, Message: "Invalid Request"}})
			continue
		}
		s.handleRequest(req)
	}
	if err := scanner.Err(); err != nil {
		s.cancelAll()
	}
	s.wg.Wait()
	return scanner.Err()
}

func validID(raw json.RawMessage) bool {
	if len(raw) == 0 {
		return true
	}
	var value any
	decoder := json.NewDecoder(bytes.NewReader(raw))
	decoder.UseNumber()
	if decoder.Decode(&value) != nil {
		return false
	}
	switch value.(type) {
	case string, json.Number:
		return true
	default:
		return false
	}
}

func (s *Server) handleNotification(req request) {
	if req.Method != "notifications/cancelled" {
		return
	}
	var params struct {
		RequestID json.RawMessage `json:"requestId"`
	}
	if json.Unmarshal(req.Params, &params) != nil || !validID(params.RequestID) || len(params.RequestID) == 0 {
		return
	}
	s.callsMu.Lock()
	cancel := s.calls[string(params.RequestID)]
	s.callsMu.Unlock()
	if cancel != nil {
		cancel()
	}
}

func (s *Server) handleRequest(req request) {
	switch req.Method {
	case "initialize":
		_ = s.writeResult(req.ID, map[string]any{
			"protocolVersion": protocolVersion,
			"capabilities":    map[string]any{"tools": map[string]any{}},
			"serverInfo":      map[string]any{"name": "destiny-rtm", "title": "Destiny RTM tools", "version": "1.0.0"},
			"instructions":    instructions,
		})
	case "ping":
		_ = s.writeResult(req.ID, map[string]any{})
	case "tools/list":
		_ = s.writeResult(req.ID, map[string]any{"tools": toolList})
	case "tools/call":
		s.startToolCall(req)
	case "shutdown":
		_ = s.writeResult(req.ID, nil)
	default:
		_ = s.writeResponse(rpcResponse{JSONRPC: "2.0", ID: req.ID, Error: &rpcError{Code: -32601, Message: "Method not found"}})
	}
}

func (s *Server) startToolCall(req request) {
	key := string(req.ID)
	s.callsMu.Lock()
	if _, exists := s.calls[key]; exists {
		s.callsMu.Unlock()
		_ = s.writeResponse(rpcResponse{JSONRPC: "2.0", ID: req.ID, Error: &rpcError{Code: -32600, Message: "Duplicate request id"}})
		return
	}
	callTimeout := 15 * time.Second
	var requestedTool struct {
		Name string `json:"name"`
	}
	if json.Unmarshal(req.Params, &requestedTool) == nil && requestedTool.Name == "rtm_read_live_inventory_pool" {
		callTimeout = 2 * time.Minute
	}
	ctx, cancel := context.WithTimeout(context.Background(), callTimeout)
	s.calls[key] = cancel
	s.callsMu.Unlock()

	s.wg.Add(1)
	go func() {
		defer s.wg.Done()
		result := s.callTool(ctx, req.Params)
		cancel()
		s.callsMu.Lock()
		delete(s.calls, key)
		s.callsMu.Unlock()
		_ = s.writeResult(req.ID, result)
	}()
}

func (s *Server) cancelAll() {
	s.callsMu.Lock()
	cancels := make([]context.CancelFunc, 0, len(s.calls))
	for _, cancel := range s.calls {
		cancels = append(cancels, cancel)
	}
	s.callsMu.Unlock()
	for _, cancel := range cancels {
		cancel()
	}
}

func (s *Server) callTool(ctx context.Context, raw json.RawMessage) map[string]any {
	var call struct {
		Name      string          `json:"name"`
		Arguments json.RawMessage `json:"arguments"`
		Meta      json.RawMessage `json:"_meta"`
	}
	if err := decodeStrict(raw, &call); err != nil || call.Name == "" {
		return errorResult(errors.New("tools/call requires a tool name and arguments object"))
	}
	switch call.Name {
	case "rtm_status":
		var args targetArgs
		if err := decodeStrict(call.Arguments, &args); err != nil {
			return errorResult(err)
		}
		if err := validateTarget(args.Target); err != nil {
			return errorResult(err)
		}
		status, err := s.client.Status(ctx, args.Target)
		if err != nil {
			return errorResult(err)
		}
		return successResult(map[string]any{"ok": true, "target_ipv4": args.Target, "webman_reachable": true, "http_status": status, "destiny_process_confirmed": false})
	case "rtm_verify_build":
		var args targetArgs
		if err := decodeStrict(call.Arguments, &args); err != nil {
			return errorResult(err)
		}
		if err := validateTarget(args.Target); err != nil {
			return errorResult(err)
		}
		profile := rtmclient.InspectDestiny0124SignatureProfile(ctx, args.Target, s.client.ReadMemory)
		return successResult(map[string]any{
			"ok":                        true,
			"read_only":                 true,
			"target_ipv4":               args.Target,
			"profile":                   profile.Profile,
			"profile_match":             profile.Matches,
			"full_eboot_hash_confirmed": false,
			"title_region_confirmed":    false,
			"signatures":                profile.Signatures,
		})
	case "rtm_read_memory":
		var args struct {
			Target  string `json:"target_ipv4"`
			Address string `json:"address"`
			Length  int    `json:"length"`
		}
		if err := decodeStrict(call.Arguments, &args); err != nil {
			return errorResult(err)
		}
		if args.Length == 0 {
			args.Length = 4
		}
		if err := validateTarget(args.Target); err != nil {
			return errorResult(err)
		}
		if args.Length < 1 || args.Length > 0x10000 {
			return errorResult(errors.New("length must be between 1 and 65536 bytes"))
		}
		address, err := parseAddress(args.Address)
		if err != nil {
			return errorResult(err)
		}
		if err := validateMemoryRange(address, args.Length); err != nil {
			return errorResult(err)
		}
		data, err := s.client.ReadMemory(ctx, args.Target, args.Address, args.Length)
		if err != nil {
			return errorResult(err)
		}
		return successResult(map[string]any{"ok": true, "address": fmt.Sprintf("0x%08X", address), "length": len(data), "hex": strings.ToUpper(hex.EncodeToString(data))})
	case "rtm_read_live_inventory_pool":
		var args targetArgs
		if err := decodeStrict(call.Arguments, &args); err != nil {
			return errorResult(err)
		}
		if err := validateTarget(args.Target); err != nil {
			return errorResult(err)
		}
		reader, ok := s.client.(interface {
			ReadLiveItemPool(context.Context, string) (rtmclient.InventoryPoolSnapshot, error)
		})
		if !ok {
			return errorResult(errors.New("RTM client does not support the BLES 01.24 live inventory-pool reader"))
		}
		snapshot, err := reader.ReadLiveItemPool(ctx, args.Target)
		if err != nil {
			return errorResult(err)
		}
		return successResult(map[string]any{
			"ok": true, "read_only": true, "snapshot_atomic": false,
			"target_ipv4": args.Target, "snapshot": snapshot,
			"character_or_vault_mapping_confirmed": false,
		})
	case "rtm_read_player_position":
		var args targetArgs
		if err := decodeStrict(call.Arguments, &args); err != nil {
			return errorResult(err)
		}
		if err := validateTarget(args.Target); err != nil {
			return errorResult(err)
		}
		position, err := s.client.ReadPlayerPosition(ctx, args.Target)
		if err != nil {
			return errorResult(err)
		}
		return successResult(map[string]any{
			"ok":                        true,
			"read_only":                 true,
			"pointer_address":           fmt.Sprintf("0x%08X", rtmclient.PlayerPointerAddress),
			"player_pointer":            fmt.Sprintf("0x%08X", position.PlayerPointer),
			"coordinates_address":       fmt.Sprintf("0x%08X", position.CoordinatesAddress),
			"coordinate_offsets":        []string{"+0x140", "+0x144", "+0x148"},
			"x":                         position.X,
			"y":                         position.Y,
			"z":                         position.Z,
			"raw_hex":                   position.RawHex,
			"gameplay_effect_confirmed": false,
		})
	case "rtm_inspect_entity_pointers":
		var args targetArgs
		if err := decodeStrict(call.Arguments, &args); err != nil {
			return errorResult(err)
		}
		if err := validateTarget(args.Target); err != nil {
			return errorResult(err)
		}
		inspection, err := s.client.InspectEntityPointers(ctx, args.Target)
		if err != nil {
			return errorResult(err)
		}
		return successResult(map[string]any{
			"ok":                            true,
			"read_only":                     true,
			"snapshot_atomic":               false,
			"player_root_address":           fmt.Sprintf("0x%08X", inspection.Player.RootAddress),
			"target_root_address":           fmt.Sprintf("0x%08X", inspection.Target.RootAddress),
			"player":                        entityPointerResult(inspection.Player),
			"target":                        entityPointerResult(inspection.Target),
			"build_compatibility_confirmed": false,
		})
	case "rtm_inspect_player_scale":
		var args targetArgs
		if err := decodeStrict(call.Arguments, &args); err != nil {
			return errorResult(err)
		}
		if err := validateTarget(args.Target); err != nil {
			return errorResult(err)
		}
		state, err := s.client.InspectPlayerScale(ctx, args.Target)
		if err != nil {
			return errorResult(err)
		}
		return successResult(map[string]any{
			"ok":                         true,
			"read_only":                  true,
			"snapshot_atomic":            state.SnapshotAtomic,
			"player_name":                state.PlayerName,
			"handle":                     fmt.Sprintf("0x%08X", state.Handle),
			"handle_table_entry_address": fmt.Sprintf("0x%08X", state.HandleTableAddress),
			"resolved_object":            fmt.Sprintf("0x%08X", state.ResolvedObject),
			"object_index":               state.ObjectIndex,
			"slot":                       state.Slot,
			"scale_address":              fmt.Sprintf("0x%08X", state.ScaleAddress),
			"scale_raw_hex":              state.ScaleRawHex,
			"scale":                      state.Scale,
			"resolver_signature_match":   state.ResolverSignatureMatch,
			"gameplay_effect_confirmed":  false,
		})
	case "rtm_set_player_scale":
		var args struct {
			Target string  `json:"target_ipv4"`
			Scale  float32 `json:"scale"`
		}
		if err := decodeStrict(call.Arguments, &args); err != nil {
			return errorResult(err)
		}
		if err := validateTarget(args.Target); err != nil {
			return errorResult(err)
		}
		s.memoryWriteMu.Lock()
		defer s.memoryWriteMu.Unlock()
		if err := s.verifyBuildCompatibility(ctx, args.Target); err != nil {
			return errorResult(err)
		}
		result, err := s.client.SetPlayerScale(ctx, args.Target, args.Scale)
		if err != nil {
			return errorResult(err)
		}
		return successResult(map[string]any{
			"ok":                           true,
			"read_only":                    false,
			"snapshot_atomic":              result.SnapshotAtomic,
			"player_name":                  result.PlayerName,
			"handle":                       fmt.Sprintf("0x%08X", result.Handle),
			"object_index":                 result.ObjectIndex,
			"slot":                         result.Slot,
			"scale_address":                fmt.Sprintf("0x%08X", result.ScaleAddress),
			"original_scale_raw_hex":       result.ScaleRawHex,
			"new_scale":                    result.NewScale,
			"readback_raw_hex":             result.ReadbackRawHex,
			"readback_exact":               result.ReadbackExact,
			"resolver_signature_match":     result.ResolverSignatureMatch,
			"gameplay_effect_confirmed":    false,
			"server_persistence_confirmed": false,
		})
	case "rtm_write_memory":
		var args struct {
			Target  string `json:"target_ipv4"`
			Address string `json:"address"`
			Hex     string `json:"hex"`
		}
		if err := decodeStrict(call.Arguments, &args); err != nil {
			return errorResult(err)
		}
		if err := validateTarget(args.Target); err != nil {
			return errorResult(err)
		}
		address, err := parseAddress(args.Address)
		if err != nil {
			return errorResult(err)
		}
		data, err := parseHex(args.Hex)
		if err != nil {
			return errorResult(err)
		}
		if err := validateMemoryRange(address, len(data)); err != nil {
			return errorResult(err)
		}
		if err := s.writeMemory(ctx, args.Target, args.Address, data); err != nil {
			return errorResult(err)
		}
		return successResult(map[string]any{"ok": true, "verified": true, "address": fmt.Sprintf("0x%08X", address), "length": len(data), "hex": strings.ToUpper(hex.EncodeToString(data))})
	case "rtm_set_brightness":
		var args struct {
			Target string `json:"target_ipv4"`
			Preset string `json:"preset"`
		}
		if err := decodeStrict(call.Arguments, &args); err != nil {
			return errorResult(err)
		}
		if err := validateTarget(args.Target); err != nil {
			return errorResult(err)
		}
		presets := map[string]string{"bright": "42480000", "normal": "3F800000", "inverted": "C1A00000"}
		hexValue, ok := presets[args.Preset]
		if !ok {
			return errorResult(errors.New("preset must be bright, normal, or inverted"))
		}
		data, _ := hex.DecodeString(hexValue)
		if err := s.writeMemory(ctx, args.Target, "006101C0", data); err != nil {
			return errorResult(err)
		}
		return successResult(map[string]any{"ok": true, "verified": true, "address": "0x006101C0", "preset": args.Preset, "hex": hexValue, "effect_confirmed": false})
	case "rtm_set_legacy_scalar_preset":
		var args struct {
			Target string `json:"target_ipv4"`
			Mode   string `json:"mode"`
		}
		if err := decodeStrict(call.Arguments, &args); err != nil {
			return errorResult(err)
		}
		if err := validateTarget(args.Target); err != nil {
			return errorResult(err)
		}
		presets := map[string]string{
			"normal":         "3F800000",
			"max_damage":     "3FFFFFFF",
			"one_hit_kill_2": "42C80000",
		}
		hexValue, ok := presets[args.Mode]
		if !ok {
			return errorResult(errors.New("mode must be normal, max_damage, or one_hit_kill_2"))
		}
		s.memoryWriteMu.Lock()
		defer s.memoryWriteMu.Unlock()
		if err := s.verifyBuildCompatibility(ctx, args.Target); err != nil {
			return errorResult(err)
		}
		code, err := s.client.ReadMemory(ctx, args.Target, "00444B64", 32)
		if err != nil {
			return errorResult(fmt.Errorf("read 01.24 damage-code signature: %w", err))
		}
		const expectedCode = "3C600044C03601283CC03F803B4349EC3061034462C4000063850000C05A0000"
		if strings.ToUpper(hex.EncodeToString(code)) != expectedCode {
			return errorResult(errors.New("01.24 damage-code signature mismatch at 0x00444B64; write refused"))
		}
		before, err := s.client.ReadMemory(ctx, args.Target, "004449EC", 4)
		if err != nil {
			return errorResult(fmt.Errorf("read current damage mode: %w", err))
		}
		beforeHex := strings.ToUpper(hex.EncodeToString(before))
		known := map[string]bool{"3F800000": true, "3FFFFFFF": true, "42C80000": true}
		if !known[beforeHex] {
			return errorResult(fmt.Errorf("current value %q at 0x004449EC is not a known source preset; write refused", beforeHex))
		}
		data, _ := hex.DecodeString(hexValue)
		if err := ctx.Err(); err != nil {
			return errorResult(err)
		}
		if err := s.client.WriteMemory(ctx, args.Target, "004449EC", data); err != nil {
			return errorResult(err)
		}
		readback, err := s.client.ReadMemory(ctx, args.Target, "004449EC", 4)
		if err != nil {
			return errorResult(fmt.Errorf("write was sent but final readback failed: %w", err))
		}
		readbackHex := strings.ToUpper(hex.EncodeToString(readback))
		if readbackHex != hexValue {
			return errorResult(fmt.Errorf("write was not verified: sent %s, read back %s", hexValue, readbackHex))
		}
		return successResult(map[string]any{
			"ok": true, "verified": true, "address": "0x004449EC", "mode": args.Mode,
			"previous_hex": beforeHex, "hex": hexValue, "build_signature_verified": true,
			"gameplay_effect_confirmed": false,
		})
	case "rtm_notify":
		var args struct {
			Target  string `json:"target_ipv4"`
			Message string `json:"message"`
		}
		if err := decodeStrict(call.Arguments, &args); err != nil {
			return errorResult(err)
		}
		if err := validateTarget(args.Target); err != nil {
			return errorResult(err)
		}
		if err := s.client.Notify(ctx, args.Target, args.Message); err != nil {
			return errorResult(err)
		}
		return successResult(map[string]any{"ok": true, "request_dispatched": true, "delivery_confirmed": false})
	case "rtm_play_beep_sequence":
		var args struct {
			Target   string `json:"target_ipv4"`
			Sequence string `json:"sequence"`
		}
		if err := decodeStrict(call.Arguments, &args); err != nil {
			return errorResult(err)
		}
		if err := validateTarget(args.Target); err != nil {
			return errorResult(err)
		}
		if _, ok := beepSequences[args.Sequence]; !ok {
			return errorResult(errors.New("sequence must be fanfare, imperial, mario, nokia, sos, or shave"))
		}
		return s.playBeepSequence(ctx, args.Target, args.Sequence)
	case "rtm_launch_game":
		var args struct {
			Target  string `json:"target_ipv4"`
			ISOPath string `json:"iso_path"`
		}
		if err := decodeStrict(call.Arguments, &args); err != nil {
			return errorResult(err)
		}
		if err := validateTarget(args.Target); err != nil {
			return errorResult(err)
		}
		if args.ISOPath == "" {
			args.ISOPath = rtmclient.DefaultDestinyISOPath
		}
		if _, err := rtmclient.ValidateDestinyISOPath(args.ISOPath); err != nil {
			return errorResult(err)
		}
		launch, err := s.client.LaunchGame(ctx, args.Target, args.ISOPath)
		data := map[string]any{
			"ok":                   err == nil,
			"target_ipv4":          args.Target,
			"iso_path":             launch.ISOPath,
			"stages":               launch.Stages,
			"game_start_confirmed": launch.GameStartConfirmed,
		}
		if err != nil {
			data["error"] = err.Error()
			return structuredErrorResult(data)
		}
		data["request_dispatched"] = true
		return successResult(data)
	default:
		return errorResult(fmt.Errorf("unknown RTM tool %q", call.Name))
	}
}

func (s *Server) playBeepSequence(ctx context.Context, target, key string) map[string]any {
	sequence := beepSequences[key]
	beeper, ok := s.client.(interface {
		Beep(context.Context, string, int) error
	})
	if !ok {
		return errorResult(errors.New("RTM client does not support PS3 system beeps"))
	}
	select {
	case s.beepSequenceSlot <- struct{}{}:
		defer func() { <-s.beepSequenceSlot }()
	case <-ctx.Done():
		return beepSequenceError(target, key, sequence, 0, ctx.Err())
	}

	acknowledged := 0
	for _, note := range sequence.notes {
		if err := ctx.Err(); err != nil {
			return beepSequenceError(target, key, sequence, acknowledged, err)
		}

		// Finish the current tone request before observing cancellation. This
		// keeps the reported count tied to HTTP acknowledgements and lets a stop
		// request take effect between tones, as the UI Jukebox does.
		toneCtx, cancel := context.WithTimeout(context.WithoutCancel(ctx), 8*time.Second)
		err := beeper.Beep(toneCtx, target, note.tone)
		cancel()
		if err != nil {
			return beepSequenceError(target, key, sequence, acknowledged, err)
		}
		acknowledged++
		if err := ctx.Err(); err != nil {
			return beepSequenceError(target, key, sequence, acknowledged, err)
		}
		if err := waitBeepPause(ctx, time.Duration(note.pauseMS)*time.Millisecond); err != nil {
			return beepSequenceError(target, key, sequence, acknowledged, err)
		}
	}

	return successResult(map[string]any{
		"ok": true, "target_ipv4": target, "sequence": key, "sequence_name": sequence.name,
		"http_acknowledged_tones": acknowledged, "total_tones": len(sequence.notes),
		"sequence_complete": true, "audio_heard_confirmed": false, "game_memory_written": false,
	})
}

func beepSequenceError(target, key string, sequence beepSequence, acknowledged int, cause error) map[string]any {
	return structuredErrorResult(map[string]any{
		"ok": false, "target_ipv4": target, "sequence": key, "sequence_name": sequence.name,
		"http_acknowledged_tones": acknowledged, "total_tones": len(sequence.notes),
		"sequence_complete": false, "audio_heard_confirmed": false, "game_memory_written": false,
		"error": cause.Error(),
	})
}

func waitBeepPause(ctx context.Context, pause time.Duration) error {
	timer := time.NewTimer(pause)
	defer timer.Stop()
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-timer.C:
		return nil
	}
}

func entityPointerResult(snapshot rtmclient.EntityPointerSnapshot) map[string]any {
	var coordinatesAddress any
	if snapshot.CoordinatesAddress != 0 {
		coordinatesAddress = fmt.Sprintf("0x%08X", snapshot.CoordinatesAddress)
	}
	var activationAddress any
	if snapshot.ActivationAddress != 0 {
		activationAddress = fmt.Sprintf("0x%08X", snapshot.ActivationAddress)
	}
	return map[string]any{
		"root_address":        fmt.Sprintf("0x%08X", snapshot.RootAddress),
		"pointer":             fmt.Sprintf("0x%08X", snapshot.Pointer),
		"pointer_raw_hex":     snapshot.PointerRawHex,
		"pointer_state":       snapshot.PointerState,
		"pointer_error":       snapshot.PointerError,
		"pointer_note":        snapshot.PointerNote,
		"coordinates_address": coordinatesAddress,
		"coordinates_raw_hex": snapshot.CoordinatesRawHex,
		"coordinates_state":   snapshot.CoordinatesState,
		"x":                   snapshot.X,
		"y":                   snapshot.Y,
		"z":                   snapshot.Z,
		"activation_address":  activationAddress,
		"activation_raw_hex":  snapshot.ActivationRawHex,
		"activation_state":    snapshot.ActivationState,
		"activation_value":    snapshot.ActivationValue,
	}
}

func (s *Server) writeMemory(ctx context.Context, target, address string, data []byte) error {
	s.memoryWriteMu.Lock()
	defer s.memoryWriteMu.Unlock()
	if err := ctx.Err(); err != nil {
		return err
	}
	parsedAddress, err := parseAddress(address)
	if err != nil {
		return err
	}
	if err := rtmclient.ValidateDestinyWriteRange(parsedAddress, len(data)); err != nil {
		return err
	}
	if err := s.verifyBuildCompatibility(ctx, target); err != nil {
		return err
	}
	return s.client.WriteMemory(ctx, target, address, data)
}

func (s *Server) verifyBuildCompatibility(ctx context.Context, target string) error {
	if verifier, ok := s.client.(interface {
		VerifyDestiny0124Profile(context.Context, string) error
	}); ok {
		return verifier.VerifyDestiny0124Profile(ctx, target)
	}
	return rtmclient.VerifyDestiny0124SignatureProfile(ctx, target, s.client.ReadMemory)
}

type targetArgs struct {
	Target string `json:"target_ipv4"`
}

func decodeStrict(raw json.RawMessage, destination any) error {
	trimmed := bytes.TrimSpace(raw)
	if len(trimmed) == 0 || trimmed[0] != '{' {
		return errors.New("arguments must be a JSON object")
	}
	decoder := json.NewDecoder(bytes.NewReader(trimmed))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(destination); err != nil {
		return fmt.Errorf("invalid arguments: %w", err)
	}
	if decoder.Decode(new(any)) != io.EOF {
		return errors.New("arguments must contain exactly one JSON object")
	}
	return nil
}

func validateTarget(target string) error {
	ip := net.ParseIP(strings.TrimSpace(target))
	if ip == nil || ip.To4() == nil || !ip.IsPrivate() || strings.TrimSpace(target) != target {
		return errors.New("target_ipv4 must be a private IPv4 address")
	}
	return nil
}

func parseAddress(input string) (uint32, error) {
	value := strings.TrimSpace(input)
	value = strings.TrimPrefix(strings.TrimPrefix(value, "0x"), "0X")
	if value == "" || len(value) > 8 {
		return 0, errors.New("address must be a 32-bit hexadecimal value")
	}
	parsed, err := strconv.ParseUint(value, 16, 32)
	if err != nil {
		return 0, errors.New("address must be a 32-bit hexadecimal value")
	}
	return uint32(parsed), nil
}

func validateMemoryRange(address uint32, length int) error {
	if length < 1 || uint64(address)+uint64(length)-1 > 0xFFFFFFFF {
		return errors.New("address range exceeds 32-bit memory")
	}
	return nil
}

func parseHex(input string) ([]byte, error) {
	value := strings.TrimSpace(input)
	value = strings.TrimPrefix(strings.TrimPrefix(value, "0x"), "0X")
	if len(value) < 2 || len(value) > 16 || len(value)%2 != 0 {
		return nil, errors.New("hex must contain 1 to 8 complete bytes")
	}
	data, err := hex.DecodeString(value)
	if err != nil {
		return nil, errors.New("hex must contain only hexadecimal bytes")
	}
	return data, nil
}

func successResult(data map[string]any) map[string]any {
	encoded, _ := json.Marshal(data)
	return map[string]any{"content": []any{map[string]any{"type": "text", "text": string(encoded)}}, "structuredContent": data, "isError": false}
}

func errorResult(err error) map[string]any {
	data := map[string]any{"ok": false, "error": err.Error()}
	return structuredErrorResult(data)
}

func structuredErrorResult(data map[string]any) map[string]any {
	encoded, _ := json.Marshal(data)
	return map[string]any{"content": []any{map[string]any{"type": "text", "text": string(encoded)}}, "structuredContent": data, "isError": true}
}

func (s *Server) writeResult(id json.RawMessage, result any) error {
	return s.writeResponse(rpcResponse{JSONRPC: "2.0", ID: id, Result: &result})
}

func (s *Server) writeResponse(response rpcResponse) error {
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	return json.NewEncoder(s.out).Encode(response)
}
