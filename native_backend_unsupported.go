//go:build native && !(linux && amd64)

package main

import "context"

func nativeBackendAvailable() bool {
	return false
}

func playVideoNative(context.Context, string, int) string {
	return "finished"
}
