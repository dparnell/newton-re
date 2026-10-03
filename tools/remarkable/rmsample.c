/*
	rmsample - a sampling profiler's trigger for newton on a reMarkable (or
	any Linux without perf, gdb or Python).

	newton samples itself (src/host/newton.cpp, HostInstallSampler): a
	thread sent SIGRTMIN+3 appends one line to newton's sample file - its
	thread id, the interrupted instruction and its callers, as offsets into
	the executable.  This sends that signal, every interval, to each thread
	of the process that is running (state R in /proc/<pid>/task/<tid>/stat:
	one waiting in the system is not a sample), for so many seconds, and
	says how many it sent.  The file is then named on any machine with the
	executable's symbols:

		python3 tools/host/linuxsample.py --samples FILE --exe ELF [--save JSON] [--top N]

	(tools/remarkable/README.md, "Profiling on the tablet").

	Usage:   rmsample <pid> [seconds (10)] [interval in microseconds (2000)]
	Output:  newton's sample file grows (NEWTON_SAMPLE_FILE in its
	         environment, else /tmp/newton-sample-<pid>.txt); rmsample
	         prints "rmsample: N signals sent".
	Build:   python tools/remarkable/build_probe.py builds it beside rmprobe.
*/

#define _GNU_SOURCE
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static double
Now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}


// whether the thread is running (its stat's state, after the name's ')')
static int
Running(int pid, const char* tid)
{
	char path[128], buf[512];
	snprintf(path, sizeof(path), "/proc/%d/task/%s/stat", pid, tid);
	FILE* f = fopen(path, "r");
	if (f == NULL)
		return 0;
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;
	char* close = strrchr(buf, ')');
	return close != NULL && close[1] == ' ' && close[2] == 'R';
}


int
main(int argc, char** argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: rmsample <pid> [seconds] [interval-us]\n");
		return 2;
	}
	int pid = atoi(argv[1]);
	double seconds = argc > 2 ? atof(argv[2]) : 10;
	long interval = argc > 3 ? atol(argv[3]) : 2000;
	char taskDir[64];
	snprintf(taskDir, sizeof(taskDir), "/proc/%d/task", pid);
	long sent = 0;
	double end = Now() + seconds;
	while (Now() < end)
	{
		DIR* d = opendir(taskDir);
		if (d == NULL)
			break;					// (the process has gone)
		struct dirent* e;
		while ((e = readdir(d)) != NULL)
		{
			if (e->d_name[0] == '.' || !Running(pid, e->d_name))
				continue;
			if (syscall(SYS_tgkill, pid, atoi(e->d_name), SIGRTMIN + 3) == 0)
				sent++;
		}
		closedir(d);
		usleep(interval);
	}
	printf("rmsample: %ld signals sent\n", sent);
	return 0;
}
