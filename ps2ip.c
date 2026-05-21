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
DEFINITION_FOR_EXTERNAL_IRX(fileXio);
DEFINITION_FOR_EXTERNAL_IRX(bdm);
DEFINITION_FOR_EXTERNAL_IRX(bdmfs_fatfs);
DEFINITION_FOR_EXTERNAL_IRX(poweroff);
DEFINITION_FOR_EXTERNAL_IRX(ps2dev9);
DEFINITION_FOR_EXTERNAL_IRX(ps2atad);
DEFINITION_FOR_EXTERNAL_IRX(ps2hdd);
DEFINITION_FOR_EXTERNAL_IRX(dvrdrv);
DEFINITION_FOR_EXTERNAL_IRX(dvrfile);
DEFINITION_FOR_EXTERNAL_IRX(usbd);
DEFINITION_FOR_EXTERNAL_IRX(usbmass_bd);

extern int prog_main(int ac, char **av);

static void poweroffCallback(void *arg)
{
    scr_printf("Powering dev9 off\n");
    while (fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0) < 0);

    scr_printf("Shutting down via mechacon\n");
    poweroffShutdown();
    scr_printf("It is now safe to shut down by holding the power button\n");
}


int main(int ac, char **av)
{
	int hdd_ok;
#if 0
	int dvr_hdd_ok;
#endif

	hdd_ok = 0;
#if 0
	dvr_hdd_ok = 0;
#endif
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
	LOADMODULEBUFFER_EXTERNAL_IRX(fileXio);

	fileXioInit();

	LOADMODULEBUFFER_EXTERNAL_IRX(bdm);
	LOADMODULEBUFFER_EXTERNAL_IRX(bdmfs_fatfs);
	LOADMODULEBUFFER_EXTERNAL_IRX(usbd);
	LOADMODULEBUFFER_EXTERNAL_IRX(usbmass_bd);

	scr_printf("Waiting for USB drive...\n");
	scr_printf("Need help? Please visit https://uyjulian.github.io/desr-help/\n");
	{
		iox_stat_t chk_stat;
		while (fileXioGetStat("mass0:/", &chk_stat) < 0)
		{
			sleep(1);
		}
	}

	LOADMODULEBUFFER_EXTERNAL_IRX(poweroff);
	poweroffInit();
	poweroffSetCallback(&poweroffCallback, NULL);

	LOADMODULEBUFFER_EXTERNAL_IRX(ps2dev9);
	{
		u16 val;
		if (!SifIopGetVal(0xb0000004, &val, LF_VAL_SHORT) && ((val & 0x02) != 0))
		{
			int hddstat;
			LOADMODULEBUFFER_EXTERNAL_IRX(ps2atad);
			LOADMODULEBUFFER_EXTERNAL_IRX(ps2hdd);
			hddstat = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
		    if (hddstat == 0 || hddstat == 1)
		    	hdd_ok = 1;
		}
	}

#if 0
	{
		u16 val;
		if (!SifIopGetVal(0xb0000004, &val, LF_VAL_SHORT) && ((val & 0x10) != 0))
		{
			// Needed in order for dvrdrv to work
			{
				u8 out[16];
				u8 in[16];

				in[0] = 0;
				// sceCdNoticeGameStart
				sceCdApplySCmd(0x29, in, 1, out);
			}
			LOADMODULEBUFFER_EXTERNAL_IRX(dvrdrv);
			LOADMODULEBUFFER_EXTERNAL_IRX(dvrfile);
		    if (!fileXioDevctl("dvr_hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0) && (fileXioDevctl("dvr_hdd0:", HDIOC_ISLBA48, NULL, 0, NULL, 0) == 1))
		    {
		    	scr_printf("DVR HDD is available!\n");
		    	dvr_hdd_ok = 1;
		    }
		}
	}
#endif

	if (!hdd_ok)
		fatal("Fatal: HDD not available\n");

	scr_clear();
	scr_printf("Now processing, please wait a moment...\n");
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
	while (fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0) < 0);
	scr_clear();
	scr_printf("Finished in %f seconds.\n", elapsed);
	scr_printf("Need help? Please visit https://uyjulian.github.io/desr-help/\n");
	scr_printf("Push the power button.\n");
	SleepThread();

	return 0;
}
#endif

#include <stdint.h>
#include <unistd.h>

#ifdef _EE
static uint8_t IOBuffer[2048];
#else
static int g_fd = -1;
#endif

#if 0
static inline uint32_t bswap32(uint32_t val)
{
#if 0
    return __builtin_bswap32(val);
#else
    return (val << 24) + ((val & 0xFF00) << 8) + ((val >> 8) & 0xFF00) + ((val >> 24) & 0xFF);
#endif
}
#endif

int hddReadSectors(uint32_t lba, uint32_t nsectors, void *buf)
{
#ifdef _EE
    hddAtaTransfer_t *args = (hddAtaTransfer_t *)IOBuffer;
    uint32_t lba_offset;

    // filexio is limited to 2048 (4 sectors) bytes both arg and buf buffers.
    // dvrfile is limited to 32768 bytes (64 sectors) both arg and buf buffers.
    for (lba_offset = 0; lba_offset < nsectors; lba_offset += args->size)
    {
    	uint32_t xlba;
    	uint32_t xsize;

        xlba = lba + lba_offset;
        xsize = (nsectors - lba_offset) > 4 ? 4 : (nsectors - lba_offset);

#if 0
        // For dvr_hdd only
        args->lba = bswap32(xlba);
        args->size = bswap32(xsize);
#else
        args->lba = xlba;
        args->size = xsize;
#endif

        if (fileXioDevctl("hdd0:", HDIOC_READSECTOR, args, sizeof(hddAtaTransfer_t), ((u8 *)buf) + (lba_offset * 512), xsize * 512) != 0)
            return -1;
    }
#else
    if (g_fd >= 0 && pread(g_fd, buf, nsectors * 512, lba * 512) != nsectors * 512)
    	return -1;
#endif


    return 0;
}

int hddWriteSectors(uint32_t lba, uint32_t nsectors, const void *buf)
{
#ifdef _EE
    static u8 WriteBuffer[3 * 512 + sizeof(hddAtaTransfer_t)]; // Has to be a different buffer from IOBuffer (input can be in IOBuffer).
    hddAtaTransfer_t *args = (hddAtaTransfer_t *)WriteBuffer;
    uint32_t lba_offset;

    // filexio is limited to 2048 (4 sectors) bytes both arg and buf buffers.
    // dvrfile is limited to 32768 bytes (64 sectors) both arg and buf buffers.
    for (lba_offset = 0; lba_offset < nsectors; lba_offset += args->size)
    {
    	uint32_t xlba;
    	uint32_t xsize;
        xlba = lba + lba_offset;
        xsize = (nsectors - lba_offset) > 3 ? 3 : (nsectors - lba_offset);
        memcpy(args->data, ((u8 *)buf) + (lba_offset * 512), xsize * 512);

#if 0
        // For dvr_hdd only
        args->lba = bswap32(xlba);
        args->size = bswap32(xsize);
#else
        args->lba = xlba;
        args->size = xsize;
#endif
        
        if (fileXioDevctl("hdd0:", HDIOC_WRITESECTOR, args, sizeof(hddAtaTransfer_t) + (xsize * 512), NULL, 0) != 0)
            return -1;
    }
#else
    if (g_fd >= 0 && pwrite(g_fd, buf, nsectors * 512, lba * 512) != nsectors * 512)
    	return -1;
#endif

    return 0;
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

static unsigned char buffer[8192];

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
	char m_filename[16];
	char m_pad3[12];
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
	}

	ac(archive_read_free(a));

	if (!ents || !ent_count)
		errorx("could not find correctly-formed JOBS.DAT in archive");

	int valid_ent_count;
	int i;

	valid_ent_count = 0;
	for (i = 0; i < ent_count; i += 1)
	{
		char fn[17];
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

	for (;;) {
		int found_idx;
		ret = archive_read_next_header(a, &e);
		if (ret == ARCHIVE_EOF)
			break;
		ac(ret);
		found_idx = -1;
		for (i = 0; i < ent_count; i += 1)
		{
			char fn[17];
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
			len = archive_read_data(a, buffer, sizeof(buffer));

			if (len < 0)
				ac(len);

			/* EOF */
			if (len == 0)
				break;

			if (hddWriteSectors(ents[found_idx].m_start_sector + written_sectors, len / 512, buffer))
				error("Disk write error");

			written_sectors += len / 512;
		}
	}

	ac(archive_read_free(a));
	close(f);
	if (ents)
		free(ents);
	return 0;
}
