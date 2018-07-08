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
	//   mem avail: 4998 MiB (63 %), swap free: 0 MiB (0 %)
	const memReport = "mem avail: "
	// earlyoom startup looks like this:
	//   earlyoom v1.1-5-g74a364b-dirty
	//   mem total: 7836 MiB, min: 783 MiB (10 %)
	//   swap total: 0 MiB, min: 0 MiB (10 %)
	// startupMsg matches the last line of the startup output.
	const startupMsg = "swap total: "
	testcases := []cliTestCase{
		{args: []string{"-h"}, code: 0, stderrContains: "this help text", stdoutEmpty: true},
		{args: []string{"-h"}, code: 0, stderrContains: "this help text", stdoutEmpty: true},
		{args: nil, code: -1, stderrContains: startupMsg, stdoutContains: memReport},
		{args: []string{"-p"}, code: -1, stdoutContains: memReport},
		{args: []string{"-v"}, code: 0, stderrContains: "earlyoom v", stdoutEmpty: true},
		{args: []string{"-d"}, code: -1, stdoutContains: "^ new victim (higher badness)"},
		{args: []string{"-m", "1"}, code: -1, stderrContains: "1 %", stdoutContains: memReport},
		{args: []string{"-m", "0"}, code: 15, stderrContains: "Invalid percentage", stdoutEmpty: true},
		{args: []string{"-s", "2"}, code: -1, stderrContains: "2 %", stdoutContains: memReport},
		{args: []string{"-s", "0"}, code: 16, stderrContains: "Invalid percentage", stdoutEmpty: true},
		//		{args: []string{"-M", "1024"}, code: -1, stderrContains: "min: 1 MiB", stdoutContains: memReport},
		//		{args: []string{"-S", "2048"}, code: -1, stderrContains: "min: 2 MiB", stdoutContains: memReport},
		{args: []string{"-r", "0"}, code: -1, stderrContains: startupMsg, stdoutEmpty: true},
	}

	for i, tc := range testcases {
		t.Logf("Testcase #%d: earlyoom %s", i, strings.Join(tc.args, " "))
		pass := true
		res := runEarlyoom(t, tc.args...)
		if res.code != tc.code {
			t.Errorf("wrong exit code: have=%d want=%d", res.code, tc.code)
			pass = false
		}
		if tc.stdoutEmpty && res.stdout != "" {
			t.Errorf("stdout should be empty but is not")
			pass = false
		}
		if !strings.Contains(res.stdout, tc.stdoutContains) {
			t.Errorf("stdout should contain %q, but does not", tc.stdoutContains)
			pass = false
		}
		if tc.stderrEmpty && res.stderr != "" {
			t.Errorf("stderr should be empty, but is not")
			pass = false
		}
		if !strings.Contains(res.stderr, tc.stderrContains) {
			t.Errorf("stderr should contain %q, but does not", tc.stderrContains)
			pass = false
		}
		if !pass {
			const empty = "(empty)"
			if res.stderr == "" {
				res.stderr = empty
			}
			if res.stdout == "" {
				res.stdout = empty
			}
			t.Logf("stderr:\n%s", res.stderr)
			t.Logf("stdout:\n%s", res.stdout)
		}
	}
}
