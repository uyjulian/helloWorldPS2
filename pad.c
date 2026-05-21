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

#include "libpwroff.h"

extern u8 poweroff_irx[];
extern int size_poweroff_irx;

extern u8 ps2dev9_irx[];
extern int size_ps2dev9_irx;

extern u8 atad_irx[];
extern int size_atad_irx;

extern u8 mymodule_irx[];
extern int size_mymodule_irx;

/*
 * Local functions
 */

/*
 * loadModules()
 */
static void
loadModules(void)
{
    int ret;

    SifLoadFileInit();
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();

    SifExecModuleBuffer(poweroff_irx, size_poweroff_irx, 0, NULL, &ret);
    if (ret < 0) {
        scr_printf("SifExecModuleBuffer poweroff failed: %d\n", ret);
        SleepThread();
    }
    SifExecModuleBuffer(ps2dev9_irx, size_ps2dev9_irx, 0, NULL, &ret);
    if (ret < 0) {
        scr_printf("SifExecModuleBuffer ps2dev9 failed: %d\n", ret);
        SleepThread();
    }
    SifExecModuleBuffer(atad_irx, size_atad_irx, 0, NULL, &ret);
    if (ret < 0) {
        scr_printf("SifExecModuleBuffer atad failed: %d\n", ret);
        SleepThread();
    }
    SifExecModuleBuffer(mymodule_irx, size_mymodule_irx, 0, NULL, &ret);
    if (ret < 0) {
        scr_printf("SifExecModuleBuffer mymodule failed: %d\n", ret);
        SleepThread();
    }
    scr_printf("Modules have been loaded\n");
}

int
main()
{
    SifInitRpc(0);
    while (!SifIopReset("", 0)) {
    };
    while (!SifIopSync()) {
    };
    SifInitRpc(0);
    sceCdInit(SCECdINoD);

    init_scr();

    scr_printf("Load modules!\n");
    loadModules();

#if 0
    poweroffInit();
    poweroffSetCallback(&poweroffCallback, NULL);
#endif
    scr_printf("Goto sleep!\n");
    SleepThread();

    return 0;
}
