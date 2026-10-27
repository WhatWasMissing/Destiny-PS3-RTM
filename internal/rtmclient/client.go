package rtmclient

import (
	"context"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"html"
	"io"
	"math"
	"net"
	"net/http"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

const DefaultProcessID = "0x1010200"

const PlayerPointerAddress uint32 = 0x0190EBB0
const TargetPointerAddress uint32 = 0x0190EBB4
const PlayerPositionOffset uint32 = 0x140
const EntityActivationOffset uint32 = 0x1B8

const (
	PointerStateValid        = "valid"
	PointerStateNull         = "null"
	PointerStateInvalid      = "invalid"
	PointerStateSameAsPlayer = "same_as_player"
)

type EntityPointerSnapshot struct {
	RootAddress        uint32   `json:"root_address"`
	Pointer            uint32   `json:"pointer"`
	PointerRawHex      string   `json:"pointer_raw_hex"`
	PointerState       string   `json:"pointer_state"`
	PointerError       string   `json:"pointer_error,omitempty"`
	PointerNote        string   `json:"pointer_note,omitempty"`
	CoordinatesAddress uint32   `json:"coordinates_address,omitempty"`
	CoordinatesRawHex  string   `json:"coordinates_raw_hex,omitempty"`
	CoordinatesState   string   `json:"coordinates_state"`
	X                  *float32 `json:"x"`
	Y                  *float32 `json:"y"`
	Z                  *float32 `json:"z"`
	ActivationAddress  uint32   `json:"activation_address,omitempty"`
	ActivationRawHex   string   `json:"activation_raw_hex,omitempty"`
	ActivationState    string   `json:"activation_state,omitempty"`
	ActivationValue    *float32 `json:"activation_value,omitempty"`
}

type EntityPointerInspection struct {
	Player EntityPointerSnapshot `json:"player"`
	Target EntityPointerSnapshot `json:"target"`
}

type PlayerPosition struct {
	PlayerPointer      uint32
	CoordinatesAddress uint32
	X                  float32
	Y                  float32
	Z                  float32
	RawHex             string
}

const maxResponseBytes = 8 * 1024 * 1024

var outputTextarea = regexp.MustCompile(`(?is)<textarea[^>]*id=["']output["'][^>]*>([0-9a-f\s]*)</textarea>`)
var htmlTag = regexp.MustCompile(`(?s)<[^>]*>`)
var nonHex = regexp.MustCompile(`[^0-9A-Fa-f]`)

type Client struct {
	HTTP      *http.Client
	ProcessID string
	sleep     func(context.Context, time.Duration) error

	buildProfileMu    sync.Mutex
	buildProfileCache map[string]time.Time

	// endpointOverride is only set by package tests so they can use loopback servers.
	endpointOverride string
}

func New() *Client {
	return &Client{
		HTTP: &http.Client{
			Timeout:       8 * time.Second,
			Transport:     &http.Transport{Proxy: nil},
			CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse },
		},
		ProcessID: DefaultProcessID,
		sleep:     sleepContext,
	}
}

const DefaultDestinyISOPath = "/dev_hdd0/PS3ISO/Destiny.iso"

type LaunchStage struct {
	Name         string `json:"name"`
	Acknowledged bool   `json:"acknowledged"`
	HTTPStatus   int    `json:"http_status,omitempty"`
	Error        string `json:"error,omitempty"`
}

type LaunchResult struct {
	TargetIPv4         string        `json:"target_ipv4"`
	ISOPath            string        `json:"iso_path"`
	Stages             []LaunchStage `json:"stages"`
	GameStartConfirmed bool          `json:"game_start_confirmed"`
}

var destinyISOName = regexp.MustCompile(`(?i)^[a-z0-9][a-z0-9 _().-]{0,124}\.iso$`)

