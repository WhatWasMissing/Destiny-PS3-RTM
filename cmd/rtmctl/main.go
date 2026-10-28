package main

import (
	"context"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"strings"
	"time"

	"destiny-rtm/internal/rtmclient"
)

func main() {
	if err := run(os.Args[1:], os.Stdout, os.Stderr); err != nil {
		if jsonRequested(os.Args[1:]) {
			_ = json.NewEncoder(os.Stdout).Encode(map[string]any{"ok": false, "error": err.Error()})
		} else {
			fmt.Fprintln(os.Stderr, "rtmctl:", err)
		}
		os.Exit(1)
	}
}

func run(args []string, stdout, stderr io.Writer) error {
	if len(args) == 0 || args[0] == "help" || args[0] == "--help" || args[0] == "-h" {
		printUsage(stdout)
		return nil
	}
	command := args[0]
	flags := flag.NewFlagSet(command, flag.ContinueOnError)
	flags.SetOutput(stderr)
	ip := flags.String("ip", "", "Private IPv4 address of the PS3 running webMAN MOD")
	address := flags.String("address", "", "32-bit memory address in hexadecimal")
	length := flags.Int("length", 4, "Number of bytes to read (1-65536)")
	value := flags.String("hex", "", "Bytes to write as hexadecimal (1-8 bytes)")
	message := flags.String("message", "", "Text for the PS3 system notification")
	jsonOutput := flags.Bool("json", false, "Print one JSON object for scripts")
	if err := flags.Parse(args[1:]); err != nil {
		return err
	}
	if flags.NArg() != 0 {
		return errors.New("unexpected positional arguments; run rtmctl help")
	}
	if strings.TrimSpace(*ip) == "" {
		return errors.New("--ip is required")
	}
	if command == "actor-pool" {
		var unsupportedFlag string
		flags.Visit(func(option *flag.Flag) {
			if option.Name != "ip" && option.Name != "json" {
				unsupportedFlag = option.Name
			}
		})
		if unsupportedFlag != "" {
			return fmt.Errorf("actor-pool accepts only --ip and --json; --%s is not supported", unsupportedFlag)
		}
	}

	client := rtmclient.New()
	commandTimeout := 12 * time.Second
	if command == "inventory-pool" {
		commandTimeout = 2 * time.Minute
	}
	ctx, cancel := context.WithTimeout(context.Background(), commandTimeout)
	defer cancel()
	switch command {
	case "status":
		status, err := client.Status(ctx, *ip)
		if err != nil {
			return err
		}
		if *jsonOutput {
			return json.NewEncoder(stdout).Encode(map[string]any{"ok": true, "target": strings.TrimSpace(*ip), "http_status": status})
		}
		fmt.Fprintf(stdout, "webMAN endpoint reachable at http://%s/ (HTTP %d)\n", strings.TrimSpace(*ip), status)
	case "read":
		if *address == "" {
			return errors.New("--address is required")
		}
		data, err := client.ReadMemory(ctx, *ip, *address, *length)
		if err != nil {
			return err
		}
		if *jsonOutput {
			return json.NewEncoder(stdout).Encode(map[string]any{"ok": true, "address": fmt.Sprintf("0x%08X", mustAddress(*address)), "length": len(data), "hex": strings.ToUpper(hex.EncodeToString(data))})
		}
		fmt.Fprintf(stdout, "0x%08X: %s\n", mustAddress(*address), strings.ToUpper(hex.EncodeToString(data)))
	case "write":
		if *address == "" || *value == "" {
			return errors.New("--address and --hex are required")
		}
		clean := strings.TrimSpace(*value)
		clean = strings.TrimPrefix(strings.TrimPrefix(clean, "0x"), "0X")
		data, err := hex.DecodeString(clean)
		if err != nil {
			return errors.New("--hex must contain complete hexadecimal bytes")
		}
		if err := client.WriteMemory(ctx, *ip, *address, data); err != nil {
			return err
		}
		if *jsonOutput {
			return json.NewEncoder(stdout).Encode(map[string]any{"ok": true, "verified": true, "address": fmt.Sprintf("0x%08X", mustAddress(*address)), "length": len(data), "hex": strings.ToUpper(hex.EncodeToString(data))})
		}
		fmt.Fprintf(stdout, "Verified 0x%08X = %s\n", mustAddress(*address), strings.ToUpper(hex.EncodeToString(data)))
	case "notify":
		if *message == "" {
			return errors.New("--message is required")
		}
		if err := client.Notify(ctx, *ip, *message); err != nil {
			return err
		}
		if *jsonOutput {
			return json.NewEncoder(stdout).Encode(map[string]any{"ok": true, "request_dispatched": true, "delivery_confirmed": false})
		}
		fmt.Fprintln(stdout, "Notification request dispatched; delivery is not confirmed.")
	case "actor-pool":
		snapshot, err := client.ReadActorPoolDescriptor(ctx, *ip)
		if err != nil {
			return err
		}
		if *jsonOutput {
			return json.NewEncoder(stdout).Encode(map[string]any{
				"ok": true, "read_only": true, "target": strings.TrimSpace(*ip),
				"snapshot": snapshot, "loaded_enemy_list_confirmed": false, "spawnability_confirmed": false,
			})
		}
		fmt.Fprintf(stdout, "BLES 01.24 actor-pool descriptor: %s (non-atomic read-only sample)\n", snapshot.DescriptorAddress)
		fmt.Fprintf(stdout, "Storage: %s\nStride: 0x%X\nCapacity: %d\nBacking span: 0x%X bytes\nInitialized: %t\nRecords read: %d\n%s\n",
			snapshot.StoragePointer, snapshot.Stride, snapshot.Capacity, snapshot.BackingBytes, snapshot.Initialized, snapshot.RecordsRead, snapshot.Note)
	case "inventory-pool":
		snapshot, err := client.ReadLiveItemPool(ctx, *ip)
		if err != nil {
			return err
		}
		if *jsonOutput {
			return json.NewEncoder(stdout).Encode(map[string]any{"ok": true, "target": strings.TrimSpace(*ip), "snapshot": snapshot})
		}
		fmt.Fprintf(stdout, "BLES 01.24 item pool: %d slots, %d populated (non-atomic read-only snapshot)\n", snapshot.SlotCount, snapshot.OccupiedCount)
		fmt.Fprintln(stdout, "Slot\tItem index")
		for _, item := range snapshot.Items {
			fmt.Fprintf(stdout, "%d\t%d\n", item.Slot, item.ItemIndex)
		}
	default:
		return fmt.Errorf("unknown command %q; run rtmctl help", command)
	}
	return nil
}

