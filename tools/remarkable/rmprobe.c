/*
	rmprobe: what newton's reMarkable port needs to know about a tablet,
	found out on the tablet (docs/host-remarkable.md, "On the device").

	Built for aarch64 Linux by tools/remarkable/build_probe.py (zig, the same
	glibc floor as newton, so that it running at all answers the first
	question).  Run over SSH:

	  ./rmprobe                    the system, the C library, the display
	                               devices, the input devices, AppLoad
	  ./rmprobe --events 15        also every input event for 15 seconds
	                               (draw with the pen, touch, press keys)
	  ./rmprobe --qtfb 30          (started from AppLoad, qtfb on) a test
	                               picture through qtfb, then the pen drawn
	                               as dots and every event printed for 30 s

	Everything goes to stdout; send the owner's copy back as it is.
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <poll.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <gnu/libc-version.h>
#include <linux/fb.h>
#include <linux/input.h>

#define BITS_PER_LONG	(sizeof(long) * 8)
#define NBITS(x)		((((x) - 1) / BITS_PER_LONG) + 1)
#define TEST_BIT(bit, array)	((array[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1)

static void
PrintFile(const char* label, const char* path, int maxLines)
{
	FILE* f = fopen(path, "r");
	if (f == NULL)
	{
		printf("%s: (no %s)\n", label, path);
		return;
	}
	char line[512];
	int n = 0;
	while (n < maxLines && fgets(line, sizeof(line), f) != NULL)
	{
		// the device tree's strings are NUL-separated
		for (size_t i = 0; i + 1 < sizeof(line) && line[i] != '\n'; i++)
			if (line[i] == 0) { if (line[i + 1] == 0) break; line[i] = ' '; }
		line[strcspn(line, "\n")] = 0;
		printf("%s: %s\n", label, line);
		n++;
	}
	fclose(f);
}

static void
System(void)
{
	struct utsname u;
	if (uname(&u) == 0)
		printf("uname: %s %s %s %s\n", u.sysname, u.release, u.version, u.machine);
	printf("glibc: %s %s\n", gnu_get_libc_version(), gnu_get_libc_release());
	printf("char: %s\n", ((char) -1) < 0 ? "signed" : "unsigned");
	printf("processors: %ld online\n", sysconf(_SC_NPROCESSORS_ONLN));
	PrintFile("model", "/proc/device-tree/model", 1);
	PrintFile("compatible", "/proc/device-tree/compatible", 1);
	PrintFile("os-release", "/etc/os-release", 12);
	PrintFile("version", "/etc/version", 2);
	PrintFile("update.conf", "/usr/share/remarkable/update.conf", 6);
	FILE* f = fopen("/proc/meminfo", "r");
	if (f != NULL)
	{
		char line[256];
		while (fgets(line, sizeof(line), f) != NULL)
			if (strncmp(line, "MemTotal", 8) == 0 || strncmp(line, "MemAvailable", 12) == 0)
				printf("meminfo: %s", line);
		fclose(f);
	}
	const char* places[] = { "/home/root", "/tmp", "/dev/shm", "/opt" };
	for (int i = 0; i < 4; i++)
	{
		struct statvfs s;
		if (statvfs(places[i], &s) == 0)
			printf("free: %s %llu MB of %llu MB\n", places[i],
				   (unsigned long long) s.f_bavail * s.f_frsize >> 20, (unsigned long long) s.f_blocks * s.f_frsize >> 20);
	}
	const char* libs[] = { "/usr/lib/libstdc++.so.6", "/lib/libstdc++.so.6", "/usr/lib/libasound.so.2", "/usr/lib/libX11.so.6" };
	for (int i = 0; i < 4; i++)
		printf("library: %s %s\n", libs[i], access(libs[i], F_OK) == 0 ? "present" : "absent");
}

static void
Display(void)
{
	for (int i = 0; i < 4; i++)
	{
		char path[32];
		snprintf(path, sizeof(path), "/dev/fb%d", i);
		int fd = open(path, O_RDONLY);		// (looked at, never written)
		if (fd < 0)
		{
			if (errno != ENOENT)
				printf("display: %s cannot be opened (%s)\n", path, strerror(errno));
			continue;
		}
		struct fb_var_screeninfo v;
		struct fb_fix_screeninfo x;
		memset(&v, 0, sizeof(v));
		memset(&x, 0, sizeof(x));
		ioctl(fd, FBIOGET_VSCREENINFO, &v);
		ioctl(fd, FBIOGET_FSCREENINFO, &x);
		printf("display: %s id '%.16s' %ux%u (virtual %ux%u) %u bpp, line %u bytes, %u bytes, grayscale %u\n",
			   path, x.id, v.xres, v.yres, v.xres_virtual, v.yres_virtual, v.bits_per_pixel, x.line_length, x.smem_len, v.grayscale);
		close(fd);
	}
	DIR* d = opendir("/dev/dri");
	if (d != NULL)
	{
		struct dirent* e;
		while ((e = readdir(d)) != NULL)
			if (e->d_name[0] != '.')
				printf("display: /dev/dri/%s\n", e->d_name);
		closedir(d);
	}
	d = opendir("/sys/class/graphics");
	if (d != NULL)
	{
		struct dirent* e;
		while ((e = readdir(d)) != NULL)
			if (e->d_name[0] != '.')
			{
				char path[300];
				snprintf(path, sizeof(path), "/sys/class/graphics/%s/name", e->d_name);
				PrintFile(e->d_name, path, 1);
			}
		closedir(d);
	}
}

static void
AbsRange(int fd, int code, const char* name)
{
	struct input_absinfo a;
	if (ioctl(fd, EVIOCGABS(code), &a) == 0)
		printf("    %s %d..%d (resolution %d)\n", name, a.minimum, a.maximum, a.resolution);
}

static void
Inputs(void)
{
	for (int i = 0; i < 16; i++)
	{
		char path[32];
		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		char name[256] = "";
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		char phys[256] = "";
		ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys);
		unsigned long ev[NBITS(EV_MAX)], keys[NBITS(KEY_MAX)], abs[NBITS(ABS_MAX)];
		memset(ev, 0, sizeof(ev));
		memset(keys, 0, sizeof(keys));
		memset(abs, 0, sizeof(abs));
		ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev);
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
		ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs);
		printf("input: %s '%s' (%s)%s%s%s%s%s%s%s%s\n", path, name, phys,
			   TEST_BIT(BTN_TOOL_PEN, keys) ? " pen" : "",
			   TEST_BIT(BTN_TOOL_RUBBER, keys) ? " eraser" : "",
			   TEST_BIT(BTN_STYLUS, keys) ? " stylus-button" : "",
			   TEST_BIT(ABS_MT_POSITION_X, abs) ? " multitouch" : "",
			   TEST_BIT(KEY_POWER, keys) ? " power-key" : "",
			   TEST_BIT(KEY_A, keys) ? " keyboard" : "",
			   TEST_BIT(KEY_HOME, keys) ? " home-key" : "",
			   TEST_BIT(EV_SW, ev) ? " switch" : "");
		if (TEST_BIT(ABS_X, abs)) AbsRange(fd, ABS_X, "ABS_X");
		if (TEST_BIT(ABS_Y, abs)) AbsRange(fd, ABS_Y, "ABS_Y");
		if (TEST_BIT(ABS_PRESSURE, abs)) AbsRange(fd, ABS_PRESSURE, "ABS_PRESSURE");
		if (TEST_BIT(ABS_DISTANCE, abs)) AbsRange(fd, ABS_DISTANCE, "ABS_DISTANCE");
		if (TEST_BIT(ABS_TILT_X, abs)) AbsRange(fd, ABS_TILT_X, "ABS_TILT_X");
		if (TEST_BIT(ABS_MT_POSITION_X, abs)) AbsRange(fd, ABS_MT_POSITION_X, "ABS_MT_POSITION_X");
		if (TEST_BIT(ABS_MT_POSITION_Y, abs)) AbsRange(fd, ABS_MT_POSITION_Y, "ABS_MT_POSITION_Y");
		close(fd);
	}
}

static void
AppLoad(void)
{
	const char* paths[] = { "/tmp/qtfb.sock", "/home/root/xovi", "/home/root/xovi/exthome/appload",
							"/home/root/xovi/exthome/appload/shims/qtfb-shim.so", "/home/root/xovi/exthome/appload/shims/qtfb-shim-32bit.so",
							"/opt/bin", "/home/root/.vellum", "/home/root/.local/share/remarkable/xochitl" };
	for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
		printf("appload: %s %s\n", paths[i], access(paths[i], F_OK) == 0 ? "present" : "absent");
	DIR* d = opendir("/home/root/xovi/exthome/appload");
	if (d != NULL)
	{
		struct dirent* e;
		while ((e = readdir(d)) != NULL)
			if (e->d_name[0] != '.')
				printf("appload: app %s\n", e->d_name);
		closedir(d);
	}
	printf("appload: QTFB_KEY %s\n", getenv("QTFB_KEY") ? getenv("QTFB_KEY") : "(not set: not started by AppLoad)");
	int s = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	struct sockaddr_un a;
	memset(&a, 0, sizeof(a));
	a.sun_family = AF_UNIX;
	strcpy(a.sun_path, "/tmp/qtfb.sock");
	printf("appload: qtfb socket %s\n", connect(s, (struct sockaddr*) &a, sizeof(a)) == 0 ? "accepts connections" : strerror(errno));
	close(s);
}

static double
Now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static void
Events(int seconds)
{
	struct pollfd p[16];
	char names[16][64];
	int n = 0;
	for (int i = 0; i < 16; i++)
	{
		char path[32];
		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		p[n].fd = fd;
		p[n].events = POLLIN;
		snprintf(names[n], sizeof(names[n]), "event%d", i);
		n++;
	}
	printf("events: %d devices for %d seconds - draw, touch, press the button and the folio's keys\n", n, seconds);
	fflush(stdout);
	double end = Now() + seconds;
	long count = 0;
	while (Now() < end && count < 5000)
	{
		if (poll(p, n, 200) <= 0)
			continue;
		for (int i = 0; i < n; i++)
		{
			struct input_event e[64];
			ssize_t got;
			while ((got = read(p[i].fd, e, sizeof(e))) > 0)
				for (int k = 0; k < (int) (got / sizeof(e[0])); k++)
				{
					if (e[k].type == EV_SYN)
						continue;
					printf("%s %ld.%06ld type %d code %d value %d\n", names[i],
						   (long) e[k].input_event_sec, (long) e[k].input_event_usec, e[k].type, e[k].code, e[k].value);
					count++;
				}
		}
	}
	printf("events: %ld seen\n", count);
}

/*------------------------------------------------------------------------------
	qtfb, as newton speaks it (src/host/remarkable/QTFB.h): a picture of the
	sixteen grays and a checkerboard, then the pen drawn as it moves (the
	fast waveform), each event printed with how long it took to answer.
------------------------------------------------------------------------------*/

