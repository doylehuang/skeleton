#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <systemd/sd-bus.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "i2c-dev.h"
#include <error.h>

#define GPU_TEMP_PATH "/tmp/gpu_tool"
#define MAX_BYTES 255
#define MBT_REG_CMD            0x5c
#define MBT_REG_DATA_KEPLER    0x5d
#define NV_CMD_GET_CAPABILITIES 0x01
#define NV_CMD_GET_TEMP 0x02
#define NV_CMD_GET_POWER 0x04
#define NV_CMD_GET_GPU_INFORMATION    0x05
#define NV_CMD_SET_RELEASE_THERMAL_ALERT    0xf4
#define NV_CMD_GET_POWER_BARKE_STATE        0xf5
#define NV_CMD_GET_THERMAL_ALERT_STATE      0xf6
#define NV_CMD_GET_POWER_SUPPLY_STATUS      0xf1
#define NV_CMD_GET_FUNDAMENTIAL_RESET_STATE 0xf3
#define NV_CMD_GET_BOARD_POWER_SUPPLY_STATUS 0xf8
#define GPU_ACCESS_SUCCESS_RETURN 0x1f
#define MAX_I2C_DEV_LEN 32
#define PMBUS_DELAY usleep(400*1000)
#define TYPE_BOARD_PART_NUMBER 0x0
#define TYPE_SERIAL_NUMBER 0x2
#define TYPE_MARKETING_NAME 0x3
#define TYPE_FIRMWARE_VERSION 0x8
#define MAX_GPU_NUM (8)
#define MAX_INFO_INDEX 16
#define MAX_INFO_LENGTH 64
#define MAX_CAPABILITIES_CMD_INDEX 5

static int g_i2c_bus = 19;
static unsigned long g_slaveaddr = 0x4d;
int fd;
unsigned char cmd_reg[4];
static int g_use_pec = 0;

struct st_gpu_map {
	int gpu_index;
	int gpu_i2c_bus;
	int gpu_i2c_slaveaddress;
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

typedef struct {
	__u8 bus_no;

	__u8 slave;

	__u8 device_index;

} gpu_device_mapping;

gpu_device_mapping gpu_device_bus[MAX_GPU_NUM] = {
	{28, 0x4f, EM_GPU_DEVICE_1},
	{29, 0x4f, EM_GPU_DEVICE_2},
	{30, 0x4f, EM_GPU_DEVICE_3},
	{31, 0x4f, EM_GPU_DEVICE_4},
	{32, 0x4f, EM_GPU_DEVICE_5},
	{33, 0x4f, EM_GPU_DEVICE_6},
	{34, 0x4f, EM_GPU_DEVICE_7},
	{35, 0x4f, EM_GPU_DEVICE_8},
};


static int internal_gpu_access(int bus, __u8 slave,__u8 *write_buf, __u8 *read_buf)
{
	int fd;
	char filename[MAX_I2C_DEV_LEN] = {0};
	int rc=-1;
	int IsRetry = 1;
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
		goto error_smbus_access;
	}
	while(IsRetry) {

		if (i2c_smbus_read_block_data(fd, MBT_REG_CMD, cmd_reg) != 4) {
			goto error_smbus_access;
		}
		if(cmd_reg[3] == GPU_ACCESS_SUCCESS_RETURN) {
			if (i2c_smbus_read_block_data(fd, MBT_REG_DATA_KEPLER, read_buf) == 4) { /*success get data*/
				close(fd);
                IsRetry = 0;
				return 0;
			}
		}
	}
error_smbus_access:
	close(fd);
	return -1;
}

static find_gpu_index(int gpu_no)
{
	int i;
	for (i = 0 ; i<MAX_GPU_NUM; i++)
	{
		if (gpu_device_bus[i].device_index == gpu_no)
			return i;
	}

	return -1;
}

