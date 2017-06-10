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
#include <err.h>
#include <errno.h>
#include <sys/select.h>
#include "libdsbmc/libdsbmc.h"

static void list(void);
static void usage(void);
static void spinner(void);
static void cb(int code, const dsbmc_dev_t *d);
static void size_cb(int code, const dsbmc_dev_t *d);
static void automount(void);

int
main(int argc, char *argv[])
{
	int	      i, ch, speed;
	bool	      aflag, mflag, uflag, lflag, sflag, vflag, eflag;
	dsbmc_event_t e;
	const dsbmc_dev_t *dev, **devlist;

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
		for (i = 0, dev = NULL;
		    i < dsbmc_get_devlist(&devlist) && dev == NULL; i++) {
			if (strcmp(argv[0], devlist[i]->dev) == 0)
				dev = devlist[i];
		}
		if (dev == NULL)
			errx(EXIT_FAILURE, "No such device '%s'", argv[0]);
	}
	if (mflag) {
		if (dsbmc_mount_async(dev, cb) == -1)
			errx(EXIT_FAILURE, "%s", dsbmc_errstr());
	} else if (sflag) {
		if (dsbmc_size_async(dev, size_cb) == -1)
			errx(EXIT_FAILURE, "%s", dsbmc_errstr());
	} else if (uflag) {
		if (dsbmc_unmount_async(dev, cb) == -1)
			errx(EXIT_FAILURE, "%s", dsbmc_errstr());
	} else if (eflag) {
		if (dsbmc_eject_async(dev, cb) == -1)
			errx(EXIT_FAILURE, "%s", dsbmc_errstr());
	} else if (vflag) {
		if (dsbmc_set_speed_async(dev, speed, cb) == -1)
			errx(EXIT_FAILURE, "%s", dsbmc_errstr());
		return (EXIT_FAILURE);
	} else
		usage();
	for (; dsbmc_fetch_event(&e) != -1; usleep(500))
		spinner();
	if (dsbmc_get_err(NULL))
		errx(EXIT_FAILURE, "%s", dsbmc_errstr());
	return (EXIT_SUCCESS);
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
spinner()
{
	int	   i;
	const char seq[] = "-|/-\\|/";

	for (i = 0; i < sizeof(seq) - 1; i++)
		(void)fprintf(stderr, "\r%c", seq[i]);
}

static void
list()
{
	int i;
	const dsbmc_dev_t **devlist;

	for (i = 0; i < dsbmc_get_devlist(&devlist); i++) {
		(void)printf("dev=%s", devlist[i]->dev);
		if (devlist[i]->volid != NULL)
			(void)printf(":volid=%s", devlist[i]->volid);
		if (devlist[i]->fsname != NULL)
			(void)printf(":fs=%s", devlist[i]->fsname);
		if (devlist[i]->mntpt != NULL)
			(void)printf(":mntpt=%s", devlist[i]->mntpt);
		(void)putchar('\n');
	}
	exit(EXIT_SUCCESS);
}

static void
automount()
{
	int	      fd, ret;
	fd_set	      fdset;
	dsbmc_event_t e;

	for (fd = dsbmc_get_fd();;) {
		FD_ZERO(&fdset); FD_SET(fd, &fdset);
		if (select(fd + 1, &fdset, 0, 0, 0) == -1) {
			if (errno != EINTR)
				err(EXIT_FAILURE, "select()");
			continue;
		}
		while (dsbmc_fetch_event(&e) > 0) {
			if (e.type == DSBMC_EVENT_ADD_DEVICE) {
				if ((ret = dsbmc_mount(e.dev)) == -1) {
					warnx("Error: dsbmc_mount(%s): %s",
					    e.dev->dev, dsbmc_errstr());
				} else if (ret > 0) {
					warnx("Mouting of %s failed: %s",
					    e.dev->dev,
					    dsbmc_errcode_to_str(ret));
				}
			} else if (e.type == DSBMC_EVENT_DEL_DEVICE)
				dsbmc_free_dev(e.dev);
		}
		if (dsbmc_get_err(NULL) & DSBMC_ERR_LOST_CONNECTION)
			errx(EXIT_FAILURE, "%s", dsbmc_errstr());
	}
}

static void
usage()
{
	(void)fprintf(stderr, "Usage: dsbmc-cli -m|-u|-e|-s dev\n" 	\
			      "       dsbmc-cli -v speed dev\n" 	\
			      "       dsbmc-cli -l\n" 			\
			      "       dsbmc-cli -a\n\n"			\
			      "Flags:\n"				\
			      "-m     Mount device\n"			\
			      "-u     Unmount device\n"			\
			      "-e     Eject device\n"			\
			      "-s     Query storage capacity\n"		\
			      "-v     Set CD/DVD reading speed\n"	\
			      "-l     List devices\n"			\
			      "-a     Automount\n");
	exit(EXIT_FAILURE);
}

