/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
#
# Pad demo app
# Quick and dirty, little or no error checks etc..
# Distributed as is
*/

#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <debug.h>
#include <iopcontrol.h>
#include <libcdvd.h>
#include <sbv_patches.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <stdlib.h>
#include <stdint.h>
#include <delaythread.h>

#include <libmc.h>

#define DEFINITION_FOR_EXTERNAL_IRX(basename) \
    extern u8 basename[]; \
    extern int size_##basename;

#define LOADMODULEBUFFER_EXTERNAL_IRX(basename) \
    { \
        int ret; \
        SifExecModuleBuffer(basename, size_##basename, 0, NULL, &ret); \
        if (ret < 0) { \
            scr_printf("SifExecModuleBuffer " #basename " failed: %d\n", ret); \
            SleepThread(); \
        } \
    }

#define LOADMODULE_ROM_IRX(basename) \
    { \
        int ret; \
        ret = SifLoadModule("rom0:" #basename, 0, NULL); \
        if (ret < 0) { \
            scr_printf("SifLoadModule " #basename " failed: %d\n", ret); \
            SleepThread(); \
        } \
    }

DEFINITION_FOR_EXTERNAL_IRX(iomanX_irx);
DEFINITION_FOR_EXTERNAL_IRX(filexio_irx);
DEFINITION_FOR_EXTERNAL_IRX(mcman_irx);
DEFINITION_FOR_EXTERNAL_IRX(mcman_old_irx);
DEFINITION_FOR_EXTERNAL_IRX(mcserv_irx);
DEFINITION_FOR_EXTERNAL_IRX(mcserv_old_irx);
DEFINITION_FOR_EXTERNAL_IRX(sio2man_irx);
DEFINITION_FOR_EXTERNAL_IRX(sio2man_old_irx);
DEFINITION_FOR_EXTERNAL_IRX(padman_irx);
DEFINITION_FOR_EXTERNAL_IRX(padman_old_irx);
DEFINITION_FOR_EXTERNAL_IRX(rpadman_irx);

/*
 * Local functions
 */

/*
 * loadModules()
 */
static void
loadModules(void)
{
    SifLoadFileInit();
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();

    LOADMODULEBUFFER_EXTERNAL_IRX(iomanX_irx);
    LOADMODULEBUFFER_EXTERNAL_IRX(filexio_irx);
#if 0
    // LOADMODULE_ROM_IRX(SIO2MAN);
    LOADMODULEBUFFER_EXTERNAL_IRX(sio2man_old_irx);
    // LOADMODULE_ROM_IRX(MCMAN);
    // LOADMODULE_ROM_IRX(MCSERV);
    LOADMODULEBUFFER_EXTERNAL_IRX(mcman_old_irx);
    LOADMODULEBUFFER_EXTERNAL_IRX(mcserv_old_irx);
    // LOADMODULE_ROM_IRX(PADMAN);
    // LOADMODULEBUFFER_EXTERNAL_IRX(padman_old_irx);
#else
    LOADMODULEBUFFER_EXTERNAL_IRX(sio2man_irx);
    LOADMODULEBUFFER_EXTERNAL_IRX(mcman_irx);
    LOADMODULEBUFFER_EXTERNAL_IRX(mcserv_irx);
    // LOADMODULEBUFFER_EXTERNAL_IRX(padman_irx);
    // LOADMODULEBUFFER_EXTERNAL_IRX(rpadman_irx);
#endif
}

void do_output(const char* str)
{
    scr_clear();
    scr_printf("%s\n", str);
}

int
main()
{
    int xret;
    SifInitRpc(0);
    while (!SifIopReset("", 0)) {
    };
    while (!SifIopSync()) {
    };
    SifInitRpc(0);

    init_scr();


    scr_printf("Load modules!\n");
    loadModules();

    scr_printf("File Xio init!\n");
    fileXioInit();
    // Increase the FILEIO R/W buffer size to reduce overhead.
    fileXioSetRWBufferSize(128 * 1024);

    scr_printf("libmc init!\n");
    xret = mcInit(MC_TYPE_MC);
    if (xret < 0)
    {
        scr_printf("Failed to initialise memcard server! %d\n", xret);
        SleepThread();
    }

    for (int p = 0; p < 2; p++)
    {
        int mc_Type, mc_Free, mc_Format, ret;

        scr_printf("testing mcGetInfo for mc%d\n", p);
        mcGetInfo(p, 0, &mc_Type, &mc_Free, &mc_Format);
        mcSync(0, NULL, &ret);
        scr_printf("mcGetInfo returned %d\n",ret);
        scr_printf("Type: %d Free: %d Format: %d\n\n", mc_Type, mc_Free, mc_Format);        
        // Assuming that the same memory card is connected, this should return 0
        mcGetInfo(p,0,&mc_Type,&mc_Free,&mc_Format);
        mcSync(0, NULL, &ret);
        scr_printf("mcGetInfo returned %d\n",ret);
        scr_printf("Type: %d Free: %d Format: %d\n\n", mc_Type, mc_Free, mc_Format);
    }
    DelayThread(1000000);
    scr_clear();
    {
#define ARRAY_ENTRIES   64
        int i, ret;
        sceMcTblGetDir mcDir[ARRAY_ENTRIES] __attribute__((aligned(64)));
        mcGetDir(0, 0, "/*", 0, ARRAY_ENTRIES - 10, mcDir);
        mcSync(0, NULL, &ret);
        scr_printf("mcGetDir returned %d\n\nListing of root directory on memory card:\n\n", ret);

        for(i=0; i < ret; i++)
        {
            if(mcDir[i].AttrFile & MC_ATTR_SUBDIR)
                scr_printf("[DIR] %s\n", mcDir[i].EntryName);
            else
                scr_printf("%s - %d bytes\n", mcDir[i].EntryName, mcDir[i].FileSizeByte);
        }
    }

    scr_printf("Goto sleep!\n");
    SleepThread();

    return 0;
}