// ValidateDestinyISOPath allows one ISO file in webMAN's standard PS3ISO folder.
// The local RTM source uses this mount location; arbitrary webMAN paths are not exposed.
func ValidateDestinyISOPath(path string) (string, error) {
	const prefix = "/dev_hdd0/PS3ISO/"
	if !strings.HasPrefix(path, prefix) || len(path) > 160 {
		return "", errors.New("ISO path must be one file under /dev_hdd0/PS3ISO/")
	}
	name := strings.TrimPrefix(path, prefix)
	if strings.Contains(name, "..") || !destinyISOName.MatchString(name) {
		return "", errors.New("ISO filename must be a simple .iso filename without URL or path characters")
	}
	return path, nil
}

func sleepContext(ctx context.Context, delay time.Duration) error {
	timer := time.NewTimer(delay)
	defer timer.Stop()
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-timer.C:
		return nil
	}
}

// LaunchGame follows the recovered RTM's webMAN launch sequence. HTTP success
// acknowledges individual requests; it cannot confirm that Destiny booted.
func (c *Client) LaunchGame(ctx context.Context, ip, isoPath string) (LaunchResult, error) {
	result := LaunchResult{TargetIPv4: ip, GameStartConfirmed: false}
	validatedPath, err := ValidateDestinyISOPath(isoPath)
	if err != nil {
		return result, err
	}
	result.ISOPath = validatedPath
	if c == nil {
		return result, errors.New("client is nil")
	}

	steps := []struct {
		name  string
		path  string
		pause time.Duration
	}{
		{name: "mount_iso", path: "/mount.ps3" + validatedPath + "&mode=silent", pause: 3 * time.Second},
		{name: "return_to_xmb", path: "/wmnav.ps3/game", pause: time.Second},
		{name: "start_mounted_game", path: "/play.ps3"},
	}

	for index, step := range steps {
		result.Stages = append(result.Stages, LaunchStage{Name: step.name})
		stage := &result.Stages[len(result.Stages)-1]
		response, requestErr := c.request(ctx, ip, step.path, nil)
		if requestErr != nil {
			stage.Error = requestErr.Error()
			return result, fmt.Errorf("%s stage failed: %w", step.name, requestErr)
		}
		stage.Acknowledged = true
		stage.HTTPStatus = response.StatusCode
		response.Body.Close()

		if step.pause > 0 {
			nextStage := steps[index+1].name
			wait := c.sleep
			if wait == nil {
				wait = sleepContext
			}
			if waitErr := wait(ctx, step.pause); waitErr != nil {
				return result, fmt.Errorf("waiting before %s stage failed after %s was acknowledged: %w", nextStage, step.name, waitErr)
			}
		}
	}
	return result, nil
}

func validateIPv4(ipText string) (net.IP, error) {
	ip := net.ParseIP(strings.TrimSpace(ipText))
	if ip == nil || ip.To4() == nil {
		return nil, errors.New("target must be a private IPv4 address")
	}
	ip = ip.To4()
	if !ip.IsPrivate() {
		return nil, errors.New("target must be a private IPv4 address")
	}
	return ip, nil
}

func (c *Client) endpoint(ipText, path, rawQuery string) (string, error) {
	ip, err := validateIPv4(ipText)
	if err != nil {
		return "", err
	}
	host := ip.String()
	if c.endpointOverride != "" {
		host = c.endpointOverride
	}
	u := url.URL{Scheme: "http", Host: host, Path: path, RawQuery: rawQuery}
	return u.String(), nil
}

func (c *Client) httpClient() *http.Client {
	if c != nil && c.HTTP != nil {
		return c.HTTP
	}
	return New().HTTP
}

func readBounded(body io.Reader, limit int64) ([]byte, error) {
	data, err := io.ReadAll(io.LimitReader(body, limit+1))
	if err != nil {
		return nil, err
	}
	if int64(len(data)) > limit {
		return nil, errors.New("console response exceeds 8 MB")
	}
	return data, nil
}

