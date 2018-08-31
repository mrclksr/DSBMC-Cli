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
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include "libdsbmc.h"

#define CMDQMAXSZ	  32
#define PATH_DSBMD_SOCKET "/var/run/dsbmd.socket"
#define ERR_SYS_FATAL	  (DSBMC_ERR_SYS|DSBMC_ERR_FATAL)

#define ERROR(ret, error, prepend, fmt, ...) do {		\
	set_error(error, prepend, fmt, ##__VA_ARGS__);		\
	return (ret);						\
} while (0)

#define VALIDATE(dev) do {					\
	if (dev == NULL || dev->removed)			\
		ERROR(-1, DSBMC_ERR_INVALID_DEVICE, false,	\
		    "Invalid device");				\
} while (0)

#define LOOKUP_DEV(arg, dev) do {				\
	VALIDATE(arg);						\
	dev = device_from_id(arg->id);				\
	VALIDATE(dev);						\
} while (0)

static struct dsbmc_sender_s {
	int	     id;	/* DSBMC_CMD_.. */
	int	     retcode;	/* Reply code from DSBMD */
	char	    *cmd;	/* Command string */
	dsbmc_dev_t *dev;
	void (*callback)(int retcode, const dsbmc_dev_t *dev);
} dsbmc_sender[CMDQMAXSZ];

static struct event_queue_s {
	int  n;			/* # of events in queue */
	int  i;			/* Current index */
#define MAXEQSZ	64
	char *ln[MAXEQSZ];
} event_queue = { 0, 0 };

struct dsbmdevent_s {
	char	    type;	/* Event type. */
	char	    *command;	/* In case of a reply, the executed command. */
	int	    mntcmderr;	/* Return code of external mount command. */
	int	    code;	/* The error code */
	uint64_t    mediasize;	/* For "size" command. */
	uint64_t    free;	/* 	 ""	       */
	uint64_t    used;	/* 	 ""	       */
	dsbmc_dev_t devinfo;	/* For Add/delete/mount/unmount message. */
} dsbmdevent;

typedef union val_u val_t;
struct dsbmdkeyword_s {
	const char *key;
	u_char	   type;
#define	KWTYPE_CHAR	0x01
#define	KWTYPE_STRING	0x02
#define	KWTYPE_COMMANDS	0x03
#define KWTYPE_INTEGER 	0x04
#define KWTYPE_UINT64	0x05
#define KWTYPE_UINT8	0x06
#define	KWTYPE_DSKTYPE	0x07
#define KWTYPE_MSGTYPE	0x08
	union val_u {
		int	 *integer;
		char	 *character;
		char	 **string;
		uint8_t  *uint8;
		uint64_t *uint64;
	} val;
} dsbmdkeywords[] = {
	{ "+",		KWTYPE_CHAR,	 (val_t)&dsbmdevent.type	   },
	{ "-",		KWTYPE_CHAR,	 (val_t)&dsbmdevent.type	   },
	{ "E",		KWTYPE_CHAR,	 (val_t)&dsbmdevent.type	   },
	{ "O",		KWTYPE_CHAR,	 (val_t)&dsbmdevent.type	   },
	{ "M",		KWTYPE_CHAR,	 (val_t)&dsbmdevent.type	   },
	{ "U",		KWTYPE_CHAR,	 (val_t)&dsbmdevent.type	   },
	{ "V",		KWTYPE_CHAR,	 (val_t)&dsbmdevent.type	   },
	{ "S",		KWTYPE_CHAR,	 (val_t)&dsbmdevent.type	   },
	{ "=",		KWTYPE_CHAR,	 (val_t)&dsbmdevent.type	   },
	{ "command=",	KWTYPE_STRING,	 (val_t)&dsbmdevent.command	   },
	{ "dev=",	KWTYPE_STRING,	 (val_t)&dsbmdevent.devinfo.dev    },
	{ "fs=",	KWTYPE_STRING,	 (val_t)&dsbmdevent.devinfo.fsname },
	{ "volid=",	KWTYPE_STRING,	 (val_t)&dsbmdevent.devinfo.volid  },
	{ "mntpt=",	KWTYPE_STRING,	 (val_t)&dsbmdevent.devinfo.mntpt  },
	{ "type=",	KWTYPE_DSKTYPE,	 (val_t)&dsbmdevent.devinfo.type   },
	{ "speed=",	KWTYPE_UINT8,	 (val_t)&dsbmdevent.devinfo.speed  },
	{ "code=",	KWTYPE_INTEGER,	 (val_t)&dsbmdevent.code	   },
	{ "cmds=",	KWTYPE_COMMANDS, (val_t)(char *)0		   },
	{ "mntcmderr=",	KWTYPE_INTEGER,	 (val_t)&dsbmdevent.mntcmderr	   },
	{ "mediasize=", KWTYPE_UINT64,	 (val_t)&dsbmdevent.mediasize	   },
	{ "used=",	KWTYPE_UINT64,	 (val_t)&dsbmdevent.used	   },
	{ "free=",	KWTYPE_UINT64,	 (val_t)&dsbmdevent.free	   }
};
#define NKEYWORDS (sizeof(dsbmdkeywords) / sizeof(struct dsbmdkeyword_s))

