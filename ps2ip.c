// Attempt to dump DVRP flash and internal IPL (failure due to DMA source restriction)

#include <stdio.h>
#include <kernel.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <debug.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <stdlib.h>
#include <string.h>
#include <hdd-ioctl.h>
#include <libpwroff.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <libcdvd.h>
#include <io_common.h>
#include <librm.h>

extern unsigned char DEV9_irx[];
extern unsigned int size_DEV9_irx;

extern unsigned char IOMANX_irx[];
extern unsigned int size_IOMANX_irx;

extern unsigned char FILEXIO_irx[];
extern unsigned int size_FILEXIO_irx;

extern unsigned char POWEROFF_irx[];
extern unsigned int size_POWEROFF_irx;

extern unsigned char DVRDRV_irx[];
extern unsigned int size_DVRDRV_irx;

extern unsigned char DVRMISC_irx[];
extern unsigned int size_DVRMISC_irx;

extern unsigned char RMMANX_irx[];
extern unsigned int size_RMMANX_irx;

extern unsigned char RMMAN2_irx[];
extern unsigned int size_RMMAN2_irx;

static void poweroffCallback(void *arg);

static int VblankStartSema, VblankEndSema;

static s32 VblankStartHandler(s32 cause)
{
    iSignalSema(VblankStartSema);

    /* As per the SONY documentation, call ExitHandler() at the very end of
       your interrupt handler. */
    ExitHandler();
    return 0;
}

static s32 VblankEndHandler(s32 cause)
{
    iSignalSema(VblankEndSema);

    /* As per the SONY documentation, call ExitHandler() at the very end of
       your interrupt handler. */
    ExitHandler();
    return 0;
}

int main(int argc, char *argv[])
{
	//Reboot IOP
	SifInitRpc(0);
	while(!SifIopReset("", 0)){};
	while(!SifIopSync()){};

	//Initialize SIF services
	SifInitRpc(0);
	SifLoadFileInit();
	SifInitIopHeap();
	sbv_patch_enable_lmb();

	//Load modules
	SifExecModuleBuffer(DEV9_irx, size_DEV9_irx, 0, NULL, NULL);

	SifExecModuleBuffer(IOMANX_irx, size_IOMANX_irx, 0, NULL, NULL);
	SifExecModuleBuffer(FILEXIO_irx, size_FILEXIO_irx, 0, NULL, NULL);


	fileXioInit();
	SifExecModuleBuffer(POWEROFF_irx, size_POWEROFF_irx, 0, NULL, NULL);
	poweroffInit();
	poweroffSetCallback(&poweroffCallback, NULL);

	init_scr();

	scr_printf("Initing DVR:\n");
#if 0
	{
		u8 out[16];
		u8 in[16];

		in[0] = 0;
		// sceCdXDVRPReset
		sceCdApplySCmd(0x33, in, 1, out);
	}
#endif
	{
		u8 out[16];
		u8 in[16];

		in[0] = 0;
		// sceCdNoticeGameStart
        // Needed for front button info to be read
		sceCdApplySCmd(0x29, in, 1, out);
	}
	//Load modules
#if 1
	SifExecModuleBuffer(DVRDRV_irx, size_DVRDRV_irx, 0, NULL, NULL);
    SifExecModuleBuffer(DVRMISC_irx, size_DVRMISC_irx, 0, NULL, NULL);
    SifExecModuleBuffer(RMMANX_irx, size_RMMANX_irx, 0, NULL, NULL);
#else
    SifExecModuleBuffer(RMMAN2_irx, size_RMMAN2_irx, 0, NULL, NULL);
#endif
    ee_sema_t sema;
    memset(&sema, 0, sizeof(sema));
    /* Prepare semaphores, for detecting Vertical-Blanking events. */
    sema.max_count  = 1;

    VblankStartSema = CreateSema(&sema);
    VblankEndSema   = CreateSema(&sema);

    /* Register VBlank start and end interrupt handlers. */
    AddIntcHandler(INTC_VBLANK_S, &VblankStartHandler, 0);
    AddIntcHandler(INTC_VBLANK_E, &VblankEndHandler, 0);


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

    /* Enable interrupt handlers */
    _EnableIntc(INTC_VBLANK_S);
    _EnableIntc(INTC_VBLANK_E);

    int wrap;
    int startY;

    /* In order to preserve the messages above,
       preserve the current Y coordinate. */
    startY = scr_getY();

    for (;;)
    {
        WaitSema(VblankStartSema);
        WaitSema(VblankEndSema);

        {
            RMMan_Read(0, 0, &rmdata);
            if ((rmolddata.status != rmdata.status) || (rmolddata.button != rmdata.button) || (rmolddata.front_button != rmdata.front_button))
            {
                /* Do not draw past the end of the screen. If this is the last line,
                   prepare to wrap around. */
                if (scr_getY() + 1 >= 27)
                    wrap = 1;

                scr_clear();
                scr_printf("RM ");
                scr_printf("%08x %08x %08x\n", rmdata.status, rmdata.button, rmdata.front_button);
                if (wrap)
                {
                    scr_setXY(0, startY);
                    wrap = 0;
                }
            }
            memcpy(&rmolddata, &rmdata, sizeof(rmdata));
        }
    } // for
	SleepThread();


	//Deinitialize SIF services
	SifExitRpc();

	return 0;
}

static void poweroffCallback(void *arg)
{
	scr_printf("Powering off\n");

    scr_printf("dev9x off\n");
    while (fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0) < 0);

    scr_printf("Shut down!!!\n");
    poweroffShutdown();
}
