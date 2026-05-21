/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
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
#include <time.h>
#include <stdlib.h>
#include <osd_config.h>
#include <elf-loader.h>

static int call_osdsys(void)
{
    int args = 0;
    static char *argv[1];
#if 0
    args++;
    argv[0] = "BootBrowser";
#endif
    LoadELFFromFileWithPartition("rom0:OSDSYS", NULL, args, argv);
    return 0;
}

int main(int ac, char **av)
{
    (void)ac;
    (void)av;

    SifInitRpc(0);
    while (!SifIopReset("", 0)) {};
    while (!SifIopSync()) {};
    SifInitRpc(0);

    call_osdsys();

    scr_printf("OSDSYS seems to not have loaded!\n");
    SleepThread();

    return 0;
}
