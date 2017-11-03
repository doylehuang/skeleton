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

#define MAX_I2C_DEV_LEN 32
#define MBT_REG_CMD            0x5c
#define MBT_REG_DATA_KEPLER    0x5d
#define NV_CMD_GET_TEMP 0x02
#define NV_CMD_GET_GPU_INFORMATION    0x05
#define GPU_ACCESS_SUCCESS_RETURN 0x1f

#define TYPE_BOARD_PART_NUMBER 0x0
#define TYPE_SERIAL_NUMBER 0x2
#define TYPE_MARKETING_NAME 0x3
#define TYPE_PART_NUMBER 0x4
#define TYPE_FIRMWARE_VERSION 0x8

#define GPU_TEMP_PATH "/run/obmc/sharememory/org/openbmc/sensors/gpu/gpu_temp"
#define GPU_MEM_TEMP_PATH "/run/obmc/sharememory/org/openbmc/sensors/gpu/gpu_mem_temp"



#define MAX_GPU_NUM (8)
#define MAX_INFO_INDEX 16
#define MAX_INFO_LENGTH 64

#define PATH_MAX_STRING_SIZE 256
#define FILE_LENGTH 0x10
#define PEX_TEMP_DEFAULT -1
#define PEX_TEMP_MAX 255
void *map_memory_gpu[MAX_GPU_NUM];
void *map_memory_gpu_mem[MAX_GPU_NUM];

enum {
	SHARE_MEMORY_GPU = 0,
	SHARE_MEMORY_GPU_MEMORY,
};

enum {
	GPU_MEM_TEMP_1 = 0x62,
	GPU_MEM_TEMP_2,
	GPU_MEM_TEMP_3,
	GPU_MEM_TEMP_4,
	GPU_MEM_TEMP_5,
	GPU_MEM_TEMP_6,
	GPU_MEM_TEMP_7,
	GPU_MEM_TEMP_8,
};

enum {
	EM_GPU_DEVICE_1 = 0,
	EM_GPU_DEVICE_2,
	EM_GPU_DEVICE_3,
	EM_GPU_DEVICE_4,
	EM_GPU_DEVICE_5,
	EM_GPU_DEVICE_6,
	EM_GPU_DEVICE_7,
	EM_GPU_DEVICE_8,
};

struct gpu_data {
	bool temp_ready;
	bool temp_mem_ready;
	__u8 temp;

	bool info_ready;
	unsigned char info_data[MAX_INFO_INDEX][MAX_INFO_LENGTH];
};

char info_string[MAX_INFO_INDEX][MAX_INFO_LENGTH] = {
	{"Board Part Number"},
	{""},
	{"Serial Number"},
	{"Marketing Name"},
	{""},
	{""},
	{""},
	{""},
	{""},
	{""},
	{""},
	{""},
	{""},
	{""},
	{""},
	{""},
};

struct gpu_data G_gpu_data[MAX_GPU_NUM];

typedef struct {
	__u8 bus_no;

	__u8 slave;

	__u8 device_index;

	__u8 sensor_number;

	char present_gpio_path[100];

	int present_gpio_value;

} gpu_device_mapping;

