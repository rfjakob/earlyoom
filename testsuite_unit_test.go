package earlyoom_testsuite

import (
	"fmt"
	"io/ioutil"
	"os"
	"strings"
	"syscall"
	"testing"
	"unicode/utf8"

	linuxproc "github.com/c9s/goprocinfo/linux"
)

// On Fedora 31 (Linux 5.4), /proc/sys/kernel/pid_max = 4194304.
// It's very unlikely that INT32_MAX will be a valid pid anytime soon.
const INT32_MAX = 2147483647
const ENOENT = 2

func TestParseTuple(t *testing.T) {
	tcs := []struct {
		arg        string
		limit      int
		shouldFail bool
		term       float64
		kill       float64
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
		// TERM value is set to KILL value when it is below TERM
		{arg: "4,5", limit: 100, term: 5, kill: 5},
		{arg: "0", limit: 100, shouldFail: true},
		{arg: "0,0", limit: 100, shouldFail: true},
		// Floating point values
		{arg: "4.0,2.0", limit: 100, term: 4, kill: 2},
		{arg: "4,0,2,0", limit: 100, shouldFail: true},
		{arg: "3.1415,2.7182", limit: 100, term: 3.1415, kill: 2.7182},
		{arg: "3.1415", limit: 100, term: 3.1415, kill: 3.1415 / 2},
		{arg: "1." + strings.Repeat("123", 100), limit: 100, shouldFail: true},
		// Leading garbage
		{arg: "x1,x2", limit: 100, shouldFail: true},
		{arg: "1,x2", limit: 100, shouldFail: true},
		// Trailing garbage
		{arg: "1x,2x", limit: 100, shouldFail: true},
		{arg: "1.1.", limit: 100, shouldFail: true},
		{arg: "1,2..", limit: 100, shouldFail: true},
	}
	for _, tc := range tcs {
		err, term, kill := parse_term_kill_tuple(tc.arg, tc.limit)
		hasFailed := (err != nil)
		if tc.shouldFail != hasFailed {
			t.Errorf("case %v: hasFailed=%v", tc, hasFailed)
			continue
		}
		if term != tc.term {
			t.Errorf("case %v: term=%v", tc, term)
		}
		if kill != tc.kill {
			t.Errorf("case %v: kill=%v", tc, kill)
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

func TestIsAliveMock(t *testing.T) {
	mockProcdir, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	procdir_path(mockProcdir)
	defer procdir_path("/proc")

	if err := os.Mkdir(mockProcdir+"/100", 0700); err != nil {
		t.Fatal(err)
	}

	statString := func(comm string, state string) string {
		template := "144815 (%s) %s 17620 144815 144815 34817 247882 4194304 20170 1855121 1 3321 28 46 3646 3366 20 0 1 0 10798280 237576192 1065 18446744073709551615 94174652813312 94174653706789 140724247111872 0 0 0 65536 3686404 1266761467 0 0 0 17 0 0 0 9 0 0 94174653946928 94174653994640 94174663303168 140724247119367 140724247119377 140724247119377 140724247121902 0"
		return fmt.Sprintf(template, comm, state)
	}
	testCases := []struct {
		content string
		res     bool
	}{
		{statString("bash", "R"), true}, // full string from actual system
		{statString("bash", "Z"), false},
		// hostile process names that try to fake "I am dead"
		{statString("foo) Z ", "R"), true},
		{statString("foo) Z", "R"), true},
		{statString("foo)Z ", "R"), true},
		{statString("foo)\nZ\n", "R"), true},
		{statString("foo)\tZ\t", "R"), true},
		{statString("foo)  Z  ", "R"), true},
		// Actual stat string from https://github.com/rfjakob/zombiemem
		{"777295 (zombiemem) Z 773303 777295 773303 34817 777295 4227084 262246 0 1 0 18 49 0 0 20 0 2 0 8669053 0 0 18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0 0 0 0 0 0 0 0 0", true},
	}

	for _, tc := range testCases {
		statFile := mockProcdir + "/100/stat"
		if err := ioutil.WriteFile(statFile, []byte(tc.content), 0600); err != nil {
			t.Fatal(err)
		}
		if is_alive(100) != tc.res {
			t.Errorf("have=%v, want=%v for /proc/100/stat=%q", is_alive(100), tc.res, tc.content)
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

func Test_get_oom_score(t *testing.T) {
	res := get_oom_score(os.Getpid())
	// On systems with a lot of RAM, our process may actually have a score of
	// zero. At least check that get_oom_score did not return an error.
	if res < 0 {
		t.Error(res)
	}
	res = get_oom_score(INT32_MAX)
	if res != -ENOENT {
		t.Errorf("want %d, but have %d", syscall.ENOENT, res)
	}
}

func Test_get_comm(t *testing.T) {
	pid := os.Getpid()
	res, comm := get_comm(pid)
	if res != 0 {
		t.Fatalf("error %d", res)
	}
	if len(comm) == 0 {
		t.Fatalf("empty process name %q", comm)
	}
	t.Logf("process name %q", comm)
	// Error case
	res, comm = get_comm(INT32_MAX)
	if res != -ENOENT {
		t.Fail()
	}
	if comm != "" {
		t.Fail()
	}
}

func Test_get_cmdline(t *testing.T) {
	pid := os.Getpid()
	res, comm := get_cmdline(pid)
	if res != 0 {
		t.Fatalf("error %d", res)
	}
	if len(comm) == 0 {
		t.Fatalf("empty process cmdline %q", comm)
	}
	t.Logf("process cmdline %q", comm)
	// Error case
	res, comm = get_cmdline(INT32_MAX)
	if res != -ENOENT {
		t.Fail()
	}
	if comm != "" {
		t.Fail()
	}
}

func Test_get_cgroupMockv1(t *testing.T) {
	// Error case
	res, _ := get_cgroup(INT32_MAX)
	if res != -ENOENT {
		t.Fail()
	}

	// memory cgroup present
	mockProcdir, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	procdir_path(mockProcdir)
	defer procdir_path("/proc")

	if err := os.Mkdir(mockProcdir+"/101", 0700); err != nil {
		t.Fatal(err)
	}

	template := "%sdocker/foo"
	content := []string{
		fmt.Sprintf(template, "13:devices:/"),
		fmt.Sprintf(template, "12:hugetlb:/"),
		"11:memory:/docker/theexpectedmemorycgroupid",
		fmt.Sprintf(template, "10:rdma:/"),
		fmt.Sprintf(template, "9:pids:/"),
		fmt.Sprintf(template, "8:misc:/"),
		fmt.Sprintf(template, "7:perf_event:/"),
		fmt.Sprintf(template, "6:net_cls,net_prio:/"),
		fmt.Sprintf(template, "5:cpuset:/"),
		fmt.Sprintf(template, "4:freezer:/"),
		fmt.Sprintf(template, "3:blkio:/"),
		fmt.Sprintf(template, "2:cpu,cpuacct:/"),
		fmt.Sprintf(template, "1:name=systemd:/"),
		"0::/asdf",
	}

	cgroupFile := mockProcdir + "/101/cgroup"
	fullContent := strings.Join(content, "\n") + "\n"
	if err := ioutil.WriteFile(cgroupFile, []byte(fullContent), 0600); err != nil {
		t.Fatal(err)
	}

	res, cgroup := get_cgroup(101)
	if res != 0 {
		t.Fatalf("v1 error %d", res)
	}

	if cgroup != "/docker/theexpectedmemorycgroupid" {
		t.Fatalf("memory cgroup incorrect: %q", cgroup)
	}

	// No cgroup for memory
	if err := os.Mkdir(mockProcdir+"/102", 0700); err != nil {
		t.Fatal(err)
	}

	content = []string{
		"2:hugetlb:/system.slice/some.service",
		"1:memory:/",
		"0::/",
	 }

	cgroupFile = mockProcdir + "/102/cgroup"
	fullContent = strings.Join(content, "\n") + "\n"
	if err := ioutil.WriteFile(cgroupFile, []byte(fullContent), 0600); err != nil {
		t.Fatal(err)
	}

	res, cgroup = get_cgroup(102)
	if res != 0 {
		t.Fatalf("error %d", res)
	}

	if cgroup != "/" {
		t.Fatalf("memory cgroup incorrect: %q", cgroup)
	}
}

func Test_get_cgroupMockv2(t *testing.T) {
	// Error case
	res, _ := get_cgroup(INT32_MAX)
	if res != -ENOENT {
		t.Fail()
	}

	// test with cgroup present
	mockProcdir, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	procdir_path(mockProcdir)
	defer procdir_path("/proc")

	if err := os.Mkdir(mockProcdir+"/103", 0700); err != nil {
		t.Fatal(err)
	}

	content := []string{
		"0::/docker/theexpectedmemorycgroup",
	 }

	cgroupFile := mockProcdir + "/103/cgroup"
	fullContent := strings.Join(content, "\n") + "\n"
	if err := ioutil.WriteFile(cgroupFile, []byte(fullContent), 0600); err != nil {
		t.Fatal(err)
	}

	res, cgroup := get_cgroup(103)
	if res != 0 {
		t.Fatalf("error %d", res)
	}

	if cgroup != "/docker/theexpectedmemorycgroup" {
		t.Fatalf("memory cgroup incorrect: %q", cgroup)
	}

	// test when no cgroup present
	if err := os.Mkdir(mockProcdir+"/104", 0700); err != nil {
		t.Fatal(err)
	}

	content = []string{
		"0::/",
	 }

	cgroupFile = mockProcdir + "/104/cgroup"
	fullContent = strings.Join(content, "\n") + "\n"
	if err := ioutil.WriteFile(cgroupFile, []byte(fullContent), 0600); err != nil {
		t.Fatal(err)
	}

	res, cgroup = get_cgroup(104)
	if res != 0 {
		t.Fatalf("error %d", res)
	}

	if cgroup != "/" {
		t.Fatalf("memory cgroup incorrect: %q", cgroup)
	}
}


func Test_parse_proc_pid_stat_buf(t *testing.T) {
	should_error_out := []string{
		"",
		"x",
		"\000\000\000",
		")",
	}
	for _, v := range should_error_out {
		res, _ := parse_proc_pid_stat_buf(v)
		if res {
			t.Errorf("Should have errored out at %q", v)
		}
	}
}

func Test_parse_proc_pid_stat_1(t *testing.T) {
	stat, err := linuxproc.ReadProcessStat("/proc/1/stat")
	if err != nil {
		t.Fatal(err)
	}

	res, have := parse_proc_pid_stat(1)
	if !res {
		t.Fatal(res)
	}

	want := have
	want.state = _Ctype_char(stat.State[0])
	want.ppid = _Ctype_int(stat.Ppid)
	want.num_threads = _Ctype_long(stat.NumThreads)
	want.rss = _Ctype_long(stat.Rss)

	if have != want {
		t.Errorf("\nhave=%#v\nwant=%#v", have, want)
	}
}

func Test_parse_proc_pid_stat_Mock(t *testing.T) {
	mockProcdir, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	procdir_path(mockProcdir)
	defer procdir_path("/proc")

	if err := os.Mkdir(mockProcdir+"/100", 0700); err != nil {
		t.Fatal(err)
	}

	// Real /proc/pid/stat string for gnome-shell
	template := "549077 (%s) S 547891 549077 549077 0 -1 4194560 245592 104 342 5 108521 28953 0 1 20 0 23 0 4816953 5260238848 65528 18446744073709551615 94179647238144 94179647245825 140730757359824 0 0 0 0 16781312 17656 0 0 0 17 1 0 0 0 0 0 94179647252976 94179647254904 94179672109056 140730757367876 140730757367897 140730757367897 140730757369827 0"
	content := []string{
		fmt.Sprintf(template, "gnome-shell"),
		fmt.Sprintf(template, ""),
		fmt.Sprintf(template, ": - )"),
		fmt.Sprintf(template, "()()()())))(((()))()()"),
		fmt.Sprintf(template, "   \n\n    "),
	}

	// Stupid hack to get a C.pid_stat_t
	_, want := parse_proc_pid_stat(1)
	want.state = 'S'
	want.ppid = 547891
	want.num_threads = 23
	want.rss = 65528

	for _, c := range content {
		statFile := mockProcdir + "/100/stat"
		if err := ioutil.WriteFile(statFile, []byte(c), 0600); err != nil {
			t.Fatal(err)
		}
		res, have := parse_proc_pid_stat(100)
		if !res {
			t.Errorf("parse_proc_pid_stat returned %v", res)
		}
		if have != want {
			t.Errorf("/proc/100/stat=%q:\nhave=%#v\nwant=%#v", c, have, want)
		}
	}
}

func permute_is_larger(t *testing.T, sort_by_rss bool, procs []mockProcProcess) {
	args := poll_loop_args_t(sort_by_rss)
	for i := range procs {
		for j := range procs {
			// If the entry is later in the list, is_larger should return true.
			want := j > i
			have := is_larger(&args, procs[i], procs[j])
			if want != have {
				t.Errorf("j%d/pid%d larger than i%d/pid%d? want=%v have=%v", j, procs[j].pid, i, procs[i].pid, want, have)
			}
		}
	}
}

func Test_is_larger(t *testing.T) {
	procs := []mockProcProcess{
		// smallest
		{pid: 100, oom_score: 100, VmRSSkiB: 1234},
		{pid: 101, oom_score: 100, VmRSSkiB: 1238},
		{pid: 102, oom_score: 101, VmRSSkiB: 4},
		{pid: 103, oom_score: 102, VmRSSkiB: 4},
		{pid: 104, oom_score: 103, VmRSSkiB: 0, num_threads: 2}, // zombie main thread
		// largest
	}

	mockProc(t, procs)
	defer procdir_path("/proc")
	t.Logf("procdir_path=%q", procdir_path(""))

	permute_is_larger(t, false, procs)
}

func Test_is_larger_by_rss(t *testing.T) {
	procs := []mockProcProcess{
		// smallest
		{pid: 100, oom_score: 100, VmRSSkiB: 4},
		{pid: 101, oom_score: 100, VmRSSkiB: 8},
		{pid: 102, oom_score: 101, VmRSSkiB: 8},
		{pid: 103, oom_score: 99, VmRSSkiB: 12},
		{pid: 104, oom_score: 102, VmRSSkiB: 0, num_threads: 2}, // zombie main thread
		{pid: 105, oom_score: 102, VmRSSkiB: 12},
		// largest
	}

	mockProc(t, procs)
	defer procdir_path("/proc")
	t.Logf("procdir_path=%q", procdir_path(""))

	permute_is_larger(t, true, procs)
}

func Benchmark_parse_meminfo(b *testing.B) {
	enable_debug(false)

	for n := 0; n < b.N; n++ {
		parse_meminfo()
	}
}

func Benchmark_kill_process(b *testing.B) {
	enable_debug(false)

	for n := 0; n < b.N; n++ {
		kill_process()
	}
}

func Benchmark_find_largest_process(b *testing.B) {
	enable_debug(false)

	for n := 0; n < b.N; n++ {
		find_largest_process()
	}
}

func Benchmark_get_oom_score(b *testing.B) {
	enable_debug(false)

	pid := os.Getpid()
	for n := 0; n < b.N; n++ {
		get_oom_score(pid)
	}
}

func Benchmark_get_oom_score_adj(b *testing.B) {
	enable_debug(false)

	pid := os.Getpid()
	for n := 0; n < b.N; n++ {
		var out int
		get_oom_score_adj(pid, &out)
	}
}

func Benchmark_get_cmdline(b *testing.B) {
	enable_debug(false)

	pid := os.Getpid()
	for n := 0; n < b.N; n++ {
		res, comm := get_cmdline(pid)
		if len(comm) == 0 {
			b.Fatalf("empty process cmdline %q", comm)
		}
		if res != 0 {
			b.Fatalf("error %d", res)
		}
	}
}

func Benchmark_parse_proc_pid_stat(b *testing.B) {
	enable_debug(false)

	pid := os.Getpid()
	for n := 0; n < b.N; n++ {
		res, out := parse_proc_pid_stat(pid)
		if out.num_threads == 0 {
			b.Fatalf("no threads???")
		}
		if !res {
			b.Fatal("failed")
		}
	}
}

func Benchmark_get_cgroup(b *testing.B) {
	enable_debug(false)

	pid := os.Getpid()
	for n := 0; n < b.N; n++ {
		get_cgroup(pid)
	}
}
