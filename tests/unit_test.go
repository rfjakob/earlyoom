package tests

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

func Test_get_process_stats(t *testing.T) {
	pid := os.Getpid()
	st := get_process_stats(pid)
	if int(st.exited) == 1 {
		t.Fatal("we have obviously not exited")
	}
	if int(st.VmRSSkiB) == 0 {
		t.Fatal("our rss can't be zero")
	}
	t.Logf("result for pid %d: %#v", pid, st)
}
