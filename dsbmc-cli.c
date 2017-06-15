/*-
 * Copyright (c) 2017 Marcel Kaiser. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "libdsbmc/libdsbmc.h"

#define PATH_LOCKF ".dsbmc-cli.lock"

#define EXEC(f)	do {							  \
	if (f == -1)							  \
		errx(EXIT_FAILURE, "%s", dsbmc_errstr());		  \
} while (0)

#define P(s, m)	do {							  \
	if (s->m != NULL)						  \
		printf("%s" #m"=%s", strcmp(#m, "dev") ? ":" : "", s->m); \
} while (0)

#define PO(opt, desc) printf("%-*s%s\n", 33, opt, desc)

enum { EVENT_MOUNT, EVENT_UNMOUNT, EVENT_ADD, EVENT_REMOVE };

static struct event_command_s {
	int	   argc;
	char	   **args;
	const char *event;
} evcmds[4] = {
	{ 0, NULL, "mount" }, { 0, NULL, "unmount" },
	{ 0, NULL, "add"   }, { 0, NULL, "remove"  }
};
#define NEVENTS (sizeof(evcmds)/sizeof(evcmds[0]))

static struct dtype_s {
	char	*name;
	uint8_t type;
} dtypes[] = {
	{ "hdd",    DSBMC_DT_HDD    }, { "usbdisk", DSBMC_DT_USBDISK },
	{ "datacd", DSBMC_DT_DATACD }, { "audiocd", DSBMC_DT_AUDIOCD },
	{ "dvd",    DSBMC_DT_DVD    }, { "vcd",	    DSBMC_DT_VCD     },
	{ "svcd",   DSBMC_DT_SVCD   }, { "mmc",	    DSBMC_DT_MMC     },
	{ "mtp",    DSBMC_DT_MTP    }, { "ptp",	    DSBMC_DT_PTP     }
};

static void list(void);
static void usage(void);
static void help(void);
static void do_listen(bool automount);
static void cleanpath(char *path);
static void do_mount(const dsbmc_dev_t *dev);
static void cb(int code, const dsbmc_dev_t *d);
static void size_cb(int code, const dsbmc_dev_t *d);
static void add_event_command(char **argv, int *argskip);
static void exec_event_command(int ev, const dsbmc_dev_t *dev);
static char *dtype_to_name(uint8_t type);
static const dsbmc_dev_t *dev_from_mnt(const char *mnt);

int
main(int argc, char *argv[])
{
	int	      i, ch, speed;
	bool	      Lflag, aflag, mflag, uflag, lflag, sflag, vflag, eflag;
	const char    seq[] = "-|/-\\|/";
	struct stat   sb;
	dsbmc_event_t e;
	const dsbmc_dev_t *dev, **dls;

	Lflag = aflag = eflag = mflag = uflag = lflag = sflag = vflag = false;
	while ((ch = getopt(argc, argv, "L:amusehv:l")) != -1) {
		switch (ch) {
		case 'L':
			Lflag = true;
			add_event_command(&argv[optind - 1], &optind);
			break;
		case 'a':
			aflag = true;
		case 'm':
			mflag = true;
			break;
		case 'u':
			uflag = true;
			break;
		case 'l':
			lflag = true;
			break;
		case 's':
			sflag = true;
			break;
		case 'e':
			eflag = true;
			break;
		case 'v':
			vflag = true;
			speed = strtol(optarg, NULL, 10);
			break;
		case 'h':
			help();
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	
	if (dsbmc_connect() == -1)
		errx(EXIT_FAILURE, "%s", dsbmc_errstr());
	if (lflag)
		list();
	else if (aflag)
		do_listen(true);
	else if (Lflag)
		do_listen(false);
	else if (argc < 1)
		usage();
	if (sflag || mflag || uflag || eflag || vflag) {
		cleanpath(argv[0]);
		if (stat(argv[0], &sb) == -1) {
			err(EXIT_FAILURE, errno == ENOENT ? "%s" : "stat(%s)",
			    argv[0]);
		}
		if (S_ISDIR(sb.st_mode) && (uflag || eflag)) {
			if ((dev = dev_from_mnt(argv[0])) == NULL)
				errx(EXIT_FAILURE, "Not a mount point");
		} else if (S_ISDIR(sb.st_mode) && !(uflag || eflag)) {
			warnx("%s is a directory", argv[0]);
			usage();
		} else if (!S_ISCHR(sb.st_mode)) {
			warnx("%s is not a character special file", argv[0]);
			usage();
		} else
			dev = NULL;
		for (i = 0; i < dsbmc_get_devlist(&dls) && dev == NULL; i++) {
			if (strcmp(argv[0], dls[i]->dev) == 0)
				dev = dls[i];
		}
		if (dev == NULL)
			errx(EXIT_FAILURE, "No such device '%s'", argv[0]);
	}
	if (mflag)
		EXEC(dsbmc_mount_async(dev, cb));
	else if (sflag)
		EXEC(dsbmc_size_async(dev, size_cb));
	else if (uflag)
		EXEC(dsbmc_unmount_async(dev, cb));
	else if (eflag)
		EXEC(dsbmc_eject_async(dev, cb));
	else if (vflag)
		EXEC(dsbmc_set_speed_async(dev, speed, cb));
	else
		usage();
	for (; dsbmc_fetch_event(&e) != -1; usleep(500)) {
		for (i = 0; i < sizeof(seq) - 1; i++)
			(void)fprintf(stderr, "\r%c", seq[i]);
	}
	if (dsbmc_get_err(NULL))
		errx(EXIT_FAILURE, "%s", dsbmc_errstr());
	return (EXIT_SUCCESS);
}

static void
add_event_command(char **argv, int *argskip)
{
	int  i, n;
	char *p, *q;
	bool terminated = false;

	for (i = 0; i < NEVENTS && strcmp(evcmds[i].event, argv[0]); i++)
		;
	if (i == NEVENTS) {
		warnx("Invalid event '%s'", argv[0]);
		usage();
	}
	for (n = 0; argv[n] != NULL; n++) {
		for (p = argv[n]; (p = strchr(p, '%')) != NULL; p++) {
			if ((q = strchr("%dlmt", *++p)) == NULL) {
				errx(EXIT_FAILURE, "Invalid sequence '%%%c'",
				    *p);
			}
			if (*q == 'm' && i != EVENT_MOUNT &&
			    i != EVENT_UNMOUNT) {
				errx(EXIT_FAILURE,
				    "%%m is not defined for the %s event",
				    argv[0]);
			}
		}
		if (!strcmp(argv[n], ";")) {
			terminated = true; (*argskip)++;
			break;
		}
	}
	if (--n < 2 || !terminated) {
		if (!terminated)
			warnx("Missing terminating ';'");
		usage();
	}
	evcmds[i].args = argv + 1; evcmds[i].argc = n; (*argskip) += n;
}

static void
exec_event_command(int ev, const dsbmc_dev_t *dev)
{
	int  i, j;
	char **args, *p, buf[1024];
	size_t len;

	if (evcmds[ev].argc == 0)
		return;
	if ((args = malloc(sizeof(char **) * (evcmds[ev].argc + 1))) == NULL)
		err(EXIT_FAILURE, "malloc()");
	args[evcmds[ev].argc] = NULL;

	for (i = 0; i < evcmds[ev].argc; i++) {
		for (j = 0, p = evcmds[ev].args[i];
		    *p != '\0' && j < sizeof(buf) - 1; p++) {
			if (*p != '%') {
				buf[j++] = *p;
				continue;
			}
			/* *p == '%' */
			switch (p[1]) {
			case '%':
				buf[j++] = '%';
				break;
			case 'd':
				(void)strncpy(buf + j, dev->dev,
				    sizeof(buf) - j - 1);
				len = strlen(buf + j); j += len;
				break;
			case 'l':
				(void)strncpy(buf + j, dev->volid != NULL ? \
				    dev->volid : dev->dev,
				    sizeof(buf) - j - 1);
				len = strlen(buf + j); j += len;
				break;
			case 'm':
				(void)strncpy(buf + j, dev->mntpt,
				    sizeof(buf) - j - 1);
				len = strlen(buf + j); j += len;
				break;
			case 't':
				(void)strncpy(buf + j, dtype_to_name(dev->type),
				    sizeof(buf) - j - 1);
				len = strlen(buf + j); j += len;
				break;
			default:
				errx(EXIT_FAILURE,
				    "Unknown placeholder '%%%c'", p[1]);
			}
			p++;
		}
		buf[j] = '\0';
		if ((args[i] = strdup(buf)) == NULL)
			err(EXIT_FAILURE, "strdup()");
	}
	switch (vfork()) {
	case -1:
		err(EXIT_FAILURE, "vfork()");
	case  0:
		(void)execvp(args[0], args);
		err(EXIT_FAILURE, "execvp(%s)", args[0]);
	default:
		while (wait(NULL) == -1) {
			if (errno != EINTR)
				err(EXIT_FAILURE, "wait()");
		}
		for (i = 0; i < evcmds[ev].argc; i++)
			free(args[i]);
		free(args);
	}
}

