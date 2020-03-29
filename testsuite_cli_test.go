package earlyoom_testsuite

import (
	"fmt"
	"io/ioutil"
	"math"
	"os/exec"
	"regexp"
	"strconv"
	"strings"
	"testing"
)

type cliTestCase struct {
	// arguments to pass to earlyoom
	args []string
	// expected exit code
	code int
	// stdout must contain
	stdoutContains string
	// stdout must be empty?
	stdoutEmpty bool
	// stderr must contain
	stderrContains string
	// stderr must be empty?
	stderrEmpty bool
}

func paseMeminfoLine(l string) int64 {
	fields := strings.Split(l, " ")
	asciiVal := fields[len(fields)-2]
	val, err := strconv.ParseInt(asciiVal, 10, 64)
	if err != nil {
		panic(err)
	}
	return val
}

func parseMeminfo() (memTotal int64, swapTotal int64) {
	/*
		/proc/meminfo looks like this:

		MemTotal:        8024108 kB
		[...]
		SwapTotal:        102396 kB
		[...]
	*/
	content, err := ioutil.ReadFile("/proc/meminfo")
	if err != nil {
		panic(err)
	}
	lines := strings.Split(string(content), "\n")
	for _, l := range lines {
		if strings.HasPrefix(l, "MemTotal:") {
			memTotal = paseMeminfoLine(l)
		}
		if strings.HasPrefix(l, "SwapTotal:") {
			swapTotal = paseMeminfoLine(l)
		}
	}
	return
}

// earlyoom RSS should never be above 1 MiB
const rssMax = 1024

func TestCli(t *testing.T) {
	memTotal, swapTotal := parseMeminfo()
	mem1percent := fmt.Sprintf("%d", memTotal*1/100)
	swap2percent := fmt.Sprintf("%d", swapTotal*2/100)
	tooBigInt32 := fmt.Sprintf("%d", uint64(math.MaxInt32+1))
	tooBigUint32 := fmt.Sprintf("%d", uint64(math.MaxUint32+1))
	// earlyoom startup looks like this:
	//   earlyoom v1.1-5-g74a364b-dirty
	//   mem total: 7836 MiB, min: 783 MiB (10 %)
	//   swap total: 0 MiB, min: 0 MiB (10 %)
	// startupMsg matches the last line of the startup output.
	const startupMsg = "swap total: "
	testcases := []cliTestCase{
		// Both -h and --help should show the help text
		{args: []string{"-h"}, code: 0, stderrContains: "this help text", stdoutEmpty: true},
		{args: []string{"--help"}, code: 0, stderrContains: "this help text", stdoutEmpty: true},
		{args: nil, code: -1, stderrContains: startupMsg, stdoutContains: memReport},
		{args: []string{"-p"}, code: -1, stdoutContains: memReport},
		{args: []string{"-v"}, code: 0, stderrContains: "earlyoom v", stdoutEmpty: true},
		{args: []string{"-d"}, code: -1, stdoutContains: "new victim"},
		{args: []string{"-m", "1"}, code: -1, stderrContains: " 1.00%", stdoutContains: memReport},
		{args: []string{"-m", "0"}, code: 15, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-m", "-10"}, code: 15, stderrContains: "fatal", stdoutEmpty: true},
		// Using "-m 100" makes no sense
		{args: []string{"-m", "100"}, code: 15, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-s", "2"}, code: -1, stderrContains: " 2.00%", stdoutContains: memReport},
		// Using "-s 100" is a valid way to ignore swap usage
		{args: []string{"-s", "100"}, code: -1, stderrContains: " 100.00%", stdoutContains: memReport},
		{args: []string{"-s", "101"}, code: 16, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-s", "0"}, code: 16, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-s", "-10"}, code: 16, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-M", mem1percent}, code: -1, stderrContains: " 1.00%", stdoutContains: memReport},
		{args: []string{"-M", "9999999999999999"}, code: 15, stderrContains: "fatal", stdoutEmpty: true},
		// We use {"-r=0"} instead of {"-r", "0"} so runEarlyoom() can detect that there will be no output
		{args: []string{"-r=0"}, code: -1, stderrContains: startupMsg, stdoutEmpty: true},
		{args: []string{"-r", "0.1"}, code: -1, stderrContains: startupMsg, stdoutContains: memReport},
		// Test --avoid and --prefer
		{args: []string{"--avoid", "MyProcess1"}, code: -1, stderrContains: "Will avoid killing", stdoutContains: memReport},
		{args: []string{"--prefer", "MyProcess2"}, code: -1, stderrContains: "Preferring to kill", stdoutContains: memReport},
		{args: []string{"-i"}, code: -1, stderrContains: "Ignoring positive oom_score_adj values"},
		// Extra arguments should error out
		{args: []string{"xyz"}, code: 13, stderrContains: "extra argument not understood", stdoutEmpty: true},
		{args: []string{"-i", "1"}, code: 13, stderrContains: "extra argument not understood", stdoutEmpty: true},
		// Tuples
		{args: []string{"-m", "2,1"}, code: -1, stderrContains: "sending SIGTERM when mem <=  2.00% and swap <= 10.00%", stdoutContains: memReport},
		{args: []string{"-m", "1,2"}, code: -1, stdoutContains: memReport},
		{args: []string{"-m", "1,-1"}, code: 15, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-m", "1000,-1000"}, code: 15, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-s", "2,1"}, code: -1, stderrContains: "sending SIGTERM when mem <= 10.00% and swap <=  2.00%", stdoutContains: memReport},
		{args: []string{"-s", "1,2"}, code: -1, stdoutContains: memReport},
		// https://github.com/rfjakob/earlyoom/issues/97
		{args: []string{"-m", "5,0"}, code: -1, stdoutContains: memReport},
		{args: []string{"-m", "5,9"}, code: -1, stdoutContains: memReport},
		// check for integer overflows
		{args: []string{"-M", "-1"}, code: 15, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-M", tooBigInt32}, code: 15, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-M", tooBigUint32}, code: 15, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-m", "-1"}, code: 15, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-m", tooBigInt32}, code: 15, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-m", tooBigUint32}, code: 15, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-S", "-1"}, code: 16, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-S", tooBigInt32}, code: 16, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-S", tooBigUint32}, code: 16, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-s", "-1"}, code: 16, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-s", tooBigInt32}, code: 16, stderrContains: "fatal", stdoutEmpty: true},
		{args: []string{"-s", tooBigUint32}, code: 16, stderrContains: "fatal", stdoutEmpty: true},
		// Floating point values
		{args: []string{"-m", "3.14"}, code: -1, stderrContains: "SIGTERM when mem <=  3.14%", stdoutContains: memReport},
		{args: []string{"-m", "7,3.14"}, code: -1, stderrContains: "SIGKILL when mem <=  3.14%", stdoutContains: memReport},
		{args: []string{"-s", "12.34"}, code: -1, stderrContains: "swap <= 12.34%", stdoutContains: memReport},
		// Use both -m/-M
		{args: []string{"-m", "10", "-M", mem1percent}, code: -1, stderrContains: "SIGTERM when mem <=  1.00%", stdoutContains: memReport},
	}
	if swapTotal > 0 {
		// Tests that cannot work when there is no swap enabled
		tc := []cliTestCase{
			{args: []string{"-S", swap2percent}, code: -1, stderrContains: " 2.00%", stdoutContains: memReport},
			// Use both -s/-S
			{args: []string{"-s", "10", "-S", swap2percent}, code: -1, stderrContains: "swap <=  1.00%", stdoutContains: memReport},
		}
		testcases = append(testcases, tc...)
	}

	for _, tc := range testcases {
		name := fmt.Sprintf("%s", strings.Join(tc.args, " "))
		t.Run(name, func(t *testing.T) {
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
			if res.rss > rssMax {
				t.Errorf("Memory usage too high! actual rss: %d, rssMax: %d", res.rss, rssMax)
				pass = false
			}
			/*
				$ ls -l /proc/42277/fd
				total 0
				lrwx------. 1 jakob jakob 64 Feb 22 14:36 0 -> /dev/pts/2
				lrwx------. 1 jakob jakob 64 Feb 22 14:36 1 -> /dev/pts/2
				lrwx------. 1 jakob jakob 64 Feb 22 14:36 2 -> /dev/pts/2
				lr-x------. 1 jakob jakob 64 Feb 22 14:36 3 -> /proc/meminfo

				Plus one for /proc/[pid]/stat which may possibly be open as well
			*/
			if res.fds > 5 {
				t.Fatalf("High number of open file descriptors: %d", res.fds)
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
		})
	}
}