static const struct cmdtbl_s {
	char	*name;
	uint8_t cmd;
} cmdtbl[] = {
	{ "mount",    DSBMC_CMD_MOUNT    },
	{ "unmount",  DSBMC_CMD_UNMOUNT  },
	{ "eject",    DSBMC_CMD_EJECT    },
	{ "speed",    DSBMC_CMD_SPEED    },
	{ "size",     DSBMC_CMD_SIZE     },
	{ "mdattach", DSBMC_CMD_MDATTACH }
};
#define NCMDS (sizeof(cmdtbl) / sizeof(struct cmdtbl_s))

/*
 * Struct to assign disk type strings to disktype IDs.
 */
static const struct disktypetbl_s {
        char   *name;
        uint8_t type;
} disktypetbl[] = {
        { "AUDIOCD", DSBMC_DT_AUDIOCD },
        { "DATACD",  DSBMC_DT_DATACD  },
        { "RAWCD",   DSBMC_DT_RAWCD   },
        { "USBDISK", DSBMC_DT_USBDISK },
        { "FLOPPY",  DSBMC_DT_FLOPPY  },
        { "DVD",     DSBMC_DT_DVD     },
        { "VCD",     DSBMC_DT_VCD     },
        { "SVCD",    DSBMC_DT_SVCD    },
        { "HDD",     DSBMC_DT_HDD     },
	{ "MMC",     DSBMC_DT_MMC     },
	{ "MTP",     DSBMC_DT_MTP     },
	{ "PTP",     DSBMC_DT_PTP     }
};
#define NDSKTYPES (sizeof(disktypetbl) / sizeof(struct disktypetbl_s))

/*
 * Error code translation.
 */
static const struct errmsg_s {
	int  code;
	char *msg;
} errmsgs[] = {
	{ DSBMC_ERR_ALREADY_MOUNTED,	"Device already mounted"   },
	{ DSBMC_ERR_PERMISSION_DENIED,	"Permission denied"	   },
	{ DSBMC_ERR_NOT_MOUNTED,	"Device not mounted"	   },
	{ DSBMC_ERR_DEVICE_BUSY,	"Device busy"		   },
	{ DSBMC_ERR_NO_SUCH_DEVICE,	"No such device"	   },
	{ DSBMC_ERR_NOT_EJECTABLE,	"Device not ejectable"	   },
	{ DSBMC_ERR_UNKNOWN_COMMAND,	"Unknown command"	   },
	{ DSBMC_ERR_UNKNOWN_OPTION,	"Unknown option"	   },
	{ DSBMC_ERR_SYNTAX_ERROR,	"Syntax error"		   },
	{ DSBMC_ERR_NO_MEDIA,		"No media"		   },
	{ DSBMC_ERR_UNKNOWN_FILESYSTEM,	"Unknown filesystem"	   },
	{ DSBMC_ERR_UNKNOWN_ERROR,	"Unknown error"		   },
	{ DSBMC_ERR_MNTCMD_FAILED,	"Mount command failed"	   },
	{ DSBMC_ERR_INVALID_ARGUMENT,	"Invalid argument"	   },
	{ DSBMC_ERR_MAX_CONN_REACHED,	"Max. number of connections reached" },
	{ DSBMC_ERR_STRING_TOO_LONG,	"Command string too long"  },
	{ DSBMC_ERR_BAD_STRING,		"Invalid command string"   },
	{ DSBMC_ERR_TIMEOUT,		"Timeout"		   },
	{ DSBMC_ERR_NOT_A_FILE,		"Not a regular file"	   },
};
#define NERRMSGS (sizeof(errmsgs) / sizeof(struct errmsg_s))

static int	   uconnect(const char *path);
static int	   send_string(const char *str);
static int	   push_event(const char *e);
static int	   parse_event(const char *str);
static int	   process_event(char *buf);
static int	   dsbmc_send_async(dsbmc_dev_t *,
			void (*cb)(int, const dsbmc_dev_t *), const char *cmd, ...);
static int	   dsbmc_send(const char *cmd, ...);
static void	   dsbmc_clearerr();
static void	   set_error(int error, bool prepend, const char *fmt, ...);
static void	   del_device(const char *);
static void	   shuffle(void);
static void	   cleanup(void);
static char	   *readln(void);
static char	   *read_event(bool block);
static char	   *pull_event(void);
static dsbmc_dev_t *add_device(const dsbmc_dev_t *d);
static dsbmc_dev_t *lookup_device(const char *dev);
static dsbmc_dev_t *device_from_id(int id);

