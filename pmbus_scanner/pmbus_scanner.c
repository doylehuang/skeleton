#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include "i2c-dev.h"

#define MAX_I2C_DEV_LEN 32
#define PMBUS_SLAVE_ADDR 0x58
#define PHYSICAL_I2C 8
#define PMBUS_NUMBER 6

#define PSU_UPDATE_NOTIFY_FILE_FORMAT "/tmp/pmbus/psu_bus_%d_updating"

static int check_psu_updating_notify(int bus)
{
    char psu_updating_file[128];
    sprintf(psu_updating_file, PSU_UPDATE_NOTIFY_FILE_FORMAT, bus);
    if( access( psu_updating_file, F_OK ) != -1 )
        return 1;
    return 0;
}

int pmbus_scan()
{
    char hwmon_path_device[256];
    char buff_path[256] = "";
    char *hwmon_dir = "/sys/class/hwmon/";
    char actual_path[256]={0};
    char buf[100] = {0};
    char bus_slave_addr[PMBUS_NUMBER][32];
    char *pch;
    char *bus_info;
    char *delim = "/";
    struct stat st;
    DIR *dirp;
    struct dirent * ptr;
    FILE *fp;
    int found[PMBUS_NUMBER];
    int i;

    int file;
    char filename[MAX_I2C_DEV_LEN] = {0};
    int bus;
    int rc = 0;
    int res;

    while(1) {
        for(i=0;i<PMBUS_NUMBER;i++) {
            /* Init pmbus node */
            bus = PHYSICAL_I2C + i;
            if (check_psu_updating_notify(bus) == 1)
                continue;
            sprintf(filename,"/dev/i2c-%d",bus);
            file = open(filename,O_RDWR);
            rc = ioctl(file, I2C_SLAVE, PMBUS_SLAVE_ADDR);
            if (rc == 0) {
                res = i2c_smbus_read_byte(file);
                if (res >= 0) {
                    sprintf(buff_path, "echo 0x58 > /sys/bus/i2c/devices/i2c-%d/delete_device", PHYSICAL_I2C+i);
                    system(buff_path);
                    sleep(1);
                    sprintf(buff_path, "echo pmbus 0x58 > /sys/bus/i2c/devices/i2c-%d/new_device", PHYSICAL_I2C+i);
                    system(buff_path);
                }
            }
            close(file);
        }
        sleep(10);
    }
}

static void save_pid (void) {
    pid_t pid = 0;
    FILE *pidfile = NULL;
    pid = getpid();
    if (!(pidfile = fopen("/run/pmbus_scanner.pid", "w"))) {
        fprintf(stderr, "failed to open pidfile\n");
        return;
    }
    fprintf(pidfile, "%d\n", pid);
    fclose(pidfile);
}

int main(void)
{
    save_pid();
    pmbus_scan();
    return 0;
}

