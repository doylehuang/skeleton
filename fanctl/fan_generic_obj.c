#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>
#include <systemd/sd-bus.h>

typedef enum {
	EM_PWM_IDX_0 = 0,
	EM_PWM_IDX_1,
	EM_PWM_IDX_2,
	EM_PWM_IDX_3,
	EM_PWM_IDX_4,
	EM_PWM_IDX_5,
	EM_PWM_IDX_6,
	EM_PWM_IDX_7,
} EM_PWM_IDX;

typedef enum {
	EM_FAN_CMD_EN = 0, //set pwm & tach enable
	EM_PWM_CMD_FALLING, //set pwm duty falling
	EM_PWM_CMD_RISING, //set pwm duty rising
	EM_PWM_CMD_TYPE, //set pwm type (M/N/O)
	EM_TACH_CMD_RPM, //set fan tach rpm
	EM_TACH_CMD_SOURCE, //set fan tach source
} EM_PWM_NODE_CMD;

#define FAN_DBUS_OBJ_ROOT "/org/openbmc"
#define FAN_DBUS_OBJ_FORMAT "pwm%d"
#define FAN_TACH_DBUS_OBJ_FORMAT "fan_tacho%d"
#define FAN_DBUS "org.openbmc"
#define FAN_INTFERFACE "org.openbmc.Fan"

#define PWM_MAX_UNIT (255)
#define MAX_FAN_NUM (6)
#define MAX_PATH_LEN 70
#define SYS_PWM_PATH "/sys/devices/platform/ast_pwm_tacho.0/"

static EM_PWM_IDX g_fan_map_pwm_tab[] = {
	EM_PWM_IDX_0,  //fan0 map to pwm0
	EM_PWM_IDX_1,  //fan1 map to pwm1
	EM_PWM_IDX_2,  //fan2 map to pwm2
	EM_PWM_IDX_3,  //fan3 map to pwm3
	EM_PWM_IDX_4,  //fan4 map to pwm4
	EM_PWM_IDX_5,  //fan5 map to pwm5
	EM_PWM_IDX_6,  //fan6 map to pwm6
	EM_PWM_IDX_7,  //fan7 map to pwm7
};

static char g_pwm_cmd_map_sys_tab[][20] = {
	"_en",  //EM_FAN_CMD_EN --> "pwmX_en", X :mean pwm index eg: "pwm0_en"
	"_falling",  //EM_PWM_CMD_FALLING --> "pwmX_falling", X :mean pwm index eg: "pwm0_falling"
	"_rising",  //EM_PWM_CMD_RISING --> "pwmX_rising", X :mean pwm index eg: "pwm0_rising"
	"_type",  //EM_PWM_CMD_TYPE --> "pwmX_type", X :mean pwm index eg: "pwm0_type"
	"_rpm",  //EM_TACH_CMD_RPM --> "tachoX_rpm", X :mean pwm index eg: "tacho0_rpm"
	"_source",  //EM_TACH_CMD_SOURCE --> "tachoX_source", X :mean pwm index eg: "tacho0_source"
};

/*
 * ----------------------------------------------------------------
 * Router function for any FAN operations that come via dbus
 *----------------------------------------------------------------
 */
static char *
get_fan_name(const char *dbus_path)
{
	char *fan_name = NULL;

	fan_name = strrchr(dbus_path, '/');
	if(fan_name) {
		fan_name++;
	}
	return fan_name;

}

static int
get_fan_index(char *fan_name, char *format_s)
{
	int fan_index;
	sscanf(fan_name, format_s, &fan_index);
	return fan_index;
}


static void
sys_pwm_write(EM_PWM_IDX pwm_idex, EM_PWM_NODE_CMD pwm_cmd, int write_value, char*prefix)
{
	char sys_pwm_path[100];
	char sys_cmd[100];
	int rc;

	if (write_value > PWM_MAX_UNIT)
		write_value = PWM_MAX_UNIT;
	else if (write_value < 0)
		write_value = 0;

	rc = sprintf(sys_pwm_path, "%s%s%d%s", SYS_PWM_PATH, prefix, pwm_idex, g_pwm_cmd_map_sys_tab[pwm_cmd]);
	sprintf(sys_cmd, "echo %d > %s", write_value, sys_pwm_path);
	system(sys_cmd);
}

