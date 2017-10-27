#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <systemd/sd-bus.h>
#include <linux/i2c-dev-user.h>
#include <log.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <sys/mman.h>


#define I2C_CLIENT_PEC          0x04    /* Use Packet Error Checking */
#define I2C_M_RECV_LEN          0x0400  /* length will be first received byte */

#define MAX_PATH_LEN 70
#define MAX_SENSOR_NUM 40
#define SAMPLING_N  20

#define CMD_OUTPUT_PORT_0 2

#define FAN_SHM_KEY  (4320)
#define FAN_SHM_PATH "skeleton/fan_algorithm2"
#define MAX_WAIT_TIMEOUT  (50000)
#define MAX_SENSOR_READING (9999)
#define MAX_CLOSELOOP_RECORD (10)
#define MAX_CLOSELOOP_SENSOR_NUM (8)
#define MAX_CLOSELOOP_PROFILE_NUM (8)
#define BASE_SHM_PATH "/run/obmc/sharememory"
#define FILE_LENGTH 0x10

struct st_closeloop_obj_data {
	int index;
	int sensor_tracking;
	int warning_temp;
	int sensor_reading;
	int interal_Err[100];
	int intergral_i;
	int last_error;
	double Kp;
	double Ki;
	double Kd;
};

struct st_fan_obj_path_info {
	char service_bus[MAX_PATH_LEN];
	char service_inf[MAX_PATH_LEN];
	char path[MAX_SENSOR_NUM][MAX_PATH_LEN];
	int  sensor_number_list[MAX_SENSOR_NUM];
	int size_sensor_list;
	int size;
	int flag_shm;
	void *map_shm_memory[MAX_SENSOR_NUM];
	void *obj_data;
	struct st_fan_obj_path_info *next;
};

struct st_fan_closeloop_par {
	double Kp;
	double Ki;
	double Kd;
	int sensor_tracking;
	int warning_temp;
	double pid_value;
	double closeloop_speed;
	int closeloop_sensor_reading;
	int sample_n;

	double current_fanspeed;
	int total_integral_error;
	int cur_integral_Err;
	int last_integral_error;
	int groups_sensor_reading[MAX_CLOSELOOP_SENSOR_NUM];
};

struct st_fan_parameter {
	int flag_closeloop; //0: init ; 1:do nothing ; 2: changed; 3:lock waiting
	int closeloop_count;
	struct st_fan_closeloop_par closeloop_param[MAX_CLOSELOOP_PROFILE_NUM];

	int flag_openloop; //0: init ; 1:do nothing ; 2: changed; 3:lock waiting
	float g_ParamA;
	float g_ParamB;
	float g_ParamC;
	int g_LowAmb;
	int g_UpAmb;
	int g_LowSpeed;
	int g_HighSpeed;
	int openloop_speed;
	double openloop_sensor_reading;
	int openloop_sensor_offset;

	int current_speed;
	int max_fanspeed;
	int min_fanspeed;
	int debug_msg_info_en; //0:close fan alogrithm debug message; 1: open fan alogrithm debug message
};



static struct st_fan_obj_path_info g_FanInputObjPath = {0};
static struct st_fan_obj_path_info g_CloseloopG1_ObjPath = {0};
static struct st_fan_obj_path_info g_CloseloopG3_ObjPath = {0};
static struct st_fan_obj_path_info g_CloseloopG2_ObjPath = {0};
static struct st_fan_obj_path_info g_AmbientObjPath = {0};
static struct st_fan_obj_path_info g_FanSpeedObjPath = {0};
static struct st_fan_obj_path_info g_FanModuleObjPath = {0};
static struct st_fan_obj_path_info g_PowerObjPath = {0};
static struct st_fan_obj_path_info *g_Closeloop_Header = NULL;
static struct st_fan_obj_path_info *g_Openloop_Header = NULL;

static struct st_fan_parameter *g_fan_para_shm = NULL;
static int g_shm_id;
static char *g_shm_addr = NULL;

struct st_closeloop_record {
	int sensor_reading;
	double pid_val;
	double cal_speed;
	int current_error;
};

static struct st_closeloop_record g_closeloop_record[MAX_CLOSELOOP_RECORD];
static int g_closeloop_record_count = 0;



//OpenLoop config parameters
static float g_ParamA= 0;
static float g_ParamB= 0;
static float g_ParamC= 0;
static int g_LowAmb = 0;
static int g_UpAmb = 0;
static int g_LowSpeed = 0;
static int g_HighSpeed = 0;

//Fan LED command & register mask
static int FAN_LED_OFF   =  0;
static int FAN_LED_PORT0_ALL_BLUE = 0;
static int FAN_LED_PORT1_ALL_BLUE = 0;
static int FAN_LED_PORT0_ALL_RED = 0;
static int FAN_LED_PORT1_ALL_RED = 0;
static int PORT0_FAN_LED_RED_MASK = 0;
static int PORT0_FAN_LED_BLUE_MASK = 0;
static int PORT1_FAN_LED_RED_MASK = 0;
static int PORT1_FAN_LED_BLUE_MASK = 0;
static int g_fanled_speed_limit = 0;

//Fan LED I2C bus
static char g_FanLed_I2CBus [MAX_PATH_LEN] = {0};
//Fan LED I2C Slave Address
static unsigned char g_FanLed_SlaveAddr = 0;

static double g_FanSpeed = 0;
static int g_Openloopspeed = 0;
static double g_Closeloopspeed = 0;
static unsigned int closeloop_first_time = 0;

static int initial_fan_config(sd_bus *bus);

static int push_fan_obj(struct st_fan_obj_path_info **header, struct st_fan_obj_path_info *item)
{
	struct st_fan_obj_path_info *t_header;

	if (*header == NULL) {
		*header = item;
		return 1;
	}

	t_header = *header;
	while (t_header->next != NULL)
		t_header = t_header->next;

	t_header->next = item;
	return 1;
}

static int freeall_fan_obj(struct st_fan_obj_path_info **header)
{
	struct st_fan_obj_path_info *t_header = NULL, *t_next = NULL;
	t_header = *header;
	while(t_header != NULL) {
		if (t_header->obj_data != NULL)
			free(t_header->obj_data);
		t_next = t_header->next;
		free(t_header);
		t_header = t_next;
	}
}