static int function_get_gpu_info(int gpu_no)
{
	unsigned char temp_writebuf[4] = {NV_CMD_GET_GPU_INFORMATION,0x0,0x0,0x80};
	unsigned char input_cmd_data[MAX_INFO_INDEX][2] = {
		{TYPE_BOARD_PART_NUMBER,24},
		{TYPE_SERIAL_NUMBER,16},
		{TYPE_MARKETING_NAME,24},
        {TYPE_FIRMWARE_VERSION,16},
		{0xFF,0xFF}, //List of end
	};
	unsigned char temp_readbuf[MAX_INFO_INDEX][32];
	unsigned char title[MAX_INFO_INDEX];
	unsigned char read_times = 0;
	unsigned char cuurent_index=0;
	int rc=-1;
	int i,j;
	int memroy_index=0;
	int i2c_bus;
	unsigned long slaveaddr;
	int gpu_index;
	
	memset(temp_readbuf, 0x0, sizeof(temp_readbuf));

	gpu_index = find_gpu_index(gpu_no);

	if (gpu_index >= MAX_GPU_NUM || gpu_index == -1)
	{
		printf("Error gpu index:%d  exceed max gpu number:%d\n", gpu_index, MAX_GPU_NUM);
		return -1;
	}

	i2c_bus = gpu_device_bus[gpu_index].bus_no;
	slaveaddr = gpu_device_bus[gpu_index].slave;
	
	for(i=0; input_cmd_data[i][0]!=0xFF; i++) {
		read_times = input_cmd_data[i][1]/4;
		for(j=0; j<read_times; j++) {
			temp_writebuf[1]=input_cmd_data[i][0]; /* type*/
			temp_writebuf[2]=j; /* times*/
			rc = internal_gpu_access(i2c_bus,  slaveaddr, temp_writebuf, &temp_readbuf[i][j*4]);
			if(rc < 0) {
                break;
			}
		}
        if(rc < 0){
            i--;
            continue;
        }
		memroy_index = input_cmd_data[i][0];
		switch (input_cmd_data[i][0])
		{
			case TYPE_MARKETING_NAME:
				strcpy(title, "Marketing Name");
				break;
			case TYPE_SERIAL_NUMBER:
				strcpy(title, "Serial Number");
				break;
			case TYPE_BOARD_PART_NUMBER:
				strcpy(title, "Board Part Number");
				break;
            case TYPE_FIRMWARE_VERSION:
                strcpy(title, "Firmware Version");
                break;
			default:
				title[0] = 0;
		}
		printf("[%s]=%s \r\n",title,  temp_readbuf[i]);
	}
	return 0;
}

static int read_gpu_temp(int gpu_no, int show_message)
{
    unsigned char temp_writebuf[4] = {NV_CMD_GET_TEMP,0x0,0x0,0x80};
	unsigned char readbuf[4];
	int i2c_bus;
	unsigned long slaveaddr;
	int ret;
	int gpu_index;
	int retry = 5;

	gpu_index = find_gpu_index(gpu_no);

	if (gpu_index >= MAX_GPU_NUM || gpu_index == -1)
	{
		printf("Error gpu no:%d  exceed max gpu number:%d\n", gpu_no, MAX_GPU_NUM);
		return -1;
	}

	i2c_bus = gpu_device_bus[gpu_index].bus_no;
	slaveaddr = gpu_device_bus[gpu_index].slave;

	while(retry > 0){
		ret = internal_gpu_access(i2c_bus, slaveaddr, temp_writebuf, readbuf);
		if (ret != 0)
		{
			retry--;
			usleep(100);

			if(retry == 0)
				return -1;
			continue;
		}

		if (show_message == 1)
			printf("GPU Temp:%d  C\n",readbuf[1]);

		break;
	}
	return 0;
}

static void scan_gpu_status(void)
{
	int i;
	int ret;
	int gpu_index;

	for (i = 0; i < MAX_GPU_NUM; i++)
	{
	    gpu_index =  find_gpu_index(i);
		ret = read_gpu_temp(i, 0);
		printf("GPU no:%d, bus:%d, addr:0x%x , status: ", i, gpu_device_bus[gpu_index].bus_no, gpu_device_bus[gpu_index].slave);
		if (ret == 0)
			printf(" ok");
		else
			printf(" failed");
		printf("\n");
	}
}

static int set_release_thermal_alert(int gpu_no, int command_type, int show_message)
{
    unsigned char temp_writebuf[4] = {NV_CMD_SET_RELEASE_THERMAL_ALERT,command_type,0x0,0x80};
	unsigned char readbuf[4];
	int i2c_bus;
	unsigned long slaveaddr;
	int ret;
	int gpu_index;

	gpu_index = find_gpu_index(gpu_no);

	if (gpu_index >= MAX_GPU_NUM || gpu_index == -1)
	{
		printf("Error gpu no:%d  exceed max gpu number:%d\n", gpu_no, MAX_GPU_NUM);
		return -1;
	}

	i2c_bus = gpu_device_bus[gpu_index].bus_no;
	slaveaddr = gpu_device_bus[gpu_index].slave;

	ret = internal_gpu_access(i2c_bus, slaveaddr, temp_writebuf, readbuf);
	if (ret != 0)
	{
		if (show_message == 1)
			printf("Fail to set/release GPU[%d - %d 0x%x] thermal alert Data: 0x%x 0x%x 0x%x 0x%x\n", gpu_index, i2c_bus, slaveaddr, readbuf[0], readbuf[1], readbuf[2], readbuf[3]);
		return -1;
	}

	if (show_message == 1)
    {
        if (command_type == 0)
        {
            printf("Release thermal alert SUCCESS!!\n");
        }
        else if(command_type == 1)
        {
            printf("Set thermal alert SUCCESS!!\n");
        }
    }
	return 0;
}