static int
sys_pwm_read(EM_PWM_IDX pwm_idex, EM_PWM_NODE_CMD pwm_cmd, char*prefix)
{
	char sys_pwm_path[100];
	FILE *fp;
	int retValue = -1;
	char buf[100] = {0};
	int ret_len;

	sprintf(sys_pwm_path, "%s%s%d%s", SYS_PWM_PATH, prefix, pwm_idex, g_pwm_cmd_map_sys_tab[pwm_cmd]);

	fp = fopen(sys_pwm_path,"r");
	if(fp == NULL) {
		fprintf(stderr,"Error:[%s] opening:[%s]\n",strerror(errno),sys_pwm_path);
		return -1;
	}
	ret_len = fread(buf, 1, 100, fp);
	sscanf(buf, "%d", &retValue);
	fclose(fp);

	return retValue;
}

static int
pwm_fan_function_router(sd_bus_message *msg, void *user_data,
			sd_bus_error *ret_error)
{
	/* Generic error reporter. */
	int rc = -1;
	/* Extract the fan name from the full dbus path */
	const char *fan_path = sd_bus_message_get_path(msg);

	if(fan_path == NULL) {
		fprintf(stderr, "Error. FAN path is empty");
		return sd_bus_reply_method_return(msg, "i", rc);
	}

	char *fan_name = get_fan_name(fan_path);
	if(fan_name == NULL) {
		fprintf(stderr, "Invalid FAN name for path :[%s]\n",fan_path);
		return sd_bus_reply_method_return(msg, "i", rc);
	}

	/* Now that we have the FAN name, get the Operation. */
	const char *fan_function = sd_bus_message_get_member(msg);
	if(fan_function == NULL) {
		fprintf(stderr, "Null FAN function specificed for : [%s]\n",fan_name);
		return sd_bus_reply_method_return(msg, "i", rc);
	}

	/* Route the user action to appropriate handlers. */
	if(strcmp(fan_function, "getValue_Fan") == 0) {
		if (strncmp(fan_name, "fan_tacho", strlen("fan_tacho")) == 0) {
			int fan_index = 0;
			rc = sd_bus_message_read(msg, "i", &fan_index);
			int tach_reading =  sys_pwm_read(fan_index, EM_TACH_CMD_RPM, "tacho");
			return sd_bus_reply_method_return(msg, "i", tach_reading);
		} else {
			int pwm_idex;
			rc = sd_bus_message_read(msg, "i", &pwm_idex);
			int pwm_reading =  sys_pwm_read(pwm_idex, EM_PWM_CMD_FALLING, "pwm");
			return sd_bus_reply_method_return(msg, "i", pwm_reading);
		}
	} else if(strcmp(fan_function, "setValue_Fan") == 0) {
		int fan_speed = 0;
		int pwm_idex = 0;
		/* Extract values into 'ss' ( string, string) */
		rc = sd_bus_message_read(msg, "ii", &pwm_idex,  &fan_speed);
		sys_pwm_write(pwm_idex, EM_PWM_CMD_FALLING, fan_speed, "pwm");
		sys_pwm_write(pwm_idex, EM_PWM_CMD_RISING, 0x00, "pwm");
	} else {
		fprintf(stderr,"Invalid FAN function:[%s]\n",fan_function);
	}

	return sd_bus_reply_method_return(msg, "i", rc);
}

/*
 * -----------------------------------------------
 * Dbus Services offered by this PWM controller
 * -----------------------------------------------
 */
