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
#include <libmouse.h>
#include <string.h>
#include <librm.h>

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
DEFINITION_FOR_EXTERNAL_IRX(filexio_irx);
DEFINITION_FOR_EXTERNAL_IRX(usbd_irx);
DEFINITION_FOR_EXTERNAL_IRX(bdm_irx);
DEFINITION_FOR_EXTERNAL_IRX(bdmfs_fatfs_irx);
DEFINITION_FOR_EXTERNAL_IRX(usbmass_bd_irx);
DEFINITION_FOR_EXTERNAL_IRX(rmman2_irx);

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
    LOADMODULEBUFFER_EXTERNAL_IRX(rmman2_irx);
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

int xsceCdRcBypassCtl(u8 arg1, u32 *result)
{
    int res;
    u8 out[16];
    u8 in[16];

    if (result) {
        *result = 0;
    }
    {
        in[0] = arg1;
        res   = sceCdApplySCmd(0x24, in, 1, out);
        if (result) {
            *result = (u8)out[0];
        }
    }
    return res;
}

static const char *getRmStatus(u32 status)
{
    switch (status)
    {
        case RM_INIT:
            return "INITIALIZING";
        case RM_READY:
            return "READY";
        case RM_KEYPRESSED:
            return "PRESSED";
        case RM_NOREMOTE:
            return "DISCONNECTED";
        default:
            return "UNKNOWN";
    }
}