static int get_power_brake_state(int gpu_no, int show_message)
{
    unsigned char temp_writebuf[4] = {NV_CMD_GET_POWER_BARKE_STATE,0x0,0x0,0x80};
	unsigned char readbuf[4];
	int i2c_bus;
	unsigned long slaveaddr;
	int ret;
	int gpu_index;

	gpu_index = find_gpu_index(gpu_no);

	if (gpu_index >= MAX_GPU_NUM || gpu_index == -1)
	{
		printf("Error gpu no:%d  exceed max gpu number:%d\n", gpu_no, MAX_GPU_NUM);
		return -1;
	}

	i2c_bus = gpu_device_bus[gpu_index].bus_no;
	slaveaddr = gpu_device_bus[gpu_index].slave;

	ret = internal_gpu_access(i2c_bus, slaveaddr, temp_writebuf, readbuf);
	if (ret != 0)
	{
		if (show_message == 1)
			printf("Fail to get GPU[%d - %d 0x%x] power brake state Data: 0x%x 0x%x 0x%x 0x%x\n", gpu_index, i2c_bus, slaveaddr, readbuf[0], readbuf[1], readbuf[2], readbuf[3]);
		return -1;
	}

	if (show_message == 1)
		printf("GPU power brake state:%d  \n",readbuf[0]);
	return 0;
}

static int get_thermal_alert_state(int gpu_no, int show_message)
{
    unsigned char temp_writebuf[4] = {NV_CMD_GET_THERMAL_ALERT_STATE,0x0,0x0,0x80};
	unsigned char readbuf[4];
	int i2c_bus;
	unsigned long slaveaddr;
	int ret;
	int gpu_index;

	gpu_index = find_gpu_index(gpu_no);

	if (gpu_index >= MAX_GPU_NUM || gpu_index == -1)
	{
		printf("Error gpu no:%d  exceed max gpu number:%d\n", gpu_no, MAX_GPU_NUM);
		return -1;
	}

	i2c_bus = gpu_device_bus[gpu_index].bus_no;
	slaveaddr = gpu_device_bus[gpu_index].slave;

	ret = internal_gpu_access(i2c_bus, slaveaddr, temp_writebuf, readbuf);
	if (ret != 0)
	{
		if (show_message == 1)
			printf("Fail to get GPU[%d - %d 0x%x] thermal alert state Data: 0x%x 0x%x 0x%x 0x%x\n", gpu_index, i2c_bus, slaveaddr, readbuf[0], readbuf[1], readbuf[2], readbuf[3]);
		return -1;
	}

	if (show_message == 1)
		printf("GPU thermal alert state:%d  \n",readbuf[0]);
	return 0;
}

static int get_capabilities(int gpu_no, int command_type, int show_message)
{
    unsigned char temp_writebuf[4] = {NV_CMD_GET_CAPABILITIES,command_type,0x0,0x80};
    unsigned char readbuf[4];
    int i2c_bus;
    unsigned long slaveaddr;
    int ret;
    int gpu_index;
    int retry = 5;

    gpu_index = find_gpu_index(gpu_no);

    if (gpu_index >= MAX_GPU_NUM || gpu_index == -1)
    {
        printf("Error gpu no:%d  exceed max gpu number:%d\n", gpu_no, MAX_GPU_NUM);
        return -1;
    }

    if (command_type >= MAX_CAPABILITIES_CMD_INDEX)
    {
        printf("Error Arg1 of gpu capabilities command, exceed max Arg1 number:%d\n", MAX_CAPABILITIES_CMD_INDEX);
        return -1;
    }

    i2c_bus = gpu_device_bus[gpu_index].bus_no;
    slaveaddr = gpu_device_bus[gpu_index].slave;

    while(retry > 0){
        ret = internal_gpu_access(i2c_bus, slaveaddr, temp_writebuf, readbuf);
        if (ret != 0)
        {
            if (show_message == 1){
                retry--;
                printf("Fail to get GPU[%d - %d 0x%x] Data: 0x%x 0x%x 0x%x 0x%x\n", gpu_index, i2c_bus, slaveaddr, readbuf[0], readbuf[1], readbuf[2], readbuf[3]);
                printf("Retrying, the remaining number: %d \n",retry);
            }
        }else{
            retry = 0;
        }
    }

    if(ret == 0){
        printf("DWord(%d): 0x%x 0x%x 0x%x 0x%x (MSB)\n",command_type,readbuf[3],readbuf[2],readbuf[1],readbuf[0]);
    }

    if (show_message == 1){
        if (ret == 0){
            printf("Get GPU capabilities SUCCESS!!\n");
        }
    }
    return 0;
}

