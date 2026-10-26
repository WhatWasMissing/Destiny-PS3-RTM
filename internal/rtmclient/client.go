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
	"time"
)

const DefaultProcessID = "0x1010200"

const PlayerPointerAddress uint32 = 0x0190EBB0
const PlayerPositionOffset uint32 = 0x140

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

func (c *Client) endpoint(ipText, path string, query url.Values) (string, error) {
	ip, err := validateIPv4(ipText)
	if err != nil {
		return "", err
	}
	host := ip.String()
	if c.endpointOverride != "" {
		host = c.endpointOverride
	}
	u := url.URL{Scheme: "http", Host: host, Path: path, RawQuery: query.Encode()}
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
	if c == nil {
		return nil, errors.New("client is nil")
	}
	endpoint, err := c.endpoint(ipText, path, query)
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
