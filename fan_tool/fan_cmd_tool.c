#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <systemd/sd-bus.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>

#define FAN_SHM_KEY  (4320)
#define FAN_SHM_PATH "skeleton/fan_algorithm2"


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
	int cur_interal_Err;
	int last_interal_error;
	int groups_sensor_reading[10];
};


struct st_fan_parameter {
	int flag_closeloop; //0: init ; 1:do nothing ; 2: changed; 3:lock waiting
	int closeloop_count;
	struct st_fan_closeloop_par closeloop_param[8];

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



static struct st_fan_parameter *g_fan_para_shm = NULL;

#define UNKNOW_VALUE (999)

void usage(const char *prog)
{
	printf("Usage: %s [options] <closeloop/openloop/pwm> [parameters..]  V2 version\n", prog);
	printf("\n  Options:\n"
	       "\n\t-w 'write fan parameters':\n"
	       "\t\t closeloop parameters settting:\n"
	       "\t\t\t -p : Kp\n"
	       "\t\t\t -i : Ki\n"
	       "\t\t\t -d : Kd\n"
	       "\t\t\t -t : target value\n"
	       "\t\t\t -n : sample number (1~100)\n"
	       "\t\t\t -e : select closeloop index(0:GPU;1:PEX9797)\n"
	       "\t\t openloop parameters setting:\n"
	       "\t\t\t -a : ParamA\n"
	       "\t\t\t -b : ParamB\n"
	       "\t\t\t -c : ParamC\n"
	       "\t\t\t -l : LowAmb\n"
	       "\t\t\t -u : UperAmb\n"
	       "\t\t\t -o : Offset\n"
	       "\t\t pwm parameters setting:\n"
	       "\t\t\t -s : set max fan speed\n"
	       "\t\t\t -m : set min fan speed\n"
	       "\t\t fan_algorithm debug message info setting:\n"
	       "\t\t\t -g : [0: close debug message; 1:open debug message]\n"
	       "\n\t-r 'read fan parameters':\n"
	       "\n\tfor example:\n"
	       "\t\t %s  -w -e 0 -p 0.45 -i -0.017 -d 0.3 -t 70 -n 20 ==> set closeloop index:0 , GPU\n"
	       "\t\t %s  -w -e 1 -p 0.45 -i -0.017 -d 0.3 -t 90 -n 20 ==> set closeloop index:1 , PEX9797\n"
	       "\t\t %s  -w -a 0 -b 2 -c 0 -l 20 -u 38\n"
	       "\t\t %s  -w -s 255 -m 0 \n"
	       "\t\t %s  -w -g 1 \n"
	       "\t\t %s  -r\n"
	       , prog,prog, prog,prog,prog
	      );
}

int read_file(char *path)
{
	FILE *fp1 = fopen(path,"r");
	int val;
	if(fp1 == NULL) 
		return -1;
	fscanf(fp1, "%d", &val);
	if (val < 0)
		val = 0;
	fflush(fp1);
	fclose(fp1);
	return val;
}

void show_gpu_sensor_temp(void)
{
	int i = 1;
	char path[100];
	int val;
	int val_mem;
	printf("\n==== GPU SENSOR TEMP LIST=======\n");
	for (i = 1; i<=8;i++)
	{
		sprintf(path, "/tmp/gpu/gpu%d_temp", i);
		val = read_file(path);

		sprintf(path, "/tmp/gpu/gpu%d_mem_temp", i);
		val_mem = read_file(path);

		printf("GPU %d temp:%d ; memory temp:%d\n", i, val, val_mem);
	}
}


void show_fio_sensor_temp(void)
{
	int i = 1;
	char path[100];
	int val_fio_1, val_fio_2;
	printf("\n==== FIO SENSOR TEMP LIST=======\n");
	val_fio_1 = read_file("/sys/class/hwmon/hwmon1/temp1_input");
	val_fio_2 = read_file("/sys/class/hwmon/hwmon2/temp1_input");

	printf("FIO Temp: [%.2f  ,  %.2f]\n\n", 
		(double) val_fio_1/1000,
		(double) val_fio_2/1000);
}


int
main(int argc, char * const argv[])
{
	key_t key = ftok(FAN_SHM_PATH, FAN_SHM_KEY);
	char *shm;
	int shmid;
	int i;
	int fd;
	int opt;
	int flag_wr = 0; //0: read fan parameters; 1:write fan parameters
	int closeloop_index = 0;
	struct st_fan_closeloop_par *closeloop;

	struct st_fan_parameter fan_p;
	fan_p.closeloop_param[0].Kp = UNKNOW_VALUE;
	fan_p.closeloop_param[0].Ki = UNKNOW_VALUE;
	fan_p.closeloop_param[0].Kd = UNKNOW_VALUE;
	fan_p.closeloop_param[0].sensor_tracking = UNKNOW_VALUE;
	fan_p.closeloop_param[0].sample_n = UNKNOW_VALUE;
	fan_p.g_ParamA = UNKNOW_VALUE;
	fan_p.g_ParamB = UNKNOW_VALUE;
	fan_p.g_ParamC = UNKNOW_VALUE;
	fan_p.g_LowAmb = UNKNOW_VALUE;
	fan_p.g_UpAmb = UNKNOW_VALUE;
	fan_p.openloop_sensor_offset = UNKNOW_VALUE;
	fan_p.max_fanspeed = UNKNOW_VALUE;
	fan_p.min_fanspeed = UNKNOW_VALUE;
	fan_p.debug_msg_info_en = UNKNOW_VALUE;

	while ((opt = getopt(argc, argv, "hwrp:i:d:t:a:b:c:l:u:s:m:n:e:o:g:")) != -1) {
		switch (opt) {
		case 'h':
			usage(argv[0]);
			return 0;
		case 'w':
			flag_wr = 1;
			break;
		case 'r':
			flag_wr = 0;
			break;
		case 'p':
			fan_p.closeloop_param[0].Kp = atof(optarg);
			break;
		case 'i':
			fan_p.closeloop_param[0].Ki =  atof(optarg);
			break;
		case 'd':
			fan_p.closeloop_param[0].Kd =  atof(optarg);
			break;
		case 't':
			fan_p.closeloop_param[0].sensor_tracking =  atoi(optarg);
			break;
		case 'a':
			fan_p.g_ParamA =  atof(optarg);
			break;
		case 'b':
			fan_p.g_ParamB =  atof(optarg);
			break;
		case 'c':
			fan_p.g_ParamC =  atof(optarg);
			break;
		case 'l':
			fan_p.g_LowAmb =  atoi(optarg);
			break;
		case 'u':
			fan_p.g_UpAmb =  atoi(optarg);
			break;
		case 's':
			fan_p.max_fanspeed =  atoi(optarg);
			break;
		case 'm':
			fan_p.min_fanspeed =  atoi(optarg);
			break;
		case 'n':
			fan_p.closeloop_param[0].sample_n =  atoi(optarg);
			if (fan_p.closeloop_param[0].sample_n<1 || fan_p.closeloop_param[0].sample_n>100) {
				printf("[Closeloop parameter] sample number must be during 1~100\n");
				return -1;
			}
			break;
	    case 'e':
			closeloop_index =  atoi(optarg);
			break;
		case 'o':
			fan_p.openloop_sensor_offset =  atoi(optarg);
			break;
		case 'g':
			fan_p.debug_msg_info_en =atoi(optarg);
			break;
		default:
			usage(argv[0]);
			return -1;
		}
	}

	shmid = shmget(key, sizeof(struct st_fan_parameter) , S_IRUSR | 0666);
	if (shmid < 0) {
		printf("Error: shmid \n");
		return -1;
	}

	shm = shmat(shmid, NULL, 0);
	if (shm == (char *) -1) {
		printf("Error: shmat \n");
		return -1;
	}

	g_fan_para_shm = (struct st_fan_parameter *) shm;


	closeloop = &(g_fan_para_shm->closeloop_param[closeloop_index]);


	if (flag_wr == 0) {
		for (i = 0 ; i<g_fan_para_shm->closeloop_count; i++)
		{
			closeloop = &(g_fan_para_shm->closeloop_param[i]);
			printf("[Closeloop Info %d] sensor_reading:%d, kp:%f, Ki:%f, Kd:%f, target:%d, pid_value:%f, closeloop speed:%f\n",
		    	   i, closeloop->closeloop_sensor_reading, closeloop->Kp, closeloop->Ki, closeloop->Kd,closeloop->sensor_tracking,
		    	   closeloop->pid_value, closeloop->closeloop_speed);

		}
		printf("[Openloop Info] sensor_reading:%f , sensor_reading_offset:%d openloop speed:%d\n",
		       g_fan_para_shm->openloop_sensor_reading, g_fan_para_shm->openloop_sensor_offset ,  g_fan_para_shm->openloop_speed);

		printf("[PWM Info] current fan speed:%d (%d~%d)\n",
		       g_fan_para_shm->current_speed, g_fan_para_shm->min_fanspeed, g_fan_para_shm->max_fanspeed);

		show_gpu_sensor_temp();
		show_fio_sensor_temp();
	} else if (flag_wr == 1) {
		if (fan_p.closeloop_param[0].Kp!=UNKNOW_VALUE || fan_p.closeloop_param[0].Ki!=UNKNOW_VALUE || fan_p.closeloop_param[0].Kd!=UNKNOW_VALUE || fan_p.closeloop_param[0].sensor_tracking!=UNKNOW_VALUE || fan_p.closeloop_param[0].sample_n != UNKNOW_VALUE) {
			g_fan_para_shm->flag_closeloop = 3; //block wait

			printf("Set Closeloop index: %d\n", closeloop_index);

			if (fan_p.closeloop_param[0].Kp != UNKNOW_VALUE) {
				printf("[Closeloop]Kp changed: %f --> %f\n", closeloop->Kp, fan_p.closeloop_param[0].Kp);
				closeloop->Kp = fan_p.closeloop_param[0].Kp;
			}
			if (fan_p.closeloop_param[0].Ki != UNKNOW_VALUE) {
				printf("[Closeloop]Ki changed: %f --> %f\n", closeloop->Ki, fan_p.closeloop_param[0].Ki);
				closeloop->Ki = fan_p.closeloop_param[0].Ki;
			}
			if (fan_p.closeloop_param[0].Kd != UNKNOW_VALUE) {
				printf("[Closeloop]Kd changed: %f --> %f\n", closeloop->Kd, fan_p.closeloop_param[0].Kd);
				closeloop->Kd = fan_p.closeloop_param[0].Kd;
			}
			if (fan_p.closeloop_param[0].sensor_tracking != UNKNOW_VALUE) {
				printf("[Closeloop]Target Value changed: %d --> %d\n", closeloop->sensor_tracking, fan_p.closeloop_param[0].sensor_tracking);
				closeloop->sensor_tracking = fan_p.closeloop_param[0].sensor_tracking;
			}
			if (fan_p.closeloop_param[0].sample_n != UNKNOW_VALUE) {
				printf("[Closeloop]Sample number changed: %d --> %d\n", closeloop->sample_n, fan_p.closeloop_param[0].sample_n);
				closeloop->sample_n = fan_p.closeloop_param[0].sample_n;
			}

			g_fan_para_shm->flag_closeloop = 2; //fan closeloop parameter changed
		}

		if (fan_p.g_ParamA!=UNKNOW_VALUE || fan_p.g_ParamB!=UNKNOW_VALUE || fan_p.g_ParamC!=UNKNOW_VALUE || fan_p.g_LowAmb!=UNKNOW_VALUE || fan_p.g_UpAmb!=UNKNOW_VALUE || fan_p.openloop_sensor_offset!=UNKNOW_VALUE) {
			g_fan_para_shm->flag_openloop = 3; //block wait

			if (fan_p.g_ParamA != UNKNOW_VALUE) {
				printf("[Openloop]g_ParamA changed: %f --> %f\n", g_fan_para_shm->g_ParamA, fan_p.g_ParamA);
				g_fan_para_shm->g_ParamA = fan_p.g_ParamA;
			}
			if (fan_p.g_ParamB != UNKNOW_VALUE) {
				printf("[Openloop]g_ParamB changed: %f --> %f\n", g_fan_para_shm->g_ParamB, fan_p.g_ParamB);
				g_fan_para_shm->g_ParamB = fan_p.g_ParamB;
			}
			if (fan_p.g_ParamC != UNKNOW_VALUE) {
				printf("[Openloop]g_ParamC changed: %f --> %f\n", g_fan_para_shm->g_ParamC, fan_p.g_ParamC);
				g_fan_para_shm->g_ParamC = fan_p.g_ParamC;
			}
			if (fan_p.g_LowAmb != UNKNOW_VALUE) {
				printf("[Openloop]g_LowAmb changed: %d --> %d\n", g_fan_para_shm->g_LowAmb, fan_p.g_LowAmb);
				g_fan_para_shm->g_LowAmb = fan_p.g_LowAmb;
			}
			if (fan_p.g_UpAmb != UNKNOW_VALUE) {
				printf("[Openloop]g_UpAmb changed: %d --> %d\n", g_fan_para_shm->g_UpAmb, fan_p.g_UpAmb);
				g_fan_para_shm->g_UpAmb = fan_p.g_UpAmb;
			}
			if (fan_p.openloop_sensor_offset!=UNKNOW_VALUE) {
				printf("[Openloop]openloop_sensor_offset changed: %d --> %d\n", g_fan_para_shm->openloop_sensor_offset, fan_p.openloop_sensor_offset);
				g_fan_para_shm->openloop_sensor_offset = fan_p.openloop_sensor_offset;
			}

			g_fan_para_shm->flag_openloop = 2; //fan openloop parameter changed
		}

		if (fan_p.max_fanspeed != UNKNOW_VALUE) {
			printf("[PWM] Max fan speed  changed: %d --> %d\n", g_fan_para_shm->max_fanspeed, fan_p.max_fanspeed);
			g_fan_para_shm->max_fanspeed = fan_p.max_fanspeed;
		}
		if (fan_p.min_fanspeed != UNKNOW_VALUE) {
			printf("[PWM] Min fan speed  changed: %d --> %d\n", g_fan_para_shm->min_fanspeed, fan_p.min_fanspeed);
			g_fan_para_shm->min_fanspeed = fan_p.min_fanspeed;
		}

		if (fan_p.debug_msg_info_en != UNKNOW_VALUE) {
			printf("fan_algorithm debug message info setting changed: %d --> %d\n", g_fan_para_shm->debug_msg_info_en, fan_p.debug_msg_info_en);
			g_fan_para_shm->debug_msg_info_en = fan_p.debug_msg_info_en;
		}


	}

}

