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
	// The periodic memory report looks like this:
	// mem avail: 4998 MiB (63 %), swap free: 0 MiB (0 %)
	const memReport = "mem avail: "

	testcases := []cliTestCase{
		{args: []string{"-h"}, code: 0, stderrContains: "this help text", stdoutEmpty: true},
		{args: []string{"-h"}, code: 0, stderrContains: "this help text", stdoutEmpty: true},
		{args: nil, code: -1, stderrContains: "swap total", stdoutContains: memReport},
		{args: []string{"-p"}, code: -1, stdoutContains: "mem avail: "},
		{args: []string{"-v"}, code: 0, stderrContains: "earlyoom v", stdoutEmpty: true},
		{args: []string{"-d"}, code: -1, stdoutContains: "^ new victim (higher badness)"},
		{args: []string{"-m", "1"}, code: -1, stderrContains: "(1 %)", stdoutContains: memReport},
		{args: []string{"-m", "0"}, code: 15, stderrContains: "Invalid percentage", stdoutEmpty: true},
		{args: []string{"-s", "2"}, code: -1, stderrContains: "(2 %)", stdoutContains: memReport},
		{args: []string{"-s", "0"}, code: 16, stderrContains: "Invalid percentage", stdoutEmpty: true},
		{args: []string{"-M", "1024"}, code: -1, stderrContains: "min: 1 MiB", stdoutContains: memReport},
		{args: []string{"-S", "2048"}, code: -1, stderrContains: "min: 2 MiB", stdoutContains: memReport},
	}

	for i, tc := range testcases {
		res := runEarlyoom(t, tc.args...)
		t.Logf("Testcase #%d: exit code = %d", i, res.code)
		if res.code != tc.code {
			t.Errorf("wrong exit code: have=%d want=%d", res.code, tc.code)
		}
		if tc.stdoutEmpty && res.stdout != "" {
			t.Errorf("stdout should be empty, but contains:\n%s", res.stdout)
		}
		if !strings.Contains(res.stdout, tc.stdoutContains) {
			t.Errorf("stdout should contain %q, but does not:\n%s", tc.stdoutContains, res.stdout)
		}
		if tc.stderrEmpty && res.stderr != "" {
			t.Errorf("stderr should be empty, but contains:\n%s", res.stderr)
		}
		if !strings.Contains(res.stderr, tc.stderrContains) {
			t.Errorf("stderr should contain %q, but does not:\n%s", tc.stderrContains, res.stderr)
		}
	}
}
