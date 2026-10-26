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
	WriteMemory(context.Context, string, string, []byte) error
	Notify(context.Context, string, string) error
	LaunchGame(context.Context, string, string) (rtmclient.LaunchResult, error)
}

type Server struct {
	client Client
	out    io.Writer

	writeMu       sync.Mutex
	memoryWriteMu sync.Mutex
	callsMu       sync.Mutex
	calls         map[string]context.CancelFunc
	wg            sync.WaitGroup
}

func New(client Client) *Server {
	return &Server{client: client, calls: make(map[string]context.CancelFunc)}
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
		Name:        "rtm_write_memory",
		Title:       "Write Destiny memory",
		Description: "Write 1–8 bytes to the Destiny process and report success only after exact fresh readback. The address is hexadecimal; only use known values for the matching game build. This does not call game functions.",
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
		Description: "Write Bright (50), Normal (1), or Inverted (-20) from the local 1.24 C# source to 0x006101C0 and verify the bytes. Readback confirms bytes only; the in-game visual effect has not been observed.",
		InputSchema: objectSchema(map[string]any{
			"target_ipv4": map[string]any{"type": "string", "description": "Private IPv4 address of the PS3 running webMAN MOD."},
			"preset":      map[string]any{"type": "string", "enum": []string{"bright", "normal", "inverted"}},
		}, "target_ipv4", "preset"),
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

const instructions = "Use only the user's private PS3 IPv4 address. Memory reads and writes target Destiny process 0x1010200 through webMAN MOD on port 80. Reads are capped at 64 KiB; writes are capped at 8 bytes and require exact readback. rtm_status checks webMAN reachability, not whether Destiny is running. Player-position and entity-pointer tools follow the local Overwalls pointer map using read-only requests; coordinates, pointer semantics, and build compatibility remain unconfirmed. Brightness effects are unverified. Notifications report dispatch, not display. Game-launch steps report HTTP acknowledgement and do not confirm that Destiny started. No generic function-call endpoint is exposed."

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
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
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
	return s.client.WriteMemory(ctx, target, address, data)
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
