package main

import (
	"fmt"
	"io/ioutil"
	"log"
	"strconv"
	"strings"
	"time"

	"golang.org/x/sys/unix"
)

func main() {
	t0 := time.Now()
	err := unix.Mlockall(unix.MCL_CURRENT | unix.MCL_FUTURE | unix.MCL_ONFAULT)
	if err != nil {
		fmt.Printf("warning: mlockall: %v. Run as root?\n\n", err)
	}
	fmt.Println("     | /proc/meminfo     | /proc/pressure/memory")
	fmt.Println("Time | MemAvail SwapFree | some avg10 full avg10")
	fmt.Println("   s |      MiB      MiB |    %     %    %     %")
	fmt.Println("     -                   -                      ")
	pOld := pressure()
	const interval = 100
	for {
		t1 := time.Now()
		t := t1.Sub(t0).Seconds()
		p := pressure()
		deltaUs := p.timestamp.Sub(pOld.timestamp).Microseconds()
		someNow := (p.someTotal - pOld.someTotal) * 100 / deltaUs
		fullNow := (p.fullTotal - pOld.fullTotal) * 100 / deltaUs
		m := meminfo()
		fmt.Printf("%4.1f | %8d %8d | %4d %5d %4d %5d\n",
			t,
			m.memAvailableMiB,
			m.swapFreeMiB,
			someNow,
			int(p.someAvg10),
			fullNow,
			int(p.fullAvg10))
		pOld = p
		time.Sleep(interval * time.Millisecond)
	}
}

type pressureVals struct {
	// percent
	someAvg10 float64
	// microseconds
	someTotal int64
	// percent
	fullAvg10 float64
	// microseconds
	fullTotal int64
	// Time when the values were read
	timestamp time.Time
}

func pressure() (p pressureVals) {
	/*
	   $ cat /proc/pressure/memory
	   some avg10=0.00 avg60=0.03 avg300=0.65 total=28851712
	   full avg10=0.00 avg60=0.01 avg300=0.27 total=12963374
	*/
	buf, err := ioutil.ReadFile("/proc/pressure/memory")
	if err != nil {
		log.Fatal(err)
	}
	p.timestamp = time.Now()
	fields := strings.Fields(string(buf))
	p.someAvg10, err = strconv.ParseFloat(fields[1][len("avg10="):], 64)
	if err != nil {
		log.Fatal(err)
	}
	p.someTotal, err = strconv.ParseInt(fields[4][len("total="):], 10, 64)
	if err != nil {
		log.Fatal(err)
	}
	p.fullAvg10, err = strconv.ParseFloat(fields[6][len("avg10="):], 64)
	if err != nil {
		log.Fatal(err)
	}
	p.fullTotal, err = strconv.ParseInt(fields[9][len("total="):], 10, 64)
	if err != nil {
		log.Fatal(err)
	}
	return
}

type meminfoStruct struct {
	memAvailableMiB     int
	memTotalMiB         int
	memAvailablePercent int
	swapFreeMiB         int
	swapTotalMiB        int
	swapFreePercent     int
}

func atoi(s string) int {
	val, err := strconv.Atoi(s)
	if err != nil {
		log.Fatal(err)
	}
	return val
}

func meminfo() (m meminfoStruct) {
	/*
	   $ cat /proc/meminfo
	   MemTotal:       24537156 kB
	   MemFree:        19759616 kB
	   MemAvailable:   19891772 kB
	   Buffers:           20564 kB
	   Cached:          1029436 kB
	   [...]
	   SwapTotal:       1049596 kB
	   SwapFree:         201864 kB
	   [...]
	*/
	buf, err := ioutil.ReadFile("/proc/meminfo")
	if err != nil {
		log.Fatal(err)
	}
	fields := strings.Fields(string(buf))
	for i, v := range fields {
		switch v {
		case "MemAvailable:":
			m.memAvailableMiB = atoi(fields[i+1]) / 1024
		case "SwapFree:":
			m.swapFreeMiB = atoi(fields[i+1]) / 1024
		case "MemTotal:":
			m.memTotalMiB = atoi(fields[i+1]) / 1024
		case "SwapTotal:":
			m.swapTotalMiB = atoi(fields[i+1]) / 1024
		}
	}
	m.memAvailablePercent = m.memAvailableMiB * 100 / m.memTotalMiB
	if m.swapTotalMiB > 0 {
		m.swapFreePercent = m.swapFreeMiB * 100 / m.swapTotalMiB
	}
	return
}
