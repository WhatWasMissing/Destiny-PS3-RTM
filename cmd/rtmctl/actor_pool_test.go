package main

import (
	"bytes"
	"strings"
	"testing"
)

func TestActorPoolCommandRefusesCustomMemoryFlagsBeforeConsoleAccess(t *testing.T) {
	for _, option := range [][]string{
		{"--address", "10000000"},
		{"--length", "4"},
		{"--hex", "00000000"},
		{"--message", "unused"},
	} {
		args := append([]string{"actor-pool", "--ip", "127.0.0.1", "--json"}, option...)
		var stdout, stderr bytes.Buffer
		err := run(args, &stdout, &stderr)
		if err == nil || !strings.Contains(err.Error(), "actor-pool accepts only --ip and --json") || stdout.Len() != 0 {
			t.Fatalf("unsupported flag %s reached the command: error=%v output=%s", option[0], err, stdout.String())
		}
	}
}
