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
// #include "proc_pid.h"
import "C"

func init() {
	C.enable_debug = 1
}

func enable_debug(state bool) (oldState bool) {
	if C.enable_debug == 1 {
		oldState = true
	}
	if state {
		C.enable_debug = 1
	} else {
		C.enable_debug = 0
	}
	return
}

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

// Wrapper so _test.go code can create a poll_loop_args_t
// struct. _test.go code cannot use C.
func poll_loop_args_t(sort_by_rss bool) (args C.poll_loop_args_t) {
	args.sort_by_rss = C.bool(sort_by_rss)
	return
}

func procinfo_t() C.procinfo_t {
	return C.procinfo_t{}
}

func is_larger(args *C.poll_loop_args_t, victim mockProcProcess, cur mockProcProcess) bool {
	cVictim := victim.toProcinfo_t()
	cCur := cur.toProcinfo_t()
	return bool(C.is_larger(args, &cVictim, &cCur))
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

func get_cgroup(pid int) (int, string) {
	cstr := C.CString(strings.Repeat("\000", 256))
	res := C.get_cgroup(C.int(pid), cstr, 256)
	return int(res), C.GoString(cstr)
}

func procdir_path(str string) string {
	if str != "" {
		cstr := C.CString(str)
		C.procdir_path = cstr
	}
	return C.GoString(C.procdir_path)
}

func parse_proc_pid_stat_buf(buf string) (res bool, out C.pid_stat_t) {
	cbuf := C.CString(buf)
	res = bool(C.parse_proc_pid_stat_buf(&out, cbuf))
	return res, out
}

func parse_proc_pid_stat(pid int) (res bool, out C.pid_stat_t) {
	res = bool(C.parse_proc_pid_stat(&out, C.int(pid)))
	return res, out
}