gpu_device_mapping gpu_device_bus[MAX_GPU_NUM] = {
	{32, 0x4d, EM_GPU_DEVICE_1, 0x41, "/sys/class/gpio/gpio236/value", -1},
	{33, 0x4d, EM_GPU_DEVICE_2, 0x42, "/sys/class/gpio/gpio237/value", -1},
	{34, 0x4d, EM_GPU_DEVICE_3, 0x43, "/sys/class/gpio/gpio238/value", -1},
	{35, 0x4d, EM_GPU_DEVICE_4, 0x44, "/sys/class/gpio/gpio239/value", -1},
	{36, 0x4d, EM_GPU_DEVICE_5, 0x45, "/sys/class/gpio/gpio240/value", -1},
	{37, 0x4d, EM_GPU_DEVICE_6, 0x46, "/sys/class/gpio/gpio241/value", -1},
	{38, 0x4d, EM_GPU_DEVICE_7, 0x47, "/sys/class/gpio/gpio242/value", -1},
	{39, 0x4d, EM_GPU_DEVICE_8, 0x48, "/sys/class/gpio/gpio243/value", -1},
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

void create_sharememory(int index, int type)
{
	int fp_share_memory;
	char f_path[128];

	if(type == SHARE_MEMORY_GPU){
		sprintf(f_path , "%s%s%s%d", GPU_TEMP_PATH, "/", "value_", gpu_device_bus[index].sensor_number);
	}else if(type == SHARE_MEMORY_GPU_MEMORY){
		sprintf(f_path , "%s%s%s%d", GPU_MEM_TEMP_PATH, "/", "value_", GPU_MEM_TEMP_1 + index);
	}

	/* Open a file to be mapped. */
	fp_share_memory = open(f_path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	lseek(fp_share_memory, 0, SEEK_SET);
	write(fp_share_memory, "-1", sizeof(PEX_TEMP_DEFAULT));
	lseek(fp_share_memory, 0, SEEK_SET);

	/* Create map memory. */
	if(type == SHARE_MEMORY_GPU){
		map_memory_gpu[index] = mmap(0, FILE_LENGTH, PROT_WRITE, MAP_SHARED, fp_share_memory, 0);
	}else if(type == SHARE_MEMORY_GPU_MEMORY){
		map_memory_gpu_mem[index] = mmap(0, FILE_LENGTH, PROT_WRITE, MAP_SHARED, fp_share_memory, 0);
	}

	close(fp_share_memory);
}


static int internal_gpu_access(int bus, __u8 slave,__u8 *write_buf, __u8 *read_buf, int flag)
{
	int fd;
	char filename[MAX_I2C_DEV_LEN] = {0};
	int rc=-1;
	int retry_gpu = 5;
	unsigned char cmd_reg[4];

	memset(cmd_reg, 0x0, sizeof(cmd_reg));
	sprintf(filename,"/dev/i2c-%d",bus);
	fd = open(filename,O_RDWR);

	if (fd == -1) {
		fprintf(stderr, "Failed to open i2c device %s", filename);
		return rc;
	}
	rc = ioctl(fd,I2C_SLAVE,slave);
	if(rc < 0) {
		fprintf(stderr, "Failed to do iotcl I2C_SLAVE\n");
		goto error_smbus_access;
	}
	if(i2c_smbus_write_block_data(fd, MBT_REG_CMD, 4, write_buf) < 0) {
		rc = -2;
		goto error_smbus_access;
	}
	while(retry_gpu) {

		if (i2c_smbus_read_block_data(fd, MBT_REG_CMD, cmd_reg) != 4) {
			printf("Error: on bus %d reading from 0x5c",bus);
			goto error_smbus_access;
		}
		if(cmd_reg[3] == GPU_ACCESS_SUCCESS_RETURN) {
			if (i2c_smbus_read_block_data(fd, MBT_REG_DATA_KEPLER, read_buf) == 4) { /*success get data*/
				close(fd);
				return 0;
			}
		} else {
			if (flag == 1) {
				close(fd);
				return -2;
			}
		}
		retry_gpu--;
	}
error_smbus_access:
	close(fd);
	return (rc!=0? -1: 0);
}

static int function_get_gpu_info(int index)
{

	unsigned char temp_writebuf[4] = {NV_CMD_GET_GPU_INFORMATION,0x0,0x0,0x80};
	unsigned char input_cmd_data[MAX_INFO_INDEX][2] = {
		{TYPE_BOARD_PART_NUMBER,24},
		{TYPE_SERIAL_NUMBER,16},
		{TYPE_MARKETING_NAME,24},
		{TYPE_PART_NUMBER,16},
		{TYPE_FIRMWARE_VERSION,14},
		{0xFF,0xFF}, //List of end
	};
	unsigned char temp_readbuf[MAX_INFO_INDEX][32];
	unsigned char read_times = 0;
	unsigned char read_times_count = 0;
	unsigned char cuurent_index=0;
	int rc=-1;
	int i,j;
	int memroy_index=0;
	memset(temp_readbuf, 0x0, sizeof(temp_readbuf));

	for(i=0; input_cmd_data[i][0]!=0xFF; i++) {
		read_times = input_cmd_data[i][1];
		for(j=0, read_times_count=0; j<read_times; j+=4, read_times_count++) {
			temp_writebuf[1]=input_cmd_data[i][0]; /* type*/
			temp_writebuf[2]=read_times_count; /* times*/
			rc = internal_gpu_access(gpu_device_bus[index].bus_no,
						 gpu_device_bus[index].slave,temp_writebuf,&temp_readbuf[i][read_times_count*4], 0);

			if(rc < 0) {
				fprintf(stderr, "failed to access gpu info index %d \n",index);
				G_gpu_data[gpu_device_bus[index].device_index].info_ready = 0;
				return rc;
			}
		}
		temp_readbuf[i][read_times] = '\0';
		memroy_index = input_cmd_data[i][0];

		sprintf(&G_gpu_data[gpu_device_bus[index].device_index].info_data[memroy_index][0],"%s\0",&temp_readbuf[i][0]);
	}
	G_gpu_data[gpu_device_bus[index].device_index].info_ready = 1;
	return 0;
}


int access_gpu_data(int index, unsigned char* writebuf, unsigned char* readbuf)
{
	int retry_temp = 20;
	int rc;
	while(retry_temp >= 0) {
		int flag_temp = 1;
		if (retry_temp == 0)
			flag_temp = 0;
		rc = internal_gpu_access(gpu_device_bus[index].bus_no,gpu_device_bus[index].slave,writebuf,readbuf, flag_temp);
		if (rc >=0)
			return 0;
		else if (rc == -2) {
			unsigned char temp_nop_writebuf[4] = {0x00,0x0,0x0,0x80};
			unsigned char readbuf_nop[4];
			internal_gpu_access(gpu_device_bus[index].bus_no,gpu_device_bus[index].slave,temp_nop_writebuf,readbuf_nop, 0);
		}
		retry_temp-=1;
	}
	return -1;
}

int do_read_file(char *path)
{
	FILE *fp1 = NULL;
	int val = -1;
	fp1= fopen(path,"r");
	if(fp1 == NULL)
		return -1;
	fscanf(fp1, "%d", &val);
	fclose(fp1);
	return val;
}

int read_file(char *path)
{
	int i ;
	int retry = 3;
	int val= -1;

	if (path == NULL)
		return -1;

	for (i = 0 ; i<retry ; i++)
	{
		val = do_read_file(path);
		if (val >= 0)
			return val;
	}
	return val;
}

int function_get_gpu_data(int index)
{

	unsigned char temp_writebuf[4] = {NV_CMD_GET_TEMP,0x0,0x0,0x80};
	unsigned char temp_mem_writebuf[4] = {NV_CMD_GET_TEMP,0x5,0x0,0x80};
	unsigned char readbuf[4];
	int rc=-1;
	char gpu_path[128];
	char sys_cmd[128];
	char gpu_info_node[256] = {0};
	int len=0;
	int i=0;
	FILE *fp;
	char temperautur_str[FILE_LENGTH];

	if (gpu_device_bus[index].present_gpio_value == -1)
		gpu_device_bus[index].present_gpio_value = read_file(gpu_device_bus[index].present_gpio_path);

	if (gpu_device_bus[index].present_gpio_value == 0)
		rc = access_gpu_data(index, temp_writebuf, readbuf); /*get gpu temp data*/
	if(rc==0) {
		int gpu_mem_temp = -1;
		G_gpu_data[gpu_device_bus[index].device_index].temp_ready = 1;
		G_gpu_data[gpu_device_bus[index].device_index].temp = readbuf[1];

		rc = access_gpu_data(index, temp_mem_writebuf, readbuf);
		sprintf(gpu_path, "%s%s%s%d", GPU_MEM_TEMP_PATH, "/", "value_", GPU_MEM_TEMP_1 + index);
		if (rc == 0) {
			gpu_mem_temp =  readbuf[1];
			sprintf(temperautur_str, "%d", readbuf[1]);
			sprintf((char *)map_memory_gpu_mem[index], "%s", temperautur_str);
			G_gpu_data[gpu_device_bus[index].device_index].temp_mem_ready =1;
		}
		else
		{
			if(G_gpu_data[gpu_device_bus[index].device_index].temp_mem_ready ==1) {
				sprintf(temperautur_str, "%d", rc);
				sprintf((char *)map_memory_gpu_mem[index], "%s", temperautur_str);
			}
			G_gpu_data[gpu_device_bus[index].device_index].temp_mem_ready =0;
		}
		/* write data to file */
		sprintf(gpu_path, "%s%s%s%d", GPU_TEMP_PATH, "/", "value_", gpu_device_bus[index].sensor_number);
		sprintf(temperautur_str, "%d", G_gpu_data[gpu_device_bus[index].device_index].temp);
		sprintf((char *)map_memory_gpu[index], "%s", temperautur_str);
	} else {
		if(G_gpu_data[gpu_device_bus[index].device_index].temp_ready) { /*if previous is ok*/
			sprintf(gpu_path, "%s%s%s%d", GPU_TEMP_PATH, "/", "value_", gpu_device_bus[index].sensor_number);
			sprintf(temperautur_str, "%d", rc);
			sprintf((char *)map_memory_gpu[index], "%s", temperautur_str);
		}
		G_gpu_data[gpu_device_bus[index].device_index].temp_ready = 0;
		return rc;
	}

	/*get gpu info data */
	if(!G_gpu_data[gpu_device_bus[index].device_index].info_ready) {
		rc=function_get_gpu_info(index);
		if(rc==0) {
			strcpy(gpu_info_node, "/org/openbmc/sensors/gpu/gpu_temp");

			sd_bus *bus = NULL;
			rc = sd_bus_open_system(&bus);
			if(rc < 0) {
				fprintf(stderr,"Error opening system bus.\n");
				return rc;
			}
			rc = set_dbus_property(bus, gpu_info_node, "Board Part Number", "s",
					       (void *) G_gpu_data[gpu_device_bus[index].device_index].info_data[TYPE_BOARD_PART_NUMBER],
					       gpu_device_bus[index].sensor_number);
			rc += set_dbus_property(bus, gpu_info_node, "Serial Number", "s",
						(void *)G_gpu_data[gpu_device_bus[index].device_index].info_data[TYPE_SERIAL_NUMBER],
						gpu_device_bus[index].sensor_number);
			rc += set_dbus_property(bus, gpu_info_node, "Marketing Name", "s",
						(void *)G_gpu_data[gpu_device_bus[index].device_index].info_data[TYPE_MARKETING_NAME],
						gpu_device_bus[index].sensor_number);
			rc += set_dbus_property(bus, gpu_info_node, "PartNumber", "s",
						(void *)G_gpu_data[gpu_device_bus[index].device_index].info_data[TYPE_PART_NUMBER],
						gpu_device_bus[index].sensor_number);
			rc += set_dbus_property(bus, gpu_info_node, "FirmwareVersion", "s",
						(void *)G_gpu_data[gpu_device_bus[index].device_index].info_data[TYPE_FIRMWARE_VERSION],
						gpu_device_bus[index].sensor_number);

			if (rc<0)
				G_gpu_data[gpu_device_bus[index].device_index].info_ready = 0;
			sd_bus_flush_close_unref(bus);
		} else
			fprintf(stderr, "failed to set_gpu_info_propetry index %d \n",index);
	}
	return rc;
}

static void notify_device_ready(char *obj_path)
{
    static int flag_notify_chk = 0;

    int rc;
    int val = 1;

    if (flag_notify_chk == 1)
        return ;

    sd_bus *bus = NULL;
    rc = sd_bus_open_system(&bus);
    if(rc < 0) {
        fprintf(stderr,"Error opening system bus.\n");
        return ;
    }
    rc = set_dbus_property(bus, obj_path, "ready", "i", (void *) &val, -1);
    if (rc >=0)
        flag_notify_chk = 1;

    sd_bus_flush_close_unref(bus);
}

void gpu_data_scan()
{
	/* init the global data */
	memset(G_gpu_data, 0x0, sizeof(G_gpu_data));
	int i =0;
	struct stat st = {0};
	char gpu_path[128];
	FILE *fp;
	/* create the file patch for dbus usage*/
	/* check if directory is existed */
	if (stat(GPU_TEMP_PATH, &st) == -1) {
		mkdir_p(GPU_TEMP_PATH, 0777);
	}
	for(i=0; i<MAX_GPU_NUM; i++) {
		sprintf(gpu_path, "%s%s%s%d", GPU_TEMP_PATH, "/", "value_", gpu_device_bus[i].sensor_number);
		printf("[PT] %s(%d)\n",__FUNCTION__,__LINE__);
		if( access( gpu_path, F_OK ) != -1 ) {
			printf("[PT] %s(%d)\n",__FUNCTION__,__LINE__);
			fprintf(stderr,"Error:[%s] opening:[%s] , existed \n",gpu_path);
			create_sharememory(i,SHARE_MEMORY_GPU);
			break;
		} else {
			fp = fopen(gpu_path,"w");
			if(fp == NULL) {
				printf("[PT] %s(%d)\n",__FUNCTION__,__LINE__);
				fprintf(stderr,"Error:[%s] opening:[%s]\n",strerror(errno),gpu_path);
				return;
			}
			fprintf(fp, "%d",PEX_TEMP_MAX);
			fclose(fp);
			create_sharememory(i,SHARE_MEMORY_GPU);
		}
	}

	/* create the file patch for dbus usage*/
	/* check if directory is existed */
	if (stat(GPU_MEM_TEMP_PATH, &st) == -1) {
		mkdir_p(GPU_MEM_TEMP_PATH, 0777);
	}
	for(i=0; i<MAX_GPU_NUM; i++) {
		sprintf(gpu_path, "%s%s%s%d", GPU_MEM_TEMP_PATH, "/", "value_", GPU_MEM_TEMP_1 + i);
		printf("[PT] %s(%d)\n",__FUNCTION__,__LINE__);
		if( access( gpu_path, F_OK ) != -1 ) {
			printf("[PT] %s(%d)\n",__FUNCTION__,__LINE__);
			fprintf(stderr,"Error:[%s] opening:[%s] , existed \n",gpu_path);
			create_sharememory(i,SHARE_MEMORY_GPU_MEMORY);
			break;
		} else {
			fp = fopen(gpu_path,"w");
			if(fp == NULL) {
				printf("[PT] %s(%d)\n",__FUNCTION__,__LINE__);
				fprintf(stderr,"Error:[%s] opening:[%s]\n",strerror(errno),gpu_path);
				return;
			}
			fprintf(fp, "%d",PEX_TEMP_MAX);
			fclose(fp);
			create_sharememory(i,SHARE_MEMORY_GPU_MEMORY);
		}
	}

	while(1) {
		for(i=0; i<MAX_GPU_NUM; i++) {
			function_get_gpu_data(i);
		}
		usleep(500*1000);
		notify_device_ready("/org/openbmc/sensors/gpu/gpu_temp");
	}
}

static void save_pid (void) {
    pid_t pid = 0;
    FILE *pidfile = NULL;
    pid = getpid();
    if (!(pidfile = fopen("/run/gpu_core.pid", "w"))) {
        fprintf(stderr, "failed to open pidfile\n");
        return;
    }
    fprintf(pidfile, "%d\n", pid);
    fclose(pidfile);
}

int
main(void)
{
    save_pid();
	gpu_data_scan();
	return 0;
}