struct ClientMessage { uint8_t type; union { struct { int32_t key; uint8_t format; } init; struct { int32_t type, x, y, w, h; } update; int32_t refreshMode; }; };
struct ServerMessage { uint8_t type; union { struct { int32_t shmKey; size_t shmSize; } init; struct { int32_t inputType, devId, x, y, d; } input; }; };

static void
Qtfb(int seconds)
{
	const char* keyText = getenv("QTFB_KEY");
	if (keyText == NULL)
	{
		printf("qtfb: QTFB_KEY not set - start rmprobe from AppLoad (its external.manifest.json has qtfb: true)\n");
		return;
	}
	int s = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	struct sockaddr_un a;
	memset(&a, 0, sizeof(a));
	a.sun_family = AF_UNIX;
	strcpy(a.sun_path, "/tmp/qtfb.sock");
	if (connect(s, (struct sockaddr*) &a, sizeof(a)) != 0)
	{
		printf("qtfb: cannot connect (%s)\n", strerror(errno));
		return;
	}
	struct ClientMessage m;
	memset(&m, 0, sizeof(m));
	m.type = 0;										// initialise: the Paper Pro's own size, RGB565
	m.init.key = atoi(keyText);
	m.init.format = 3;
	send(s, &m, sizeof(m), 0);
	struct ServerMessage r;
	memset(&r, 0, sizeof(r));
	if (recv(s, &r, sizeof(r), 0) < 1)
	{
		printf("qtfb: no answer\n");
		return;
	}
	char name[32];
	snprintf(name, sizeof(name), "/qtfb_%d", r.init.shmKey);
	int fd = shm_open(name, O_RDWR, 0);
	uint16_t* fb = fd >= 0 ? (uint16_t*) mmap(NULL, r.init.shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : (uint16_t*) MAP_FAILED;
	if (fb == (uint16_t*) MAP_FAILED)
	{
		printf("qtfb: cannot map %s (%s)\n", name, strerror(errno));
		return;
	}
	const int W = 1620, H = 2160;
	printf("qtfb: %s, %zu bytes (%d x %d x 2 = %d)\n", name, r.init.shmSize, W, H, W * H * 2);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
		{
			unsigned g;
			if (y < H / 2)
				g = 255 - (x * 16 / W) * 17;					// sixteen grays across the top half
			else
				g = ((x / 4 + y / 4) & 1) ? 255 : 0;			// a checkerboard of 4-pixel squares below
			fb[y * W + x] = (uint16_t) (((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3));
		}
	memset(&m, 0, sizeof(m));
	m.type = 6;										// a full refresh
	send(s, &m, sizeof(m), 0);
	printf("qtfb: test picture sent - draw with the pen for %d seconds\n", seconds);
	fflush(stdout);
	memset(&m, 0, sizeof(m));
	m.type = 5;
	m.refreshMode = 1;								// fast
	send(s, &m, sizeof(m), 0);
	double end = Now() + seconds;
	struct pollfd p = { s, POLLIN, 0 };
	while (Now() < end)
	{
		if (poll(&p, 1, 200) <= 0)
			continue;
		memset(&r, 0, sizeof(r));
		if (recv(s, &r, sizeof(r), 0) < 1)
		{
			printf("qtfb: AppLoad closed the connection\n");
			break;
		}
		double t0 = Now();
		printf("qtfb: message %d input 0x%x dev %d x %d y %d d %d\n", r.type, r.input.inputType, r.input.devId, r.input.x, r.input.y, r.input.d);
		if (r.type == 4 && (r.input.inputType == 0x20 || r.input.inputType == 0x22) && r.input.x >= 2 && r.input.y >= 2 && r.input.x < W - 2 && r.input.y < H - 2)
		{
			for (int dy = -2; dy <= 2; dy++)
				for (int dx = -2; dx <= 2; dx++)
					fb[(r.input.y + dy) * W + r.input.x + dx] = 0;
			memset(&m, 0, sizeof(m));
			m.type = 1;
			m.update.type = 1;
			m.update.x = r.input.x - 2;
			m.update.y = r.input.y - 2;
			m.update.w = 5;
			m.update.h = 5;
			send(s, &m, sizeof(m), 0);
			printf("qtfb: dot sent %.3f ms after the event arrived\n", (Now() - t0) * 1000);
		}
		fflush(stdout);
	}
	memset(&m, 0, sizeof(m));
	m.type = 3;
	send(s, &m, sizeof(m), 0);
	close(s);
}

int
main(int argc, char** argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("rmprobe 1 (newton's reMarkable port, docs/host-remarkable.md)\n");
	System();
	Display();
	Inputs();
	AppLoad();
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--events") == 0 && i + 1 < argc)
			Events(atoi(argv[++i]));
		else if (strcmp(argv[i], "--qtfb") == 0 && i + 1 < argc)
			Qtfb(atoi(argv[++i]));
	}
	printf("rmprobe: done\n");
	return 0;
}