static int get_power(int gpu_no, int command_type, int show_message)
{
    unsigned char temp_writebuf[4] = {NV_CMD_GET_POWER,command_type,0x0,0x80};
    unsigned char readbuf[4];
    int i2c_bus;
    unsigned long slaveaddr;
    int ret;
    int gpu_index;
    int retry = 5;

    gpu_index = find_gpu_index(gpu_no);

    if (gpu_index >= MAX_GPU_NUM || gpu_index == -1)
    {
        printf("Error gpu no:%d  exceed max gpu number:%d\n", gpu_no, MAX_GPU_NUM);
        return -1;
    }
#if 0
    if (command_type >= MAX_CAPABILITIES_CMD_INDEX)
    {
        printf("Error Arg1 of gpu capabilities command, exceed max Arg1 number:%d\n", MAX_CAPABILITIES_CMD_INDEX);
        return -1;
    }
#endif
    i2c_bus = gpu_device_bus[gpu_index].bus_no;
    slaveaddr = gpu_device_bus[gpu_index].slave;

    while(retry > 0){
        ret = internal_gpu_access(i2c_bus, slaveaddr, temp_writebuf, readbuf);
        if (ret != 0)
        {
            if (show_message == 1){
                retry--;
                printf("Fail to get GPU[%d - %d 0x%x] Data: 0x%x 0x%x 0x%x 0x%x\n", gpu_index, i2c_bus, slaveaddr, readbuf[0], readbuf[1], readbuf[2], readbuf[3]);
                printf("Retrying, the remaining number: %d \n",retry);
            }
        }else{
            retry = 0;
        }
    }

    if(ret == 0){
        printf("Power in mw: %d %d %d %d\n",command_type,readbuf[0],readbuf[1],readbuf[2],readbuf[3]);
    }

    if (show_message == 1){
        if (ret == 0){
            printf("Get GPU power SUCCESS!!\n");
        }
    }
    return 0;
}

static int get_power_supply_status(int gpu_no, int show_message)
{
    unsigned char temp_writebuf[4] = {NV_CMD_GET_POWER_SUPPLY_STATUS,0x0,0x0,0x80};
	unsigned char readbuf[4];
	int i2c_bus;
	unsigned long slaveaddr;
	int ret;
	int gpu_index;

	gpu_index = find_gpu_index(gpu_no);

	if (gpu_index >= MAX_GPU_NUM || gpu_index == -1)
	{
		printf("Error gpu no:%d  exceed max gpu number:%d\n", gpu_no, MAX_GPU_NUM);
		return -1;
	}

	i2c_bus = gpu_device_bus[gpu_index].bus_no;
	slaveaddr = gpu_device_bus[gpu_index].slave;

	ret = internal_gpu_access(i2c_bus, slaveaddr, temp_writebuf, readbuf);
	if (ret != 0)
	{
		if (show_message == 1)
			printf("Fail to get GPU[%d - %d 0x%x] read data: 0x%x 0x%x 0x%x 0x%x\n", gpu_index, i2c_bus, slaveaddr, readbuf[0], readbuf[1], readbuf[2], readbuf[3]);
		return -1;
	}

	if (show_message == 1)
		printf("GPU power supply status: %d \n",readbuf[0]);
	return 0;
}

static int get_pcie_fundamential_reset_state(int gpu_no, int show_message)
{
    unsigned char temp_writebuf[4] = {NV_CMD_GET_FUNDAMENTIAL_RESET_STATE,0x0,0x0,0x80};
	unsigned char readbuf[4];
	int i2c_bus;
	unsigned long slaveaddr;
	int ret;
	int gpu_index;

	gpu_index = find_gpu_index(gpu_no);

	if (gpu_index >= MAX_GPU_NUM || gpu_index == -1)
	{
		printf("Error gpu no:%d  exceed max gpu number:%d\n", gpu_no, MAX_GPU_NUM);
		return -1;
	}

	i2c_bus = gpu_device_bus[gpu_index].bus_no;
	slaveaddr = gpu_device_bus[gpu_index].slave;

	ret = internal_gpu_access(i2c_bus, slaveaddr, temp_writebuf, readbuf);
	if (ret != 0)
	{
		if (show_message == 1)
			printf("Fail to get GPU[%d - %d 0x%x] read data: 0x%x 0x%x 0x%x 0x%x\n", gpu_index, i2c_bus, slaveaddr, readbuf[0], readbuf[1], readbuf[2], readbuf[3]);
		return -1;
	}

	if (show_message == 1)
		printf("GPU fundamential reset state: %d \n",readbuf[0]);
	return 0;
}