static int    dsbmd, _error, ndevs, cmdqsz;
static char   *lnbuf, errormsg[_POSIX2_LINE_MAX];
static size_t rd, bufsz, slen;
static pthread_mutex_t mutex;

#define MAXDEVS	64
dsbmc_dev_t *devs[MAXDEVS];

int
dsbmc_mount(const dsbmc_dev_t *d)
{
	int	     ret;
	dsbmc_dev_t *dev;

	LOOKUP_DEV(d, dev);
	if ((ret = dsbmc_send("mount %s\n", dev->dev)) == 0) {
		if (dsbmdevent.devinfo.mntpt == NULL) {
			ERROR(-1, DSBMC_ERR_UNKNOWN_ERROR, false,
			    "mntpt == NULL");
		}
		dev->mounted = true; free(dev->mntpt);
		dev->mntpt = strdup(dsbmdevent.devinfo.mntpt);
		if (dev->mntpt == NULL)
			ERROR(-1, ERR_SYS_FATAL, false, "strdup()");
	}
	return (ret);
}

int
dsbmc_unmount(const dsbmc_dev_t *d, bool force)
{
	int ret;
	dsbmc_dev_t *dev;

	LOOKUP_DEV(d, dev);
	if ((ret = dsbmc_send(force ? "unmount -f %s\n" : "unmount %s\n",
	    d->dev)) == 0) {
		dev->mounted = false;
	}
	return (ret);
}

int
dsbmc_eject(const dsbmc_dev_t *d, bool force)
{
	dsbmc_dev_t *dev;

	LOOKUP_DEV(d, dev);
	return (dsbmc_send(force ? "eject -f %s\n" : "eject %s\n", d->dev));
}

int
dsbmc_size(const dsbmc_dev_t *d)
{
	int ret;
	dsbmc_dev_t *dev;

	LOOKUP_DEV(d, dev);
	if ((ret = dsbmc_send("size %s\n", d->dev)) == 0) {
		dev->mediasize = dsbmdevent.mediasize;
		dev->used = dsbmdevent.used;
		dev->free = dsbmdevent.free;
	}
	return (ret);
}

int
dsbmc_set_speed(const dsbmc_dev_t *d, int speed)
{
	dsbmc_dev_t *dev;

	LOOKUP_DEV(d, dev);
	return (dsbmc_send("speed %s %d\n", dev->dev, speed));
}

int
dsbmc_mdattach(const char *image)
{
	return (dsbmc_send("mdattach %s\n", image));
}

int
dsbmc_mount_async(const dsbmc_dev_t *d, void (*cb)(int, const dsbmc_dev_t *))
{
	dsbmc_dev_t *dev;

	LOOKUP_DEV(d, dev);
	return (dsbmc_send_async(dev, cb, "mount %s\n", dev->dev));
}

int
dsbmc_unmount_async(const dsbmc_dev_t *d, bool force,
	void (*cb)(int, const dsbmc_dev_t *))
{
	dsbmc_dev_t *dev;

	LOOKUP_DEV(d, dev);
	return (dsbmc_send_async(dev, cb, force ? "unmount -f %s\n" : \
	    "unmount %s\n", dev->dev));
}

int
dsbmc_eject_async(const dsbmc_dev_t *d, bool force,
	void (*cb)(int, const dsbmc_dev_t *))
{
	dsbmc_dev_t *dev;

	LOOKUP_DEV(d, dev);
	return (dsbmc_send_async(dev, cb, force ? "eject -f %s\n" : \
	    "eject %s\n", dev->dev));
}

int
dsbmc_set_speed_async(const dsbmc_dev_t *d, int speed,
    void (*cb)(int, const dsbmc_dev_t *))
{
	dsbmc_dev_t *dev;

	LOOKUP_DEV(d, dev);
	return (dsbmc_send_async(dev, cb, "speed %s %d\n", dev->dev, speed));
}

int
dsbmc_size_async(const dsbmc_dev_t *d, void (*cb)(int, const dsbmc_dev_t *))
{
	dsbmc_dev_t *dev;

	LOOKUP_DEV(d, dev);
	return (dsbmc_send_async(dev, cb, "size %s\n", dev->dev));
}

int
dsbmc_mdattach_async(const char *image, void (*cb)(int, const dsbmc_dev_t *))
{
	return (dsbmc_send_async(NULL, cb, "mdattach %s\n", image));
}

void
dsbmc_disconnect()
{
	(void)dsbmc_send("quit\n");
	cleanup();
}

