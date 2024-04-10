package earlyoom_testsuite

import (
	"bufio"
	"bytes"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"syscall"
	"testing"
	"time"

	linuxproc "github.com/c9s/goprocinfo/linux"
)

// #include "meminfo.h"
import "C"

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

	stat, err := linuxproc.ReadProcessStat(fmt.Sprintf("/proc/%d/stat", cmd.Process.Pid))
	if err != nil {
		panic(err)
	}
	rss := int(stat.Rss)
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

type mockProcProcess struct {
	pid         int
	state       string // set to "R" when empty
	oom_score   int
	VmRSSkiB    int
	comm        string
	num_threads int // set to 1 when zero
}

func (m *mockProcProcess) toProcinfo_t() (p C.procinfo_t) {
	p.pid = C.int(m.pid)
	p.oom_score = C.int(m.oom_score)
	p.VmRSSkiB = C.longlong(m.VmRSSkiB)
	for i, v := range []byte(m.comm) {
		p.name[i] = C.char(v)
	}
	return p
}

func mockProc(t *testing.T, procs []mockProcProcess) {
	mockProcdir, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	procdir_path(mockProcdir)

	for _, p := range procs {
		if p.state == "" {
			p.state = "R"
		}
		if p.num_threads == 0 {
			p.num_threads = 1
		}

		pidDir := fmt.Sprintf("%s/%d", mockProcdir, int(p.pid))
		if err := os.Mkdir(pidDir, 0755); err != nil {
			t.Fatal(err)
		}
		// statm
		//
		// rss = 2nd field, in pages. The other fields are not used by earlyoom.
		rss := p.VmRSSkiB * 1024 / os.Getpagesize()
		content := []byte(fmt.Sprintf("1 %d 3 4 5 6 7\n", rss))
		if err := ioutil.WriteFile(pidDir+"/statm", content, 0444); err != nil {
			t.Fatal(err)
		}
		// stat
		//
		// Real /proc/pid/stat string for gnome-shell
		template := "549077 (%s) S 547891 549077 549077 0 -1 4194560 245592 104 342 5 108521 28953 0 1 20 0 %d 0 4816953 5260238848 %d 18446744073709551615 94179647238144 94179647245825 140730757359824 0 0 0 0 16781312 17656 0 0 0 17 1 0 0 0 0 0 94179647252976 94179647254904 94179672109056 140730757367876 140730757367897 140730757367897 140730757369827 0\n"
		content = []byte(fmt.Sprintf(template, p.comm, p.num_threads, rss))
		if err := ioutil.WriteFile(pidDir+"/stat", content, 0444); err != nil {
			t.Fatal(err)
		}
		// oom_score
		content = []byte(fmt.Sprintf("%d\n", p.oom_score))
		if err := ioutil.WriteFile(pidDir+"/oom_score", content, 0444); err != nil {
			t.Fatal(err)
		}
		// oom_score_adj
		if err := ioutil.WriteFile(pidDir+"/oom_score_adj", []byte("0\n"), 0444); err != nil {
			t.Fatal(err)
		}
		// comm
		if err := ioutil.WriteFile(pidDir+"/comm", []byte(p.comm+"\n"), 0444); err != nil {
			t.Fatal(err)
		}
		// cmdline
		if err := ioutil.WriteFile(pidDir+"/cmdline", []byte("foo\000-bar\000-baz"), 0444); err != nil {
			t.Fatal(err)
		}
	}
}