func jsonRequested(args []string) bool {
	for _, arg := range args {
		if arg == "--json" || arg == "--json=true" {
			return true
		}
		if strings.HasPrefix(arg, "--json=") {
			return false
		}
	}
	return false
}

func mustAddress(address string) uint32 {
	value, _ := rtmclient.ParseAddress(address)
	return value
}

func printUsage(w io.Writer) {
	fmt.Fprintln(w, `Destiny RTM command-line client

Usage:
  rtmctl status --ip 192.168.0.62
  rtmctl read --ip 192.168.0.62 --address 0030D230 --length 4
  rtmctl inventory-pool --ip 192.168.0.62
  rtmctl actor-pool --ip 192.168.0.62 --json
  rtmctl write --ip 192.168.0.62 --address 0030D230 --hex 3DCCCCCD
  rtmctl notify --ip 192.168.0.62 --message "Hello from RTM"
  rtmctl read --ip 192.168.0.62 --address 0030D230 --length 4 --json

Writes are limited to 1-8 bytes and only report success after matching memory readback.
The client targets private IPv4 addresses on port 80 and uses Destiny PID 0x1010200.
actor-pool verifies the BLES profile and actor-path code, reads only the 20-byte
descriptor twice, and never follows its storage pointer or enumerates actors.
inventory-pool is read-only, verifies the BLES 01.24 profile plus item-pool code and
descriptor signatures, and never returns instance or owner IDs. It does not call
undocumented PS3 function-call endpoints.`)
}