static int get_board_power_supply_status(int gpu_no, int show_message)
{
    unsigned char temp_writebuf[4] = {NV_CMD_GET_BOARD_POWER_SUPPLY_STATUS,0x0,0x0,0x80};
	unsigned char readbuf[4];
	int i2c_bus;
	unsigned long slaveaddr;
	int ret;
	int gpu_index;

	gpu_index = find_gpu_index(gpu_no);

	if (gpu_index >= MAX_GPU_NUM || gpu_index == -1)
	{
		printf("Error gpu no:%d  exceed max gpu number:%d\n", gpu_no, MAX_GPU_NUM);
		return -1;
	}

	i2c_bus = gpu_device_bus[gpu_index].bus_no;
	slaveaddr = gpu_device_bus[gpu_index].slave;

	ret = internal_gpu_access(i2c_bus, slaveaddr, temp_writebuf, readbuf);
	if (ret != 0)
	{
		if (show_message == 1)
			printf("Fail to get GPU[%d - %d 0x%x] read data: 0x%x 0x%x 0x%x 0x%x\n", gpu_index, i2c_bus, slaveaddr, readbuf[0], readbuf[1], readbuf[2], readbuf[3]);
		return -1;
	}

	if (show_message == 1)
		printf("GPU board power supply status: %d \n",readbuf[0]);
	return 0;
}

void gpu_data_scan()
{
	int i =0;
	struct stat st = {0};
	char gpu_path[128];
	FILE *fp;
	/* create the file patch for dbus usage*/
	/* check if directory is existed */
	if (stat(GPU_TEMP_PATH, &st) == -1) {
		mkdir(GPU_TEMP_PATH, 0777);
	}
}

static void set_gpu_map(int gpu_no, int i2c_bus, int i2c_addr)
{
	int gpu_index;
	char gpu_path[128];
	FILE *fp;
	
	gpu_index = find_gpu_index(gpu_no);
	if (gpu_index >= MAX_GPU_NUM || gpu_index == -1)
	{
		printf("Error gpu no:%d  exceed max gpu number:%d\n", gpu_no, MAX_GPU_NUM);
		return ;
	}
	printf("set gpu map: gpu_no:%d, i2c_bus:%d i2c_addr:0x%x\n", gpu_no, i2c_bus, i2c_addr);

	sprintf(gpu_path , "%s%s%d%s", GPU_TEMP_PATH, "/gpu", gpu_no,"_map");

	fp = fopen(gpu_path,"w");
	if(fp == NULL) {
		return;
	}
	fprintf(fp, "%d,  %d\n", i2c_bus, i2c_addr);
	fclose(fp);
}

static void read_gpu_map()
{
	int gpu_index;
	char gpu_path[128];
	FILE *fp;
	int i2c_bus;
	int i2c_addr;
	int gpu_no;
	
	for (gpu_no = 0; gpu_no<MAX_GPU_NUM; gpu_no++)
	{
		gpu_index = find_gpu_index(gpu_no);
		
		sprintf(gpu_path , "%s%s%d%s", GPU_TEMP_PATH, "/gpu", gpu_no,"_map");
		fp = fopen(gpu_path,"r");
		if(fp == NULL) {
			continue;
		}
		fscanf(fp, "%d,  %d\n", &i2c_bus, &i2c_addr);
		fclose(fp);
		
		gpu_device_bus[gpu_index].bus_no = i2c_bus;
		gpu_device_bus[gpu_index].slave = i2c_addr;
	}
}

static int smbus_commmand_write(int i2c_bus, int i2c_addr, int *write_data, int write_count, int i2c_command)
{
	int fd;
	char filename[MAX_I2C_DEV_LEN] = {0};
	int rc=-1;
	int retry_gpu = 5;
	unsigned char cmd_reg[4];
	int count = 0;
	int w_count = 0;
    __u8 write_data_b[write_count];

	memset(cmd_reg, 0x0, sizeof(cmd_reg));
	sprintf(filename,"/dev/i2c-%d",i2c_bus);
	fd = open(filename,O_RDWR);

	if (fd == -1) {
		fprintf(stderr, "Failed to open i2c device %s", filename);
		return rc;
	}
	rc = ioctl(fd,I2C_SLAVE,i2c_addr);
	if(rc < 0) {
		fprintf(stderr, "Failed to do iotcl I2C_SLAVE\n");
		return rc;
	}
    for (count = 0; count<write_count; count++)
        write_data_b[count] = (__u8) write_data[count];

	count = 0;
	while(count < write_count) {
		if (count + 4 < write_count)
			w_count = 4;
		else
			w_count = write_count - count;
    	PMBUS_DELAY;
		if(i2c_smbus_write_block_data(fd, i2c_command, w_count, &write_data_b[count]) < 0) {
			return -1;
		}
    	PMBUS_DELAY;
		count=count+w_count;
	}
	return 0;
}

