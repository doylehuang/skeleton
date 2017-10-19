#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#define NTP_OFFSET 0x2030
#define EEPROM_PATH "/sys/devices/platform/ahb/ahb:apb/1e78a000.i2c/i2c-4/i2c-4/4-0050/eeprom"
#define NTP_SYNC_PERIOD 3600 // 1hr
#define NTP_SYNC_SUCCESS 0
#define NTP_SENSOR_NUM 0x81
#define NTP_SENSOR_TYPE 0x12
#define NTP_EVENT_TYPE 0x71
#define EVENT_DIR_ASSERT 0x0
#define EVENT_DIR_DEASSERT 0x1
#define EVD_SYNC_TIME 0x1
#define EVD_SYNC_TIME_FAIL 0x3

int sntp_sync()
{
    FILE* fd;
    char buf[4];
    int stat = 0;
    int prevstat = 0;
    int assert_ntp_failed = 0;
    int event_dir_type = 0;
    int evd1 = 0;
    char cmd[256];
    struct timeval tv_pre, tv_cur;
    int rc;
    int timeshift;

    while (1) {
        fd = fopen(EEPROM_PATH, "r");
        if (fd == NULL) {
            printf("open eeprom read ntp address failed\n");
            return -1;
        }
        /* Get NTP server address from eeprom */
        fseek(fd, NTP_OFFSET, SEEK_SET);
        rc = fread(buf, 1, 4, fd);
        fclose(fd);
        if (rc == 0) {
            printf("read ntp address from eeprom failed\n");
            return -1;
        }
        printf("eeprom ntp address = %d %d %d %d, doing period sync\n", buf[0], buf[1], buf[2], buf[3]);

        /* Get current timestamp */
        gettimeofday(&tv_pre,NULL);

        /* Execute sntp sync command */
        sprintf(cmd, "/usr/sbin/sntp -d -S %d.%d.%d.%d", buf[0], buf[1], buf[2], buf[3]);
        stat = WEXITSTATUS(system(cmd));
        printf("sntp synced stat = %d\n", stat);

        if (stat == NTP_SYNC_SUCCESS) {
            if (assert_ntp_failed) {
                printf("De-Assert NTP sync failed\n");
                event_dir_type = (EVENT_DIR_DEASSERT << 7) | NTP_EVENT_TYPE;
                evd1 = EVD_SYNC_TIME_FAIL;
                sprintf(cmd, "/usr/sbin/eventctl.exe add Critical 0x%x 0x%x 0x%x 0x%x", NTP_SENSOR_TYPE, NTP_SENSOR_NUM, \
                    event_dir_type, evd1);
                system(cmd);

                sprintf(cmd, "python /usr/sbin/bmclogevent_ctl.py  %d  %d", NTP_SENSOR_NUM, (EVENT_DIR_DEASSERT << 7));
                system(cmd);
                assert_ntp_failed = 0;
            } else {
                gettimeofday(&tv_cur,NULL);
                timeshift = abs(tv_cur.tv_sec - tv_pre.tv_sec);
                // If the timestamp diff is more than 1 second, we should log the synced success event
                if (timeshift > 1) {
                    printf("Synced with NTP server\n");
                    event_dir_type = (EVENT_DIR_ASSERT << 7) | NTP_EVENT_TYPE;
                    evd1 = EVD_SYNC_TIME;
                    sprintf(cmd, "/usr/sbin/eventctl.exe add OK 0x%x 0x%x 0x%x 0x%x", NTP_SENSOR_TYPE, NTP_SENSOR_NUM, \
                        event_dir_type, evd1);
                    system(cmd);
                }
            }
        } else {
            if (!assert_ntp_failed) {
                printf("Assert NTP sync failed\n");
                event_dir_type = (EVENT_DIR_ASSERT << 7) | NTP_EVENT_TYPE;
                evd1 = EVD_SYNC_TIME_FAIL;
                sprintf(cmd, "/usr/sbin/eventctl.exe add Critical 0x%x 0x%x 0x%x 0x%x", NTP_SENSOR_TYPE, NTP_SENSOR_NUM, \
                    event_dir_type, evd1);
                system(cmd);

                sprintf(cmd, "python /usr/sbin/bmclogevent_ctl.py  %d  %d", NTP_SENSOR_NUM, (EVENT_DIR_ASSERT << 7));
                system(cmd);
                assert_ntp_failed = 1;
            }
        }

        sleep(NTP_SYNC_PERIOD);
    }
}

int main(void)
{
    sntp_sync();
    return 0;
}
