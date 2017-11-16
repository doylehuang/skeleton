#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

/*----------------------------------------------------------------*/
/* Main Event Loop                                                */
#define PHYSICAL_I2C 7
#define PSU_NUM 6

#define NOTIFY_COMPLETE_PATH "/run/obmc/node_init_complete"

static void notify_node_init_complete(void)
{
    FILE *file;
    file=fopen(NOTIFY_COMPLETE_PATH, "w+");
    if (file)
        fclose(file);
}

int
main(int argc, char *argv[])
{
    char buff_path[256] = "";
    int i = 0;

    /* Check the ntp server address in EEPROM */
    system("python /usr/sbin/ntp_eeprom.py --check-ntp");

    /* Init pmbus node */
    for(i=1; i<=PSU_NUM; i++)
    {
	    /* Liteon PS-2162-1F Solution */
	    sprintf(buff_path, "i2craw.exe %d 0x58 -f -w \"0x05 0x04 0x01 0x1b 0x7c 0xff\"", PHYSICAL_I2C+i);
	    printf("%s\n", buff_path);
	    system(buff_path);
    }

    /* Export sys throttle gpio node */
    system("gpioutil -n RM_SYS_THROTTLE_N");
    system("gpioutil -n FIO_RM_SYS_THROTTLE_N");

    notify_node_init_complete(); // notify hwmon for submanage system to trigger SEL

    return 0;
}
