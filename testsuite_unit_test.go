package earlyoom_testsuite

import (
	"os"
	"testing"
	"unicode/utf8"
)

func TestSanitize(t *testing.T) {
	type testCase struct {
		in  string
		out string
	}
	tcs := []testCase{
		{in: "", out: ""},
		{in: "foo", out: "foo"},
		{in: "foo bar", out: "foo_bar"},
		{in: "foo\\", out: "foo_"},
		{in: "foo234", out: "foo234"},
		{in: "foo$", out: "foo_"},
		{in: "foo\"bar", out: "foo_bar"},
		{in: "foo\x00bar", out: "foo"},
		{in: "foo!§$%&/()=?`'bar", out: "foo_____________bar"},
	}
	for _, tc := range tcs {
		out := sanitize(tc.in)
		if out != tc.out {
			t.Errorf("wrong result: in=%q want=%q have=%q ", tc.in, tc.out, out)
		}
	}
}

func TestParseTuple(t *testing.T) {
	tcs := []struct {
		arg        string
		limit      int
		shouldFail bool
		term       int
		kill       int
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
		// SIGTERM value is set to zero when it is below SIGKILL
		{arg: "4,5", limit: 100, term: 5, kill: 5},
		{arg: "0", limit: 100, shouldFail: true},
		{arg: "0,0", limit: 100, shouldFail: true},
	}
	for _, tc := range tcs {
		err, term, kill := parse_term_kill_tuple(tc.arg, tc.limit)
		hasFailed := (err != nil)
		if tc.shouldFail != hasFailed {
			t.Errorf("case %v: hasFailed=%v", tc, hasFailed)
			continue
		}
		if term != tc.term {
			t.Errorf("case %v: term=%d", tc, term)
		}
		if kill != tc.kill {
			t.Errorf("case %v: kill=%d", tc, kill)
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
}

func Test_get_oom_score(t *testing.T) {
	res := get_oom_score(os.Getpid())
	// On systems with a lot of RAM, our process may actually have a score of
	// zero. At least check that get_oom_score did not return an error.
	if res < 0 {
		t.Error(res)
	}
	// On Fedora 31 (Linux 5.4), /proc/sys/kernel/pid_max = 4194304.
	const INT32_MAX = 2147483647
	res = get_oom_score(INT32_MAX)
	if res != -1 {
		t.Errorf("want -1, but have %d", res)
	}
}

func Test_get_comm(t *testing.T) {
	pid := os.Getpid()
	comm := get_comm(pid)
	if len(comm) == 0 {
		t.Fatalf("empty process name %q", comm)
	}
	t.Logf("process name %q", comm)
}

func Benchmark_parse_meminfo(b *testing.B) {
	for n := 0; n < b.N; n++ {
		parse_meminfo()
	}
}

func Benchmark_kill_largest_process(b *testing.B) {
	for n := 0; n < b.N; n++ {
		kill_largest_process()
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
		comm := get_comm(pid)
		if len(comm) == 0 {
			b.Fatalf("empty process name %q", comm)
		}
	}
}
