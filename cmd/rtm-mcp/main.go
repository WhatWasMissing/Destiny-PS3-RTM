package main

import (
	"fmt"
	"os"

	"destiny-rtm/internal/rtmclient"
	"destiny-rtm/internal/rtmmcp"
)

func main() {
	server := rtmmcp.New(rtmclient.New())
	if err := server.Serve(os.Stdin, os.Stdout); err != nil {
		fmt.Fprintln(os.Stderr, "destiny-rtm-mcp:", err)
		os.Exit(1)
	}
}