func (c *Client) request(ctx context.Context, ipText, path string, query url.Values) (*http.Response, error) {
	rawQuery := ""
	if query != nil {
		rawQuery = query.Encode()
	}
	return c.requestWithRawQuery(ctx, ipText, path, rawQuery)
}

func (c *Client) requestWithRawQuery(ctx context.Context, ipText, path, rawQuery string) (*http.Response, error) {
	if c == nil {
		return nil, errors.New("client is nil")
	}
	endpoint, err := c.endpoint(ipText, path, rawQuery)
	if err != nil {
		return nil, err
	}
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, endpoint, nil)
	if err != nil {
		return nil, err
	}
	response, err := c.httpClient().Do(request)
	if err != nil {
		return nil, fmt.Errorf("console request failed: %w", err)
	}
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		response.Body.Close()
		return nil, fmt.Errorf("console returned HTTP %d", response.StatusCode)
	}
	return response, nil
}

func (c *Client) Status(ctx context.Context, ip string) (int, error) {
	response, err := c.request(ctx, ip, "/", nil)
	if err != nil {
		return 0, err
	}
	response.Body.Close()
	return response.StatusCode, nil
}

func (c *Client) ReadMemory(ctx context.Context, ip, address string, length int) ([]byte, error) {
	if length < 1 || length > 0x10000 {
		return nil, errors.New("length must be between 1 and 65536 bytes")
	}
	addr, err := ParseAddress(address)
	if err != nil {
		return nil, err
	}
	if uint64(addr)+uint64(length)-1 > 0xFFFFFFFF {
		return nil, errors.New("address range exceeds 32-bit memory")
	}
	process := c.ProcessID
	if process == "" {
		process = DefaultProcessID
	}
	query := url.Values{
		"proc": {process},
		"addr": {fmt.Sprintf("%08X", addr)},
		"len":  {strconv.Itoa(length)},
	}
	response, err := c.request(ctx, ip, "/getmem.ps3mapi", query)
	if err != nil {
		return nil, err
	}
	defer response.Body.Close()
	page, err := readBounded(response.Body, maxResponseBytes)
	if err != nil {
		return nil, err
	}
	data, err := parseMemoryResponse(string(page), addr, length)
	if err != nil {
		return nil, err
	}
	return data, nil
}

// VerifyDestiny0124Profile checks the fixed-address code fingerprint for the
// 01.24 update before a write. A successful check is cached briefly per console
// so multi-write RTM actions do not repeat the same three reads for each byte.
func (c *Client) VerifyDestiny0124Profile(ctx context.Context, ip string) error {
	if c == nil {
		return errors.New("console client is unavailable; write refused")
	}
	process := c.ProcessID
	if process == "" {
		process = DefaultProcessID
	}
	if !strings.EqualFold(process, DefaultProcessID) {
		return errors.New("build verification and memory writes must target the Destiny process")
	}
	cacheKey := ip + "|" + process
	c.buildProfileMu.Lock()
	defer c.buildProfileMu.Unlock()
	if until := c.buildProfileCache[cacheKey]; time.Now().Before(until) {
		return nil
	}
	if err := VerifyDestiny0124SignatureProfile(ctx, ip, c.ReadMemory); err != nil {
		return err
	}
	if c.buildProfileCache == nil {
		c.buildProfileCache = make(map[string]time.Time)
	}
	c.buildProfileCache[cacheKey] = time.Now().Add(destiny0124ProfileCacheTTL)
	return nil
}

// InspectDestiny0124Profile returns fresh per-signature reads without applying
// the write gate or caching the result.
func (c *Client) InspectDestiny0124Profile(ctx context.Context, ip string) BuildProfileStatus {
	if c == nil {
		return InspectDestiny0124SignatureProfile(ctx, ip, nil)
	}
	return InspectDestiny0124SignatureProfile(ctx, ip, c.ReadMemory)
}

