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

#ifndef _LIB_DSBMC_H_
#define _LIB_DSBMC_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
#include <stdbool.h>
#include <stdio.h>

#define DSBMC_ERR_SYS			(1 << 0)
#define DSBMC_ERR_FATAL			(1 << 1)
#define DSBMC_ERR_LOST_CONNECTION	(1 << 2)
#define DSBMC_ERR_INVALID_DEVICE	(1 << 3)
#define DSBMC_ERR_CMDQ_BUSY		(1 << 4)
#define DSBMC_ERR_COMMAND_IN_PROGRESS	(1 << 5)

#define DSBMC_ERR_ALREADY_MOUNTED	((1 << 8) + 0x01)
#define DSBMC_ERR_PERMISSION_DENIED	((1 << 8) + 0x02)
#define DSBMC_ERR_NOT_MOUNTED		((1 << 8) + 0x03)
#define DSBMC_ERR_DEVICE_BUSY		((1 << 8) + 0x04)
#define DSBMC_ERR_NO_SUCH_DEVICE	((1 << 8) + 0x05)
#define DSBMC_ERR_MAX_CONN_REACHED	((1 << 8) + 0x06)
#define DSBMC_ERR_NOT_EJECTABLE		((1 << 8) + 0x07)
#define DSBMC_ERR_UNKNOWN_COMMAND	((1 << 8) + 0x08)
#define DSBMC_ERR_UNKNOWN_OPTION	((1 << 8) + 0x09)
#define DSBMC_ERR_SYNTAX_ERROR		((1 << 8) + 0x0a)
#define DSBMC_ERR_NO_MEDIA		((1 << 8) + 0x0b)
#define DSBMC_ERR_UNKNOWN_FILESYSTEM	((1 << 8) + 0x0c)
#define DSBMC_ERR_UNKNOWN_ERROR		((1 << 8) + 0x0d)
#define DSBMC_ERR_MNTCMD_FAILED		((1 << 8) + 0x0e)
#define DSBMC_ERR_INVALID_ARGUMENT	((1 << 8) + 0x0f)

#define DSBMC_EVENT_SUCCESS_MSG		'O'
#define DSBMC_EVENT_WARNING_MSG		'W'
#define DSBMC_EVENT_ERROR_MSG		'E'
#define DSBMC_EVENT_INFO_MSG		'I'
#define DSBMC_EVENT_ADD_DEVICE		'+'
#define DSBMC_EVENT_DEL_DEVICE		'-'
#define DSBMC_EVENT_END_OF_LIST		'='
#define DSBMC_EVENT_MOUNT		'M'
#define DSBMC_EVENT_UNMOUNT		'U'
#define DSBMC_EVENT_SPEED		'V'
#define DSBMC_EVENT_SHUTDOWN		'S'

typedef struct dsbmc_dev_s {
	uint8_t cmds;			/* Supported commands. */
#define DSBMC_CMD_MOUNT			(1 << 0x00)
#define DSBMC_CMD_UNMOUNT		(1 << 0x01)
#define DSBMC_CMD_EJECT			(1 << 0x02)
#define DSBMC_CMD_PLAY			(1 << 0x03)
#define DSBMC_CMD_OPEN			(1 << 0x04)
#define DSBMC_CMD_SPEED			(1 << 0x05)
#define DSBMC_CMD_SIZE			(1 << 0x06)
	uint8_t type;
#define DSBMC_DT_HDD			0x01
#define DSBMC_DT_USBDISK		0x02
#define DSBMC_DT_DATACD			0x03
#define	DSBMC_DT_AUDIOCD		0x04
#define	DSBMC_DT_RAWCD			0x05
#define	DSBMC_DT_DVD			0x06
#define	DSBMC_DT_VCD			0x07
#define	DSBMC_DT_SVCD			0x08
#define	DSBMC_DT_FLOPPY			0x09
#define DSBMC_DT_MMC			0x0a
#define DSBMC_DT_MTP			0x0b
#define DSBMC_DT_PTP			0x0c
	int	 id;
	char	 *dev;			/* Device name */
	char	 *volid;		/* Volume ID */
	char	 *mntpt;		/* Mount point */
	char	 *fsname;		/* Filesystem name */
	bool	 mounted;		/* Whether drive is mounted. */
	bool	 removed;		/* Whether device was removed */
	uint8_t	 speed;			/* CD/DVD reading speed */
	uint64_t mediasize;		/* For "size" command. */
	uint64_t free;			/* 	 ""	       */
	uint64_t used;			/* 	 ""	       */
} dsbmc_dev_t;

typedef struct dsbmc_event_s {
	int type;			/* Event type */
	int code;			/* Error code */
	dsbmc_dev_t *dev;		/* Pointer into dev list */
} dsbmc_event_t;

extern int  dsbmc_fetch_event(dsbmc_event_t *ev);
extern int  dsbmc_get_devlist(const dsbmc_dev_t ***);
extern int  dsbmc_mount(const dsbmc_dev_t *d);
extern int  dsbmc_unmount(const dsbmc_dev_t *d, bool force);
extern int  dsbmc_eject(const dsbmc_dev_t *d, bool force);
extern int  dsbmc_set_speed(const dsbmc_dev_t *d, int speed);
extern int  dsbmc_size(const dsbmc_dev_t *d);
extern int  dsbmc_set_speed_async(const dsbmc_dev_t *d, int speed,
		void (*cb)(int, const dsbmc_dev_t *));
extern int  dsbmc_mount_async(const dsbmc_dev_t *d,
		void (*cb)(int, const dsbmc_dev_t *));
extern int  dsbmc_unmount_async(const dsbmc_dev_t *d, bool force,
		void (*cb)(int, const dsbmc_dev_t *));
extern int  dsbmc_eject_async(const dsbmc_dev_t *d, bool force,
		void (*cb)(int, const dsbmc_dev_t *));
extern int  dsbmc_size_async(const dsbmc_dev_t *d,
		void (*cb)(int, const dsbmc_dev_t *));
extern int  dsbmc_connect(void);
extern int  dsbmc_get_fd(void);
extern int  dsbmc_get_err(const char **);
extern void dsbmc_disconnect(void);
extern void dsbmc_free_dev(const dsbmc_dev_t *);
extern const char *dsbmc_errstr(void);
extern const char *dsbmc_errcode_to_str(int);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
#endif  /* !_LIB_DSBMC_H_ */

