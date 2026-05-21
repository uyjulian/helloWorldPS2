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

#include "libpad.h"

#define CDVD_R_NDIN ((volatile u8 *)0xBF402005)
#define CDVD_R_POFF ((volatile u8 *)0xBF402008)
#define CDVD_R_SCMD ((volatile u8 *)0xBF402016)
#define CDVD_R_SDIN ((volatile u8 *)0xBF402017)


/*
 * Global var's
 */
// pad_dma_buf is provided by the user, one buf for each pad
// contains the pad's current state
static char padBuf[256] __attribute__((aligned(64)));

static char actAlign[6];
static int actuators;

extern u8 iomanX_irx[];
extern int size_iomanX_irx;

#define DEFINITION_FOR_EXTERNAL_IRX(basename) \
    extern u8 basename[]; \
    extern int size_##basename;

#define LOADMODULEBUFFER_EXTERNAL_IRX(basename) \
    { \
        int ret; \
        SifExecModuleBuffer(basename, size_##basename, 0, NULL, &ret); \
        if (ret < 0) { \
            scr_printf("SifExecModuleBuffer " #basename " failed: %d\n", ret); \
        } \
    }

DEFINITION_FOR_EXTERNAL_IRX(iomanX_irx);
DEFINITION_FOR_EXTERNAL_IRX(iomanX_irx);
DEFINITION_FOR_EXTERNAL_IRX(filexio_irx);
DEFINITION_FOR_EXTERNAL_IRX(usbd_irx);
DEFINITION_FOR_EXTERNAL_IRX(bdm_irx);
DEFINITION_FOR_EXTERNAL_IRX(bdmfs_fatfs_irx);
DEFINITION_FOR_EXTERNAL_IRX(usbmass_bd_irx);

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

    ret = SifLoadModule("rom0:SIO2MAN", 0, NULL);
    if (ret < 0) {
        scr_printf("sifLoadModule sio failed: %d\n", ret);
        SleepThread();
    }

    ret = SifLoadModule("rom0:PADMAN", 0, NULL);
    if (ret < 0) {
        scr_printf("sifLoadModule pad failed: %d\n", ret);
        SleepThread();
    }

    LOADMODULEBUFFER_EXTERNAL_IRX(iomanX_irx);
    LOADMODULEBUFFER_EXTERNAL_IRX(filexio_irx);
    LOADMODULEBUFFER_EXTERNAL_IRX(usbd_irx);
    LOADMODULEBUFFER_EXTERNAL_IRX(bdm_irx);
    LOADMODULEBUFFER_EXTERNAL_IRX(bdmfs_fatfs_irx);
    LOADMODULEBUFFER_EXTERNAL_IRX(usbmass_bd_irx);
}

/*
 * waitPadReady()
 */
static int waitPadReady(int port, int slot)
{
    int state;
    int lastState;
    char stateString[16];

    state = padGetState(port, slot);
    lastState = -1;
    while((state != PAD_STATE_STABLE) && (state != PAD_STATE_FINDCTP1)) {
        if (state != lastState) {
            padStateInt2String(state, stateString);
            scr_printf("Please wait, pad(%d,%d) is in state %s\n",
                       port, slot, stateString);
        }
        lastState = state;
        state=padGetState(port, slot);
    }
    // Were the pad ever 'out of sync'?
    if (lastState != -1) {
        scr_printf("Pad OK!\n");
    }
    return 0;
}


/*
 * initializePad()
 */
static int
initializePad(int port, int slot)
{

    int ret;
    int modes;
    int i;

    waitPadReady(port, slot);

    // How many different modes can this device operate in?
    // i.e. get # entrys in the modetable
    modes = padInfoMode(port, slot, PAD_MODETABLE, -1);
    scr_printf("The device has %d modes\n", modes);

    if (modes > 0) {
        scr_printf("( ");
        for (i = 0; i < modes; i++) {
            scr_printf("%d ", padInfoMode(port, slot, PAD_MODETABLE, i));
        }
        scr_printf(")");
    }

    scr_printf("It is currently using mode %d\n",
               padInfoMode(port, slot, PAD_MODECURID, 0));

    // If modes == 0, this is not a Dual shock controller
    // (it has no actuator engines)
    if (modes == 0) {
        scr_printf("This is a digital controller?\n");
        return 1;
    }

    // Verify that the controller has a DUAL SHOCK mode
    i = 0;
    do {
        if (padInfoMode(port, slot, PAD_MODETABLE, i) == PAD_TYPE_DUALSHOCK)
            break;
        i++;
    } while (i < modes);
    if (i >= modes) {
        scr_printf("This is no Dual Shock controller\n");
        return 1;
    }

    // If ExId != 0x0 => This controller has actuator engines
    // This check should always pass if the Dual Shock test above passed
    ret = padInfoMode(port, slot, PAD_MODECUREXID, 0);
    if (ret == 0) {
        scr_printf("This is no Dual Shock controller??\n");
        return 1;
    }

    scr_printf("Enabling dual shock functions\n");

    // When using MMODE_LOCK, user cant change mode with Select button
    padSetMainMode(port, slot, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);

    waitPadReady(port, slot);
    scr_printf("infoPressMode: %d\n", padInfoPressMode(port, slot));

    waitPadReady(port, slot);
    scr_printf("enterPressMode: %d\n", padEnterPressMode(port, slot));

    waitPadReady(port, slot);
    actuators = padInfoAct(port, slot, -1, 0);
    scr_printf("# of actuators: %d\n",actuators);

    if (actuators != 0) {
        actAlign[0] = 0;   // Enable small engine
        actAlign[1] = 1;   // Enable big engine
        actAlign[2] = 0xff;
        actAlign[3] = 0xff;
        actAlign[4] = 0xff;
        actAlign[5] = 0xff;

        waitPadReady(port, slot);
        scr_printf("padSetActAlign: %d\n",
                   padSetActAlign(port, slot, actAlign));
    }
    else {
        scr_printf("Did not find any actuators.\n");
    }

    waitPadReady(port, slot);

    return 1;
}

void do_output(const char* str)
{
    scr_clear();
    scr_printf("%s\n", str);
}

int
main()
{
    int ret;
    int port, slot;
    int i;
    struct padButtonStatus buttons;
    u32 paddata;
    u32 old_pad = 0;
    u32 new_pad;


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

    scr_printf("File Xio init!\n");
    fileXioInit();
    // Increase the FILEIO R/W buffer size to reduce overhead.
    fileXioSetRWBufferSize(128 * 1024);

    scr_printf("Pad init!\n");
    padInit(0);



    port = 0; // 0 -> Connector 1, 1 -> Connector 2
    slot = 0; // Always zero if not using multitap

    scr_printf("PortMax: %d\n", padGetPortMax());
    scr_printf("SlotMax: %d\n", padGetSlotMax(port));


    if((ret = padPortOpen(port, slot, padBuf)) == 0) {
        scr_printf("padOpenPort failed: %d\n", ret);
        SleepThread();
    }

    if(!initializePad(port, slot)) {
        scr_printf("pad initalization failed!\n");
        SleepThread();
    }

    padEnterPressMode(port, slot);

    for (;;) {      // We are phorever people

        i=0;
        ret=padGetState(port, slot);
        while((ret != PAD_STATE_STABLE) && (ret != PAD_STATE_FINDCTP1)) {
            if(ret==PAD_STATE_DISCONN) {
                scr_printf("Pad(%d, %d) is disconnected\n", port, slot);
            }
            ret=padGetState(port, slot);
        }
        if(i==1) {
            scr_printf("Pad: OK!\n");
        }

        ret = padRead(port, slot, &buttons); // port, slot, buttons

        if (ret != 0) {
            paddata = 0xffff ^ buttons.btns;

            new_pad = paddata & ~old_pad;
            old_pad = paddata;

            // Directions
            if(new_pad & PAD_LEFT) {
                do_output("LEFT");
                
                {
                    iox_stat_t chk_stat;
                    char mass_path[8] = "mass0:/";
                    char mass_path2[9] = "mass0:/.";
                    for (i = 0; i < 10; i++) {
                        mass_path[4] = '0' + i;
                        mass_path2[4] = '0' + i;
                        scr_printf("%s %d\n", mass_path, fileXioGetStat(mass_path, &chk_stat));
                        iox_dirent_t record;
                        int n = 0, dd = -1;

                        if ((dd = fileXioDopen(mass_path2)) >= 0)
                        {
                            while (fileXioDread(dd, &record) > 0) {
                                n += 1;
                            }
                            fileXioDclose(dd);  // Close directory if opened above
                        }
                        scr_printf("%d %d\n", dd, n);
                        
                    }  // ends for loop
                }

            }
            if(new_pad & PAD_DOWN) {
                do_output("DOWN");
            }
            if(new_pad & PAD_RIGHT) {
                do_output("RIGHT");
            }
            if(new_pad & PAD_UP) {
                do_output("UP");
            }
            if(new_pad & PAD_START) {
                do_output("START");
            }
            if(new_pad & PAD_R3) {
                do_output("R3");
            }
            if(new_pad & PAD_L3) {
                do_output("L3");
            }
            if(new_pad & PAD_SELECT) {
                do_output("SELECT");
            }
            if(new_pad & PAD_SQUARE) {
                do_output("SQUARE");
            }
            if(new_pad & PAD_CROSS) {
                do_output("CROSS");
            }
            if(new_pad & PAD_CIRCLE) {
                do_output("CIRCLE");
            }
            if(new_pad & PAD_TRIANGLE) {
                do_output("TRIANGLE");
            }
            if(new_pad & PAD_R1) {
                do_output("R1");
            }
            if(new_pad & PAD_L1) {
                do_output("L1");
            }
            if(new_pad & PAD_R2) {
                do_output("R2");
            }
            if(new_pad & PAD_L2) {
                do_output("L2");
            }
        }

    } // for

    scr_printf("Goto sleep!\n");
    SleepThread();

    return 0;
}
