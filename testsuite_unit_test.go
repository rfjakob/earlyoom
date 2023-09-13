package earlyoom_testsuite

import (
	"io/ioutil"
	"os"
	"strings"
	"syscall"
	"testing"
	"unicode/utf8"
)

// On Fedora 31 (Linux 5.4), /proc/sys/kernel/pid_max = 4194304.
// It's very unlikely that INT32_MAX will be a valid pid anytime soon.
const INT32_MAX = 2147483647
const ENOENT = 2

func TestParseTuple(t *testing.T) {
	tcs := []struct {
		arg        string
		limit      int
		shouldFail bool
		term       float64
		kill       float64
	}{
		{arg: "2,1", limit: 100, term: 2, kill: 1},
		{arg: "20,10", limit: 100, term: 20, kill: 10},
		{arg: "30", limit: 100, term: 30, kill: 15},
		{arg: "30", limit: 20, shouldFail: true},
		// https://github.com/rfjakob/earlyoom/issues/97
		{arg: "22[,20]", limit: 100, shouldFail: true},
		{arg: "220[,160]", limit: 300, shouldFail: true},
		{arg: "180[,170]", limit: 300, shouldFail: true},
		{arg: "5,0", limit: 100, term: 5, kill: 0},
		{arg: "5,9", limit: 100, term: 9, kill: 9},
		{arg: "0,5", limit: 100, term: 5, kill: 5},
		// TERM value is set to KILL value when it is below TERM
		{arg: "4,5", limit: 100, term: 5, kill: 5},
		{arg: "0", limit: 100, shouldFail: true},
		{arg: "0,0", limit: 100, shouldFail: true},
		// Floating point values
		{arg: "4.0,2.0", limit: 100, term: 4, kill: 2},
		{arg: "4,0,2,0", limit: 100, shouldFail: true},
		{arg: "3.1415,2.7182", limit: 100, term: 3.1415, kill: 2.7182},
		{arg: "3.1415", limit: 100, term: 3.1415, kill: 3.1415 / 2},
		{arg: "1." + strings.Repeat("123", 100), limit: 100, shouldFail: true},
		// Leading garbage
		{arg: "x1,x2", limit: 100, shouldFail: true},
		{arg: "1,x2", limit: 100, shouldFail: true},
		// Trailing garbage
		{arg: "1x,2x", limit: 100, shouldFail: true},
		{arg: "1.1.", limit: 100, shouldFail: true},
		{arg: "1,2..", limit: 100, shouldFail: true},
	}
	for _, tc := range tcs {
		err, term, kill := parse_term_kill_tuple(tc.arg, tc.limit)
		hasFailed := (err != nil)
		if tc.shouldFail != hasFailed {
			t.Errorf("case %v: hasFailed=%v", tc, hasFailed)
			continue
		}
		if term != tc.term {
			t.Errorf("case %v: term=%v", tc, term)
		}
		if kill != tc.kill {
			t.Errorf("case %v: kill=%v", tc, kill)
		}
	}
}

func TestIsAlive(t *testing.T) {
	tcs := []struct {
		pid int
		res bool
	}{
		{os.Getpid(), true},
		{1, true},
		{999999, false},
		{0, false},
	}
	for _, tc := range tcs {
		if res := is_alive(tc.pid); res != tc.res {
			t.Errorf("pid %d: expected %v, got %v", tc.pid, tc.res, res)
		}
	}
}

func TestIsAliveMock(t *testing.T) {
	mockProcdir, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	procdir_path(mockProcdir)
	defer procdir_path("/proc")

	if err := os.Mkdir(mockProcdir+"/100", 0700); err != nil {
		t.Fatal(err)
	}

	testCases := []struct {
		content string
		res     bool
	}{
		{"144815 (bash) S 17620 144815 144815 34817 247882 4194304 20170 1855121 1 3321 28 46 3646 3366 20 0 1 0 10798280 237576192 1065 18446744073709551615 94174652813312 94174653706789 140724247111872 0 0 0 65536 3686404 1266761467 0 0 0 17 0 0 0 9 0 0 94174653946928 94174653994640 94174663303168 140724247119367 140724247119377 140724247119377 140724247121902 0",
			true}, // full string from actual system
		{"123 (bash) R 123 123 123", true}, // truncated for brevity
		{"123 (bash) Z 123 123 123", false},
		// hostile process names that try to fake "I am dead"
		{"123 (foo) Z ) R 123 123 123", true},
		{"123 (foo) Z) R 123 123 123", true},
		{"123 (foo)Z ) R 123 123 123", true},
		{"123 (foo)\nZ\n) R 123 123 123", true},
		{"123 (foo)\tZ\t) R 123 123 123", true},
		{"123 (foo)  Z   ) R 123 123 123", true},
	}

	for _, tc := range testCases {
		statFile := mockProcdir + "/100/stat"
		if err := ioutil.WriteFile(statFile, []byte(tc.content), 0600); err != nil {
			t.Fatal(err)
		}
		if is_alive(100) != tc.res {
			t.Errorf("have=%v, want=%v for /proc/100/stat=%q", is_alive(100), tc.res, tc.content)
		}
	}
}