func TestRss(t *testing.T) {
	res := runEarlyoom(t)
	if res.rss == 0 {
		t.Error("rss is zero!?")
	}
	if res.rss > rssMax {
		t.Error("rss above 1 MiB")
	}
	t.Logf("earlyoom RSS: %d kiB", res.rss)
}

// TestI tests that `earlyoom -i` works as expected
func TestI(t *testing.T) {
	cmd := exec.Command("sleep", "60")
	err := cmd.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer cmd.Process.Kill()
	path := fmt.Sprintf("/proc/%d/oom_score_adj", cmd.Process.Pid)
	err = ioutil.WriteFile(path, []byte("1000"), 0600)
	if err != nil {
		t.Fatal(err)
	}
	res := runEarlyoom(t, "-d")
	// We should see a line like this:
	//   pid 2308155: badness 1000 vm_rss     708 uid 1026 "sleep" <--- new victim
	// Or, for some reason, this:
	//   pid  6950: badness 999 vm_rss     772 uid 1026 "sleep" <--- new victim
	matched := false
	for _, b := range []int{1000, 999} {
		pattern := fmt.Sprintf(`pid\s+%d: badness %d`, cmd.Process.Pid, b)
		matched, err = regexp.MatchString(pattern, res.stdout)
		if err != nil {
			t.Fatal(err)
		}
		if matched {
			break
		}
	}
	if !matched {
		t.Error("did not see badness 1000 or 999 in output")
		t.Log(res.stdout)
	}
	res = runEarlyoom(t, "-d", "-i")
	pattern := fmt.Sprintf(`pid\s+%d: badness %d`, cmd.Process.Pid, 1000)
	matched, err = regexp.MatchString(pattern, res.stdout)
	if err != nil {
		t.Fatal(err)
	}
	if matched {
		t.Error("saw badness 1000, but should not have")
	}
}
