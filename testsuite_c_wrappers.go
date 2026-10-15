package earlyoom_testsuite

import (
	"fmt"
	"log"
	"strings"
	"unsafe"
)

// #cgo CFLAGS: -std=gnu99 -DCGO
// #include <regex.h>
// #include <stdlib.h>
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

// The struct type under a name that _test.go code can use for
// function parameters.
type pollLoopArgs = C.poll_loop_args_t

// Same as poll_loop_args_t, plus an --avoid regex.
func poll_loop_args_t_with_avoid(sort_by_rss bool, avoid string) (args C.poll_loop_args_t) {
	args.sort_by_rss = C.bool(sort_by_rss)
	args.avoid_regex = (*C.regex_t)(C.malloc(C.sizeof_regex_t))
	cs := C.CString(avoid)
	defer C.free(unsafe.Pointer(cs))
	if C.regcomp(args.avoid_regex, cs, C.REG_EXTENDED|C.REG_NOSUB) != 0 {
		log.Panicf("could not compile regex %q", avoid)
	}
	return
}

// Wrapper with use_kernel_oom_killer and dryrun support
func poll_loop_args_t_with_kernel_oom(sort_by_rss bool, kernel_oom bool, dryrun bool) (args C.poll_loop_args_t) {
	args.sort_by_rss = C.bool(sort_by_rss)
	args.kernel_oom = C.bool(kernel_oom)
	args.dryrun = C.bool(dryrun)
	return
}

func procinfo_t() C.procinfo_t {
	return C.procinfo_t{}
}

func is_larger(args *C.poll_loop_args_t, victim mockProcProcess, cur mockProcProcess) bool {
	// In find_largest_process() the victim is a process that went through
	// is_larger() as "cur" before, which is where --prefer and --avoid
	// adjust VmRSSkiB. Give the victim the same treatment here.
	cVictim := victim.toProcinfo_t()
	cNone := C.procinfo_t{}
	m := C.meminfo_t{
		MemTotalKiB: 10485760, // assume 10 GiB RAM
	}
	C.is_larger(args, &m, &cNone, &cVictim)
	cCur := cur.toProcinfo_t()
	return bool(C.is_larger(args, &m, &cVictim, &cCur))
}

func find_largest_process() {
	var args C.poll_loop_args_t
	var m C.meminfo_t
	C.find_largest_process(&args, &m)
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

// get_cmdline passes a buffer with a canary byte in front of it to
// get_cmdline() and panics if the canary was overwritten
// (https://github.com/rfjakob/earlyoom/issues/386).
func get_cmdline(pid int) (int, string) {
	const outlen = 256
	const canary = C.char(0x7f)
	buf := (*[outlen + 1]C.char)(C.calloc(outlen+1, 1))
	defer C.free(unsafe.Pointer(buf))
	buf[0] = canary
	res := C.get_cmdline(C.int(pid), &buf[1], outlen)
	if buf[0] != canary {
		log.Panicf("get_cmdline wrote before the buffer")
	}
	return int(res), C.GoString(&buf[1])
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

func trigger_kernel_oom_killer(args C.poll_loop_args_t) int {
	return int(C.trigger_kernel_oom(&args))
}
