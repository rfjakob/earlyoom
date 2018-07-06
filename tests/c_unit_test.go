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