static int smbus_i2c_read(int i2c_bus, int i2c_addr, int read_count, unsigned int cmd)
{
	int fd;
	char filename[MAX_I2C_DEV_LEN] = {0};
	int rc=-1;
	int retry_gpu = 5;
	unsigned char cmd_reg[4];
	int count = 0;
	int r_count = 0;
	int i;

	sprintf(filename,"/dev/i2c-%d",i2c_bus);
	fd = open(filename,O_RDWR);

	if (fd == -1) {
		fprintf(stderr, "Failed to open i2c device %s", filename);
		return rc;
	}
	rc = ioctl(fd,I2C_SLAVE,i2c_addr);
	if(rc < 0) {
		fprintf(stderr, "Failed to do iotcl I2C_SLAVE\n");
		return rc;
	}
	count = 0;
	printf("smbus_read data:[%d]\n", read_count);
	printf("==============================\n");
	while(count < read_count) {
		if (count + 4 < read_count)
			r_count = 4;
		else
			r_count = read_count - count;
		memset(cmd_reg, 0x0, sizeof(cmd_reg));
    	PMBUS_DELAY;
		if(i2c_smbus_read_block_data(fd, cmd, cmd_reg) != 4) {
			return -1;
		}
    	PMBUS_DELAY;
		for ( i = 0; i<r_count; i++)
		{
			printf("%x ", cmd_reg[i]);
		}
		count=count+r_count;
		if (count % 16 == 0)
			printf("\n");
	}
	printf("\n");
	return 0;
}

static int smbus_commmand_read(int i2c_bus, int i2c_addr, int read_count, int i2c_cmd)
{
	return smbus_i2c_read(i2c_bus, i2c_addr, read_count, i2c_cmd);
}

static int smbus_Kepler_read(int i2c_bus, int i2c_addr, int read_count)
{
	return smbus_i2c_read(i2c_bus, i2c_addr, read_count, MBT_REG_DATA_KEPLER );
}

static int i2c_io(int fd, int flag_read_write, int slave_addr, short len , char *data_bytes) {
  struct i2c_rdwr_ioctl_data data;
  struct i2c_msg msg[2];
  int n_msg = 0;
  int rc;

  memset(&msg, 0, sizeof(msg));

  if (flag_read_write == 0) {
    msg[n_msg].addr = slave_addr;
    msg[n_msg].flags = (g_use_pec) ? I2C_CLIENT_PEC : 0;
    msg[n_msg].len = len ;
    msg[n_msg].buf = data_bytes;
    n_msg++;
  }

  if (flag_read_write == 1) {
    msg[n_msg].addr = slave_addr;
    msg[n_msg].flags = I2C_M_RD
      | ((g_use_pec) ? I2C_CLIENT_PEC : 0)
      | ((len == 0) ? I2C_M_RECV_LEN : 0);
    /*
     * In case of g_n_read is 0, block length will be added by
     * the underlying bus driver.
     */
    msg[n_msg].len = (len) ? len : 256;
    msg[n_msg].buf = data_bytes ;
    if (len == 0) {
      /* If we're using variable length block reads, we have to set the
       * first byte of the buffer to at least one or the kernel complains.
       */
      data_bytes[0] = 1;
    }
    n_msg++;
  }

  data.msgs = msg;
  data.nmsgs = n_msg;

  rc = ioctl(fd, I2C_RDWR, &data);
  if (rc < 0) {
    printf("Failed to do raw io");
    return -1;
  }

  return 0;
}

static i2c_raw_access(int i2c_bus, int i2c_addr, int flag_read_write, short len , char *data_bytes)
{
	int fd;
	char filename[MAX_I2C_DEV_LEN] = {0};
	int rc=-1;
	int retry_gpu = 5;
	unsigned char cmd_reg[4];
	int count = 0;
	int r_count = 0;
	int i;
	
		
	sprintf(filename,"/dev/i2c-%d",i2c_bus);
	fd = open(filename,O_RDWR);
	
	if (fd == -1) {
		fprintf(stderr, "Failed to open i2c device %s", filename);
		return rc;
	}
	rc = ioctl(fd,I2C_SLAVE,i2c_addr);
	if(rc < 0) {
		fprintf(stderr, "Failed to do iotcl I2C_SLAVE\n");
		return rc;
	}
	
	rc = i2c_io(fd, flag_read_write, i2c_addr, len, data_bytes);

	return rc;
}