void
dsbmc_free_dev(const dsbmc_dev_t *dev)
{
	if (dev == NULL || !dev->removed)
		return;
	del_device(dev->dev);
}

static void
cleanup()
{
	while (ndevs > 0) 
		del_device(devs[0]->dev);
	while (pull_event() != NULL)
		;
	(void)close(dsbmd);
	(void)pthread_mutex_destroy(&mutex);
	free(lnbuf);
	lnbuf = NULL; rd = slen = bufsz = 0;
}

int
dsbmc_get_err(const char **errmsg)
{
	if (!_error)
		return (0);
	if (errmsg != NULL)
		*errmsg = errormsg;
	return (_error);
}

int
dsbmc_get_fd()
{
	return (dsbmd);
}

const char *
dsbmc_errstr()
{
	return (errormsg);
}

const char *
dsbmc_errcode_to_str(int code)
{
	int i;

	if (code < (1 << 8))
		return (strerror(code));
	for (i = 0; i < NERRMSGS; i++) {
		if (errmsgs[i].code == code)
			return (errmsgs[i].msg);
	}
	return (NULL);
}

int
dsbmc_fetch_event(dsbmc_event_t *ev)
{
	int  error;
	char *e;

	(void)pthread_mutex_lock(&mutex);
	dsbmc_clearerr();
	while ((e = read_event(false)) != NULL) {
		if (push_event(e) == -1) {
			(void)pthread_mutex_unlock(&mutex);
			ERROR(-1, 0, true, "push_event()");
		}
	}
	if (dsbmc_get_err(NULL) != 0) {
		(void)pthread_mutex_unlock(&mutex);
		ERROR(-1, 0, true, "read_event()");
	}
	if ((e = pull_event()) == NULL) {
		(void)pthread_mutex_unlock(&mutex);
		return (0);
	}
	if ((error = process_event(e)) == 1) {
		ev->type = dsbmdevent.type;
		ev->code = dsbmdevent.code;
		if (dsbmdevent.devinfo.dev != NULL)
			ev->dev = lookup_device(dsbmdevent.devinfo.dev);
	}
	(void)pthread_mutex_unlock(&mutex);
	return (error);
}

int
dsbmc_connect()
{
	char *e;

	cmdqsz = ndevs = 0;
	event_queue.n = event_queue.i = 0;

	(void)pthread_mutex_init(&mutex, NULL);

	if ((dsbmd = uconnect(PATH_DSBMD_SOCKET)) == -1) {
		ERROR(-1, ERR_SYS_FATAL, true, "uconnect(%s)",
		    PATH_DSBMD_SOCKET);
	}
	/* Get device list */
	while ((e = read_event(true)) != NULL) {
		if (process_event(e) == -1)
			ERROR(-1, 0, true, "parse_event()");
		if (dsbmdevent.type == DSBMC_EVENT_ERROR_MSG &&
		    dsbmdevent.code == DSBMC_ERR_PERMISSION_DENIED) {
			ERROR(-1, DSBMC_ERR_FATAL | DSBMC_ERR_CONN_DENIED,
			    false, "Permission denied");
		}
		if (dsbmdevent.type != DSBMC_EVENT_ADD_DEVICE &&
		    dsbmdevent.type != DSBMC_EVENT_END_OF_LIST) {
			ERROR(-1, ERR_SYS_FATAL, false,
			    "Unexpected event (%d) received", dsbmdevent.type);
		} else if (dsbmdevent.type == DSBMC_EVENT_END_OF_LIST)
			break;
	}
	return (0);
}

int
dsbmc_get_devlist(const dsbmc_dev_t ***list)
{
	*list = (const dsbmc_dev_t **)devs;

	return (ndevs);
}

static void
dsbmc_clearerr()
{
	_error = 0;
}

