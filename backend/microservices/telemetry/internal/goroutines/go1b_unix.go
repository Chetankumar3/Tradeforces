//go:build !windows

package goroutines

import "syscall"

func writeRaw(fd uintptr, buf []byte) (int, error) {
	return syscall.Write(int(fd), buf)
}