// ReadPlayerPosition follows the dynamic player pointer and coordinate offsets
// recorded in the local Overwalls source. It is read-only and validates the
// pointer and decoded values before returning them.
func (c *Client) ReadPlayerPosition(ctx context.Context, ip string) (PlayerPosition, error) {
	var result PlayerPosition
	pointerBytes, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", PlayerPointerAddress), 4)
	if err != nil {
		return result, fmt.Errorf("read player pointer at 0x%08X: %w", PlayerPointerAddress, err)
	}
	if len(pointerBytes) != 4 {
		return result, errors.New("player pointer read returned an invalid length")
	}
	pointer := binary.BigEndian.Uint32(pointerBytes)
	const lastCoordinateByte = PlayerPositionOffset + 0x0B
	if pointer < 0x00010000 || pointer&3 != 0 || pointer > 0x7FFFFFFF-lastCoordinateByte {
		return result, fmt.Errorf("invalid player pointer 0x%08X", pointer)
	}

	coordinatesAddress := pointer + PlayerPositionOffset
	coordinateBytes, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", coordinatesAddress), 12)
	if err != nil {
		return result, fmt.Errorf("read player coordinates at 0x%08X: %w", coordinatesAddress, err)
	}
	if len(coordinateBytes) != 12 {
		return result, errors.New("player coordinate read returned an invalid length")
	}
	values := [3]float32{
		math.Float32frombits(binary.BigEndian.Uint32(coordinateBytes[0:4])),
		math.Float32frombits(binary.BigEndian.Uint32(coordinateBytes[4:8])),
		math.Float32frombits(binary.BigEndian.Uint32(coordinateBytes[8:12])),
	}
	for _, value := range values {
		if math.IsNaN(float64(value)) || math.IsInf(float64(value), 0) {
			return result, errors.New("player coordinates contain a non-finite Float32 value")
		}
	}
	return PlayerPosition{
		PlayerPointer:      pointer,
		CoordinatesAddress: coordinatesAddress,
		X:                  values[0],
		Y:                  values[1],
		Z:                  values[2],
		RawHex:             strings.ToUpper(hex.EncodeToString(coordinateBytes)),
	}, nil
}

// InspectEntityPointers reads the Overwalls player and OPK-target pointer roots,
// then decodes source-mapped fields for structurally valid pointers. Null and
// invalid pointers are represented without dereferencing them; self-targets
// are marked in the result. This method performs memory reads only.
func (c *Client) InspectEntityPointers(ctx context.Context, ip string) (EntityPointerInspection, error) {
	var result EntityPointerInspection
	playerRoot, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", PlayerPointerAddress), 4)
	if err != nil {
		return result, fmt.Errorf("read player pointer at 0x%08X: %w", PlayerPointerAddress, err)
	}
	targetRoot, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", TargetPointerAddress), 4)
	if err != nil {
		return result, fmt.Errorf("read target pointer at 0x%08X: %w", TargetPointerAddress, err)
	}
	if len(playerRoot) != 4 || len(targetRoot) != 4 {
		return result, errors.New("entity pointer read returned an invalid length")
	}

	playerPointer := binary.BigEndian.Uint32(playerRoot)
	targetPointer := binary.BigEndian.Uint32(targetRoot)
	result.Player = EntityPointerSnapshot{
		RootAddress:      PlayerPointerAddress,
		Pointer:          playerPointer,
		PointerRawHex:    strings.ToUpper(hex.EncodeToString(playerRoot)),
		CoordinatesState: "not_read",
		ActivationState:  "not_applicable",
	}
	result.Target = EntityPointerSnapshot{
		RootAddress:      TargetPointerAddress,
		Pointer:          targetPointer,
		PointerRawHex:    strings.ToUpper(hex.EncodeToString(targetRoot)),
		CoordinatesState: "not_read",
		ActivationState:  "not_read",
	}

	classifyEntityPointer(&result.Player)
	classifyEntityPointer(&result.Target)
	if result.Player.PointerState == PointerStateValid {
		if err := c.readEntityCoordinates(ctx, ip, &result.Player); err != nil {
			return result, fmt.Errorf("read player entity: %w", err)
		}
	}
	if result.Target.PointerState == PointerStateValid {
		if result.Player.PointerState == PointerStateValid && targetPointer == playerPointer {
			result.Target.PointerState = PointerStateSameAsPlayer
			result.Target.PointerNote = "target pointer equals the player pointer; source OPK logic skips self-targets"
		}
		if err := c.readEntityCoordinates(ctx, ip, &result.Target); err != nil {
			return result, fmt.Errorf("read target entity: %w", err)
		}
		activationAddress := targetPointer + EntityActivationOffset
		result.Target.ActivationAddress = activationAddress
		activationBytes, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", activationAddress), 4)
		if err != nil {
			return result, fmt.Errorf("read target activation field at 0x%08X: %w", activationAddress, err)
		}
		if len(activationBytes) != 4 {
			return result, errors.New("target activation read returned an invalid length")
		}
		activation := math.Float32frombits(binary.BigEndian.Uint32(activationBytes))
		result.Target.ActivationRawHex = strings.ToUpper(hex.EncodeToString(activationBytes))
		if finiteFloat32(activation) {
			result.Target.ActivationValue = float32Pointer(activation)
			result.Target.ActivationState = "valid"
		} else {
			result.Target.ActivationState = "non_finite"
		}
	}
	return result, nil
}

