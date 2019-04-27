package tests

import (
	"fmt"
)

// #cgo CFLAGS: -std=gnu99
// #include "../globals.c"
// #include "../sanitize.c"
// #include "../msg.c"
// #include "../meminfo.c"
import "C"

func sanitize(s string) string {
	cs := C.CString(s)
	C.sanitize(cs)
	return C.GoString(cs)
}

func parse_term_kill_tuple(optarg string, upper_limit int) (error, int, int) {
	cs := C.CString(optarg)
	tuple := C.parse_term_kill_tuple(cs, C.long(upper_limit))
	errmsg := C.GoString(&(tuple.err[0]))
	if len(errmsg) > 0 {
		return fmt.Errorf(errmsg), 0, 0
	}
	return nil, int(tuple.term), int(tuple.kill)
}

func is_alive(pid int) bool {
	res := C.is_alive(C.int(pid))
	return bool(res)
}
