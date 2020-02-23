package earlyoom_testsuite

import (
	"fmt"
	"strings"
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

func parse_meminfo() C.meminfo_t {
	return C.parse_meminfo()
}

func kill_largest_process() {
	var args C.poll_loop_args_t
	C.kill_largest_process(args, 0)
}

func get_oom_score(pid int) int {
	return int(C.get_oom_score(C.int(pid)))
}

func get_oom_score_adj(pid int, out *int) int {
	var out2 C.int
	res := C.get_oom_score_adj(C.int(pid), &out2)
	*out = int(out2)
	return int(res)
}

func get_vm_rss_kib(pid int) int {
	return int(C.get_vm_rss_kib(C.int(pid)))
}

func get_comm(pid int) string {
	cstr := C.CString(strings.Repeat("\000", 256))
	C.get_comm(C.int(pid), cstr, 256)
	return C.GoString(cstr)
}
