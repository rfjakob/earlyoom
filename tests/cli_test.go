package tests

import (
	"strings"
	"testing"
)

type cliTestCase struct {
	args           []string
	code           int
	stdoutContains string
	stdoutEmpty    bool
	stderrContains string
	stderrEmpty    bool
}

func TestCli(t *testing.T) {
	testcases := []cliTestCase{
		{args: []string{"-h"}, code: 0, stderrContains: "this help text", stdoutEmpty: true},
		{args: []string{"-h"}, code: 0, stderrContains: "this help text", stdoutEmpty: true},
	}
	for i, tc := range testcases {
		t.Logf("Running testcase #%d", i)
		res := runEarlyoom(t, tc.args...)
		if res.code != tc.code {
			t.Errorf("wrong exit code: have=%d want=%d", res.code, tc.code)
		}
		if tc.stdoutEmpty && res.stdout != "" {
			t.Errorf("stdout should be empty, but contains:\n%s", res.stdout)
		}
		if !strings.Contains(res.stdout, tc.stdoutContains) {
			t.Errorf("stdout should contain %q, but does not", tc.stdoutContains)
		}
		if tc.stderrEmpty && res.stderr != "" {
			t.Errorf("stderr should be empty, but contains:\n%s", res.stderr)
		}
		if !strings.Contains(res.stderr, tc.stderrContains) {
			t.Errorf("stdout should contain %q, but does not", tc.stderrContains)
		}
	}
}
