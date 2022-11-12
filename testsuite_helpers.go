package earlyoom_testsuite

import (
	"bufio"
	"bytes"
	"fmt"
	"log"
	"os"
	"os/exec"
	"syscall"
	"testing"
	"time"
)

type exitVals struct {
	stdout string
	stderr string
	// Exit code
	code int
	// RSS in kiB
	rss int
	// Number of file descriptors
	fds int
}

const earlyoomBinary = "./earlyoom"

// The periodic memory report looks like this:
//
//	mem avail: 4998 MiB (63 %), swap free: 0 MiB (0 %)
const memReport = "mem avail: "

// runEarlyoom runs earlyoom, waits for the first "mem avail:" status line,
// and kills it.
func runEarlyoom(t *testing.T, args ...string) exitVals {
	var stdoutBuf, stderrBuf bytes.Buffer
	cmd := exec.Command(earlyoomBinary, args...)
	cmd.Stderr = &stderrBuf
	p, err := cmd.StdoutPipe()
	if err != nil {
		t.Fatal(err)
	}
	stdoutScanner := bufio.NewScanner(p)

	// If "-r=0" is passed, earlyoom will not print a memory report,
	// so we set a shorter timeout and not report an error on timeout
	// kill.
	expectMemReport := true
	for _, a := range args {
		if a == "-r=0" {
			expectMemReport = false
		}
	}
	var timer *time.Timer
	if expectMemReport {
		timer = time.AfterFunc(10*time.Second, func() {
			t.Error("timeout")
			cmd.Process.Kill()
		})
	} else {
		timer = time.AfterFunc(100*time.Millisecond, func() {
			cmd.Process.Kill()
		})
	}

	err = cmd.Start()
	if err != nil {
		t.Fatal(err)
	}

	// Read until the first status line, looks like this:
	// mem avail: 19377 of 23915 MiB (81 %), swap free:    0 of    0 MiB ( 0 %)
	for stdoutScanner.Scan() {
		line := stdoutScanner.Bytes()
		stdoutBuf.Write(line)
		// Scanner strips the newline, add it back
		stdoutBuf.Write([]byte{'\n'})
		if bytes.HasPrefix(line, []byte(memReport)) {
			break
		}
	}
	timer.Stop()
	rss := get_vm_rss_kib(cmd.Process.Pid)
	fds := countFds(cmd.Process.Pid)
	cmd.Process.Kill()
	err = cmd.Wait()

	return exitVals{
		code:   extractCmdExitCode(err),
		stdout: string(stdoutBuf.Bytes()),
		stderr: string(stderrBuf.Bytes()),
		rss:    rss,
		fds:    fds,
	}
}

/*
$ ls -l /proc/$(pgrep earlyoom)/fd
total 0
lrwx------. 1 jakob jakob 64 Feb 22 14:36 0 -> /dev/pts/2
lrwx------. 1 jakob jakob 64 Feb 22 14:36 1 -> /dev/pts/2
lrwx------. 1 jakob jakob 64 Feb 22 14:36 2 -> /dev/pts/2
lr-x------. 1 jakob jakob 64 Feb 22 14:36 3 -> /proc/meminfo

Plus one for /proc/[pid]/stat which may possibly be open as well
*/
const openFdsMax = 5

func countFds(pid int) int {
	dir := fmt.Sprintf("/proc/%d/fd", pid)
	f, err := os.Open(dir)
	if err != nil {
		return -1
	}
	defer f.Close()
	// Note: Readdirnames filters "." and ".."
	names, err := f.Readdirnames(0)
	if err != nil {
		return -1
	}
	if len(names) > openFdsMax {
		fmt.Printf("countFds: earlyoom has too many open fds:\n")
		for _, n := range names {
			linkName := fmt.Sprintf("%s/%s", dir, n)
			linkTarget, err := os.Readlink(linkName)
			fmt.Printf("%s -> %s, err=%v\n", linkName, linkTarget, err)
		}
	}
	return len(names)
}

// extractCmdExitCode extracts the exit code from an error value that was
// returned from exec / cmd.Run()
func extractCmdExitCode(err error) int {
	if err == nil {
		return 0
	}
	// OMG this is convoluted
	if err2, ok := err.(*exec.ExitError); ok {
		return err2.Sys().(syscall.WaitStatus).ExitStatus()
	}
	if err2, ok := err.(*os.PathError); ok {
		return int(err2.Err.(syscall.Errno))
	}
	log.Panicf("could not decode error %#v", err)
	return 0
}