static void
set_error(int error, bool prepend, const char *fmt, ...)
{
	int	_errno;
	char	errbuf[sizeof(errormsg)];
	size_t  len;
	va_list ap;

	_errno = errno;

	va_start(ap, fmt);
	if (prepend) {
		_error |= error;
		if (error & DSBMC_ERR_FATAL) {
			if (strncmp(errormsg, "Fatal error: ", 13) == 0) {
				memmove(errormsg, errormsg + 13,
				    strlen(errormsg) - 12);
			}
			(void)strncpy(errbuf, "Fatal error: ",
			    sizeof(errbuf) - 1);
			len = strlen(errbuf);
		} else if (strncmp(errormsg, "Error: ", 7) == 0) {
			memmove(errormsg, errormsg + 7, strlen(errormsg) - 6);
			(void)strncpy(errbuf, "Error: ", sizeof(errbuf) - 1);
			len = strlen(errbuf);
 		} else
			len = 0;
		(void)vsnprintf(errbuf + len, sizeof(errbuf) - len, fmt, ap);

		len = strlen(errbuf);
		(void)snprintf(errbuf + len, sizeof(errbuf) - len, ": %s",
		    errormsg);
		(void)strcpy(errormsg, errbuf);
	} else {
		_error = error;
		(void)vsnprintf(errormsg, sizeof(errormsg), fmt, ap);
		if (error == DSBMC_ERR_FATAL) {
			(void)snprintf(errbuf, sizeof(errbuf),
			    "Fatal error: %s", errormsg);
		} else {
			(void)snprintf(errbuf, sizeof(errbuf),
			    "Error: %s", errormsg);
		}	
		(void)strcpy(errormsg, errbuf);
	}
	if ((error & DSBMC_ERR_SYS) && _errno != 0) {
		len = strlen(errormsg);
		(void)snprintf(errormsg + len, sizeof(errormsg) - len, ": %s",
		    strerror(_errno));
		errno = 0;
	}
}

static int
uconnect(const char *path)
{
	int  s;
	struct sockaddr_un saddr;

	if ((s = socket(PF_LOCAL, SOCK_STREAM, 0)) == -1)
		ERROR(-1, ERR_SYS_FATAL, false, "socket()");
	(void)memset(&saddr, (unsigned char)0, sizeof(saddr));
	(void)snprintf(saddr.sun_path, sizeof(saddr.sun_path), "%s", path);
	saddr.sun_family = AF_LOCAL;
	if (connect(s, (struct sockaddr *)&saddr, sizeof(saddr)) == -1)
		ERROR(-1, ERR_SYS_FATAL, false, "connect(%s)", path);
	if (fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK) == -1)
		ERROR(-1, ERR_SYS_FATAL, false, "setvbuf()/fcntl()");
	return (s);
}

static dsbmc_dev_t *
add_device(const dsbmc_dev_t *d)
{
	static int   id = 1;
	dsbmc_dev_t *dp;

	if ((dp = lookup_device(d->dev)) != NULL && !dp->removed)
		return (NULL);
	if ((devs[ndevs] = malloc(sizeof(dsbmc_dev_t))) == NULL)
		ERROR(NULL, ERR_SYS_FATAL, false, "malloc()");
	dp = devs[ndevs];
	if ((dp->dev = strdup(d->dev)) == NULL)
		ERROR(NULL, ERR_SYS_FATAL, false, "strdup()");
	if (d->volid != NULL) {
		if ((dp->volid = strdup(d->volid)) == NULL)
			ERROR(NULL, ERR_SYS_FATAL, false, "strdup()");
	} else
		dp->volid = NULL;
	if (d->mntpt != NULL) {
		if ((dp->mntpt = strdup(d->mntpt)) == NULL)
			ERROR(NULL, ERR_SYS_FATAL, false, "strdup()");
		dp->mounted = true;
	} else {
		dp->mntpt   = NULL;
		dp->mounted = false;
	}
	if (d->fsname != NULL) {
		if ((dp->fsname = strdup(d->fsname)) == NULL)
			ERROR(NULL, ERR_SYS_FATAL, false, "strdup()");
	} else
		dp->fsname = NULL;
	dp->id	    = id++;
	dp->type    = d->type;
	dp->cmds    = d->cmds;
	dp->speed   = d->speed;
	dp->removed = false;

	/* Add our own commands to the device's command list, and set VolIDs. */
	switch (d->type) {
	case DSBMC_DT_AUDIOCD:
		dp->volid = strdup("Audio CD");
	case DSBMC_DT_DVD:
		if (dp->volid == NULL)
			dp->volid = strdup("DVD");
	case DSBMC_DT_SVCD:
		if (dp->volid == NULL)
			dp->volid = strdup("SVCD");
	case DSBMC_DT_VCD:
		if (dp->volid == NULL)
			dp->volid = strdup("VCD");
		/* Playable media. */
		dp->cmds |= DSBMC_CMD_PLAY;
	}
	if (dp->volid == NULL)
		dp->volid = strdup(d->dev);
	if (dp->volid == NULL)
		ERROR(NULL, ERR_SYS_FATAL, false, "strdup()");
	if ((d->cmds & DSBMC_CMD_MOUNT)) {
		/* Device we can open in a filemanager. */
		dp->cmds |= DSBMC_CMD_OPEN;
	}
	return (devs[ndevs++]);
}

static void
del_device(const char *dev)
{
	int i;

	for (i = 0; i < ndevs; i++) {
		if (strcmp(devs[i]->dev, dev) == 0)
			break;
	}
	if (i == ndevs)
		return;
	free(devs[i]->dev);
	free(devs[i]->volid);
	free(devs[i]->mntpt);
	free(devs[i]);
	for (; i < ndevs - 1; i++)
		devs[i] = devs[i + 1];
	ndevs--;
}

