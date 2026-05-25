/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
#
*/

#ifdef _EE
#define _GNU_SOURCE

#include <stdio.h>
#include <kernel.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <debug.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <hdd-ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <libpwroff.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <libcdvd.h>
#include <unistd.h>

__attribute__((format(printf,1,2))) static inline void fatal(const char *format, ...)
{
    va_list opt;
    scr_clear();
    scr_printf("\n");
    va_start(opt, format);
    scr_vprintf(format, opt);
    va_end(opt);
    scr_printf("Need help? Please visit https://uyjulian.github.io/desr-help/\n");
    SleepThread();
}

#define DEFINITION_FOR_EXTERNAL_IRX(basename) \
    extern u8 basename##_irx[]; \
    extern int size_##basename##_irx;

#define LOADMODULEBUFFER_EXTERNAL_IRX(basename) \
    { \
        int ret; \
        scr_printf("Loading %s...\n", #basename); \
        SifExecModuleBuffer(basename##_irx, size_##basename##_irx, 0, NULL, &ret); \
        if (ret < 0) { \
            fatal("Fatal: SifExecModuleBuffer %s failed: %d\n", #basename, ret); \
        } \
    }

#define LOADMODULE_ROM_IRX(basename) \
    { \
        int ret; \
        scr_printf("Loading %s...\n", #basename); \
        ret = SifLoadModule("rom0:" #basename, 0, NULL); \
        if (ret < 0) { \
            fatal("Fatal: SifLoadModule %s failed: %d\n", #basename, ret); \
        } \
    }

DEFINITION_FOR_EXTERNAL_IRX(iomanX);
DEFINITION_FOR_EXTERNAL_IRX(xdevctl);
DEFINITION_FOR_EXTERNAL_IRX(fileXio);
DEFINITION_FOR_EXTERNAL_IRX(bdm);
DEFINITION_FOR_EXTERNAL_IRX(bdmfs_fatfs);
DEFINITION_FOR_EXTERNAL_IRX(poweroff);
DEFINITION_FOR_EXTERNAL_IRX(ps2dev9);
DEFINITION_FOR_EXTERNAL_IRX(ps2atad);
DEFINITION_FOR_EXTERNAL_IRX(ps2hdd);
DEFINITION_FOR_EXTERNAL_IRX(dvrdrv);
DEFINITION_FOR_EXTERNAL_IRX(dvrrelay);
DEFINITION_FOR_EXTERNAL_IRX(dvrfile);
DEFINITION_FOR_EXTERNAL_IRX(usbd);
DEFINITION_FOR_EXTERNAL_IRX(usbmass_bd);

extern int prog_main(int ac, char **av);

static int g_use_dvr_hdd;

extern int _iop_reboot_count;
static SifRpcClientData_t g_xdevctl_cd;
static int g_xdevctl_inited;

static int init_xdevctl(void)
{
    int res;

    static int _rb_count = -1;
    if (_rb_count != _iop_reboot_count)
    {
        _rb_count = _iop_reboot_count;
        memset(&g_xdevctl_cd, 0, sizeof(g_xdevctl_cd));
        g_xdevctl_inited = 0;
    }

    if (g_xdevctl_inited)
        return 0;

    sceSifInitRpc(0);

    while ((res = sceSifBindRpc(&g_xdevctl_cd, 0x79444556, 0)) < 0 || !g_xdevctl_cd.server)
        nopdelay();

    g_xdevctl_inited = 1;

    return 0;
}

struct rpc_79444556_stru
{
	int m_cmd;
	int m_arglen;
	int m_buflen;
	char *m_name;
	void *m_arg;
	void *m_buf;
};

static int call_xdevctl_param(struct rpc_79444556_stru *stru)
{
    struct rpc_79444556_stru arg;
    int ret __attribute__((__aligned__(64)));

    if (init_xdevctl() < 0)
        return -1;

    memcpy(&arg, stru, sizeof(arg));

    if (sceSifCallRpc(&g_xdevctl_cd, 0, 0, &arg, sizeof(arg), &ret, sizeof(ret), NULL, NULL) < 0)
        return -1;

    return *(int *)(UNCACHED_SEG(&ret));
}

static void *alloc_str_iop(const char *str)
{
    size_t size;
    SifDmaTransfer_t dmat[1];
    int trid;
    void *iop_addr;
    static char str_aligned[64] __attribute__((aligned(64)));

    size = strlen(str) + 1;
    if (size > sizeof(str_aligned))
    	return NULL;
    memset(UNCACHED_SEG(str_aligned), 0, sizeof(str_aligned));
    memcpy(UNCACHED_SEG(str_aligned), str, size);

    if (!(iop_addr = SifAllocIopHeap(size)))
        return NULL;

    dmat[0].src  = str_aligned;
    dmat[0].dest = iop_addr;
    dmat[0].size = sizeof(str_aligned);
    dmat[0].attr = 0;
    trid = sceSifSetDma(dmat, sizeof(dmat)/sizeof(dmat[0]));

    if (!trid)
        return NULL;

    while (sceSifDmaStat(trid) >= 0);

    return iop_addr;
}

static int call_xdevctl_main(void *iop_name, int cmd, void *iop_arg, int arg_len, void *iop_buf, int buf_len)
{
    struct rpc_79444556_stru arg;

    memset(&arg, 0, sizeof(arg));
    arg.m_cmd = cmd;
    arg.m_arglen = arg_len;
    arg.m_buflen = buf_len;
    arg.m_name = iop_name;
    arg.m_arg = iop_arg;
    arg.m_buf = iop_buf;
    return call_xdevctl_param(&arg);
}

static int call_xdevctl_simple(void *iop_name, int cmd)
{
    return call_xdevctl_main(iop_name, cmd, NULL, 0, NULL, 0);
}

static void *g_iop_str_hdd;
static void *g_iop_str_dvr_hdd;
static void *g_iop_str_dev9x;

static void poweroffCallback(void *arg)
{
    scr_printf("Powering dev9 off\n");
    while (fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0) < 0);

    scr_printf("Shutting down via mechacon\n");
    poweroffShutdown();
    scr_printf("It is now safe to shut down by holding the power button\n");
}

static void disable_dev9(void)
{
	{
		u16 val;
		if (!SifIopGetVal(0xb0000004, &val, LF_VAL_SHORT) && ((val & 0x10) != 0))
		{
			// Needed in order for dvrdrv to work
			{
				u8 out[16];
				u8 in[16];

				in[0] = 1;
				// sceCdNoticeGameStart
				sceCdApplySCmd(0x29, in, 1, out);
			}
		}
	}
	while (fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0) < 0);
}

int main(int ac, char **av)
{
	int hdd_ok;
	int dvr_hdd_ok;

	hdd_ok = 0;
	dvr_hdd_ok = 0;
	init_scr();

	scr_printf("Rebooting IOP and preparing SIF...\n");

	//Reboot IOP
	SifInitRpc(0);
	while(!SifIopReset("", 0)){};
	while(!SifIopSync()){};

	//Initialize SIF services
	SifInitRpc(0);
	SifLoadFileInit();
	SifInitIopHeap();
	sbv_patch_enable_lmb();

	LOADMODULEBUFFER_EXTERNAL_IRX(iomanX);
	LOADMODULEBUFFER_EXTERNAL_IRX(xdevctl);

	init_xdevctl();

	LOADMODULEBUFFER_EXTERNAL_IRX(fileXio);

	fileXioInit();
    // Increase the FILEIO R/W buffer size to reduce overhead.
    fileXioSetRWBufferSize(256 * 1024);

	LOADMODULEBUFFER_EXTERNAL_IRX(bdm);
	LOADMODULEBUFFER_EXTERNAL_IRX(bdmfs_fatfs);
	LOADMODULEBUFFER_EXTERNAL_IRX(usbd);
	LOADMODULEBUFFER_EXTERNAL_IRX(usbmass_bd);

	scr_clear();
	scr_printf("Waiting for USB drive...\n");
	scr_printf("Need help? Please visit https://uyjulian.github.io/desr-help/\n");
	{
		iox_stat_t chk_stat;
		while (fileXioGetStat("mass0:/", &chk_stat) < 0)
		{
			sleep(1);
		}
	}

	scr_clear();
	LOADMODULEBUFFER_EXTERNAL_IRX(poweroff);
	poweroffInit();
	poweroffSetCallback(&poweroffCallback, NULL);

	LOADMODULEBUFFER_EXTERNAL_IRX(ps2dev9);
	
	g_iop_str_hdd = alloc_str_iop("hdd0:");
	g_iop_str_dvr_hdd = alloc_str_iop("dvr_hdd0:");
	g_iop_str_dev9x = alloc_str_iop("dev9x:");
	if (!g_iop_str_hdd || !g_iop_str_dvr_hdd || !g_iop_str_dev9x)
	{
		fatal("IOP string allocation failed");
	}
	{
		u16 val;
		if (!SifIopGetVal(0xb0000004, &val, LF_VAL_SHORT) && ((val & 0x02) != 0))
		{
			int hddstat;
			LOADMODULEBUFFER_EXTERNAL_IRX(ps2atad);
			LOADMODULEBUFFER_EXTERNAL_IRX(ps2hdd);
			hddstat = call_xdevctl_simple(g_iop_str_hdd, HDIOC_STATUS);
		    if (hddstat == 0 || hddstat == 1)
		    	hdd_ok = 1;
		}
	}

	{
		u16 val;
		if (!SifIopGetVal(0xb0000004, &val, LF_VAL_SHORT) && ((val & 0x10) != 0))
		{
			int hddstat;
			// Needed in order for dvrdrv to work
			{
				u8 out[16];
				u8 in[16];

				in[0] = 0;
				// sceCdNoticeGameStart
				sceCdApplySCmd(0x29, in, 1, out);
			}
			LOADMODULEBUFFER_EXTERNAL_IRX(dvrdrv);
			LOADMODULEBUFFER_EXTERNAL_IRX(dvrrelay);
			LOADMODULEBUFFER_EXTERNAL_IRX(dvrfile);
			hddstat = call_xdevctl_simple(g_iop_str_dvr_hdd, HDIOC_STATUS);
		    if ((hddstat == 0 || hddstat == 1) && (call_xdevctl_simple(g_iop_str_dvr_hdd, HDIOC_ISLBA48) == 1))
		    	dvr_hdd_ok = 1;
		}
	}

	if (!hdd_ok)
		fatal("Fatal: HDD not available\n");

	g_use_dvr_hdd = dvr_hdd_ok ? 1 : 0;

	scr_clear();
	if (g_use_dvr_hdd)
		scr_printf("Using %s as device\n", g_use_dvr_hdd ? "dvr_hdd0:" : "hdd0:");
	scr_printf("Now processing, this may take a while (at most 25 minutes)...\n");
	scr_printf("Need help? Please visit https://uyjulian.github.io/desr-help/\n");
	time_t start = time(0);
	
	{
		int xac = 2;
		char *xav[] = {
			av[0],
			"mass0:/HDDREWK.7Z",
			NULL,
		};
		prog_main(xac, xav);
	}
	double elapsed = difftime(time(0), start);
	disable_dev9();
	scr_clear();
	scr_printf("Finished in %f seconds.\n", elapsed);
	if (elapsed > 25 * 60)
		scr_printf("Warning: this took longer than it should take.\n");
	scr_printf("Need help? Please visit https://uyjulian.github.io/desr-help/\n");
	scr_printf("Make a note of the above, then push the power button.\n");
	SleepThread();

	return 0;
}
#endif

#include <stdint.h>
#include <unistd.h>
#ifndef _EE
#include <aio.h>
#include <errno.h>
#include <string.h>
#endif

#ifndef _EE
static int g_fd = -1;
#endif

struct my_io_buffer_bookkeeping
{
#ifdef _EE
	char m_pad[56];
	char m_devctlparam[8];
#else
	char m_pad[64];
#endif
};

// filexio is limited to 2048 (4 sectors) bytes both arg and buf buffers. (no longer using)
// dvrfile is limited to 32768 bytes (64 sectors) both arg and buf buffers.
// Former buffer size: 0x80000
struct my_io_buffer
{
	struct my_io_buffer_bookkeeping m_bookkeeping;
	char m_buf[0x8000];
} __attribute__((__aligned__(64)));

static inline uint32_t bswap32(uint32_t val)
{
#if 0
    return __builtin_bswap32(val);
#else
    return (val << 24) + ((val & 0xFF00) << 8) + ((val >> 8) & 0xFF00) + ((val >> 24) & 0xFF);
#endif
}

static struct my_io_buffer g_buffer[2];
static int g_current_buffer;
static void *g_iop_buffer[2];

#ifndef _EE
static int my_aio_rw_common(uint32_t lba, uint32_t nsectors, int bufidx, int dir)
{
	static struct aiocb aio;
	static const struct aiocb *aio_list[] = {NULL};
	struct my_io_buffer *buf;
	int ret;
	buf = &g_buffer[bufidx];
	if (aio_list[0])
	{
		ret = aio_suspend(aio_list, sizeof(aio_list)/sizeof(aio_list[0]), NULL);
		if (ret == -1)
		{
			aio_list[0] = NULL;
			return errno;
		}
		ret = aio_error(&aio);
		if (ret)
		{
			aio_list[0] = NULL;
			return ret;
		}
		ret = aio_return(&aio);
		if (ret != aio.aio_nbytes)
		{
			aio_list[0] = NULL;
			return -1;
		}
		aio_list[0] = NULL;
	}
    if (nsectors > 0)
    {
    	if (g_fd >= 0)
    	{
			memset(&aio, 0, sizeof(aio));
			aio.aio_fildes = g_fd;
			aio.aio_buf = (void *)buf->m_buf;
			aio.aio_nbytes = nsectors * 512;
			aio.aio_offset = lba * 512;
			(dir ? aio_write : aio_read)(&aio);
			aio_list[0] = &aio;
    	}
    }
    return 0;
}
#endif

static int hddInitReadWrite(void)
{
#ifdef _EE
	int size;
	int buffer_count;
	void *iop_addr;
	int i;

	buffer_count = sizeof(g_buffer)/sizeof(g_buffer[0]);
	size = sizeof(g_buffer[0]) * buffer_count;
    /* Round the size up to the nearest 16 bytes. */
    size = (size + 15) & -16;

    iop_addr = SifAllocIopHeap(size);
    if (!iop_addr)
        return -1;

    for (i = 0; i < buffer_count; i += 1)
    	g_iop_buffer[i] = ((u8 *)iop_addr) + sizeof(g_buffer[0]);
#endif
    return 0;
}

static int hddReadSectors(uint32_t lba, uint32_t nsectors, int bufidx)
{
#ifdef _EE
	struct my_io_buffer *buf;

	buf = &g_buffer[bufidx];
	if (nsectors > 0)
	{
	    hddAtaTransfer_t *args = (hddAtaTransfer_t *)buf->m_bookkeeping.m_devctlparam;

		size_t size;
		SifDmaTransfer_t dmat[1];
		int trid;
		void *iop_dstbuf_addr;

		uint32_t xlba;
		uint32_t xsize;
		SifRpcReceiveData_t rdata;

		xlba = lba;
		xsize = nsectors;

		if (g_use_dvr_hdd)
		{
		    // For dvr_hdd only
		    args->lba = bswap32(xlba);
		    args->size = bswap32(xsize);
		}
		else
		{
		    args->lba = xlba;
		    args->size = xsize;
		}

		size = sizeof(*args);
		size = (size + 15) & -16;
		dmat[0].src  = buf;
		dmat[0].dest = g_iop_buffer[bufidx];
		dmat[0].size = size;
		dmat[0].attr = 0;
		sceSifWriteBackDCache(dmat[0].dest, dmat[0].size);
		trid = sceSifSetDma(dmat, sizeof(dmat)/sizeof(dmat[0]));

		if (!trid)
		    return -1;

		while (sceSifDmaStat(trid) >= 0);

		iop_dstbuf_addr = (u8 *)(dmat[0].dest) + sizeof(struct my_io_buffer_bookkeeping);

		if (call_xdevctl_main(g_use_dvr_hdd ? g_iop_str_dvr_hdd : g_iop_str_hdd, HDIOC_READSECTOR, (u8 *)(dmat[0].dest) +  + sizeof(buf->m_bookkeeping.m_pad), sizeof(*args), iop_dstbuf_addr, xsize * 512) != 0)
			return -1;
		
		SyncDCache(buf->m_buf, (u8 *)(buf->m_buf) + sizeof(buf->m_buf));
		if (sceSifGetOtherData(&rdata, iop_dstbuf_addr, buf->m_buf, xsize * 512, 0) < 0)
			return -1;
	}
    return 0;
#else
    return my_aio_rw_common(lba, nsectors, bufidx, 0);
#endif
}

static int hddWriteSectors(uint32_t lba, uint32_t nsectors, int bufidx)
{
#ifdef _EE
	struct my_io_buffer *buf;

	buf = &g_buffer[bufidx];
	if (nsectors > 0)
	{
	    hddAtaTransfer_t *args = (hddAtaTransfer_t *)buf->m_bookkeeping.m_devctlparam;

		size_t size;
		SifDmaTransfer_t dmat[1];
		int trid;

		uint32_t xlba;
		uint32_t xsize;

		xlba = lba;
		xsize = nsectors;

		if (g_use_dvr_hdd)
		{
		    // For dvr_hdd only
		    args->lba = bswap32(xlba);
		    args->size = bswap32(xsize);
		}
		else
		{
		    args->lba = xlba;
		    args->size = xsize;
		}

		size = sizeof(*args) + (xsize * 512);
		size = (size + 15) & -16;
		dmat[0].src  = buf;
		dmat[0].dest = g_iop_buffer[bufidx];
		dmat[0].size = size;
		dmat[0].attr = 0;
		sceSifWriteBackDCache(dmat[0].dest, dmat[0].size);
		trid = sceSifSetDma(dmat, sizeof(dmat)/sizeof(dmat[0]));

		if (!trid)
		    return -1;

		while (sceSifDmaStat(trid) >= 0);

		if (call_xdevctl_main(g_use_dvr_hdd ? g_iop_str_dvr_hdd : g_iop_str_hdd, HDIOC_WRITESECTOR, (u8 *)(dmat[0].dest) + sizeof(buf->m_bookkeeping.m_pad), sizeof(*args) + (xsize * 512), NULL, 0) != 0)
			return -1;
	}
	return 0;
#else
    return my_aio_rw_common(lba, nsectors, bufidx, 1);
#endif
}


/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Based on bsdunzip from libbarchive
 * Copyright (c) 2009, 2010 Joerg Sonnenberger <joerg@NetBSD.org>
 * Copyright (c) 2007-2008 Dag-Erling Smørgrav
 * All rights reserved.
 */

#include <archive.h>
#include <archive_entry.h>


#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define __LA_NORETURN


#ifdef _EE
#define FAIL_EXIT() SleepThread()
#else
#define FAIL_EXIT() exit(EXIT_FAILURE)
#endif


#ifdef _EE
#define STDERR_PRINTF(...) scr_printf(__VA_ARGS__)
#define STDERR_VPRINTF(...) scr_vprintf(__VA_ARGS__)
#define STDERR_FLUSH() do {} while (0)
#else
#define STDERR_PRINTF(...) fprintf(stderr, __VA_ARGS__)
#define STDERR_VPRINTF(...) vfprintf(stderr, __VA_ARGS__)
#define STDERR_FLUSH() fflush(stderr)
#endif


/* convenience macro */
/* XXX should differentiate between ARCHIVE_{WARN,FAIL,RETRY} */
#define ac(call)						\
	do {							\
		int acret = (call);				\
		if (acret != ARCHIVE_OK)			\
			errorx("%s", archive_error_string(a));	\
	} while (0)

/* fatal error message + errno */
static void __LA_NORETURN
error(const char *fmt, ...)
{
	va_list ap;

	STDERR_FLUSH();
	STDERR_PRINTF("error: ");
	va_start(ap, fmt);
	STDERR_VPRINTF(fmt, ap);
	va_end(ap);
	STDERR_PRINTF(": %s\n", strerror(errno));
#ifdef _EE
	scr_printf("Need help? Please visit https://uyjulian.github.io/desr-help/\n");
	disable_dev9();
#endif
	FAIL_EXIT();
}

/* fatal error message, no errno */
static void __LA_NORETURN
errorx(const char *fmt, ...)
{
	va_list ap;

	STDERR_FLUSH();
	STDERR_PRINTF("error: ");
	va_start(ap, fmt);
	STDERR_VPRINTF(fmt, ap);
	va_end(ap);
	STDERR_PRINTF("\n");
#ifdef _EE
	scr_printf("Need help? Please visit https://uyjulian.github.io/desr-help/\n");
	disable_dev9();
#endif
	FAIL_EXIT();
}

/* non-fatal error message, no errno */
static void
warningx(const char *fmt, ...)
{
	va_list ap;

	STDERR_FLUSH();
	STDERR_PRINTF("warning: ");
	va_start(ap, fmt);
	STDERR_VPRINTF(fmt, ap);
	va_end(ap);
	STDERR_PRINTF("\n");
}

/* informational message, no errno */
static void
infox(const char *fmt, ...)
{
	va_list ap;

	STDERR_FLUSH();
	STDERR_PRINTF("info: ");
	va_start(ap, fmt);
	STDERR_VPRINTF(fmt, ap);
	va_end(ap);
	STDERR_PRINTF("\n");
}

static int
validate_single(struct archive *a, struct archive_entry *e, const char *wanted_filename, uint64_t wanted_size)
{
	const char *pathname;
	mode_t filetype;
	const char *p, *q;

	if ((pathname = archive_entry_pathname(e)) == NULL)
	{
		warningx("skipping empty or unreadable filename entry");
		return 1;
	}

	if (archive_entry_symlink(e))
	{
		warningx("skipping symlink");
		return 1;
	}
	filetype = archive_entry_filetype(e);

	/* I don't think this can happen in a zipfile.. */
	if (!S_ISDIR(filetype) && !S_ISREG(filetype)) {
		warningx("skipping non-regular entry '%s'", pathname);
		return 1;
	}

	/* skip directories  */
	if (S_ISDIR(filetype)) {
		return 1;
	}

	for (p = q = pathname; *p; ++p)
		if (*p == '/' || *p == '\\')
			q = p + 1;

	if (strcasecmp(wanted_filename, q)) {
		return 1;
	}

	if (wanted_size && archive_entry_size(e) != wanted_size) {
		warningx("skipping entry '%s' due to incorrect size", q);
		return 1;
	}

	return 0;
}

struct jobs_dat_header
{
	uint32_t m_count;
	char m_pad1[12];
};

struct jobs_dat_entry
{
	uint32_t m_start_sector;
	char m_pad1[12];
	uint32_t m_end_sector;
	char m_pad2[12];
	char m_filename[28];
	uint32_t m_job_type;
};

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef _EE
int prog_main(int ac, char **av)
#else
int main(int ac, char **av)
#endif
{
	struct archive *a;
	struct archive_entry *e;
	struct jobs_dat_entry *ents;
	int ent_count;
	int ret;
	int f;

	if (ac < 2)
		errorx("Invalid argument count");

#ifndef _EE
	if (ac > 2)
	{
		g_fd = open(av[2], O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, 0644);
		if (g_fd < 0)
			error("Could not open image for writing");
	}
#endif

	if (hddInitReadWrite())
		errorx("Could not init IOP memory for read/write");

	infox("Opening archive file...");

	ents = NULL;
	ent_count = 0;
	f = open(av[1], O_RDONLY | O_BINARY);

	if (f < 0)
		error("unable to open archive file");

	if ((a = archive_read_new()) == NULL)
		error("archive_read_new failed");

	ac(archive_read_support_format_7zip(a));
	ac(archive_read_support_filter_lzma(a));

	lseek(f, 0, SEEK_SET);
	ac(archive_read_open_fd(a, f, 8192));

	infox("Reading JOBS.DAT...");
	for (;;) {
		ret = archive_read_next_header(a, &e);
		if (ret == ARCHIVE_EOF)
			break;

		ac(ret);

		if (validate_single(a, e, "JOBS.DAT", 0))
		{
			ac(archive_read_data_skip(a));
		}
		else
		{
			struct jobs_dat_header hdr;

			ssize_t len;

			for (;;)
			{
				int ents_size;
				len = archive_read_data(a, &hdr, sizeof(hdr));

				if (len < 0)
					ac(len);

				if (len != sizeof(hdr))
				{
					warningx("skipping JOBS.DAT due to short file for header");
					break;
				}

				if (!hdr.m_count)
				{
					warningx("skipping JOBS.DAT due to no jobs");
					break;
				}

				ents_size = sizeof(struct jobs_dat_entry) * hdr.m_count;
				if (archive_entry_size(e) != (sizeof(hdr) + ents_size))
				{
					warningx("skipping JOBS.DAT due to incorrect entry count for header");
					break;
				}

				ents = malloc(ents_size);
				if (!ents)
					error("could not allocate memory for JOBS.DAT entries");
				len = archive_read_data(a, ents, ents_size);

				if (len < 0)
					ac(len);

				if (len != ents_size)
					errorx("could not read JOBS.DAT entries");

				ent_count = hdr.m_count;
				break;
			}
		}
		if (ent_count)
			break;
	}

	ac(archive_read_free(a));

	if (!ents || !ent_count)
		errorx("could not find correctly-formed JOBS.DAT in archive");

	infox("Validating JOBS.DAT...");
	int valid_ent_count;
	int i;

	valid_ent_count = 0;
	for (i = 0; i < ent_count; i += 1)
	{
		char fn[29];
		int sector_count;
		if (ents[i].m_job_type != 3)
			errorx("entry %d in JOBS.DAT has job type %d", i, ents[i].m_job_type);
		strncpy(fn, ents[i].m_filename, sizeof(fn) - 1);
		fn[sizeof(fn) - 1] = 0;
		sector_count = ents[i].m_end_sector - ents[i].m_start_sector;

		if ((a = archive_read_new()) == NULL)
			error("archive_read_new failed");

		ac(archive_read_support_format_7zip(a));
		ac(archive_read_support_filter_lzma(a));

		lseek(f, 0, SEEK_SET);
		ac(archive_read_open_fd(a, f, 8192));

		for (;;) {
			ret = archive_read_next_header(a, &e);
			if (ret == ARCHIVE_EOF)
				break;
			ac(ret);
			if (validate_single(a, e, fn, sector_count * 512))
			{
				ac(archive_read_data_skip(a));
			}
			else
			{
				valid_ent_count += 1;
				break;
			}
		}

		ac(archive_read_free(a));
	}


	if (valid_ent_count != ent_count)
		errorx("could not find all files in archive referenced by JOBS.DAT");

	if ((a = archive_read_new()) == NULL)
		error("archive_read_new failed");

	ac(archive_read_support_format_7zip(a));
	ac(archive_read_support_filter_lzma(a));

	lseek(f, 0, SEEK_SET);
	ac(archive_read_open_fd(a, f, 8192));

	infox("Writing chunks...");
	g_current_buffer = 0;
	for (;;) {
		int found_idx;
		ret = archive_read_next_header(a, &e);
		if (ret == ARCHIVE_EOF)
			break;
		ac(ret);
		found_idx = -1;
		for (i = 0; i < ent_count; i += 1)
		{
			char fn[29];
			int sector_count;
			if (ents[i].m_job_type != 3)
				errorx("entry %d in JOBS.DAT has job type %d", i, ents[i].m_job_type);
			strncpy(fn, ents[i].m_filename, sizeof(fn) - 1);
			fn[sizeof(fn) - 1] = 0;
			sector_count = ents[i].m_end_sector - ents[i].m_start_sector;
			if (validate_single(a, e, fn, sector_count * 512))
				continue;
			found_idx = i;
			break;
		}
		if (found_idx < 0)
		{
			ac(archive_read_data_skip(a));
			continue;
		}

		ssize_t len;
		uint32_t written_sectors;

		written_sectors = 0;
		for (;;)
		{
			len = archive_read_data(a, g_buffer[g_current_buffer].m_buf, sizeof(g_buffer[g_current_buffer].m_buf));

			if (len < 0)
				ac(len);

			/* EOF */
			if (len == 0)
				break;

			if (hddWriteSectors(ents[found_idx].m_start_sector + written_sectors, len / 512, g_current_buffer))
				error("Disk write error");

			written_sectors += len / 512;
			g_current_buffer ^= 1;
		}
	}

	if (hddWriteSectors(0, 0, 0))
		error("Disk write error");

	ac(archive_read_free(a));
	close(f);
	if (ents)
		free(ents);
	return 0;
}
