package tests

import (
	"bytes"
	"log"
	"os"
	"os/exec"
	"syscall"
	"testing"
	"time"
)

type exitVals struct {
	code   int
	stdout string
	stderr string
}

// runEarlyoom runs earlyoom with a timeout
func runEarlyoom(t *testing.T, args ...string) exitVals {
	var stdoutBuf, stderrBuf bytes.Buffer
	cmd := exec.Command("../earlyoom", args...)
	cmd.Stdout = &stdoutBuf
	cmd.Stderr = &stderrBuf

	// Start with 100 ms timeout
	var timer *time.Timer
	timer = time.AfterFunc(100*time.Millisecond, func() {
		timer.Stop()
		t.Logf("killing process after timeout")
		cmd.Process.Kill()
	})
	err := cmd.Run()
	timer.Stop()

	return exitVals{
		code:   extractCmdExitCode(err),
		stdout: string(stdoutBuf.Bytes()),
		stderr: string(stderrBuf.Bytes()),
	}
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