static void
cleanpath(char *path)
{
	int   i;
	char *p;
	size_t len = strlen(path);

	while ((p = strchr(path++, '/')) != NULL) {
		while (p[1] == '/') {
			for (i = 0; i < len - (p - path); i++)
				p[i] = p[i + 1];
			p[--len] = 0;
			path = p + 1;
		}
	}
}

static const dsbmc_dev_t *
dev_from_mnt(const char *mnt)
{
	int   i;
	char  rpath1[PATH_MAX], rpath2[PATH_MAX];
	const dsbmc_dev_t **dls;

	if (realpath(mnt, rpath1) == NULL) {
		if (errno == ENOENT)
			return (NULL);
		err(EXIT_FAILURE, "realpath(%s)", mnt);
	}
	for (i = 0; i < dsbmc_get_devlist(&dls); i++) {
		if (!dls[i]->mounted)
			continue;
		if (realpath(dls[i]->mntpt, rpath2) == NULL)
			err(EXIT_FAILURE, "realpath(%s)", dls[i]->mntpt);
		if (strcmp(rpath1, rpath2) == 0)
			return (dls[i]);
	}
	return (NULL);
}

static char *
dtype_to_name(uint8_t type)
{
	int	    i;
	static char *empty = "";

	for (i = 0; i < sizeof(dtypes) / sizeof(dtypes[0]); i++) {
		if (dtypes[i].type == type)
			return (dtypes[i].name);
	}
	return (empty);
}