static void * create_shared_memory(char *obj_path, int sensor_number)
{
	void *shm_ptr = NULL;
	int fp_share_memory;
	char file_path[100];

	sprintf(file_path, "%s%s/value_%d", BASE_SHM_PATH, obj_path, sensor_number);
	fp_share_memory = open(file_path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fp_share_memory<0)
		return NULL;
	shm_ptr = mmap(0, FILE_LENGTH, PROT_WRITE, MAP_SHARED, fp_share_memory, 0);
	close(fp_share_memory);
	return shm_ptr;
}


static int set_fanled(uint8_t port0, uint8_t port1, int use_pec)
{
	struct i2c_rdwr_ioctl_data data;
	struct i2c_msg msg[1];
	uint8_t write_bytes[3];
	int fd = -1;

	if (strlen(g_FanLed_I2CBus)<=0)
		return -1;

	fd = open(g_FanLed_I2CBus, O_RDWR);
	if (fd != -1) {
		LOG_ERR(errno, "Failed to open i2c device %s", g_FanLed_I2CBus);
		close(fd);
		return -1;
	}

	memset(&msg, 0, sizeof(msg));

	write_bytes[0] = CMD_OUTPUT_PORT_0;
	write_bytes[1] = port0;
	write_bytes[2] = port1;

	msg[0].addr = g_FanLed_SlaveAddr;
	msg[0].flags = (use_pec) ? I2C_CLIENT_PEC : 0;
	msg[0].len = sizeof(write_bytes);
	msg[0].buf = write_bytes;

	data.msgs = msg;
	data.nmsgs = 1;
	if (ioctl(fd, I2C_RDWR, &data) < 0) {
		LOG_ERR(errno, "Failed to do raw io");
		close(fd);
		return -1;
	}

	return 0;
}

static int calculate_closeloop(struct st_closeloop_obj_data *sensor_data, double current_fanspeed)
{
	int total_integral_error;
	int i;
	double pid_value;
	double pwm_speed;
	double Kp = 0, Ki = 0, Kd = 0;
	int cur_integral_Err = 0;
	int sample_n = 0;
	int index;

	if (sensor_data == NULL)
		return 0;

	index = sensor_data->index;

	sensor_data->sensor_reading = g_fan_para_shm->closeloop_param[index].closeloop_sensor_reading;

	if (sensor_data->sensor_reading <=0) {
		pwm_speed = 100;
		g_closeloop_record[g_closeloop_record_count].cal_speed = 100;
		g_closeloop_record[g_closeloop_record_count].current_error = 0;
		g_closeloop_record[g_closeloop_record_count].pid_val = 0;
		g_closeloop_record[g_closeloop_record_count].sensor_reading = sensor_data->sensor_reading;
		g_closeloop_record_count++;


		g_fan_para_shm->closeloop_param[index].pid_value = 0;
		g_fan_para_shm->closeloop_param[index].closeloop_speed = 100;
		g_fan_para_shm->closeloop_param[index].current_fanspeed = current_fanspeed;
		g_Closeloopspeed = 100;

		return 1;
	}

	Kp = sensor_data->Kp;
	Ki = sensor_data->Ki;
	Kd = sensor_data->Kd;

	

	if (g_fan_para_shm != NULL)
		sample_n = g_fan_para_shm->closeloop_param[index].sample_n;
	else
		sample_n = SAMPLING_N;

	if (g_fan_para_shm->debug_msg_info_en == 1)
		printf("[FAN_ALGORITHM][%s, %d] [PID value] kp:%f, Ki:%f, Kd:%f, target: %d\n", __FUNCTION__, __LINE__, Kp, Ki, Kd, sensor_data->sensor_tracking);

	cur_integral_Err =(int) (sensor_data->sensor_reading - sensor_data->sensor_tracking);
	sensor_data->intergral_i = sensor_data->intergral_i % sample_n;
	sensor_data->interal_Err[sensor_data->intergral_i] = cur_integral_Err;
	sensor_data->intergral_i=(sensor_data->intergral_i+1) % sample_n;
	total_integral_error = 0;

	for(i=0; i<sample_n; i++) {
		total_integral_error += sensor_data->interal_Err[i] ;
		if (g_fan_para_shm->debug_msg_info_en == 1)
			printf("[FAN_ALGORITHM][%s, %d]  interal_Err[%d]:%d\n", __FUNCTION__, __LINE__, i, sensor_data->interal_Err[i]);
	}

	pid_value = ((double) Kp * cur_integral_Err +  (double)Ki * total_integral_error + (double)Kd * (cur_integral_Err - sensor_data->last_error));
	if (g_fan_para_shm->debug_msg_info_en == 1)
		printf("[FAN_ALGORITHM][%s, %d] cur_integral_Err:%d, total_integral_error:%d, last_error:%d, pid_value:%f\n", __FUNCTION__, __LINE__, cur_integral_Err,
		       total_integral_error, sensor_data->last_error, pid_value);
	//pwm_speed = pid_value + g_FanSpeed;

	if (closeloop_first_time <= 10) {
		if (current_fanspeed == 100)
			current_fanspeed = 40;
		else {
			closeloop_first_time +=1;
		}
	}

	pwm_speed = pid_value + (double) current_fanspeed;

	g_fan_para_shm->closeloop_param[index].pid_value = pid_value;
	g_fan_para_shm->closeloop_param[index].closeloop_speed = pwm_speed;

	g_fan_para_shm->closeloop_param[index].current_fanspeed = current_fanspeed;
	g_fan_para_shm->closeloop_param[index].total_integral_error = total_integral_error;
	g_fan_para_shm->closeloop_param[index].cur_integral_Err  = cur_integral_Err;
	g_fan_para_shm->closeloop_param[index].last_integral_error  = sensor_data->last_error;

	if (g_fan_para_shm->debug_msg_info_en == 1)
		printf("[FAN_ALGORITHM][%s, %d] [Closeloop pid_value] %f; [Closeloop Calculate Fan Speed]: %f\n", __FUNCTION__, __LINE__, pid_value, pwm_speed);

	if(pwm_speed > 100)
		pwm_speed = 100;

	if(pwm_speed < 0)
		pwm_speed = 0;

	sensor_data->last_error = (int) cur_integral_Err;

	g_closeloop_record[g_closeloop_record_count].cal_speed = pwm_speed;
	g_closeloop_record[g_closeloop_record_count].current_error = cur_integral_Err;
	g_closeloop_record[g_closeloop_record_count].pid_val = pid_value;
	g_closeloop_record[g_closeloop_record_count].sensor_reading = sensor_data->sensor_reading;
	g_closeloop_record_count++;

	if (g_Closeloopspeed < pwm_speed)
		g_Closeloopspeed = pwm_speed;

	if (sensor_data->sensor_reading>=sensor_data->warning_temp)
		g_Closeloopspeed = 100;

	return 1;
}

