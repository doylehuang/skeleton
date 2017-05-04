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
#define FAN_SHM_PATH "skeleton/fan_algorithm"


struct st_fan_parameter {
	int flag_closeloop; //0: init ; 1:do nothing ; 2: changed; 3:lock waiting
	double Kp;
	double Ki;
	double Kd;
	int sensor_tracking;
	int warning_temp;
	int pid_value;
	int closeloop_speed;
	int closeloop_sensor_reading;
	int sample_n;

	int flag_openloop; //0: init ; 1:do nothing ; 2: changed; 3:lock waiting
	float g_ParamA;
	float g_ParamB;
	float g_ParamC;
	int g_LowAmb;
	int g_UpAmb;
	int g_LowSpeed;
	int g_HighSpeed;
	int openloop_speed;
	int openloop_sensor_reading;

	int current_speed;
	int max_fanspeed;
	int min_fanspeed;
};



static struct st_fan_parameter *g_fan_para_shm = NULL;

#define UNKNOW_VALUE (999)

void usage(const char *prog)
{
	printf("Usage: %s [options] <closeloop/openloop/pwm> [parameters..]\n", prog);
	printf("\n  Options:\n"
	       "\n\t-w 'write fan parameters':\n"
	       "\t\t closeloop parameters settting:\n"
	       "\t\t\t -p : Kp\n"
	       "\t\t\t -i : Ki\n"
	       "\t\t\t -d : Kd\n"
	       "\t\t\t -t : target value\n"
	       "\t\t\t -n : sample number (1~100)\n"
	       "\t\t openloop parameters setting:\n"
	       "\t\t\t -a : ParamA\n"
	       "\t\t\t -b : ParamB\n"
	       "\t\t\t -c : ParamC\n"
	       "\t\t\t -l : LowAmb\n"
	       "\t\t\t -u : UperAmb\n"
	       "\t\t pwm parameters setting:\n"
	       "\t\t\t -s : set max fan speed\n"
	       "\t\t\t -m : set min fan speed\n"
	       "\n\t-r 'read fan parameters':\n"
	       "\n\tfor example:\n"
	       "\t\t %s  -w -p 0.45 -i -0.017 -d 0.3 -t 70 -n 20\n"
	       "\t\t %s  -w -a 0 -b 2 -c 0 -l 20 -u 38\n"
	       "\t\t %s  -w -s 255 -m 0 \n"
	       "\t\t %s  -r\n"
	       , prog,prog, prog
	      );
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

	struct st_fan_parameter fan_p;
	fan_p.Kp = UNKNOW_VALUE;
	fan_p.Ki = UNKNOW_VALUE;
	fan_p.Kd = UNKNOW_VALUE;
	fan_p.sensor_tracking = UNKNOW_VALUE;
	fan_p.sample_n = UNKNOW_VALUE;
	fan_p.g_ParamA = UNKNOW_VALUE;
	fan_p.g_ParamB = UNKNOW_VALUE;
	fan_p.g_ParamC = UNKNOW_VALUE;
	fan_p.g_LowAmb = UNKNOW_VALUE;
	fan_p.g_UpAmb = UNKNOW_VALUE;
	fan_p.max_fanspeed = UNKNOW_VALUE;
	fan_p.min_fanspeed = UNKNOW_VALUE;

	while ((opt = getopt(argc, argv, "hwrp:i:d:t:a:b:c:l:u:s:m:n:")) != -1) {
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
			fan_p.Kp =  atof(optarg);
			break;
		case 'i':
			fan_p.Ki =  atof(optarg);
			break;
		case 'd':
			fan_p.Kd =  atof(optarg);
			break;
		case 't':
			fan_p.sensor_tracking =  atoi(optarg);
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
			fan_p.sample_n =  atoi(optarg);
			if (fan_p.sample_n<1 || fan_p.sample_n>100) {
				printf("[Closeloop parameter] sample number must be during 1~100\n");
				return -1;
			}
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

	if (flag_wr == 0) {
		printf("[Closeloop Info] sensor_reading:%d pid_value:%d, closeloop speed:%d\n",
		       g_fan_para_shm->closeloop_sensor_reading, g_fan_para_shm->pid_value, g_fan_para_shm->closeloop_speed);

		printf("[Openloop Info] sensor_reading:%d , closeloop speed:%d\n",
		       g_fan_para_shm->openloop_sensor_reading, g_fan_para_shm->openloop_speed);

		printf("[PWM Info] current fan speed:%d (%d~%d)\n",
		       g_fan_para_shm->current_speed, g_fan_para_shm->min_fanspeed, g_fan_para_shm->max_fanspeed);
	} else if (flag_wr == 1) {
		if (fan_p.Kp!=UNKNOW_VALUE || fan_p.Ki!=UNKNOW_VALUE || fan_p.Kd!=UNKNOW_VALUE || fan_p.sensor_tracking!=UNKNOW_VALUE || fan_p.sample_n != UNKNOW_VALUE) {
			g_fan_para_shm->flag_closeloop = 3; //block wait

			if (fan_p.Kp != UNKNOW_VALUE) {
				printf("[Closeloop]Kp changed: %f --> %f\n", g_fan_para_shm->Kp, fan_p.Kp);
				g_fan_para_shm->Kp = fan_p.Kp;
			}
			if (fan_p.Ki != UNKNOW_VALUE) {
				printf("[Closeloop]Ki changed: %f --> %f\n", g_fan_para_shm->Ki, fan_p.Ki);
				g_fan_para_shm->Ki = fan_p.Ki;
			}
			if (fan_p.Kd != UNKNOW_VALUE) {
				printf("[Closeloop]Kd changed: %f --> %f\n", g_fan_para_shm->Kd, fan_p.Kd);
				g_fan_para_shm->Kd = fan_p.Kd;
			}
			if (fan_p.sensor_tracking != UNKNOW_VALUE) {
				printf("[Closeloop]Target Value changed: %d --> %d\n", g_fan_para_shm->sensor_tracking, fan_p.sensor_tracking);
				g_fan_para_shm->sensor_tracking = fan_p.sensor_tracking;
			}
			if (fan_p.sample_n != UNKNOW_VALUE) {
				printf("[Closeloop]Sample number changed: %d --> %d\n", g_fan_para_shm->sample_n, fan_p.sample_n);
				g_fan_para_shm->sample_n = fan_p.sample_n;
			}

			g_fan_para_shm->flag_closeloop = 2; //fan closeloop parameter changed
		}

		if (fan_p.g_ParamA!=UNKNOW_VALUE || fan_p.g_ParamB!=UNKNOW_VALUE || fan_p.g_ParamC!=UNKNOW_VALUE || fan_p.g_LowAmb!=UNKNOW_VALUE || fan_p.g_UpAmb!=UNKNOW_VALUE) {
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


	}

}
