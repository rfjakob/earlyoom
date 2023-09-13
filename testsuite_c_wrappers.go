package earlyoom_testsuite

import (
	"fmt"
	"strings"
)

// #cgo CFLAGS: -std=gnu99 -DCGO
// #include "meminfo.h"
// #include "kill.h"
// #include "msg.h"
// #include "globals.h"
import "C"

func parse_term_kill_tuple(optarg string, upper_limit int) (error, float64, float64) {
	cs := C.CString(optarg)
	tuple := C.parse_term_kill_tuple(cs, C.longlong(upper_limit))
	errmsg := C.GoString(&(tuple.err[0]))
	if len(errmsg) > 0 {
		return fmt.Errorf(errmsg), 0, 0
	}
	return nil, float64(tuple.term), float64(tuple.kill)
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

func find_largest_process() {
	var args C.poll_loop_args_t
	C.find_largest_process(&args)
}

func kill_process() {
	var args C.poll_loop_args_t
	var victim C.procinfo_t
	victim.pid = 1
	C.kill_process(&args, 0, &victim)
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

func get_comm(pid int) (int, string) {
	cstr := C.CString(strings.Repeat("\000", 256))
	res := C.get_comm(C.int(pid), cstr, 256)
	return int(res), C.GoString(cstr)
}

func get_cmdline(pid int) (int, string) {
	cstr := C.CString(strings.Repeat("\000", 256))
	res := C.get_cmdline(C.int(pid), cstr, 256)
	return int(res), C.GoString(cstr)
}

func procdir_path(str string) {
	cstr := C.CString(str)
	C.procdir_path = cstr
}