static int calculate_openloop (double sensorreading)
{
	int speed = 0;

	if (sensorreading<=0) {
		g_Openloopspeed = 0;
		return 0;
	}

	sensorreading=sensorreading+g_fan_para_shm->openloop_sensor_offset;
	//sensorreading+=7; //because temp4 getsensors reading already offset (-7)


	if (sensorreading > g_UpAmb) {
		speed = g_HighSpeed;
	} else if (sensorreading <= g_LowAmb) {
		speed = g_LowSpeed;
	} else {
		speed =(int) (( (double)g_ParamA * sensorreading * sensorreading ) + ((double) g_ParamB * sensorreading ) + (double)g_ParamC);
		speed = (speed > g_HighSpeed)? g_HighSpeed : ((speed < g_LowSpeed)? g_LowSpeed : speed);
	}

	g_fan_para_shm->openloop_speed = speed;

	if (g_fan_para_shm->debug_msg_info_en == 1)
		printf("[FAN_ALGORITHM][%s, %d] [Openloop Parameters: g_UpAmb, g_LowAmb, A, B, C] %d ,%d, %f, %f, %f; [Openloop Calculate Fan Speed]: %d\n", __FUNCTION__, __LINE__,
		       g_UpAmb, g_LowAmb, g_ParamA, g_ParamB, g_ParamC, speed);

	if (g_Openloopspeed < speed)
		g_Openloopspeed = speed;
	return 1;
}

static int get_sensor_reading(sd_bus *bus, char *obj_path, int *sensor_reading, struct st_fan_obj_path_info *fan_obj, int idx)
{
	sd_bus_error bus_error = SD_BUS_ERROR_NULL;
	sd_bus_message *response = NULL;
	int rc;

	*sensor_reading = 0;

	if (strlen(fan_obj->service_bus) <= 0 || strlen(fan_obj->service_inf) <= 0 || strlen(obj_path) <= 0)
		return -1;

	if (fan_obj->size_sensor_list > 0 && idx < fan_obj->size_sensor_list) {
		char fn_name[20];
		sprintf(fn_name, "value_%d", fan_obj->sensor_number_list[idx]);
		rc = sd_bus_call_method(bus,
					fan_obj->service_bus,
					obj_path,
					"org.freedesktop.DBus.Properties",
					"Get",
					&bus_error,
					&response,
					"ss",
					fan_obj->service_inf,
					fn_name);
	} else {
		rc = sd_bus_call_method(bus,
					fan_obj->service_bus,
					obj_path,
					fan_obj->service_inf,
					"getValue",
					&bus_error,
					&response,
					NULL);
	}

	if(rc < 0) {
		fprintf(stderr, "obj_path: %s Failed to get temperature from dbus: %s\n", obj_path, bus_error.message);
	} else {
		rc = sd_bus_message_read(response,"v", "i", sensor_reading);
		if (rc < 0)
			fprintf(stderr, "obj_path: %s Failed to parse response message:[%s]\n",obj_path, strerror(-rc));
	}

	sd_bus_error_free(&bus_error);
	response = sd_bus_message_unref(response);
	return rc;
}

static int get_max_sensor_reading(sd_bus *bus, struct st_fan_obj_path_info *fan_obj)
{
	int i;
	int rc;
	int sensor_reading;
	int max_value = 0;

	for(i=0; i<fan_obj->size; i++) {
		rc = get_sensor_reading(bus, fan_obj->path[i], &sensor_reading, fan_obj, i);
		if (rc >= 0)
			max_value = (max_value < sensor_reading)? sensor_reading : max_value;
	}

	return max_value;
}

int read_file(char *path)
{
	FILE *fp1 = NULL;
	int retry = 10;
	int i;
	int val = 0;
	for (i =0 ; i<retry; i++)
	{
		fp1= fopen(path, "r");
		if(fp1 != NULL)
			break;
	}
	if ( i == retry) {
		return -1;
	}
	fscanf(fp1, "%d", &val);
	if (val < 0)
		val = 0;
	fclose(fp1);
	return val;
}

static int get_sensor_reading_file(char *obj_path, int *sensor_reading, struct st_fan_obj_path_info *fan_obj, int idx)
{
	char file_path[100];
	int rc;
	int retry = 100;
	int i;
	*sensor_reading = 0;

	if (fan_obj->flag_shm == 1) {//conduct file share memory read
		rc = 0;
		if (fan_obj->map_shm_memory[idx]) {
			sscanf((char *) fan_obj->map_shm_memory[idx], "%d", &rc);
		}
		*sensor_reading = rc;
		return rc;
	} else
		sprintf(file_path, obj_path, (idx+1));

	for (i = 0; i<retry; i++) {
		rc = read_file(file_path);
		if (rc > 0)
			break;
		usleep(1000);
	}
	if (rc >= 0)
		*sensor_reading = rc;

	return rc;
}


static int get_max_sensor_reading_file(struct st_fan_obj_path_info *fan_obj, int index)
{
	int i;
	int rc;
	int sensor_reading = 0;
	int max_value = 0;
	int flag = 0;

	for(i=0; i<fan_obj->size; i++) {
		rc = get_sensor_reading_file(fan_obj->path[i], &sensor_reading, fan_obj, i);
		if (rc >= 0) {
			if (sensor_reading <=0)
				flag = -1;
			if (index >=0) {
				g_fan_para_shm->closeloop_param[index].groups_sensor_reading[i] =  sensor_reading;
			}
			max_value = (max_value < sensor_reading)? sensor_reading : max_value;
		}
	}

	return (flag == -1? -1 : max_value);
}

static int get_sensor_reading_Fan(sd_bus *bus, char *obj_path, int *sensor_reading, struct st_fan_obj_path_info *fan_obj, int idx)
{
	sd_bus_error bus_error = SD_BUS_ERROR_NULL;
	sd_bus_message *response = NULL;
	int rc;

	*sensor_reading = 0;

	if (strlen(fan_obj->service_bus) <= 0 || strlen(fan_obj->service_inf) <= 0 || strlen(obj_path) <= 0)
		return -1;

	if (fan_obj->size_sensor_list > 0 && idx < fan_obj->size_sensor_list) {
		rc = sd_bus_call_method(bus,
					fan_obj->service_bus,
					obj_path,
					fan_obj->service_inf,
					"getValue_Fan",
					&bus_error,
					&response,
					"i",
					idx+1);
	} else {
		rc = sd_bus_call_method(bus,
					fan_obj->service_bus,
					obj_path,
					fan_obj->service_inf,
					"getValue_Fan",
					&bus_error,
					&response,
					NULL);
	}


	if(rc < 0) {
		fprintf(stderr, "obj_path: %s Failed to get temperature from dbus: %s\n", obj_path, bus_error.message);
	} else {
		rc = sd_bus_message_read(response, "i", sensor_reading);
		if (rc < 0)
			fprintf(stderr, "obj_path: %s Failed to parse response message:[%s]\n",obj_path, strerror(-rc));
	}

	sd_bus_error_free(&bus_error);
	response = sd_bus_message_unref(response);

	return rc;
}

