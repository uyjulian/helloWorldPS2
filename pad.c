#include <elf-loader.h>
#include <stdio.h>
#include <kernel.h>

int main() {
    int my_ac;
    char *my_av[2];
    int ret;

    my_ac = 1;
    my_av[0] = "BootClock";
    my_av[1] = NULL;
    ret = LoadELFFromFile("rom0:OSDSYS", my_ac, my_av);
    printf("Load failure %d\n", ret);
    // ExecOSD(my_ac, my_av);
    SleepThread();
    
    return 0;
}