static const sd_bus_vtable fan_control_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("getValue_Fan", "i", "i", &pwm_fan_function_router, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("setValue_Fan", "ii", "i", &pwm_fan_function_router, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

static int get_dbus_fan_parameters(sd_bus *bus, char *request_param, int *reponse_len, char reponse_data[50][200], int *size_sensor_list, int *base_sensor_number)
{
	sd_bus_error bus_error = SD_BUS_ERROR_NULL;
	sd_bus_message *response = NULL;
	int rc = 0;
	const char*  response_param;

	*reponse_len = 0; //clear reponse_len
	*size_sensor_list = 0;
	*base_sensor_number = -1;

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

	if (*reponse_len==4) {
		if (strcmp(reponse_data[1], "SensorNumberList") == 0) {
			*base_sensor_number = atof(reponse_data[2]);
			*size_sensor_list = atoi(reponse_data[3]);
		}
	}

	return rc;
}

static int
register_fan_services(sd_bus *bus_type, sd_bus_slot *fan_slot, char *fan_object)
{
	int rc = -1;
	/* Install the object */
	rc = sd_bus_add_object_vtable(bus_type,
				      &fan_slot,
				      fan_object, /* object path */
				      FAN_INTFERFACE, /* interface name */
				      fan_control_vtable,
				      NULL);
	if(rc < 0) {
		fprintf(stderr, "Failed to add object to dbus: %s\n", strerror(-rc));
		return rc;
	}

	rc = sd_bus_emit_object_added(bus_type, fan_object);
	if(rc < 0) {
		fprintf(stderr, "Failed to emit InterfacesAdded "
			"signal: %s\n", strerror(-rc));
		return rc;
	}

	return rc;
}

int
start_fan_services()
{
	const char *fan_dbus_root = FAN_DBUS_OBJ_ROOT;
	/* Generic error reporter. */
	int rc = -1;
	int num_fans = 0;

	/* Bus and slot where we are offering the Fan dbus service. */
	sd_bus *bus_type = NULL;
	sd_bus_slot *fan_slot = NULL;

	/* Get a hook onto system bus. */
	rc = sd_bus_open_system(&bus_type);
	if(rc < 0) {
		fprintf(stderr,"Error opening system bus.\n");
		return rc;
	}

	/* Install a freedesktop object manager */
	rc = sd_bus_add_object_manager(bus_type, NULL, fan_dbus_root);
	if(rc < 0) {
		fprintf(stderr, "Failed to add object to dbus: %s\n",
			strerror(-rc));
		sd_bus_slot_unref(fan_slot);
		sd_bus_flush_close_unref(bus_type);
		return rc;
	}

	int reponse_len = 0;
	char reponse_data[50][200];
	int i;
	int fan_index=0, map_pwm_index=0;
	int base_sensor_number = 0;
	int size_sensor_list = 0;
	get_dbus_fan_parameters(bus_type, "FAN_INPUT_OBJ", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number);
	if (size_sensor_list>0)
		reponse_len = size_sensor_list;
	for (i = 0; i<reponse_len; i++) {
		 map_pwm_index = (i%6) + 1;
		 fan_index = i + 1;
		//Enable fan tach
		sys_pwm_write(fan_index, EM_TACH_CMD_SOURCE, map_pwm_index, "tacho");
		sys_pwm_write(fan_index, EM_FAN_CMD_EN, 1, "tacho");
	}

	get_dbus_fan_parameters(bus_type, "FAN_OUTPUT_OBJ", &reponse_len, reponse_data, &size_sensor_list, &base_sensor_number);
	if (size_sensor_list > 0) {
		register_fan_services(bus_type, fan_slot, reponse_data[0]);
	} else {
		for (i = 0; i<reponse_len; i++)
			register_fan_services(bus_type, fan_slot, reponse_data[i]);
	}

	/* If we had success in adding the providers, request for a bus name. */
	if(rc >= 0) {
		/* Take one in OpenBmc */
		rc = sd_bus_request_name(bus_type, FAN_DBUS, 0);
		if(rc < 0) {
			fprintf(stderr, "Failed to acquire service name: %s\n", strerror(-rc));
		} else {
			for(;;) {
				/* Process requests */
				rc = sd_bus_process(bus_type, NULL);
				if(rc < 0) {
					fprintf(stderr, "Failed to process bus: %s\n", strerror(-rc));
					break;
				}

				if(rc > 0) {
					continue;
				}

				rc = sd_bus_wait(bus_type, (uint64_t) - 1);
				if(rc < 0) {
					fprintf(stderr, "Failed to wait on bus: %s\n", strerror(-rc));
					break;
				}
			}
		}
	}

	sd_bus_slot_unref(fan_slot);
	sd_bus_flush_close_unref(bus_type);
	return rc;
}

static void save_pid (void) {
    pid_t pid = 0;
    FILE *pidfile = NULL;
    pid = getpid();
    if (!(pidfile = fopen("/run/fan_generic_obj.pid", "w"))) {
        fprintf(stderr, "failed to open pidfile\n");
        return;
    }
    fprintf(pidfile, "%d\n", pid);
    fclose(pidfile);
}

int
main(void)
{
	int rc = 0;

	/* This call is not supposed to return. If it does, then an error */
    save_pid();
	rc = start_fan_services();
	if(rc < 0) {
		fprintf(stderr, "Error starting FAN Services. Exiting");
	}
	return rc;
}