func Test_fix_truncated_utf8(t *testing.T) {
	// From https://gist.github.com/w-vi/67fe49106c62421992a2
	str := "___😀∮ E⋅da = Q,  n → ∞, 𐍈∑ f(i) = ∏ g(i)"
	// a range loop will split at runes - we *want* broken utf8 so use raw
	// counter.
	for i := 3; i < len(str); i++ {
		truncated := str[:i]
		fixed := fix_truncated_utf8(truncated)
		if len(fixed) < 3 {
			t.Fatalf("truncated: %q", fixed)
		}
		if !utf8.Valid([]byte(fixed)) {
			t.Errorf("Invalid utf8: %q", fixed)
		}
	}
}

func Test_get_vm_rss_kib(t *testing.T) {
	pid := os.Getpid()
	rss := get_vm_rss_kib(pid)
	if rss <= 0 {
		t.Fatalf("our rss can't be <= 0, but we got %d", rss)
	}
	// Error case
	res := get_vm_rss_kib(INT32_MAX)
	if res != -ENOENT {
		t.Fail()
	}
}

func Test_get_oom_score(t *testing.T) {
	res := get_oom_score(os.Getpid())
	// On systems with a lot of RAM, our process may actually have a score of
	// zero. At least check that get_oom_score did not return an error.
	if res < 0 {
		t.Error(res)
	}
	res = get_oom_score(INT32_MAX)
	if res != -ENOENT {
		t.Errorf("want %d, but have %d", syscall.ENOENT, res)
	}
}

func Test_get_comm(t *testing.T) {
	pid := os.Getpid()
	res, comm := get_comm(pid)
	if res != 0 {
		t.Fatalf("error %d", res)
	}
	if len(comm) == 0 {
		t.Fatalf("empty process name %q", comm)
	}
	t.Logf("process name %q", comm)
	// Error case
	res, comm = get_comm(INT32_MAX)
	if res != -ENOENT {
		t.Fail()
	}
	if comm != "" {
		t.Fail()
	}
}

func Test_get_cmdline(t *testing.T) {
	pid := os.Getpid()
	res, comm := get_cmdline(pid)
	if res != 0 {
		t.Fatalf("error %d", res)
	}
	if len(comm) == 0 {
		t.Fatalf("empty process cmdline %q", comm)
	}
	t.Logf("process name %q", comm)
	// Error case
	res, comm = get_cmdline(INT32_MAX)
	if res != -ENOENT {
		t.Fail()
	}
	if comm != "" {
		t.Fail()
	}
}

func Benchmark_parse_meminfo(b *testing.B) {
	for n := 0; n < b.N; n++ {
		parse_meminfo()
	}
}

func Benchmark_kill_process(b *testing.B) {
	for n := 0; n < b.N; n++ {
		kill_process()
	}
}

func Benchmark_find_largest_process(b *testing.B) {
	for n := 0; n < b.N; n++ {
		find_largest_process()
	}
}

func Benchmark_get_oom_score(b *testing.B) {
	pid := os.Getpid()
	for n := 0; n < b.N; n++ {
		get_oom_score(pid)
	}
}

func Benchmark_get_oom_score_adj(b *testing.B) {
	pid := os.Getpid()
	for n := 0; n < b.N; n++ {
		var out int
		get_oom_score_adj(pid, &out)
	}
}

func Benchmark_get_vm_rss_kib(b *testing.B) {
	pid := os.Getpid()
	for n := 0; n < b.N; n++ {
		rss := get_vm_rss_kib(pid)
		if rss <= 0 {
			b.Fatalf("rss <= 0: %d", rss)
		}
	}
}

func Benchmark_get_comm(b *testing.B) {
	pid := os.Getpid()
	for n := 0; n < b.N; n++ {
		res, comm := get_comm(pid)
		if len(comm) == 0 {
			b.Fatalf("empty process name %q", comm)
		}
		if res != 0 {
			b.Fatalf("error %d", res)
		}
	}
}

func Benchmark_get_cmdline(b *testing.B) {
	pid := os.Getpid()
	for n := 0; n < b.N; n++ {
		res, comm := get_cmdline(pid)
		if len(comm) == 0 {
			b.Fatalf("empty process cmdline %q", comm)
		}
		if res != 0 {
			b.Fatalf("error %d", res)
		}
	}
}