static int i2c_raw_write(int i2c_bus, int i2c_addr, int *wdata_bytes, short len )
{
	char data_bytes[256];
	int i;
	for (i = 0; i<len; i++)
		data_bytes[i] = (char) wdata_bytes[i];
	
	return i2c_raw_access(i2c_bus, i2c_addr, 0, len, data_bytes);
}

static int i2c_raw_read(int i2c_bus, int i2c_addr, short len)
{
	char data_bytes[256];
	int rc;
	int i;
	
	rc = i2c_raw_access(i2c_bus, i2c_addr, 1, len, data_bytes);
	if (rc == 0)
	{
		for (i = 0; i<len; i++)
			printf("0x%x\n", data_bytes[i]);
	}
	return rc;
}

static void
usage(const char *prog)
{
	printf("Usage: %s [Options]\n", prog);
	printf("\n  Options:\n"
		   "\n\t-h Help\n"
	       "\t-t [gpu number] :read GPU temp\n"
	       "\t-i [gpu number] :read GPU info\n"
	       "\t-x [gpu number] [Arg1]: Set/Release the thermal alert\n"
	       "\t-y [gpu number] :Get power brake state\n"
	       "\t-z [gpu number] :Get thermal alert state\n"
	       "\t-c [gpu number] [Arg1]: Get GPU capabilities\n"
	       "\t-w [gpu number] [Arg1]: Get GPU power(mW)\n"
	       "\t-v [gpu number] :Get power supply status\n"
	       "\t-u [gpu number] :Get PCIe fundamential reset state\n"
	       "\t-o [gpu number] :Get board power supply status\n"
	       "\t-s scan GPU status\n"
	       "\t-m [gpu number] [i2c number] [i2c slaveaddress] :set GPU map\n"
	       "\t-p [i2c bus] [i2c slave address] [command code] [i2c data....] :block write i2c smbus command data\n"
	       "\t-r [i2c bus] [i2c slave address] [command code]  [read data count] :block read i2c smbus  command data\n"
	       "\t-k [i2c bus] [i2c slave address] [read data count] :block read i2c smbus KEPLER data\n"
	       "\t-a [i2c bus] [i2c slave address] [i2c data....] :write  raw i2c data\n"
	       "\t-b [i2c bus] [i2c slave address] [read data count] :read raw i2c data\n"
	      );
}