static void
shuffle()
{
	int i, n;

	if (!devs[ndevs - 1]->removed)
		return;
	for (n = ndevs - 2; n >= 0 && devs[n]->removed; n--)
		;
	if (n++ < 0)
		return;
	if (ndevs + 1 > MAXDEVS)
		return;
	for (i = ndevs - 1; i >= n; i--)
		devs[i + 1] = devs[i];
	devs[n] = devs[ndevs];	
}

static void
set_removed(const char *dev)
{
	dsbmc_dev_t *dp;

	if ((dp = lookup_device(dev)) == NULL)
		return;
	dp->removed = true;
}

static dsbmc_dev_t *
lookup_device(const char *dev)
{
	int i;
	
	if (dev == NULL)
		return (NULL);
	for (i = 0; i < ndevs; i++) {
		if (strcmp(devs[i]->dev, dev) == 0 && !devs[i]->removed)
			return (devs[i]);
	
	}
	for (i = 0; i < ndevs; i++) {
		if (strcmp(devs[i]->dev, dev) == 0)
			return (devs[i]);
	
	}
	return (NULL);
}

static dsbmc_dev_t *
device_from_id(int id)
{
	int i;

	for (i = 0; i < ndevs; i++) {
		if (devs[i]->id == id)
			return (devs[i]);
	}
	return (NULL);
}

char *
readln()
{
	int  i, n;

	if (lnbuf == NULL) {
		if ((lnbuf = malloc(_POSIX2_LINE_MAX)) == NULL)
			return (NULL);
		bufsz = _POSIX2_LINE_MAX;
	}
	n = 0;
	do {
		rd += n;
		if (slen > 0)
			(void)memmove(lnbuf, lnbuf + slen, rd - slen);
		rd  -= slen;
		slen = 0;
		for (i = 0; i < rd && lnbuf[i] != '\n'; i++)
			;
		if (i < rd) {
			lnbuf[i] = '\0';
			slen = i + 1;
			if (slen >= bufsz)
				slen = rd = 0;
			return (lnbuf);
		}
		if (rd >= bufsz) {
			lnbuf = realloc(lnbuf, bufsz + _POSIX2_LINE_MAX);
			if (lnbuf == NULL)
				ERROR(NULL, ERR_SYS_FATAL, false, "realloc()");
			bufsz += _POSIX2_LINE_MAX;
		}
	} while ((n = read(dsbmd, lnbuf + rd, bufsz - rd)) > 0);

	if (n == 0) {
		rd = slen = 0;
		ERROR(NULL, DSBMC_ERR_LOST_CONNECTION, false,
		    "Lost connection to DSBMD");
	} else if (n == -1) {
		rd = slen = 0;
		if (errno != EINTR && errno != EAGAIN)
			ERROR(NULL, ERR_SYS_FATAL, false, "read()");
	}
	return (NULL);
}

static char *
read_event(bool block)
{
	char   *ln;
	fd_set rset;

	if ((ln = readln()) == NULL) {
		if ((_error & DSBMC_ERR_LOST_CONNECTION) || !block)
			return (NULL);
	} else
		return (ln);
	FD_ZERO(&rset); FD_SET(dsbmd, &rset);
	/* Block until data is available. */
	while (select(dsbmd + 1, &rset, NULL, NULL, NULL) == -1) {
		if (errno != EINTR)
			return (NULL);
		else
			ERROR(NULL, ERR_SYS_FATAL, false, "select()");
	}
	return (readln());
}

static int
push_event(const char *e)
{
	if (event_queue.n >= MAXEQSZ)
		ERROR(-1, ERR_SYS_FATAL, false, "MAXEQSZ exceeded.");
	if ((event_queue.ln[event_queue.n] = strdup(e)) == NULL)
		ERROR(-1, ERR_SYS_FATAL, false, "strdup()");
	event_queue.n++;

	return (0);
}

static char *
pull_event()
{
	int i;

	if (event_queue.i >= event_queue.n) {
		/* Reset queue */
		for (i = 0; i < event_queue.n; i++) {
			free(event_queue.ln[i]);
			event_queue.ln[i] = NULL;
		}
		event_queue.n = event_queue.i = 0;
		return (NULL);
	}
	return (event_queue.ln[event_queue.i++]);
}

