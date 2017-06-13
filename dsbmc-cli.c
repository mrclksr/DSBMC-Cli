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

static void list(void);
static void usage(void);
static void automount(void);
static void do_mount(const dsbmc_dev_t *dev);
static void cb(int code, const dsbmc_dev_t *d);
static void size_cb(int code, const dsbmc_dev_t *d);
static void cleanpath(char *path);
static const dsbmc_dev_t *dev_from_mnt(const char *mnt);

int
main(int argc, char *argv[])
{
	int	      i, ch, speed;
	bool	      aflag, mflag, uflag, lflag, sflag, vflag, eflag;
	const char    seq[] = "-|/-\\|/";
	struct stat   sb;
	dsbmc_event_t e;
	const dsbmc_dev_t *dev, **dls;

	aflag = eflag = mflag = uflag = lflag = sflag = vflag = false;
	while ((ch = getopt(argc, argv, "amusehv:l")) != -1) {
		switch (ch) {
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
		automount();
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
automount()
{
	int	      i, fd, s;
	char	      path[PATH_MAX];
	fd_set	      fdset;
	dsbmc_event_t e;
	struct passwd *pw;
	const dsbmc_dev_t **dls;

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
		if (!dls[i]->mounted)
			do_mount(dls[i]);
	}
	for (s = dsbmc_get_fd();;) {
		FD_ZERO(&fdset); FD_SET(s, &fdset);

		if (select(s + 1, &fdset, 0, 0, 0) == -1) {
			if (errno != EINTR)
				err(EXIT_FAILURE, "select()");
			continue;
		}
		while (dsbmc_fetch_event(&e) > 0) {
			if (e.type == DSBMC_EVENT_ADD_DEVICE)
				do_mount(e.dev);
			else if (e.type == DSBMC_EVENT_DEL_DEVICE)
				dsbmc_free_dev(e.dev);
		}
		if (dsbmc_get_err(NULL) & DSBMC_ERR_LOST_CONNECTION)
			errx(EXIT_FAILURE, "%s", dsbmc_errstr());
	}
}

static void
usage()
{
	(void)fprintf(stderr,
	    "Usage: dsbmc-cli {-e | -m | -s | -u | -v <speed>} <device>\n" \
	    "       dsbmc-cli {-e | -u} <mount point>\n"		   \
	    "       dsbmc-cli {-a | -l}\n"				   \
	    "       dsbmc-cli [-h]\n\n"					   \
	    "Options:\n"						   \
	    "-m     Mount device\n"					   \
	    "-u     Unmount device\n"					   \
	    "-e     Eject device\n"					   \
	    "-s     Query storage capacity\n"				   \
	    "-v     Set CD/DVD reading speed\n"				   \
	    "-l     List devices\n"					   \
	    "-a     Automount\n");
	exit(EXIT_FAILURE);
}