const entityPointerMaxOffset uint32 = EntityActivationOffset + 3

func classifyEntityPointer(snapshot *EntityPointerSnapshot) {
	if snapshot.Pointer == 0 {
		snapshot.PointerState = PointerStateNull
		return
	}
	if snapshot.Pointer < 0x00010000 {
		snapshot.PointerState = PointerStateInvalid
		snapshot.PointerError = "pointer is below the user-memory range"
		return
	}
	if snapshot.Pointer&3 != 0 {
		snapshot.PointerState = PointerStateInvalid
		snapshot.PointerError = "pointer is not four-byte aligned"
		return
	}
	if snapshot.Pointer > 0x7FFFFFFF-entityPointerMaxOffset {
		snapshot.PointerState = PointerStateInvalid
		snapshot.PointerError = "pointer plus source fields exceeds the supported 32-bit user-memory range"
		return
	}
	snapshot.PointerState = PointerStateValid
}

func (c *Client) readEntityCoordinates(ctx context.Context, ip string, snapshot *EntityPointerSnapshot) error {
	address := snapshot.Pointer + PlayerPositionOffset
	snapshot.CoordinatesAddress = address
	data, err := c.ReadMemory(ctx, ip, fmt.Sprintf("%08X", address), 12)
	if err != nil {
		return fmt.Errorf("read coordinates at 0x%08X: %w", address, err)
	}
	if len(data) != 12 {
		return errors.New("coordinate read returned an invalid length")
	}
	snapshot.CoordinatesRawHex = strings.ToUpper(hex.EncodeToString(data))
	values := [3]float32{
		math.Float32frombits(binary.BigEndian.Uint32(data[0:4])),
		math.Float32frombits(binary.BigEndian.Uint32(data[4:8])),
		math.Float32frombits(binary.BigEndian.Uint32(data[8:12])),
	}
	snapshot.X = finiteFloat32Pointer(values[0])
	snapshot.Y = finiteFloat32Pointer(values[1])
	snapshot.Z = finiteFloat32Pointer(values[2])
	if snapshot.X == nil || snapshot.Y == nil || snapshot.Z == nil {
		snapshot.CoordinatesState = "non_finite"
	} else {
		snapshot.CoordinatesState = "valid"
	}
	return nil
}

func finiteFloat32(value float32) bool {
	return !math.IsNaN(float64(value)) && !math.IsInf(float64(value), 0)
}