static int
parse_event(const char *str)
{
	int	    i, len;
	char	    *p, *q, *tmp;
	static char buf[_POSIX2_LINE_MAX];

	/* Init */
	(void)strncpy(buf, str, sizeof(buf) - 1);
	for (i = 0; i < NKEYWORDS; i++) {
		if (dsbmdkeywords[i].val.string != NULL)
			*dsbmdkeywords[i].val.string = NULL;
	}
	for (p = buf; (p = strtok(p, ":\n")) != NULL; p = NULL) {
		for (i = 0; i < NKEYWORDS; i++) {
			len = strlen(dsbmdkeywords[i].key);
			if (strncmp(dsbmdkeywords[i].key, p, len) == 0)
				break;
		}
		if (i == NKEYWORDS) {
			warnx("Unknown keyword '%s'", p);
			continue;
		}
		switch (dsbmdkeywords[i].type) {
		case KWTYPE_STRING:
			*dsbmdkeywords[i].val.string = p + len;
			break;
		case KWTYPE_CHAR:
			*dsbmdkeywords[i].val.character = *p;
			break;
		case KWTYPE_INTEGER:
			*dsbmdkeywords[i].val.integer =
			    strtol(p + len, NULL, 10);
			break;
		case KWTYPE_UINT64:
			*dsbmdkeywords[i].val.uint64 =
			    (uint64_t)strtoll(p + len, NULL, 10);
			break;
		case KWTYPE_COMMANDS:
			dsbmdevent.devinfo.cmds = 0;
			if ((q = tmp = strdup(p + len)) == NULL)
				ERROR(-1, ERR_SYS_FATAL, false, "strdup()");
			for (i = 0; (q = strtok(q, ",")) != NULL; q = NULL) {
				for (i = 0; i < NCMDS; i++) {
					if (strcmp(cmdtbl[i].name, q) == 0) {
						dsbmdevent.devinfo.cmds |=
						    cmdtbl[i].cmd;
					}
				}
			}
			free(tmp);
			break;
		case KWTYPE_DSKTYPE:
			for (i = 0; i < NDSKTYPES; i++) {
				if (strcmp(disktypetbl[i].name, p + len) == 0) {
					dsbmdevent.devinfo.type =
					    disktypetbl[i].type;
					break;
                        	}
                	}
			break;
		}
	}
	return (0);
}

/*
 * Parses  a  string  read from the dsbmd socket. Depending on the event type,
 * process_event() adds or deletes drives from the list, or updates a drive's
 * status.
 */
static int
process_event(char *buf)
{
	int	     i;
	dsbmc_dev_t *d;

	if (parse_event(buf) != 0)
		ERROR(-1, 0, true, "parse_event()");
	if (dsbmdevent.type == DSBMC_EVENT_SUCCESS_MSG) {
		if (cmdqsz <= 0)
			return (0);
		d = dsbmc_sender[0].dev;
	} else {
		switch (dsbmdevent.type) {
		case DSBMC_EVENT_MOUNT:
		case DSBMC_EVENT_UNMOUNT:
		case DSBMC_EVENT_SPEED:
			d = lookup_device(dsbmdevent.devinfo.dev);
			if (d == NULL) {
				warnx("Unknown device %s", dsbmdevent.devinfo.dev);
				return (-1);
			}
		}
	}
	switch (dsbmdevent.type) {
	case DSBMC_EVENT_SUCCESS_MSG:
		if (cmdqsz <= 0)
			return (0);
		if (dsbmc_sender[0].id == DSBMC_CMD_MOUNT) {
			d->mounted = true; free(d->mntpt);
			d->mntpt = strdup(dsbmdevent.devinfo.mntpt);
			if (d->mntpt == NULL)
				ERROR(-1, ERR_SYS_FATAL, false, "strdup()");
		} else if (dsbmc_sender[0].id == DSBMC_CMD_UNMOUNT) {
			d->mounted = false;
		} else if (dsbmc_sender[0].id == DSBMC_CMD_SIZE) {
			d->mediasize = dsbmdevent.mediasize;
			d->used = dsbmdevent.used;
			d->free = dsbmdevent.free;
		}
	case DSBMC_EVENT_ERROR_MSG:
		if (cmdqsz <= 0)
			return (0);
		dsbmc_sender[0].callback(dsbmdevent.code, dsbmc_sender[0].dev);
		free(dsbmc_sender[0].cmd);
		for (i = 0; i < cmdqsz - 1; i++) {
			dsbmc_sender[i].dev = dsbmc_sender[i + 1].dev;
			dsbmc_sender[i].callback = dsbmc_sender[i + 1].callback;
			dsbmc_sender[i].cmd = dsbmc_sender[i + 1].cmd;
			dsbmc_sender[i].id = dsbmc_sender[i + 1].id;
		}
		if (--cmdqsz == 0)
			return (0);
		if (send_string(dsbmc_sender[i].cmd) == -1)
			ERROR(-1, 0, true, "send_string()");
		return (0);
	case DSBMC_EVENT_ADD_DEVICE:
		if ((d = add_device(&dsbmdevent.devinfo)) == NULL)
			ERROR(-1, 0, true, "add_device()");
		return (1);
	case DSBMC_EVENT_DEL_DEVICE:
		set_removed(dsbmdevent.devinfo.dev);
		shuffle();
		return (1);
	case DSBMC_EVENT_END_OF_LIST:
		return (0);
	case DSBMC_EVENT_SHUTDOWN:
		return (1);
	case DSBMC_EVENT_MOUNT:
		d->mounted = true; free(d->mntpt);
		d->mntpt = strdup(dsbmdevent.devinfo.mntpt);
		if (d->mntpt == NULL)
			ERROR(-1, ERR_SYS_FATAL, false, "strdup()");
		return (1);
	case DSBMC_EVENT_UNMOUNT:
		d->mounted = false;
		return (1);
	case DSBMC_EVENT_SPEED:
		d->speed = dsbmdevent.devinfo.speed;
		return (1);
	default:
		warnx("Invalid event received.");
		return (-1);
	}
	return (0);
}

