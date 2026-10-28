//go:build windows

package main

import (
	"syscall"
	"unsafe"
)

var messageBox = syscall.NewLazyDLL("user32.dll").NewProc("MessageBoxW")

func showStartupError(title, message string) {
	titleText, err := syscall.UTF16PtrFromString(title)
	if err != nil {
		return
	}
	messageText, err := syscall.UTF16PtrFromString(message)
	if err != nil {
		return
	}
	const mbIconError = 0x00000010
	const mbSetForeground = 0x00010000
	const mbTopmost = 0x00040000
	_, _, _ = messageBox.Call(0, uintptr(unsafe.Pointer(messageText)), uintptr(unsafe.Pointer(titleText)), mbIconError|mbSetForeground|mbTopmost)
}