static int get_max_sensor_reading_Fan(sd_bus *bus, struct st_fan_obj_path_info *fan_obj)
{
	int i;
	int rc;
	int sensor_reading;
	int max_value = 0;

	for(i=0; i<fan_obj->size; i++) {
		rc = get_sensor_reading_Fan(bus, fan_obj->path[i], &sensor_reading, fan_obj, i);
		if (rc >= 0)
			max_value = (max_value < sensor_reading)? sensor_reading : max_value;
	}

	return max_value;
}

static void check_change_closeloop_params(struct st_closeloop_obj_data *sensor_data)
{
	int wait_times = 0;
	int index;

	if (g_fan_para_shm == NULL)
		return ;


	while (g_fan_para_shm->flag_closeloop == 3 && wait_times<MAX_WAIT_TIMEOUT)
		wait_times++;

	if (g_fan_para_shm->flag_closeloop == 2) {
		index = sensor_data->index;
		sensor_data->Kp = g_fan_para_shm->closeloop_param[index].Kp;
		sensor_data->Ki = g_fan_para_shm->closeloop_param[index].Ki;
		sensor_data->Kd = g_fan_para_shm->closeloop_param[index].Kd;
		sensor_data->sensor_tracking = g_fan_para_shm->closeloop_param[index].sensor_tracking;
		sensor_data->warning_temp = g_fan_para_shm->closeloop_param[index].warning_temp;
	}
}

static void check_change_openloop_params()
{
	int wait_times = 0;

	if (g_fan_para_shm == NULL)
		return ;


	while (g_fan_para_shm->flag_openloop == 3 && wait_times<MAX_WAIT_TIMEOUT)
		wait_times++;

	if (g_fan_para_shm->flag_openloop == 2) {
		g_ParamA = g_fan_para_shm->g_ParamA;
		g_ParamB = g_fan_para_shm->g_ParamB;
		g_ParamC = g_fan_para_shm->g_ParamC;
		g_LowAmb = g_fan_para_shm->g_LowAmb;
		g_UpAmb = g_fan_para_shm->g_UpAmb;
		g_LowSpeed = g_fan_para_shm->g_LowSpeed;
		g_HighSpeed = g_fan_para_shm->g_HighSpeed;
	}
}


