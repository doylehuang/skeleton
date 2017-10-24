#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <systemd/sd-bus.h>
#include <linux/i2c-dev-user.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sdbus_property.h>
#include <sys/mman.h>

#define MAX_MDOT2_NUM 4
#define MDOT2_WRITE_CMD 0x0
#define PATH_MAX_STRING_SIZE 256

#define PCIE_TEMP_PATH "/run/obmc/sharememory/org/openbmc/sensors/M2/M2_TMP"

typedef struct
{
    uint8_t bus;
    uint8_t slave_addr;
    uint8_t sensor_number;
} pcie_dev_mapping;

pcie_dev_mapping Mdot2[MAX_MDOT2_NUM] = {
    {26, 0x6a, 0x70},
    {27, 0x6a, 0x71},
    {28, 0x6a, 0x72},
    {29, 0x6a, 0x73},
};

int mkdir_p(const char *dir, const mode_t mode) {
    char tmp[PATH_MAX_STRING_SIZE];
    char *p = NULL;
    struct stat sb;
    size_t len;

    /* copy path */
    strncpy(tmp, dir, sizeof(tmp));
    len = strlen(tmp);
    if (len >= sizeof(tmp)) {
        return -1;
    }

    /* remove trailing slash */
    if(tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    /* recursive mkdir */
    for(p = tmp + 1; *p; p++) {
        if(*p == '/') {
            *p = 0;
            /* test path */
            if (stat(tmp, &sb) != 0) {
                /* path does not exist - create directory */
                if (mkdir(tmp, mode) < 0) {
                    return -1;
                }
            } else if (!S_ISDIR(sb.st_mode)) {
                /* not a directory */
                return -1;
            }
            *p = '/';
        }
    }
    /* test path */
    if (stat(tmp, &sb) != 0) {
        /* path does not exist - create directory */
        if (mkdir(tmp, mode) < 0) {
            return -1;
        }
    } else if (!S_ISDIR(sb.st_mode)) {
        /* not a directory */
        return -1;
    }
    return 0;
}

int detect_i2cdevice_slave(int fd, int slave_addr)
{
    int rc;
    int retry = 20;
    unsigned long funcs;

    if (fd > 0) {
        if (ioctl(fd, I2C_FUNCS, &funcs) < 0)
            return -1;
        else
            if (ioctl(fd,I2C_SLAVE, slave_addr)<0)
                return -1;
    }
    else
        return -1;

    while (retry > 0)
    {
        rc = i2c_smbus_write_quick(fd, I2C_SMBUS_WRITE);
        if (rc>=0)
            break;
        usleep(500*1000);
        retry-=1;
    }
    return rc;

}

int get_Mdot2_data(int index)
{
    struct i2c_rdwr_ioctl_data data;
    struct i2c_msg msg[1];
    uint8_t write_bytes[1];
    uint8_t g_read_bytes[4];
    int fd = -1;
    char filename[32] = {0};

    sprintf(filename,"/dev/i2c-%d", Mdot2[index].bus);
    /* open i2c device*/
    fd = open(filename,O_RDWR);
    if (detect_i2cdevice_slave(fd, Mdot2[index].slave_addr)<0)
        return -1;

    memset(&msg, 0, sizeof(msg));

    /* do write 1 byte */
    write_bytes[0] = MDOT2_WRITE_CMD;

    msg[0].addr = Mdot2[index].slave_addr;
    msg[0].flags = 0;
    msg[0].len = sizeof(write_bytes);
    msg[0].buf = write_bytes;
    data.msgs = msg;
    data.nmsgs = 1;
    if (ioctl(fd, I2C_RDWR, &data) < 0) {
        close(fd);
        return -1;
    }

    /* do read 4 bytes */
    memset(&msg, 0, sizeof(msg));

    msg[0].addr = Mdot2[index].slave_addr;
    msg[0].flags = I2C_M_RD;
    msg[0].len = 4;
    msg[0].buf = g_read_bytes;
    data.msgs = msg;
    data.nmsgs = 1;
    if (ioctl(fd, I2C_RDWR, &data) < 0) {
        close(fd);
        return -1;
    }
    else {
        close(fd);
        // the 4th byte is the M.2 temperature
        return (g_read_bytes[3]);
    }
}

void pcie_data_scan()
{
    #define FILE_LENGTH 0x10
    #define PCIE_TEMP_MAX 255
    #define PCIE_TEMP_DEFAULT -1
    int i, fp_share_memory;
    FILE *fp;
    struct stat st = {0};
    char pcie_path[128];
    char sys_cmd[128];
    void *map_memory[MAX_MDOT2_NUM];
    char pcie_temperautur_str[FILE_LENGTH];
    /* create the file patch for dbus usage*/
    /* check if directory is existed */
    if (stat(PCIE_TEMP_PATH, &st) == -1) {
        mkdir_p(PCIE_TEMP_PATH, 0777);
    }
    for(i=0; i<MAX_MDOT2_NUM; i++) {
        sprintf(pcie_path , "%s%s%s%d", PCIE_TEMP_PATH, "/", "value_", Mdot2[i].sensor_number);
        if( access( pcie_path, F_OK ) != -1 ) {
            fprintf(stderr,"Error:[%s] opening:[%s] , existed \n",pcie_path);
            break;
        } else {
            fp = fopen(pcie_path,"w");
            if(fp == NULL) {
               fprintf(stderr,"Error:[%s] opening:[%s]\n",strerror(errno),pcie_path);
                return;
            }
            fprintf(fp, "%d",PCIE_TEMP_MAX);
            fclose(fp);
        }
    }

    for(i=0; i<MAX_MDOT2_NUM; i++) {
        sprintf(pcie_path , "%s%s%s%d", PCIE_TEMP_PATH, "/", "value_", Mdot2[i].sensor_number);

        /* Open a file to be mapped. */
        fp_share_memory = open(pcie_path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        lseek(fp_share_memory, FILE_LENGTH+1, SEEK_SET);
        write(fp_share_memory, "", PCIE_TEMP_DEFAULT);
        lseek(fp_share_memory, 0, SEEK_SET);

        /* Create map memory. */
        map_memory[i] = mmap(0, FILE_LENGTH, PROT_WRITE, MAP_SHARED, fp_share_memory, 0);
        close(fp_share_memory);
	}

    while(1)
    {
        for(i=0; i<MAX_MDOT2_NUM; i++)
        {
            sprintf(pcie_temperautur_str, "%d", get_Mdot2_data(i));
            sprintf((char *)map_memory[i], "%s", pcie_temperautur_str);
            sleep(1);
        }
    }
}

static void save_pid (void) {
    pid_t pid = 0;
    FILE *pidfile = NULL;
    pid = getpid();
    if (!(pidfile = fopen("/run/pcie-device-temperature.pid", "w"))) {
        fprintf(stderr, "failed to open pidfile\n");
        return;
    }
    fprintf(pidfile, "%d\n", pid);
    fclose(pidfile);
}

int main(void)
{
    save_pid();
    pcie_data_scan();
    return 0;
}
