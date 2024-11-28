#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>

#include <ps2_filesystem_driver.h>

// Used to get BDM driver name
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <usbhdfsd-common.h>
#include <ps2sdkapi.h>

static void reset_IOP() {
    SifInitRpc(0);
#if !defined(DEBUG) || defined(BUILD_FOR_PCSX2)
    /* Comment this line if you don't wanna debug the output */
    while (!SifIopReset(NULL, 0)) {};
#endif

    while (!SifIopSync()) {};
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
}

static void init_drivers() {
    init_ps2_filesystem_driver();
}

static void deinit_drivers() {
    deinit_ps2_filesystem_driver();
}

int main(int argc, char **argv) {
    int res, fd;
    reset_IOP();
    init_drivers();

    printf("Opening FONTM\n");
    fd = open("rom0:FONTM", O_RDONLY);
    if (fd < 0) {
        printf("ERROR: Failed to open: %d, errno: %d\n", fd, errno);
    } else {
        close(fd);
    }

    printf("Done");

    deinit_drivers();
    SleepThread();
}