static void
cb(int code, const dsbmc_dev_t *d)
{
	(void)fputc('\r', stderr);
	if (code != 0)
		errx(EXIT_FAILURE, "Error: %s", dsbmc_errcode_to_str(code));
	exit(EXIT_SUCCESS);
}

static void
size_cb(int code, const dsbmc_dev_t *d)
{
	(void)fputc('\r', stderr);
	if (code != 0)
		errx(EXIT_FAILURE, "Error: %s", dsbmc_errcode_to_str(code));
	(void)printf("size=%lu:used=%lu:free=%lu\n", d->mediasize, d->used,
	    d->free);
	exit(EXIT_SUCCESS);
}

static void
list()
{
	int i;
	const dsbmc_dev_t **devlist;

	for (i = 0; i < dsbmc_get_devlist(&devlist); putchar('\n'), i++) {
		P(devlist[i], dev);
		P(devlist[i], volid);
		P(devlist[i], fsname);
		P(devlist[i], mntpt);
		(void)printf(":type=%s", dtype_to_name(devlist[i]->type));
	}
	exit(EXIT_SUCCESS);
}

static void
do_mount(const dsbmc_dev_t *dev)
{
	int ret;

	if ((ret = dsbmc_mount(dev)) == -1) {
		warnx("Error: dsbmc_mount(%s): %s", dev->dev,
		    dsbmc_errstr());
	} else if (ret > 0) {
		warnx("Mouting of %s failed: %s", dev->dev,
		    dsbmc_errcode_to_str(ret));
	}
}