func float32Pointer(value float32) *float32 {
	return &value
}

func finiteFloat32Pointer(value float32) *float32 {
	if !finiteFloat32(value) {
		return nil
	}
	return float32Pointer(value)
}

func (c *Client) WriteMemory(ctx context.Context, ip, address string, data []byte) error {
	if len(data) < 1 || len(data) > 8 {
		return errors.New("writes must be between 1 and 8 bytes")
	}
	addr, err := ParseAddress(address)
	if err != nil {
		return err
	}
	if uint64(addr)+uint64(len(data))-1 > 0xFFFFFFFF {
		return errors.New("address range exceeds 32-bit memory")
	}
	if err := ValidateDestinyWriteRange(addr, len(data)); err != nil {
		return err
	}
	if err := c.VerifyDestiny0124Profile(ctx, ip); err != nil {
		return err
	}
	process := c.ProcessID
	if process == "" {
		process = DefaultProcessID
	}
	query := url.Values{
		"proc": {process},
		"addr": {fmt.Sprintf("%08X", addr)},
		"val":  {strings.ToUpper(hex.EncodeToString(data))},
	}
	response, err := c.request(ctx, ip, "/setmem.ps3mapi", query)
	if err != nil {
		return err
	}
	response.Body.Close()
	readback, err := c.ReadMemory(ctx, ip, address, len(data))
	if err != nil {
		return fmt.Errorf("write was sent but readback failed: %w", err)
	}
	if !equalBytes(data, readback) {
		return fmt.Errorf("write was not verified: sent %X, read back %X", data, readback)
	}
	return nil
}

func (c *Client) Notify(ctx context.Context, ip, message string) error {
	message = strings.TrimSpace(message)
	if message == "" || len([]rune(message)) > 128 {
		return errors.New("message must contain 1 to 128 characters")
	}
	query := url.Values{"msg": {message}}
	response, err := c.request(ctx, ip, "/notify.ps3mapi", query)
	if err != nil {
		return err
	}
	response.Body.Close()
	return nil
}

// Beep sends one webMAN system beep request. Tone IDs mirror the RTM's
// existing Jukebox: 1 is short/high, 2 is short/low, and 3 is long/low.
func (c *Client) Beep(ctx context.Context, ip string, tone int) error {
	if tone < 1 || tone > 3 {
		return errors.New("tone must be 1, 2, or 3")
	}
	response, err := c.requestWithRawQuery(ctx, ip, "/beep.ps3", strconv.Itoa(tone))
	if err != nil {
		return err
	}
	response.Body.Close()
	return nil
}

func ParseAddress(input string) (uint32, error) {
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

func parseMemoryResponse(page string, address uint32, length int) ([]byte, error) {
	wanted := length * 2
	if match := outputTextarea.FindStringSubmatch(page); len(match) == 2 {
		raw := strings.ToUpper(nonHex.ReplaceAllString(match[1], ""))
		if len(raw) >= wanted {
			return hex.DecodeString(raw[:wanted])
		}
	}
	addr := fmt.Sprintf("%08X", address)
	cleanPage := html.UnescapeString(htmlTag.ReplaceAllString(page, " "))
	marker := strings.Index(strings.ToUpper(cleanPage), addr)
	if marker >= 0 {
		rest := cleanPage[marker+len(addr):]
		fields := strings.Fields(rest)
		bytes := make([]byte, 0, length)
		for _, field := range fields {
			if len(field) != 2 {
				if len(bytes) > 0 {
					break
				}
				continue
			}
			value, err := hex.DecodeString(field)
			if err != nil {
				break
			}
			bytes = append(bytes, value[0])
			if len(bytes) == length {
				return bytes, nil
			}
		}
	}
	return nil, fmt.Errorf("could not parse %d bytes at 0x%s", length, addr)
}

func equalBytes(a, b []byte) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}
