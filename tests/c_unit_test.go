package tests

import (
	"testing"
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
		{arg: "0,5", limit: 100, term: 0, kill: 5},
		// SIGTERM value is set to zero when it is below SIGKILL
		{arg: "4,5", limit: 100, term: 0, kill: 5},
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