static int
dsbmc_send_async(dsbmc_dev_t *dev, void (*cb)(int, const dsbmc_dev_t *),
    const char *cmd, ...)
{
	int	i;
	char	buf[_POSIX2_LINE_MAX];
	size_t	len;
	va_list ap;
	
	(void)pthread_mutex_lock(&mutex);
	dsbmc_clearerr();

	if (cmdqsz >= CMDQMAXSZ) {
		(void)pthread_mutex_unlock(&mutex);
		ERROR(-1, DSBMC_ERR_CMDQ_BUSY, false, "Command queue busy");
	}
	va_start(ap, cmd);
	(void)vsnprintf(buf, sizeof(buf) - 1, cmd, ap);

	dsbmc_sender[cmdqsz].dev = dev;
	dsbmc_sender[cmdqsz].callback = cb;

	for (i = 0; i < NCMDS; i++) {
		len = strlen(cmdtbl[i].name);
		if (strncmp(cmdtbl[i].name, cmd, len) == 0) {
			dsbmc_sender[cmdqsz].id = cmdtbl[i].cmd;
			break;
		}
	}
	if ((dsbmc_sender[cmdqsz].cmd = strdup(buf)) == NULL) {
		(void)pthread_mutex_unlock(&mutex);
		ERROR(-1, ERR_SYS_FATAL, false, "strdup()");
	}
	if (cmdqsz++ > 0) {
		(void)pthread_mutex_lock(&mutex);
		return (0);
	}
	if (send_string(buf) == -1) {
		(void)pthread_mutex_unlock(&mutex);
		ERROR(-1, 0, true, "send_string()");
	}
	(void)pthread_mutex_unlock(&mutex);
	return (0);
}

static int
dsbmc_send(const char *cmd, ...)
{
	char	buf[_POSIX2_LINE_MAX], *e;
	va_list	ap;

	(void)pthread_mutex_lock(&mutex);
	dsbmc_clearerr();

	if (cmdqsz > 0) {
		ERROR(-1, DSBMC_ERR_COMMAND_IN_PROGRESS, false,
		    "dsbmc_send(): Command already in progress");
	}
	va_start(ap, cmd);
	
	(void)vsnprintf(buf, sizeof(buf) - 1, cmd, ap);
	if (send_string(buf) == -1) {
		(void)pthread_mutex_unlock(&mutex);
		ERROR(-1, 0, true, "send_string()");
	}
	while ((e = read_event(true)) != NULL) {
		if (parse_event(e) == -1) {
			(void)pthread_mutex_unlock(&mutex);
			ERROR(-1, 0, true, "parse_event()");
		}
		if (dsbmdevent.type == DSBMC_EVENT_SUCCESS_MSG) {
			(void)pthread_mutex_unlock(&mutex);
			return (0);
		} else if (dsbmdevent.type == DSBMC_EVENT_ERROR_MSG) {
			(void)pthread_mutex_unlock(&mutex);
			return (dsbmdevent.code);
		} else if (push_event(e) == -1) {
			(void)pthread_mutex_unlock(&mutex);
			ERROR(-1, 0, true, "push_event()");
		}
	}
	(void)pthread_mutex_unlock(&mutex);
	ERROR(-1, 0, true, "read_event()");
}

static int
send_string(const char *str)
{
	fd_set wrset;

	FD_ZERO(&wrset); FD_SET(dsbmd, &wrset);
	while (select(dsbmd + 1, 0, &wrset, 0, 0) == -1) {
		if (errno != EINTR)
			ERROR(-1, ERR_SYS_FATAL, false, "select()");
	}
	while (write(dsbmd, str, strlen(str)) == -1) {
		if (errno != EINTR)
			ERROR(-1, ERR_SYS_FATAL, false, "write()");
	}		
	return (0);
}

