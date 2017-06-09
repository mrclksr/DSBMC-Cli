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
#include "libdsbmc/libdsbmc.h"

static void list(void);
static void usage(void);
static void spinner(void);
static void cb(int code, const dsbmc_dev_t *d);
static void size_cb(int code, const dsbmc_dev_t *d);

int
main(int argc, char *argv[])
{
	int	      i, ch, speed;
	bool	      mflag, uflag, lflag, sflag, vflag, eflag;
	dsbmc_dev_t   *dev, **devlist;
	dsbmc_event_t e;

	eflag = mflag = uflag = lflag = sflag = vflag = false;
	while ((ch = getopt(argc, argv, "musehv:l")) != -1) {
		switch (ch) {
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
	dsbmc_dev_t **devlist;

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
usage()
{
	(void)fprintf(stderr, "Usage: dsbmc-cli -m|-u|-e|-s dev\n" \
			      "       dsbmc-cli -v speed dev\n" \
			      "       dsbmc-cli -l\n");
	exit(EXIT_FAILURE);
}