static int fan_control_algorithm_monitor(void)
{
	sd_bus *bus = NULL;
	sd_bus_error bus_error = SD_BUS_ERROR_NULL;
	sd_bus_message *response = NULL;
	int rc = 0, i = 0, offset = 0;
	int Fan_tach, FinalFanSpeed = 255;
	int Power_state = 0, fan_led_port0 = 0xFF, fan_led_port1 = 0xFF;
	char fan_presence[MAX_SENSOR_NUM] = {0}, fan_presence_previous[MAX_SENSOR_NUM] = {0};
	struct st_fan_obj_path_info *t_header = NULL;
	struct st_closeloop_obj_data *t_closeloop_data = NULL;
	int closeloop_reading = 0, openloop_reading = 0;
	int current_fanspeed = 0;
	int first_time_set = 0;

	double real_fanspeed = 0.0;
	do {
		/* Connect to the user bus this time */
		rc = sd_bus_open_system(&bus);
		if(rc < 0) {
			fprintf(stderr, "Failed to connect to system bus for fan_algorithm: %s\n", strerror(-rc));
			bus = sd_bus_flush_close_unref(bus);
			sleep(1);
		}
	} while (rc < 0);

	initial_fan_config(bus);

	real_fanspeed = 0;
	while (1) {
		Power_state = read_file("/sys/class/gpio/gpio390/value");

		if (Power_state == 0 ) { //gpio390  inverse , 0:power good , 1:power off

			current_fanspeed = get_max_sensor_reading_Fan(bus, &g_FanSpeedObjPath);
			g_fan_para_shm->current_speed = current_fanspeed;
			if (g_fan_para_shm->debug_msg_info_en == 1)
				printf("[FAN_ALGORITHM][Current FanSpeed value] :%d\n", current_fanspeed);
			if (current_fanspeed <0)
				current_fanspeed = 0;
			else {
				current_fanspeed = current_fanspeed*100;
				if (real_fanspeed == 0)
					real_fanspeed = (double) current_fanspeed / 255;
				current_fanspeed =(int) current_fanspeed / 255;
			}
			if (current_fanspeed > 100)
				current_fanspeed = 100;

			if (real_fanspeed == 0 || real_fanspeed >= 100)
				real_fanspeed = (double) current_fanspeed;
			

			closeloop_reading = MAX_SENSOR_READING;
			openloop_reading = 0;
			
			t_header = g_Closeloop_Header;
			g_Closeloopspeed = 0;
			g_closeloop_record_count = 0;
			int closeloop_index = 0;
			while (t_header != NULL) {
				t_closeloop_data = (struct st_closeloop_obj_data *) t_header->obj_data;

				if (t_closeloop_data != NULL) {
					t_closeloop_data->sensor_reading = get_max_sensor_reading_file(t_header, t_closeloop_data->index);
					if (closeloop_index == 3 && t_closeloop_data->sensor_reading>=0)
						t_closeloop_data->sensor_reading = (int) t_closeloop_data->sensor_reading / 1000;
					g_fan_para_shm->closeloop_param[t_closeloop_data->index].closeloop_sensor_reading = t_closeloop_data->sensor_reading;
					//if (t_closeloop_data->sensor_reading == 0)
					//	t_closeloop_data->sensor_reading = 30;
					check_change_closeloop_params(t_closeloop_data);
					if (g_fan_para_shm->debug_msg_info_en == 1)
					{
						switch(closeloop_index)
						{
							case 0:
								printf("[FAN_ALGORITHM] calculate_closeloop : GPU sensor =======================\n");
								break;
							case 1:
								printf("[FAN_ALGORITHM] calculate_closeloop : PEX9797 sensor =======================\n");
								break;
							case 2:
								printf("[FAN_ALGORITHM] calculate_closeloop : HBM (GPU Memory) sensor =======================\n");
								break;
							case 3:
								printf("[FAN_ALGORITHM] calculate_closeloop : FGPA sensor =======================\n");
								break;
							case 4:
								printf("[FAN_ALGORITHM] calculate_closeloop : AVA card sensor =======================\n");
								break;
							default:
								break;
						}
					}
					calculate_closeloop(t_closeloop_data, real_fanspeed);
					closeloop_reading = (closeloop_reading>t_closeloop_data->sensor_reading)? t_closeloop_data->sensor_reading:closeloop_reading;
					if (g_fan_para_shm->debug_msg_info_en == 1)
						printf("[FAN_ALGORITHM] ===========================================================================\n");
				}
				t_header = t_header->next;
				closeloop_index = closeloop_index +1;
			}
			double adjust_closeloop_speed = -1;
			int min_current_error = 9999;
			for (i = 0; i<g_closeloop_record_count; i++)
			{
				if (g_fan_para_shm->debug_msg_info_en == 1)
						printf("[FAN_ALGORITHM][Adjust Closeloop list ] i:%d, current_error:%d, adjust speed:%f pid_valu:%f\n",
						    i, g_closeloop_record[i].current_error, g_closeloop_record[i].cal_speed, 
						    g_closeloop_record[i].pid_val);
				if (g_closeloop_record[i].pid_val >=0) {
					adjust_closeloop_speed = -1;
					break;
				}
				int c_error = g_closeloop_record[i].current_error;
				if (c_error < 0)
					c_error = c_error*-1;
				if (c_error > min_current_error){
					adjust_closeloop_speed = g_closeloop_record[i].cal_speed;
					min_current_error =c_error; 
					if (g_fan_para_shm->debug_msg_info_en == 1)
						printf("[FAN_ALGORITHM][Adjust Closeloop ] i:%d, current_error:%d, adjust speed:%f \n",
						    i, adjust_closeloop_speed, adjust_closeloop_speed);
				}
			}
			if (adjust_closeloop_speed >=0) {
				if (g_fan_para_shm->debug_msg_info_en == 1)
					printf("[FAN_ALGORITHM][Adjust Closeloop Speed:original: %f, adjust: %f \n", g_Closeloopspeed, adjust_closeloop_speed);
				g_Closeloopspeed = adjust_closeloop_speed;
			}
			

			check_change_openloop_params();
			openloop_reading = 0;
			t_header = g_Openloop_Header;
			g_Openloopspeed = 0;
			while (t_header != NULL) {
				int t_reading;
				t_reading = get_max_sensor_reading_file(t_header, -1);
				g_fan_para_shm->openloop_sensor_reading =(double) t_reading/1000;
				calculate_openloop(g_fan_para_shm->openloop_sensor_reading);
				openloop_reading = (openloop_reading<t_reading? t_reading:openloop_reading);
				t_header = t_header->next;
			}

			if (closeloop_reading > 0 && openloop_reading > 0) {
				if((double) g_Openloopspeed > g_Closeloopspeed) {
					g_FanSpeed = (double)g_Openloopspeed;
				} else {
					g_FanSpeed = (double)g_Closeloopspeed;
				}

				if (first_time_set == 0 && g_Openloopspeed>0 && g_Closeloopspeed>0) {
					if (g_Closeloopspeed == 100)
						g_FanSpeed = (double)g_Openloopspeed;
					first_time_set = 1;
				}


				real_fanspeed = g_FanSpeed;

				FinalFanSpeed =(int) ((double)g_FanSpeed * 255)/100;

				if(g_FanSpeed > g_fanled_speed_limit) {
					fan_led_port0 = FAN_LED_PORT0_ALL_BLUE;
					fan_led_port1 = FAN_LED_PORT1_ALL_BLUE;
				} else {
					fan_led_port0 = FAN_LED_PORT0_ALL_RED;
					fan_led_port1 = FAN_LED_PORT1_ALL_RED;
				}
			} else {
				FinalFanSpeed = 255;
				real_fanspeed = 100;
				fan_led_port0 = FAN_LED_PORT0_ALL_BLUE;
				fan_led_port1 = FAN_LED_PORT1_ALL_BLUE;
			}

			int k;
			for(i=0, k = 0; k<g_FanInputObjPath.size; i++) {

				rc = get_sensor_reading_file(g_FanInputObjPath.path[k], &Fan_tach, &g_FanInputObjPath, k);
				if (g_fan_para_shm->debug_msg_info_en == 1)
					printf("[FAN_ALGORITHM][Fan Tach: %d] value:%d, rc:%d, %s\n", i,  Fan_tach, rc, g_FanInputObjPath.path[k]);
				if (rc < 0)
					Fan_tach = 0;

				if (Fan_tach == 0) {
					FinalFanSpeed = 255;
					if (i <= 3) { //FAN 1 & 2
						offset = i / 2 * 2;
						fan_led_port1 &= ~(PORT1_FAN_LED_RED_MASK >> offset); //turn on red led
						fan_led_port1 |= PORT1_FAN_LED_BLUE_MASK >> offset; //turn off blue led
					} else { //FAN 3~6
						offset = (i - 4) / 2 * 2;
						fan_led_port0 &= ~(PORT0_FAN_LED_RED_MASK << offset); //turn on red led
						fan_led_port0 |= PORT0_FAN_LED_BLUE_MASK << offset; //turn off blue led
					}
				} else {
					fan_presence[i/2] = 1;
				}

				if (g_FanInputObjPath.size_sensor_list > 0)
					k+=1;
				else
					k+=2;
			}
		} else {
			FinalFanSpeed = 255;
			closeloop_first_time = 0;
			first_time_set = 0;
			fan_led_port0 = FAN_LED_OFF;
			fan_led_port1 = FAN_LED_OFF;
			real_fanspeed = 100;
		}

		set_fanled(fan_led_port0,fan_led_port1, 0);

		if (g_fan_para_shm != NULL) {
			if (FinalFanSpeed < g_fan_para_shm->min_fanspeed) {
				FinalFanSpeed = g_fan_para_shm->min_fanspeed;
				real_fanspeed = (double)  g_fan_para_shm->min_fanspeed/100;
			}
			else if (FinalFanSpeed > g_fan_para_shm->max_fanspeed) {
				FinalFanSpeed = g_fan_para_shm->max_fanspeed;
				real_fanspeed = (double)  g_fan_para_shm->max_fanspeed/100;
			}
		}

		if (g_fan_para_shm->debug_msg_info_en == 1)
			printf("[FAN_ALGORITHM][Set FanSpeed value] :%d\n", FinalFanSpeed);
		for(i=0; i<g_FanSpeedObjPath.size; i++) {
			rc = sd_bus_call_method(bus,
						g_FanSpeedObjPath.service_bus,
						g_FanSpeedObjPath.path[i],			// Object path
						g_FanSpeedObjPath.service_inf,
						"setValue_Fan",
						&bus_error,
						&response,
						"ii",
						(i+1),
						FinalFanSpeed);
			if(rc < 0)
				fprintf(stderr, "Failed to adjust fan speed via dbus: %s\n", bus_error.message);
			sd_bus_error_free(&bus_error);
			response = sd_bus_message_unref(response);
		}
		usleep(200*1000);
	}
	bus = sd_bus_flush_close_unref(bus);
	freeall_fan_obj(&g_Closeloop_Header);
	freeall_fan_obj(&g_Openloop_Header);
	shmdt(g_shm_addr);
	shmctl(g_shm_id, IPC_RMID, NULL);
	return rc < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int get_dbus_fan_parameters(sd_bus *bus, char *request_param, int *reponse_len, char reponse_data[50][200], int *size_sensor_list, int *base_sensor_number, int *flag_shm)
{
	sd_bus_error bus_error = SD_BUS_ERROR_NULL;
	sd_bus_message *response = NULL;
	int rc = 0;
	const char*  response_param;

	*reponse_len = 0; //clear reponse_len
	*size_sensor_list = 0;
	*base_sensor_number = -1;
	*flag_shm = 0;

	rc = sd_bus_call_method(bus,
				"org.openbmc.managers.System",
				"/org/openbmc/managers/System",
				"org.openbmc.managers.System",
				"getFanControlParams",
				&bus_error,
				&response,
				"s", request_param);
	if(rc < 0) {
		printf("%s, %d response message:[%s]\n", __FUNCTION__, __LINE__, strerror(-rc));
	} else {
		rc = sd_bus_message_read(response, "s", &response_param);
		if (rc < 0 ) {
			fprintf(stderr, "Failed to parse response message:[%s]\n", strerror(-rc));
			return rc;
		}

		int stard_idx = 0, end_idx = 0;
		int str_len = 0;
		while (response_param[end_idx]!=0) {
			if (response_param[end_idx] == ';') { // ';' means to indentify every data token
				if (stard_idx < end_idx) {
					str_len = end_idx - stard_idx;
					if (str_len < MAX_PATH_LEN) {
						memcpy(reponse_data[*reponse_len], response_param+stard_idx, str_len);
						reponse_data[*reponse_len][str_len] = '\0';
						*reponse_len=*reponse_len+1;
					} else
						printf("Error:%s[%d], parse string exceds length:%d, str_len:%d\n", __FUNCTION__, __LINE__, MAX_PATH_LEN, str_len);
				}
				stard_idx=end_idx+1;
			}
			end_idx++;
		}
	}
	sd_bus_error_free(&bus_error);
	response = sd_bus_message_unref(response);

	if (*reponse_len==4 || *reponse_len==5) {
		if (strcmp(reponse_data[1], "SensorNumberList") == 0) {
			*base_sensor_number = atof(reponse_data[2]);
			*size_sensor_list = atoi(reponse_data[3]);
			if (*reponse_len==5)
				*flag_shm = atoi(reponse_data[4]);
		}
	}
	return rc;
}

static int initial_fan_config(sd_bus *bus)
{
	int reponse_len = 0;
	char reponse_data[50][200];
	int i;
	int obj_count = 0;
	char *p;
	int index;
	int base_sensor_number = 0;
	int size_sensor_list = 0;
	int flag_shm = 0;

	get_dbus_fan_parameters(bus, "FAN_INPUT_OBJ", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	g_FanInputObjPath.size_sensor_list = size_sensor_list;
	g_FanInputObjPath.flag_shm = flag_shm;
	if (size_sensor_list > 0) {
		g_FanInputObjPath.size = size_sensor_list;
		for (i = 0; i<size_sensor_list; i++) {
			strcpy(g_FanInputObjPath.path[i], reponse_data[0]);
			g_FanInputObjPath.sensor_number_list[i] = base_sensor_number+i;
			if (g_FanInputObjPath.flag_shm  == 1)
				g_FanInputObjPath.map_shm_memory[i] = create_shared_memory(g_FanInputObjPath.path[i] , g_FanInputObjPath.sensor_number_list[i]);
		}
	} else {
		g_FanInputObjPath.size = reponse_len;
		for (i = 0; i<reponse_len; i+=2) {
			strcpy(g_FanInputObjPath.path[i], reponse_data[i]);
		}
	}
	get_dbus_fan_parameters(bus, "FAN_DBUS_INTF_LOOKUP#FAN_INPUT_OBJ", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	if (reponse_len == 2) {
		strcpy(g_FanInputObjPath.service_bus, reponse_data[0]);
		strcpy(g_FanInputObjPath.service_inf, reponse_data[1]);
	}

	get_dbus_fan_parameters(bus, "FAN_OUTPUT_OBJ", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	g_FanSpeedObjPath.size_sensor_list = size_sensor_list;
	g_FanSpeedObjPath.flag_shm = flag_shm;
	if (size_sensor_list > 0) {
		g_FanSpeedObjPath.size = size_sensor_list;
		for (i = 0; i<size_sensor_list; i++) {
			strcpy(g_FanSpeedObjPath.path[i], reponse_data[0]);
			g_FanSpeedObjPath.sensor_number_list[i] = base_sensor_number+i;
			if (g_FanSpeedObjPath.flag_shm  == 1)
				g_FanSpeedObjPath.map_shm_memory[i] = create_shared_memory(g_FanSpeedObjPath.path[i] , g_FanSpeedObjPath.sensor_number_list[i]);
		}

	} else {
		g_FanSpeedObjPath.size = reponse_len;
		for (i = 0; i<reponse_len; i++) {
			strcpy(g_FanSpeedObjPath.path[i], reponse_data[i]);
		}
	}
	get_dbus_fan_parameters(bus, "FAN_DBUS_INTF_LOOKUP#FAN_OUTPUT_OBJ", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	if (reponse_len == 2) {
		strcpy(g_FanSpeedObjPath.service_bus, reponse_data[0]);
		strcpy(g_FanSpeedObjPath.service_inf, reponse_data[1]);
	}

	get_dbus_fan_parameters(bus, "OPEN_LOOP_PARAM", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	g_ParamA = atof(reponse_data[0]);
	g_ParamB = atof(reponse_data[1]);
	g_ParamC = atof(reponse_data[2]);
	g_LowAmb = atoi(reponse_data[3]);
	g_UpAmb = atoi(reponse_data[4]);
	g_LowSpeed = atoi(reponse_data[5]);
	g_HighSpeed = atoi(reponse_data[6]);

	g_fan_para_shm->g_ParamA= g_ParamA;
	g_fan_para_shm->g_ParamB= g_ParamB;
	g_fan_para_shm->g_ParamC= g_ParamC;
	g_fan_para_shm->g_LowAmb= g_LowAmb;
	g_fan_para_shm->g_UpAmb= g_UpAmb;
	g_fan_para_shm->g_LowSpeed= g_LowSpeed;
	g_fan_para_shm->g_HighSpeed= g_HighSpeed;
	g_fan_para_shm->flag_openloop= 1;


	obj_count = 1;
	struct st_closeloop_obj_data *t_closeloop_data = NULL;
	while (1) {
		char prefix_closeloop[100];
		struct st_fan_obj_path_info *t_fan_obj = NULL;

		prefix_closeloop[0] = 0;
		sprintf(prefix_closeloop, "CLOSE_LOOP_GROUPS_%d", obj_count);
		get_dbus_fan_parameters(bus, prefix_closeloop, &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
		if (size_sensor_list>0)
			reponse_len = size_sensor_list;
		else
			if (reponse_len <= 2)
				break;

		t_fan_obj =(struct st_fan_obj_path_info *) malloc(sizeof(struct st_fan_obj_path_info));
		t_fan_obj->size_sensor_list = size_sensor_list;
		t_fan_obj->flag_shm = flag_shm;
		if (size_sensor_list>0) {
			t_fan_obj->size = size_sensor_list;
			for (i = 0; i<size_sensor_list; i++) {
				strcpy(t_fan_obj->path[i], reponse_data[0]);
				t_fan_obj->sensor_number_list[i] = base_sensor_number+i;
				if (t_fan_obj->flag_shm  == 1)
					t_fan_obj->map_shm_memory[i] = create_shared_memory(t_fan_obj->path[i] , t_fan_obj->sensor_number_list[i]);
			}
		} else {
			t_fan_obj->size = reponse_len;
			for (i = 0; i<reponse_len ; i++)
				strcpy(t_fan_obj->path[i], reponse_data[i]);
		}

		prefix_closeloop[0] = 0;
		sprintf(prefix_closeloop, "FAN_DBUS_INTF_LOOKUP#CLOSE_LOOP_GROUPS_%d", obj_count);
		get_dbus_fan_parameters(bus, prefix_closeloop, &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
		if (reponse_len == 2) {
			strcpy(t_fan_obj->service_bus, reponse_data[0]);
			strcpy(t_fan_obj->service_inf, reponse_data[1]);
		} else {
			free(t_fan_obj);
			break;
		}

		prefix_closeloop[0] = 0;
		sprintf(prefix_closeloop, "CLOSE_LOOP_PARAM_%d", obj_count);
		get_dbus_fan_parameters(bus, prefix_closeloop, &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
		if (reponse_len > 0) {
			t_closeloop_data = (struct st_closeloop_obj_data *) malloc(sizeof(struct st_closeloop_obj_data));
			index = obj_count -1;
			t_closeloop_data->index = index;
			t_closeloop_data->Kp = (double)atof(reponse_data[0]);
			t_closeloop_data->Ki = (double)atof(reponse_data[1]);
			t_closeloop_data->Kd = (double)atof(reponse_data[2]);
			t_closeloop_data->sensor_tracking = atoi(reponse_data[3]);
			t_closeloop_data->warning_temp = atoi(reponse_data[4]);

			g_fan_para_shm->closeloop_param[index].Kp = t_closeloop_data->Kp;
			g_fan_para_shm->closeloop_param[index].Ki = t_closeloop_data->Ki;
			g_fan_para_shm->closeloop_param[index].Kd = t_closeloop_data->Kd;
			g_fan_para_shm->closeloop_param[index].sensor_tracking = t_closeloop_data->sensor_tracking;
			g_fan_para_shm->closeloop_param[index].warning_temp = t_closeloop_data->warning_temp;
			g_fan_para_shm->flag_closeloop = 1;

			for (i = 0 ; i<100; i++)
				t_closeloop_data->interal_Err[i] = 0;
			t_closeloop_data->last_error = 0;
			t_closeloop_data->intergral_i = 0;
		}
		t_fan_obj->obj_data = (void*)t_closeloop_data;


		push_fan_obj(&g_Closeloop_Header, t_fan_obj);
		obj_count++;
	}
	g_fan_para_shm->closeloop_count =  obj_count-1;

	obj_count = 1;
	while (1) {
		char prefix_openloop[100];
		struct st_fan_obj_path_info *t_fan_obj = NULL;

		prefix_openloop[0] = 0;
		sprintf(prefix_openloop, "OPEN_LOOP_GROUPS_%d", obj_count);
		get_dbus_fan_parameters(bus, prefix_openloop, &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
		if (size_sensor_list>0)
			reponse_len = size_sensor_list;
		if (reponse_len == 0)
			break;

		t_fan_obj =(struct st_fan_obj_path_info *) malloc(sizeof(struct st_fan_obj_path_info));
		t_fan_obj->size_sensor_list = size_sensor_list;
		t_fan_obj->flag_shm = flag_shm;
		if (size_sensor_list>0) {
			t_fan_obj->size = size_sensor_list;
			for (i = 0; i<size_sensor_list; i++) {
				strcpy(t_fan_obj->path[i], reponse_data[0]);
				t_fan_obj->sensor_number_list[i] = base_sensor_number+i;
				if (t_fan_obj->flag_shm  == 1)
					t_fan_obj->map_shm_memory[i] = create_shared_memory(t_fan_obj->path[i] , t_fan_obj->sensor_number_list[i]);
			}
		} else {
			t_fan_obj->size = reponse_len;
			for (i = 0; i<reponse_len ; i++)
				strcpy(t_fan_obj->path[i], reponse_data[i]);
		}

		prefix_openloop[0] = 0;
		sprintf(prefix_openloop, "FAN_DBUS_INTF_LOOKUP#OPEN_LOOP_GROUPS_%d", obj_count);
		get_dbus_fan_parameters(bus, prefix_openloop, &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
		if (reponse_len == 2) {
			strcpy(t_fan_obj->service_bus, reponse_data[0]);
			strcpy(t_fan_obj->service_inf, reponse_data[1]);
		} else {
			free(t_fan_obj);
			break;
		}

		push_fan_obj(&g_Openloop_Header, t_fan_obj);

		obj_count++;
	}

	get_dbus_fan_parameters(bus, "FAN_LED_OFF", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	FAN_LED_OFF = reponse_len > 0? strtoul(reponse_data[0], &p, 16): FAN_LED_OFF;

	get_dbus_fan_parameters(bus, "FAN_LED_PORT0_ALL_BLUE", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	FAN_LED_PORT0_ALL_BLUE = reponse_len > 0? strtoul(reponse_data[0], &p, 16): FAN_LED_PORT0_ALL_BLUE;

	get_dbus_fan_parameters(bus, "FAN_LED_PORT1_ALL_BLUE", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	FAN_LED_PORT1_ALL_BLUE = reponse_len > 0? strtoul(reponse_data[0], &p, 16): FAN_LED_PORT1_ALL_BLUE;

	get_dbus_fan_parameters(bus, "FAN_LED_PORT0_ALL_RED", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	FAN_LED_PORT0_ALL_RED = reponse_len > 0? strtoul(reponse_data[0], &p, 16): FAN_LED_PORT0_ALL_RED;

	get_dbus_fan_parameters(bus, "FAN_LED_PORT1_ALL_RED", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	FAN_LED_PORT1_ALL_RED = reponse_len > 0? strtoul(reponse_data[0], &p, 16): FAN_LED_PORT1_ALL_RED;

	get_dbus_fan_parameters(bus, "PORT0_FAN_LED_RED_MASK", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	PORT0_FAN_LED_RED_MASK = reponse_len > 0? strtoul(reponse_data[0], &p, 16): PORT0_FAN_LED_RED_MASK;

	get_dbus_fan_parameters(bus, "PORT0_FAN_LED_BLUE_MASK", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	PORT0_FAN_LED_BLUE_MASK = reponse_len > 0? strtoul(reponse_data[0], &p, 16): PORT0_FAN_LED_BLUE_MASK;

	get_dbus_fan_parameters(bus, "PORT1_FAN_LED_RED_MASK", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	PORT1_FAN_LED_RED_MASK = reponse_len > 0? strtoul(reponse_data[0], &p, 16): PORT1_FAN_LED_RED_MASK;

	get_dbus_fan_parameters(bus, "PORT1_FAN_LED_BLUE_MASK", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	PORT1_FAN_LED_BLUE_MASK = reponse_len > 0? strtoul(reponse_data[0], &p, 16): PORT1_FAN_LED_BLUE_MASK;

	get_dbus_fan_parameters(bus, "FAN_LED_SPEED_LIMIT", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	g_fanled_speed_limit = reponse_len > 0? atoi(reponse_data[0]): g_fanled_speed_limit;

	get_dbus_fan_parameters(bus, "FAN_LED_I2C_BUS", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	if (reponse_len > 0)
		strcpy(g_FanLed_I2CBus, reponse_data[0]);

	get_dbus_fan_parameters(bus, "FAN_LED_I2C_SLAVE_ADDRESS", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	g_FanLed_SlaveAddr = reponse_len > 0? strtoul(reponse_data[0], &p, 16): g_FanLed_SlaveAddr;

	//Refere to FRU_INSTANCES in config/.py file to search inventory item object path with keywords: 'fan'
	get_dbus_fan_parameters(bus, "INVENTORY_FAN", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	g_FanModuleObjPath.size = reponse_len;
	g_FanModuleObjPath.size_sensor_list = size_sensor_list;
	for (i = 0; i<reponse_len; i++) {
		strcpy(g_FanModuleObjPath.path[i], reponse_data[i]);
	}

	get_dbus_fan_parameters(bus, "CHASSIS_POWER_STATE", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	g_PowerObjPath.size_sensor_list = size_sensor_list;
	if (reponse_len > 0) {
		strcpy(g_PowerObjPath.path[0], reponse_data[0]);
		g_PowerObjPath.size = 1;
	}
	get_dbus_fan_parameters(bus, "FAN_DBUS_INTF_LOOKUP#CHASSIS_POWER_STATE", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number, &flag_shm);
	g_PowerObjPath.size_sensor_list = size_sensor_list;
	if (reponse_len == 2) {
		strcpy(g_PowerObjPath.service_bus, reponse_data[0]);
		strcpy(g_PowerObjPath.service_inf, reponse_data[1]);
	}
}

static void inital_fan_pid_shm()
{
	key_t key = ftok(FAN_SHM_PATH, FAN_SHM_KEY);
	int i, j;

	g_shm_id = shmget(key, sizeof(struct st_fan_parameter), (IPC_CREAT | 0666));
	if (g_shm_id < 0) {
		printf("Error: shmid \n");
		return ;
	}
	g_shm_addr =  shmat(g_shm_id, NULL, 0);
	if (g_shm_addr == (char *) -1) {
		printf("Error: shmat \n");
		return ;
	}

	g_fan_para_shm = (struct st_fan_parameter *) g_shm_addr;
	if (g_fan_para_shm != NULL) {
		g_fan_para_shm->flag_closeloop = 0;
		g_fan_para_shm->flag_openloop = 0;
		g_fan_para_shm->max_fanspeed = 255;
		g_fan_para_shm->min_fanspeed = 0;
		for (i = 0 ; i<MAX_CLOSELOOP_PROFILE_NUM; i++) {
			g_fan_para_shm->closeloop_param[i].closeloop_sensor_reading = 0;
			g_fan_para_shm->closeloop_param[i].sample_n = SAMPLING_N;
			for (j = 0; j<MAX_CLOSELOOP_SENSOR_NUM; j++)
				g_fan_para_shm->closeloop_param[i].groups_sensor_reading[j] = 0;
		}
		g_fan_para_shm->closeloop_count = 0;
		g_fan_para_shm->openloop_sensor_offset = 0;
		g_fan_para_shm->debug_msg_info_en = 0;
	}
}

static void save_pid (void) {
    pid_t pid = 0;
    FILE *pidfile = NULL;
    pid = getpid();
    if (!(pidfile = fopen("/run/fan_algorithm.pid", "w"))) {
        fprintf(stderr, "failed to open pidfile\n");
        return;
    }
    fprintf(pidfile, "%d\n", pid);
    fclose(pidfile);
}

int main(int argc, char *argv[])
{
    save_pid();
	inital_fan_pid_shm();
	return fan_control_algorithm_monitor();
}


