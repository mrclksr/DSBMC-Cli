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
#include <inttypes.h>
#include <pwd.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <pthread.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "libdsbmc/libdsbmc.h"

#define MAX_BLISTSZ 256
#define PATH_LOCKF  ".dsbmc-cli.lock"

#define EXEC(dh, f) do {						  \
	if (f == -1)							  \
		errx(EXIT_FAILURE, "%s", dsbmc_errstr(dh));		  \
} while (0)

#define P(s, m)	do {							  \
	if (s->m != NULL)						  \
		printf("%s" #m"=%s", strcmp(#m, "dev") ? ":" : "", s->m); \
} while (0)

#define PU(s) fprintf(stderr, "%-*s%s %s\n", s[0] == '!' ? 0 : 7,	  \
	s[0] == '!' ? "Usage: " : "", PROGRAM, &s[(s[0] == '!' ? 1 : 0)])

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
static void do_listen(bool automount, bool autounmount);
static void cleanpath(char *path);
static void do_mount(const dsbmc_dev_t *dev);
static void cb(int code, const dsbmc_dev_t *d);
static void size_cb(int code, const dsbmc_dev_t *d);
static void add_event_command(char **argv, int *argskip);
static void exec_event_command(int ev, const dsbmc_dev_t *dev);
static void sighandler(int signo);
static void pdie(void);
static void remove_thread(pthread_t tid);
static void add_unmount_thread(const dsbmc_dev_t *dev);
static bool blacklisted(const dsbmc_dev_t *dev);
static void *auto_unmount(void *arg);
static char *dtype_to_name(uint8_t type);
static const dsbmc_dev_t *dev_from_mnt(const char *mnt);

static char	 *blist[MAX_BLISTSZ];
static size_t	 blistsz;
static size_t	 unmount_time;
static size_t	 ntids;
static dsbmc_t	 *dh;
static pthread_t *tids;
static pthread_mutex_t tl_mtx, dh_mtx;

