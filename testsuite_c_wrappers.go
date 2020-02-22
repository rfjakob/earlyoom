package earlyoom_testsuite

import (
	"fmt"
)

// #cgo CFLAGS: -std=gnu99 -DCGO
// #include "meminfo.h"
// #include "kill.h"
// #include "msg.h"
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

func fix_truncated_utf8(str string) string {
	cstr := C.CString(str)
	C.fix_truncated_utf8(cstr)
	return C.GoString(cstr)
}

// getRss returns the RSS of process `pid` in kiB.
func getRss(pid int) uint64 {
	st := get_process_stats(pid)
	return uint64(st.VmRSSkiB)
}

func get_process_stats(pid int) C.struct_procinfo {
	return C.get_process_stats(C.int(pid))
}

func parse_meminfo() C.meminfo_t {
	return C.parse_meminfo()
}

func kill_largest_process() {
	var args C.poll_loop_args_t
	C.kill_largest_process(args, 0)
}
