//go:build !native

package main

import "context"

func nativeBackendAvailable() bool {
	return false
}

func playVideoNative(context.Context, string, int) string {
	return "finished"
}