static const char *getRmButton(u32 button)
{
    switch (button)
    {
        case RM_DVD_ONE:
            return "RM_DVD_ONE";
        case RM_DVD_TWO:
            return "RM_DVD_TWO";
        case RM_DVD_THREE:
            return "RM_DVD_THREE";
        case RM_DVD_FOUR:
            return "RM_DVD_FOUR";
        case RM_DVD_FIVE:
            return "RM_DVD_FIVE";
        case RM_DVD_SIX:
            return "RM_DVD_SIX";
        case RM_DVD_SEVEN:
            return "RM_DVD_SEVEN";
        case RM_DVD_EIGHT:
            return "RM_DVD_EIGHT";
        case RM_DVD_NINE:
            return "RM_DVD_NINE";
        case RM_DVD_ZERO:
            return "RM_DVD_ZERO";
        case RM_DVD_ENTER:
            return "RM_DVD_ENTER";
        case RM_DVD_BROWSE:
            return "RM_DVD_BROWSE";
        case RM_DVD_SET:
            return "RM_DVD_SET";
        case RM_DVD_RETURN:
            return "RM_DVD_RETURN";
        case RM_DVD_CLEAR:
            return "RM_DVD_CLEAR";
        case RM_DVD_SOURCE:
            return "RM_DVD_SOURCE";
        case RM_DVD_CHUP:
            return "RM_DVD_CHUP";
        case RM_DVD_CHDOWN:
            return "RM_DVD_CHDOWN";
        case RM_DVD_REC:
            return "RM_DVD_REC";
        case RM_DVD_TITLE:
            return "RM_DVD_TITLE";
        case RM_DVD_MENU:
            return "RM_DVD_MENU";
        case RM_DVD_PROGRAM:
            return "RM_DVD_PROGRAM";
        case RM_DVD_TIME:
            return "RM_DVD_TIME";
        case RM_DVD_ATOB:
            return "RM_DVD_ATOB";
        case RM_DVD_REPEAT:
            return "RM_DVD_REPEAT";
        case RM_DVD_PREV:
            return "RM_DVD_PREV";
        case RM_DVD_NEXT:
            return "RM_DVD_NEXT";
        case RM_DVD_PLAY:
            return "RM_DVD_PLAY";
        case RM_DVD_SCAN_BACK:
            return "RM_DVD_SCAN_BACK";
        case RM_DVD_SCAN_FORW:
            return "RM_DVD_SCAN_FORW";
        case RM_DVD_SHUFFLE:
            return "RM_DVD_SHUFFLE";
        case RM_DVD_STOP:
            return "RM_DVD_STOP";
        case RM_DVD_PAUSE:
            return "RM_DVD_PAUSE";
        case RM_DVD_DISPLAY:
            return "RM_DVD_DISPLAY";
        case RM_DVD_SLOW_BACK:
            return "RM_DVD_SLOW_BACK";
        case RM_DVD_SLOW_FORW:
            return "RM_DVD_SLOW_FORW";
        case RM_DVD_SUBTITLE:
            return "RM_DVD_SUBTITLE";
        case RM_DVD_AUDIO:
            return "RM_DVD_AUDIO";
        case RM_DVD_ANGLE:
            return "RM_DVD_ANGLE";
        case RM_DVD_UP:
            return "RM_DVD_UP";
        case RM_DVD_DOWN:
            return "RM_DVD_DOWN";
        case RM_DVD_LEFT:
            return "RM_DVD_LEFT";
        case RM_DVD_RIGHT:
            return "RM_DVD_RIGHT";
        case RM_PS2_RESET:
            return "RM_PS2_RESET";
        case RM_PS2_EJECT:
            return "RM_PS2_EJECT";
        case RM_PS2_POWERON:
            return "RM_PS2_POWERON";
        case RM_PS2_POWEROFF:
            return "RM_PS2_POWEROFF";
        case RM_PS2_SELECT:
            return "RM_PS2_SELECT";
        case RM_PS2_L3:
            return "RM_PS2_L3";
        case RM_PS2_R3:
            return "RM_PS2_R3";
        case RM_PS2_START:
            return "RM_PS2_START";
        case RM_PS2_UP:
            return "RM_PS2_UP";
        case RM_PS2_RIGHT:
            return "RM_PS2_RIGHT";
        case RM_PS2_DOWN:
            return "RM_PS2_DOWN";
        case RM_PS2_LEFT:
            return "RM_PS2_LEFT";
        case RM_PS2_L2:
            return "RM_PS2_L2";
        case RM_PS2_R2:
            return "RM_PS2_R2";
        case RM_PS2_L1:
            return "RM_PS2_L1";
        case RM_PS2_R1:
            return "RM_PS2_R1";
        case RM_PS2_TRIANGLE:
            return "RM_PS2_TRIANGLE";
        case RM_PS2_CIRCLE:
            return "RM_PS2_CIRCLE";
        case RM_PS2_CROSS:
            return "RM_PS2_CROSS";
        case RM_PS2_SQUARE:
            return "RM_PS2_SQUARE";
        case RM_RELEASED:
            return "RM_RELEASED";
        case RM_IDLE:
            return "RM_IDLE";
        default:
            return "UNKNOWN";
    }
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

#if 0
    scr_printf("RC gameplay!\n");
    while ( 1 )
    {
        u32 rek;
        u32 r;
        r = xsceCdRcBypassCtl(0, &rek);
        if ( (rek & 0x100) != 0 )
            break;
        if ( (rek & 0x80) == 0 && r )
            break;
    }
#endif

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

    /* Initialize the RMMAN RPC service */
    scr_printf("Rm init!\n");
    RMMan_Init();
    scr_printf("Rm init done!\n");
    scr_printf("Module version: 0x%04x\n", RMMan_GetModuleVersion());
    static u8 rmData[256] __attribute__((aligned(64)));
    struct remote_data rmdata, rmolddata;

    scr_printf("Rm open!\n");
    scr_printf("Res %d\n", RMMan_Open(0, 0, rmData));
    scr_printf("Rm open done!\n");
    memset(&rmolddata, 0, sizeof(rmolddata));

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

        {
            RMMan_Read(0, 0, &rmdata);
            if ((rmolddata.status != rmdata.status) || (rmolddata.button != rmdata.button))
            {
                do_output("RM");
                scr_printf("%08x %08x\n", rmdata.status, rmdata.button);
                scr_printf("%s %s\n", getRmStatus(rmdata.status), getRmButton(rmdata.button));
            }
            memcpy(&rmolddata, &rmdata, sizeof(rmdata));
        }

    } // for

    scr_printf("Goto sleep!\n");
    SleepThread();

    return 0;
}