static void
do_listen(bool automount)
{
	int	      i, fd, s;
	char	      path[PATH_MAX];
	fd_set	      fdset;
	dsbmc_event_t e;
	struct passwd *pw;
	const dsbmc_dev_t **dls;

	if (automount) {
		if ((pw = getpwuid(getuid())) == NULL)
			err(EXIT_FAILURE, "getpwuid()");
		endpwent();

		(void)snprintf(path, sizeof(path) - 1, "%s/%s", pw->pw_dir,
		    PATH_LOCKF);

		if ((fd = open(path, O_CREAT | O_WRONLY)) == -1)
			err(EXIT_FAILURE, "Couldn't open/create %s", path);
		if (lockf(fd, F_TLOCK, 0) == -1) {
			if (errno != EAGAIN)
				err(EXIT_FAILURE, "lockf()");
			errx(EXIT_FAILURE, "Another instance of 'dsbmc-cli " \
			    "-a' is already running.");
		}
		for (i = 0; i < dsbmc_get_devlist(&dls); i++) {
			if (!dls[i]->mounted) {
				do_mount(dls[i]);
				exec_event_command(EVENT_MOUNT, dls[i]);
			}
		}
	}
	for (s = dsbmc_get_fd();;) {
		FD_ZERO(&fdset); FD_SET(s, &fdset);

		if (select(s + 1, &fdset, 0, 0, 0) == -1) {
			if (errno != EINTR)
				err(EXIT_FAILURE, "select()");
			continue;
		}
		while (dsbmc_fetch_event(&e) > 0) {
			switch (e.type) {
			case DSBMC_EVENT_ADD_DEVICE:
				exec_event_command(EVENT_ADD, e.dev);
				if (automount) {
					do_mount(e.dev);
					exec_event_command(EVENT_MOUNT, e.dev);
				}
				break;
			case DSBMC_EVENT_DEL_DEVICE:
				exec_event_command(EVENT_REMOVE, e.dev);
				dsbmc_free_dev(e.dev);
				break;
			case DSBMC_EVENT_MOUNT:
				exec_event_command(EVENT_MOUNT, e.dev);
				break;
			case DSBMC_EVENT_UNMOUNT:
				exec_event_command(EVENT_UNMOUNT, e.dev);
				break;
			}
		}
		if (dsbmc_get_err(NULL) & DSBMC_ERR_LOST_CONNECTION)
			errx(EXIT_FAILURE, "%s", dsbmc_errstr());
	}
}

static void
usage()
{
	(void)fprintf(stderr,
	  "Usage: dsbmc-cli -L <event> <command> [arg ...] ; [ -L ...]\n"     \
	  "       dsbmc-cli -a [[-L <event> <command> [arg ...] ; [-L ...]]\n"\
	  "       dsbmc-cli {-e | -m | -s | -u | -v <speed>} <device>\n"      \
	  "       dsbmc-cli {-e | -u} <mount point>\n"			      \
	  "       dsbmc-cli -l\n"					      \
	  "       dsbmc-cli [-h]\n");
	exit(EXIT_FAILURE);
}

static void
help()
{
	(void)printf("Options:\n");
	PO("-L <event> <command> [arg ...] ;",
	    "Listen for <event>, and execute <command> every");
	PO("", "time the event is received. Possible events are");
	PO("", "mount, unmount, add, and remove.");
	PO("-a [-L ...]",
	    "Automount. Wait for devices added to the sys-");
	PO("", "tem, and mount them.");
	PO("-e <device>", "Eject <device>");
	PO("-e <mount point>", "Eject the device mounted on <mount point>");
	PO("-l", "List available devices supported by DSBMD.");
	PO("-m <device>", "Mount <device>");
	PO("-s <device>", "Query storage capacity of <device>");
	PO("-u <device>", "Unmount <device>");
	PO("-u <mount point>", "Unmount <device> mounted on <mount point>");
	PO("-v <speed> <device>",
	    "Set reading <speed> of the CD/DVD <device>");
	exit(EXIT_SUCCESS);
}