int main(int argc, char **argv)
{
	int opt;
	int i, j;
	int write_count = 0;
	int read_count = 0;
	char **opt_start = argv;
	int gpu_no;
	int command_type;
	int val;
	int i2c_bus;
	int i2c_addr;
	int i2c_command;
	int write_data[MAX_BYTES];
	int write_len = 0, read_len = 0;
	

	gpu_data_scan();
	read_gpu_map();
	
	while ((opt = getopt(argc, argv, "hst:x:y:z:c:w:v:u:o:i:m:p:r:k:a:b:")) != -1) {
		switch (opt) {
		case 'h':
			usage(argv[0]);
			return 0;
		case 't':
			gpu_no =  atoi(optarg);
			read_gpu_temp(gpu_no, 1);
			break;
		case 'i':
			gpu_no =  atoi(optarg);
			function_get_gpu_info(gpu_no);
			break;
		case 'x':
			for (i = 0; i<argc; i++)
			{
				if (strcmp(opt_start[i], "-x") == 0)
				{
					if (i+2 >= argc)
					{
						printf("Error parameter\n");
						usage(argv[0]);
						return -1;
					}

					gpu_no = atoi(opt_start[i+1]);
					command_type = atoi(opt_start[i+2]);
					break;
				}
			}
			set_release_thermal_alert(gpu_no, command_type, 1);

			break;
		case 'y':
			gpu_no =  atoi(optarg);
			get_power_brake_state(gpu_no, 1);
			break;
		case 'z':
			gpu_no =  atoi(optarg);
			get_thermal_alert_state(gpu_no, 1);
			break;
        case 'c':
			for (i = 0; i<argc; i++)
			{
				if (strcmp(opt_start[i], "-c") == 0)
				{
					if (i+2 >= argc)
					{
						printf("Error parameter\n");
						usage(argv[0]);
						return -1;
					}

					gpu_no = atoi(opt_start[i+1]);
					command_type = atoi(opt_start[i+2]);
					break;
				}
			}
			get_capabilities(gpu_no, command_type, 1);
			break;
        case 'w':
            for (i = 0; i<argc; i++)
            {
                if (strcmp(opt_start[i], "-w") == 0)
                {
                    if (i+2 >= argc)
                    {
                        printf("Error parameter\n");
                        usage(argv[0]);
                        return -1;
                    }
        
                    gpu_no = atoi(opt_start[i+1]);
                    command_type = atoi(opt_start[i+2]);
                    break;
                }
            }
            get_power(gpu_no, command_type, 1);
            break;
		case 'v':
			gpu_no =  atoi(optarg);
			get_power_supply_status(gpu_no, 1);
			break;
		case 'u':
			gpu_no =  atoi(optarg);
			get_pcie_fundamential_reset_state(gpu_no, 1);
			break;
		case 'o':
			gpu_no =  atoi(optarg);
			get_board_power_supply_status(gpu_no, 1);
			break;
		case 's':
			scan_gpu_status();
			break;
		case 'm':
			for (i = 0; i<argc; i++)
			{
				if (strcmp(opt_start[i], "-m") == 0)
				{
					if (i+3 >= argc)
					{
						printf("Error parameter\n");
						usage(argv[0]);
						return -1;
					}
				
					gpu_no = atoi(opt_start[i+1]);
					i2c_bus = atoi(opt_start[i+2]);
					i2c_addr = strtoul(opt_start[i+3], NULL, 16);
					break;
				}
			}
			set_gpu_map(gpu_no, i2c_bus, i2c_addr);
			
			break;
		case 'p':
		case 'a':
		{
			int flag_smbus_write = -1;
			int write_status = -1;
			for (i = 0; i<argc; i++)
			{
				if (strcmp(opt_start[i], "-p") == 0 || strcmp(opt_start[i], "-a") == 0)
				{
					if (i+4 >= argc)
					{
						printf("Error parameter\n");
						usage(argv[0]);
						return -1;
					}
					i2c_bus = atoi(opt_start[i+1]);
					i2c_addr = strtoul(opt_start[i+2], NULL, 16);
					i2c_command = strtoul(opt_start[i+3], NULL, 16);

					write_len = 0;
					for (j = i+4; j<argc; j++)
					{
						write_data[write_len] = strtoul(opt_start[j], NULL, 16);
						printf("%d\n", write_data[write_len] );
					    write_len+=1;
					}

					if (strcmp(opt_start[i], "-p") == 0)
						flag_smbus_write = 0;
					else if (strcmp(opt_start[i], "-a") == 0) 
						flag_smbus_write = 1;
					
					break;
				}
			}

			if (flag_smbus_write == 0)
				flag_smbus_write = smbus_commmand_write(i2c_bus, i2c_addr, write_data, write_len, i2c_command);
			else if (flag_smbus_write == 1)
				flag_smbus_write = i2c_raw_write(i2c_bus, i2c_addr, write_data, write_len);

			if (flag_smbus_write == 0)
				printf("Write i2c smbus command success!!! \n");
			else
				printf("Write i2c smbus command failed !!! \n");
			
			break;
		}
		
		case 'r':
		case 'k':
		case 'b':
		{
			int flag_smbus_read = -1;
			int read_status = -1;
			for (i = 0; i<argc; i++)
			{
				if (strcmp(opt_start[i], "-r") == 0 || strcmp(opt_start[i], "-k") == 0 || strcmp(opt_start[i], "-b") == 0)
				{
					if (i+4 >= argc)
					{
						printf("Error parameter\n");
						usage(argv[0]);
						return -1;
					}
					i2c_bus = atoi(opt_start[i+1]);
					i2c_addr = strtoul(opt_start[i+2], NULL, 16);
					i2c_command = strtoul(opt_start[i+3], NULL, 16);
					read_len = atoi(opt_start[i+4]);

					if (strcmp(opt_start[i], "-r") == 0)
						flag_smbus_read = 0;
					else if (strcmp(opt_start[i], "-k") == 0) 
						flag_smbus_read = 1;
					else if (strcmp(opt_start[i], "-b") == 0) 
						flag_smbus_read = 2;
					
					break;
				}
			}

			if (flag_smbus_read == 0)
				read_status = smbus_commmand_read(i2c_bus, i2c_addr, read_len, i2c_command);
			else if  (flag_smbus_read == 1)
				read_status = smbus_Kepler_read(i2c_bus, i2c_addr, read_len);
			else if  (flag_smbus_read == 2)
				read_status = i2c_raw_read(i2c_bus, i2c_addr, read_len);

			if ( read_status == 0)
				printf("Read i2c smbus  success!!! \n");
			else
				printf("Read i2c smbus failed !!! \n");
			
			break;
		}
		default:
			usage(argv[0]);
			return 0;
		}
	}
	return 0;
}