int
main(int argc, char *argv[])
{
	int	      i, ch, speed;
	bool	      sflag, vflag, eflag, fflag, bflag, Uflag;
	bool	      Lflag, aflag, mflag, uflag, lflag, iflag;
	char	      *image, *p;
	const char    seq[] = "-|/-\\|/";
	struct stat   sb;
	dsbmc_event_t e;
	const dsbmc_dev_t *dev;

	fflag = lflag = sflag = vflag = bflag = Uflag = false;
	Lflag = aflag = eflag = mflag = uflag = iflag = false;
	while ((ch = getopt(argc, argv, "L:U:ab:fimusehv:l")) != -1) {
		switch (ch) {
		case 'L':
			Lflag = true;
			add_event_command(&argv[optind - 1], &optind);
			break;
		case 'U':
			Uflag = true;
			unmount_time = strtol(optarg, NULL, 10);
			break;
		case 'a':
			aflag = true;
			break;
		case 'b':
			bflag = true;
			for (p = optarg;
			    (p = strtok(p, ",")) != NULL &&
			    blistsz < MAX_BLISTSZ; p = NULL)
				blist[blistsz++] = p;
			if (blistsz >= MAX_BLISTSZ)
				warnx("MAX_BLISTSZ (%d) exceeded", MAX_BLISTSZ);
			break;
		case 'f':
			fflag = true;
			break;
		case 'i':
			iflag = true;
			break;
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
	
	if (Uflag && !aflag)
		usage();
	if ((aflag || Lflag) && (mflag || eflag || uflag || sflag || vflag))
		usage();
	if (!!mflag + !!uflag + !!eflag + !!sflag + !!vflag + !!iflag > 1)
		usage();
	if (bflag && !aflag)
		usage();
	if ((dh = dsbmc_alloc_handle()) == NULL)
		err(EXIT_FAILURE, "dsbmc_alloc_handle()");
	if (dsbmc_connect(dh) == -1)
		errx(EXIT_FAILURE, "%s", dsbmc_errstr(dh));
	if (lflag)
		list();
	else if (aflag)
		do_listen(true, Uflag);
	else if (Lflag)
		do_listen(false, false);
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
		} else if ((dev = dsbmc_dev_from_name(dh, argv[0])) == NULL)
			errx(EXIT_FAILURE, "No such device '%s'", argv[0]);
	} else if (iflag) {
		if ((image = realpath(argv[0], NULL)) == NULL)
			err(EXIT_FAILURE, "realpath(%s)", argv[0]);
	}
	if (mflag)
		EXEC(dh, dsbmc_mount_async(dh, dev, cb));
	else if (sflag)
		EXEC(dh, dsbmc_size_async(dh, dev, size_cb));
	else if (uflag)
		EXEC(dh, dsbmc_unmount_async(dh, dev, fflag, cb));
	else if (eflag)
		EXEC(dh, dsbmc_eject_async(dh, dev, fflag, cb));
	else if (vflag)
		EXEC(dh, dsbmc_set_speed_async(dh, dev, speed, cb));
	else if (iflag) {
		if (stat(image, &sb) == -1)
			err(EXIT_FAILURE, "stat(%s)", image);
		if (!S_ISREG(sb.st_mode))
			errx(EXIT_FAILURE, "%s is not a regular file", image);
		EXEC(dh, dsbmc_mdattach_async(dh, image, cb));
	} else
		usage();
	for (; dsbmc_fetch_event(dh, &e) != -1; usleep(500)) {
		for (i = 0; i < sizeof(seq) - 1; i++)
			(void)fprintf(stderr, "\r%c", seq[i]);
	}
	if (dsbmc_get_err(dh, NULL))
		errx(EXIT_FAILURE, "%s", dsbmc_errstr(dh));
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
	if (--n < 1 || !terminated) {
		if (!terminated)
			warnx("Missing terminating ';'");
		else
			warnx("Command not defined");
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

		if (*p != '\0') {
			if (i > 0) {
				errx(EXIT_FAILURE, "Error: Length of " \
				     "command argument %d, '%s ...', " \
				     "exceeds the limit of %lu bytes!",
				     i, buf, sizeof(buf) - 1);
			} else {
				errx(EXIT_FAILURE, "Error: Length of " \
				     "command, '%s ...', exceeds the " \
				     "limit of %lu bytes!",
				    buf, sizeof(buf) - 1);
			}
		}
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
	const dsbmc_dev_t *dev;

	if (realpath(mnt, rpath1) == NULL) {
		if (errno == ENOENT)
			return (NULL);
		err(EXIT_FAILURE, "realpath(%s)", mnt);
	}
	for (i = 0; (dev = dsbmc_next_dev(dh, &i, false)) != NULL;) {
		if (!dev->mounted)
			continue;
		if (realpath(dev->mntpt, rpath2) == NULL)
			err(EXIT_FAILURE, "realpath(%s)", dev->mntpt);
		if (strcmp(rpath1, rpath2) == 0)
			return (dev);
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

static bool
blacklisted(const dsbmc_dev_t *dev)
{
	int  i;
	bool listed;
	char *buf, *path;

	path = NULL;
	for (i = 0, listed = false; i < blistsz && !listed; i++) {
		if (strncmp(blist[i], "volid=", 6) == 0) {
			if (dev->volid != NULL &&
			    strcasecmp(dev->volid, blist[i] + 6) == 0)
				return (true);
		} else if (strncmp(blist[i], _PATH_DEV, strlen(_PATH_DEV)) != 0) {
			buf = malloc(strlen(blist[i]) + sizeof(_PATH_DEV));
			if (buf == NULL)
				err(EXIT_FAILURE, "malloc()");
			(void)sprintf(buf, "%s%s", _PATH_DEV, blist[i]);
			if ((path = realpath(buf, NULL)) == NULL)
				warn("realpath(%s)", buf);
			free(buf);
		} else if ((path = realpath(blist[i], NULL)) == NULL)
			warn("realpath(%s)", blist[i]);
		if (path != NULL) {
			if (strcmp(dev->dev, path) == 0)
				listed = true;
			free(path);
		}
	}
	return (listed);
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
	(void)printf("size=%" PRId64 ":used=%" PRId64 ":free=%" PRId64 "\n",
	    d->mediasize, d->used, d->free);
	exit(EXIT_SUCCESS);
}

static void
list()
{
	int i;
	const dsbmc_dev_t *dev;

	for (i = 0; (dev = dsbmc_next_dev(dh, &i, false)) != NULL;) {
		P(dev, dev);
		P(dev, volid);
		P(dev, fsname);
		P(dev, mntpt);
		(void)printf(":type=%s\n", dtype_to_name(dev->type));
	}
	exit(EXIT_SUCCESS);
}

static void
do_mount(const dsbmc_dev_t *dev)
{
	int ret;

	if ((ret = dsbmc_mount(dh, dev)) == -1) {
		warnx("Error: dsbmc_mount(%s): %s", dev->dev,
		    dsbmc_errstr(dh));
	} else if (ret > 0) {
		warnx("Mouting of %s failed: %s", dev->dev,
		    dsbmc_errcode_to_str(ret));
	}
}

static void
sighandler(int signo)
{
	return;
}

static void
remove_thread(pthread_t tid)
{
	int i;

	(void)pthread_mutex_lock(&tl_mtx);
	for (i = 0; i < ntids && tids[i] != tid; i++)
		;
	for (; i < ntids - 1; i++)
		tids[i] = tids[i + 1];
	ntids--;
	(void)pthread_mutex_unlock(&tl_mtx);
}

static void
pdie()
{
	(void)pthread_mutex_unlock(&dh_mtx);
	remove_thread(pthread_self());
	pthread_exit(NULL);
}

static void
add_unmount_thread(const dsbmc_dev_t *dev)
{
	int *id;

	(void)pthread_mutex_lock(&tl_mtx);
	tids = realloc(tids, sizeof(pthread_t) * (ntids + 1));
	if (tids == NULL)
		err(EXIT_FAILURE, "realloc()");
	if ((id = malloc(sizeof(int))) == NULL)
		err(EXIT_FAILURE, "malloc()");
	*id = dev->id;
	if (pthread_create(&tids[ntids], NULL, auto_unmount, id) != 0)
		err(EXIT_FAILURE, "pthread_create()");
	(void)pthread_detach(tids[ntids++]);
	(void)pthread_mutex_unlock(&tl_mtx);
}

static void *
auto_unmount(void *arg)
{
	int	     ret, id;
	unsigned int rem;
	const dsbmc_dev_t *dev;

	id  = *((int *)arg); free(arg);
	dev = dsbmc_dev_from_id(dh, id);
	if (dev == NULL)
		pdie();
	for (;;) {
		rem = unmount_time;
		do {
			rem = sleep(rem);
			if (rem != 0 && errno == EINTR) {
				if (!dev->mounted) {
					/*
					 * Device was unmounted from another
					 * process. There's nothing left to do.
					 */
					pdie();
				}
			}
		} while (rem != 0);

		(void)pthread_mutex_lock(&dh_mtx);
		if (dev->removed || !dev->mounted) {
			if (dev->removed)
				dsbmc_free_dev(dh, dev);
			pdie();
		}
		if ((ret = dsbmc_unmount(dh, dev, false)) == -1) {
			if (ret & DSBMC_ERR_FATAL)
				err(EXIT_FAILURE, "%s", dsbmc_errstr(dh));
			else
				warnx("%s", dsbmc_errstr(dh));
		} else if (ret > 0) {
			if (ret != EBUSY && ret != DSBMC_ERR_DEVICE_BUSY)
				warnx("%s", dsbmc_errcode_to_str(ret));
		} else {
			exec_event_command(EVENT_UNMOUNT, dev);
			pdie();
		}
		(void)pthread_mutex_unlock(&dh_mtx);
	}
}

static void
do_listen(bool automount, bool autounmount)
{
	int	      i, fd, s;
	char	      path[PATH_MAX];
	fd_set	      fdset;
	dsbmc_event_t e;
	struct passwd *pw;
	const dsbmc_dev_t *dev;

	if (autounmount) {
		if (signal(SIGUSR1, sighandler) == SIG_ERR)
			err(EXIT_FAILURE, "signal()");
		if (pthread_mutex_init(&tl_mtx, NULL) == -1)
			err(EXIT_FAILURE, "pthread_mutex_init()");
		if (pthread_mutex_init(&dh_mtx, NULL) == -1)
			err(EXIT_FAILURE, "pthread_mutex_init()");
	}
	if (automount) {
		if ((pw = getpwuid(getuid())) == NULL)
			err(EXIT_FAILURE, "getpwuid()");
		endpwent();

		(void)snprintf(path, sizeof(path) - 1, "%s/%s", pw->pw_dir,
		    PATH_LOCKF);

		if ((fd = open(path, O_CREAT|O_WRONLY, S_IWUSR|S_IRUSR)) == -1)
			err(EXIT_FAILURE, "Couldn't open/create %s", path);
		if (lockf(fd, F_TLOCK, 0) == -1) {
			if (errno != EAGAIN)
				err(EXIT_FAILURE, "lockf()");
			errx(EXIT_FAILURE, "Another instance of 'dsbmc-cli " \
			    "-a' is already running.");
		}
		(void)pthread_mutex_lock(&dh_mtx);
		for (i = 0; (dev = dsbmc_next_dev(dh, &i, false)) != NULL;) {
			if (!(dev->cmds & DSBMC_CMD_MOUNT))
				continue;
			if (dev->mounted)
				continue;
			if (blacklisted(dev))
				continue;
			do_mount(dev);
			exec_event_command(EVENT_MOUNT, dev);
			if (autounmount)
				add_unmount_thread(dev);
		}
		(void)pthread_mutex_unlock(&dh_mtx);
	}
	for (s = dsbmc_get_fd(dh);;) {
		FD_ZERO(&fdset); FD_SET(s, &fdset);

		if (select(s + 1, &fdset, 0, 0, 0) == -1) {
			if (errno != EINTR)
				err(EXIT_FAILURE, "select()");
			continue;
		}
		(void)pthread_mutex_lock(&dh_mtx);
		while (dsbmc_fetch_event(dh, &e) > 0) {
			switch (e.type) {
			case DSBMC_EVENT_ADD_DEVICE:
				exec_event_command(EVENT_ADD, e.dev);
				if (automount &&
				    (e.dev->cmds & DSBMC_CMD_MOUNT) &&
				    !blacklisted(e.dev)) {
					do_mount(e.dev);
					exec_event_command(EVENT_MOUNT, e.dev);
				}
				if (autounmount)
					add_unmount_thread(e.dev);
				break;
			case DSBMC_EVENT_DEL_DEVICE:
				exec_event_command(EVENT_REMOVE, e.dev);
				if (!autounmount)
					dsbmc_free_dev(dh, e.dev);	
				break;
			case DSBMC_EVENT_MOUNT:
				exec_event_command(EVENT_MOUNT, e.dev);
				break;
			case DSBMC_EVENT_UNMOUNT:
				/*
				 * Wake up all threads, so they can check the
				 * mount status of their devices.
				 */
				for (i = 0; i < ntids; i++)
					(void)pthread_kill(tids[i], SIGUSR1);
				exec_event_command(EVENT_UNMOUNT, e.dev);
				break;
			}
		}
		if (dsbmc_get_err(dh, NULL) & DSBMC_ERR_LOST_CONNECTION) {
			errx(EXIT_FAILURE, "Lost connection %s",
			    dsbmc_errstr(dh));
		}
		(void)pthread_mutex_unlock(&dh_mtx);
	}
}

static void
usage()
{
	PU("!-L <event> <command> [arg ...] ; [ -L ...]");
	PU("-a [-b dev1,dev2,...] [-U <time>] [[-L <event> <command> " \
	    "[arg ...] ; [-L ...]]");
	PU("{{-e | -u} [-f] | {-m | -s | -v <speed>}} <device>");
	PU("{-e | -u} [-f] <mount point>");
	PU("-i <disk image>");
	PU("-l");
	PU("[-h]");
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
	PO("-U <time>", "Auto-unmount. Try to unmount automounted de-");
	PO("", "vices every <time> seconds.");
	PO("-a [-L ...]",
	    "Automount. Wait for devices added to the sys-");
	PO("", "tem, and mount them.");
	PO("-b dev1,dev2,...", "Define a comma-separated list of devices");
	PO("", "and/or volume labels to ignore if the -a option");
	PO("", "is given. Volume labels must be prefixed by");
	PO("", "\"volid=\".");
	PO("-e <device>", "Eject <device>");
	PO("-e <mount point>", "Eject the device mounted on <mount point>");
	PO("-f", "Force operation even if device is busy.");
	PO("-i <disk image>", "Create a memory disk to access the given image.");
	PO("-l", "List available devices supported by DSBMD.");
	PO("-m <device>", "Mount <device>");
	PO("-s <device>", "Query storage capacity of <device>");
	PO("-u <device>", "Unmount <device>");
	PO("-u <mount point>", "Unmount <device> mounted on <mount point>");
	PO("-v <speed> <device>",
	    "Set reading <speed> of the CD/DVD <device>");
	exit(EXIT_SUCCESS);
}

