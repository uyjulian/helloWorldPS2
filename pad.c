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

#include <libpad.h>

#define CDVD_R_NDIN ((volatile u8 *)0xBF402005)
#define CDVD_R_POFF ((volatile u8 *)0xBF402008)
#define CDVD_R_SCMD ((volatile u8 *)0xBF402016)
#define CDVD_R_SDIN ((volatile u8 *)0xBF402017)

const uint32_t crc32_tab[] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

uint32_t crc32(const void *buf, size_t size)
{
    const uint8_t *p = buf;
    uint32_t crc;

    crc = ~0U;
    while (size--)
        crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return crc ^ ~0U;
}

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
    LOADMODULEBUFFER_EXTERNAL_IRX(padman_old_irx);
#else
    LOADMODULEBUFFER_EXTERNAL_IRX(sio2man_irx);
    // LOADMODULEBUFFER_EXTERNAL_IRX(mcman_irx);
    // LOADMODULEBUFFER_EXTERNAL_IRX(mcserv_irx);
    LOADMODULEBUFFER_EXTERNAL_IRX(padman_irx);
    // LOADMODULEBUFFER_EXTERNAL_IRX(rpadman_irx);
#endif
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
            scr_printf("Please wait, pad(%d,%d) is in state %s %d\n",
                       port, slot, stateString, padGetReqState(port, slot));
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

    scr_printf("Pad init! %d\n", padInit(0));



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
