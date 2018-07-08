package tests

// #cgo CFLAGS: -std=gnu99
// #include "../sanitize.c"
import "C"

func sanitize(s string) string {
	cs := C.CString(s)
	C.sanitize(cs)
	return C.GoString(cs)
}
