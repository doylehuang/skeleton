#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <syslog.h>
#include <openbmc_intf.h>
#include <openbmc.h>
#include <gpio.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

struct Device {
	const char *device;
	const char *driver;
	uint8_t bound : 1;
};

enum GPIODirection {
	GPIO_IN,
	GPIO_OUT,
};

struct GPIOExport {
	const int pin;
	const enum GPIODirection dir;
	uint8_t exported : 1;
	uint8_t configured : 1;
};

/* ------------------------------------------------------------------------- */
static const gchar* dbus_object_path = "/org/openbmc/control";
static const gchar* instance_name = "power0";
static const gchar* dbus_name = "org.openbmc.control.Power";

//This object will use these GPIOs
GPIO power_pin    = (GPIO)
{ "SYS_FORCE_PWR_OFF"
};
GPIO pgood        = (GPIO)
{ "PWR_ON_REQ_N"
};
GPIO power_led    = (GPIO)
{ "PWR_STA_LED_N"
};

static GDBusObjectManagerServer *manager = NULL;

time_t pgood_timeout_start = 0;
enum {
	POWER_OFF,
	POWER_ON
};

static void
power_led_init()
{
	uint8_t gpio;

	gpio_open(&pgood);
	gpio_read(&pgood,&gpio);
	gpio_close(&pgood);

	if(gpio) {
		gpio_open(&power_led);
		gpio_write(&power_led,1);
		gpio_close(&power_led);
	} else {
		gpio_open(&power_led);
		gpio_write(&power_led,0);
		gpio_close(&power_led);
	}
}

static void bind_devices(void)
{
	/* NOTE the order must conform with DTS, otherwise virtual I2C bus numbers
	 * may change.
	 */
	static struct Device devs[] = {
		{ .device = "0-0010", .driver = "/sys/bus/i2c/drivers/lm25066", },
		{ .device = "0-0011", .driver = "/sys/bus/i2c/drivers/lm25066", },
		{ .device = "0-0040", .driver = "/sys/bus/i2c/drivers/lm25066", },
		{ .device = "0-0041", .driver = "/sys/bus/i2c/drivers/lm25066", },
		{ .device = "0-0042", .driver = "/sys/bus/i2c/drivers/lm25066", },
		{ .device = "0-0043", .driver = "/sys/bus/i2c/drivers/lm25066", },
		{ .device = "0-0044", .driver = "/sys/bus/i2c/drivers/lm25066", },
		{ .device = "0-0045", .driver = "/sys/bus/i2c/drivers/lm25066", },
		{ .device = "0-0046", .driver = "/sys/bus/i2c/drivers/lm25066", },
		{ .device = "0-0047", .driver = "/sys/bus/i2c/drivers/lm25066", },
		{ .device = "2-0076", .driver = "/sys/bus/i2c/drivers/pca954x", },
		{ .device = "2-0075", .driver = "/sys/bus/i2c/drivers/pca954x", },
		{ .device = "3-0070", .driver = "/sys/bus/i2c/drivers/pca954x", },
	};

	int i = 0;
	struct Device *dev = NULL;
	char buff[64] = {0};
	struct stat stat = {0};
	FILE *fp = NULL;

	for (i = 0; i < sizeof(devs) / sizeof(struct Device); i++) {
		dev = &devs[i];
		if (!dev->bound) {
			/* Test whether device is already bound. */

			if (64 <= snprintf(buff, 64, "%s/%s", dev->driver, dev->device)) {
				printf("ERROR not enough space for device path\n");
				continue;
			}

			if (!lstat(buff, &stat)) {
				dev->bound = 1;
				continue;
			}

			if (errno != ENOENT) {
				printf("ERROR on lstat(%s): %s\n", buff, strerror(errno));
				continue;
			}

			/* Bind device to specified driver. */

			if (64 <= snprintf(buff, 64, "%s/bind", dev->driver)) {
				printf("ERROR not enough space for bind path\n");
				continue;
			}

			if (!(fp = fopen(buff, "w"))) {
				printf("ERROR on fopen(%s): %s\n", buff, strerror(errno));
				continue;
			}

			fwrite(dev->device, sizeof(char), strlen(dev->device), fp);

			fclose(fp);
		}
	}
}

static void export_gpios(void)
{
	static struct GPIOExport gpios[] = {
		// GPU
		{ .pin = 236, .dir = GPIO_IN, },
		{ .pin = 237, .dir = GPIO_IN, },
		{ .pin = 238, .dir = GPIO_IN, },
		{ .pin = 239, .dir = GPIO_IN, },
		{ .pin = 240, .dir = GPIO_IN, },
		{ .pin = 241, .dir = GPIO_IN, },
		{ .pin = 242, .dir = GPIO_IN, },
		{ .pin = 243, .dir = GPIO_IN, },
		// PCIE present
		{ .pin = 252, .dir = GPIO_IN, },
		{ .pin = 253, .dir = GPIO_IN, },
		{ .pin = 254, .dir = GPIO_IN, },
		{ .pin = 255, .dir = GPIO_IN, },
	};

	int i = 0;
	struct GPIOExport *gpio = NULL;
	char buff[64] = {0};
	struct stat stat = {0};
	FILE *fp = NULL;

	for (i = 0; i < sizeof(gpios) / sizeof(struct GPIOExport); i++) {
		gpio = &gpios[i];
		if (!gpio->exported) {
			/* Test whether GPIO is exported. */

			if (64 <= snprintf(buff, 64, "/sys/class/gpio/gpio%d", gpio->pin)) {
				printf("ERROR not enough space for gpio path\n");
				continue;
			}

			if (!lstat(buff, &stat)) {
				gpio->exported = 1;
				goto config;
			}

			if (errno != ENOENT) {
				printf("ERROR on lstat(%s): %s\n", buff, strerror(errno));
				continue;
			}

			/* Export GPIO. */

			if (!(fp = fopen("/sys/class/gpio/export", "w"))) {
				printf("ERROR on fopen(/sys/class/gpio/export): %s\n",
						strerror(errno));
				continue;
			}

			fprintf(fp, "%d", gpio->pin);

			fclose(fp);
		}

config:
		if (!gpio->configured) {
			/* Test whether GPIO direction conforms with configuration. */

			if (!(fp = fopen("/sys/class/gpio/gpio%d/direction", "r"))) {
				printf("ERROR on fopen(/sys/class/gpio/gpio%d/direction): %s\n",
						strerror(errno));
				continue;
			}

			if (!fgets(buff, 63, fp)) {
				printf("ERROR on fgets(/sys/class/gpio/gpio%d/direction): %s\n",
						strerror(errno));
				continue;
			}

			fclose(fp);

			if (gpio->dir == GPIO_IN && !strncmp("in", buff, 2)) {
				gpio->configured = 1;
				continue;
			} else if (gpio->dir == GPIO_OUT && !strncmp("out", buff, 3)) {
				gpio->configured = 1;
				continue;
			}

			/* Configure GPIO direction. */

			if (!(fp = fopen("/sys/class/gpio/gpio%d/direction", "w"))) {
				printf("ERROR on fopen(/sys/class/gpio/gpio%d/direction): %s\n",
						strerror(errno));
				continue;
			}

			fprintf(fp, (gpio->dir == GPIO_IN) ? "in" : "out");

			fclose(fp);
		}
	}
}

// TODO:  Change to interrupt driven instead of polling
static gboolean
poll_pgood(gpointer user_data)
{
	ControlPower *control_power = object_get_control_power((Object*)user_data);
	Control* control = object_get_control((Object*)user_data);

	//send the heartbeat
	guint poll_int = control_get_poll_interval(control);
	if(poll_int == 0) {
		printf("ERROR PowerControl: Poll interval cannot be 0\n");
		return FALSE;
	}
	//handle timeout
	time_t current_time = time(NULL);
	if(difftime(current_time,pgood_timeout_start) > control_power_get_pgood_timeout(control_power)
	    && pgood_timeout_start != 0) {
		printf("ERROR PowerControl: Pgood poll timeout\n");
		// set timeout to 0 so timeout doesn't happen again
		control_power_set_pgood_timeout(control_power,0);
		pgood_timeout_start = 0;
		return TRUE;
	}
	uint8_t gpio;

	int rc = gpio_open(&pgood);
	rc = gpio_read(&pgood,&gpio);
	gpio_close(&pgood);

	if(rc == GPIO_OK) {
		if (gpio == 1) {
			bind_devices();
			export_gpios();
		}
		//if changed, set property and emit signal
		if(gpio != control_power_get_pgood(control_power)) {
			control_power_set_pgood(control_power,gpio);
			if(gpio==0) {
				control_power_emit_power_lost(control_power);
				control_emit_goto_system_state(control,"HOST_POWERED_OFF");

				rc = gpio_open(&power_led);
				rc = gpio_write(&power_led,POWER_OFF);
				gpio_close(&power_led);
			} else {
				control_power_emit_power_good(control_power);
				control_emit_goto_system_state(control,"HOST_POWERED_ON");

				rc = gpio_open(&power_led);
				rc = gpio_write(&power_led,POWER_ON);
				gpio_close(&power_led);
			}
		}
	} else {
		printf("ERROR PowerControl: GPIO read error (gpio=%s,rc=%d)\n",pgood.name,rc);
		//return false so poll won't get called anymore
		return FALSE;
	}
	//pgood is not at desired state yet
	if(gpio != control_power_get_state(control_power) &&
	    control_power_get_pgood_timeout(control_power) > 0) {
		if(pgood_timeout_start == 0 ) {
			pgood_timeout_start = current_time;
		}
	} else {
		pgood_timeout_start = 0;
	}
	return TRUE;
}

static gboolean
on_set_power_state(ControlPower *pwr,
		   GDBusMethodInvocation *invocation,
		   guint state,
		   gpointer user_data)
{
	Control* control = object_get_control((Object*)user_data);
	if(state > 1) {
		g_dbus_method_invocation_return_dbus_error(invocation,
				"org.openbmc.ControlPower.Error.Failed",
				"Invalid power state");
		return TRUE;
	}
	// return from method call
	control_power_complete_set_power_state(pwr,invocation);
	if(state == control_power_get_state(pwr)) {
		g_print("Power already at requested state: %d\n",state);
	} else {
		int error = 0;
		do {
			if(state == 1) {
				control_emit_goto_system_state(control,"HOST_POWERING_ON");
			} else {
				control_emit_goto_system_state(control,"HOST_POWERING_OFF");
			}
			error = gpio_open(&power_pin);
			if(error != GPIO_OK) {
				break;
			}
			error = gpio_write(&power_pin,!state);
			if(error != GPIO_OK) {
				break;
			}
			gpio_close(&power_pin);
			control_power_set_state(pwr,state);
		} while(0);
		if(error != GPIO_OK) {
			printf("ERROR PowerControl: GPIO set power state (rc=%d)\n",error);
		}
	}
	return TRUE;
}

static gboolean
on_init(Control *control,
	GDBusMethodInvocation *invocation,
	gpointer user_data)
{
	pgood_timeout_start = 0;
	//guint poll_interval = control_get_poll_interval(control);
	//g_timeout_add(poll_interval, poll_pgood, user_data);
	control_complete_init(control,invocation);
	return TRUE;
}

static gboolean
on_get_power_state(ControlPower *pwr,
		   GDBusMethodInvocation *invocation,
		   gpointer user_data)
{
	guint pgood = control_power_get_pgood(pwr);
	control_power_complete_get_power_state(pwr,invocation,pgood);
	return TRUE;
}

static void
on_bus_acquired(GDBusConnection *connection,
		const gchar *name,
		gpointer user_data)
{
	ObjectSkeleton *object;
	cmdline *cmd = user_data;
	if(cmd->argc < 3) {
		g_print("Usage: power_control.exe [poll interval] [timeout]\n");
		return;
	}
	manager = g_dbus_object_manager_server_new(dbus_object_path);
	gchar *s;
	s = g_strdup_printf("%s/%s",dbus_object_path,instance_name);
	object = object_skeleton_new(s);
	g_free(s);

	ControlPower* control_power = control_power_skeleton_new();
	object_skeleton_set_control_power(object, control_power);
	g_object_unref(control_power);

	Control* control = control_skeleton_new();
	object_skeleton_set_control(object, control);
	g_object_unref(control);

	//define method callbacks here
	g_signal_connect(control_power,
			 "handle-set-power-state",
			 G_CALLBACK(on_set_power_state),
			 object); /* user_data */

	g_signal_connect(control_power,
			 "handle-get-power-state",
			 G_CALLBACK(on_get_power_state),
			 NULL); /* user_data */

	g_signal_connect(control,
			 "handle-init",
			 G_CALLBACK(on_init),
			 object); /* user_data */


	/* Export the object (@manager takes its own reference to @object) */
	g_dbus_object_manager_server_set_connection(manager, connection);
	g_dbus_object_manager_server_export(manager, G_DBUS_OBJECT_SKELETON(object));
	g_object_unref(object);

	// get gpio device paths
	int rc = GPIO_OK;
	do {
		rc = gpio_init(connection,&power_pin);
		if(rc != GPIO_OK) {
			break;
		}
		rc = gpio_init(connection,&pgood);
		if(rc != GPIO_OK) {
			break;
		}
		rc = gpio_init(connection,&power_led);
		if(rc != GPIO_OK) {
			break;
		}
		uint8_t gpio;
		rc = gpio_open(&pgood);
		if(rc != GPIO_OK) {
			break;
		}
		rc = gpio_read(&pgood,&gpio);
		if(rc != GPIO_OK) {
			break;
		}
		gpio_close(&pgood);
		control_power_set_pgood(control_power,gpio);
		control_power_set_state(control_power,gpio);
		printf("Pgood state: %d\n",gpio);

	} while(0);
	if(rc != GPIO_OK) {
		printf("ERROR PowerControl: GPIO setup (rc=%d)\n",rc);
	}

	power_led_init();

	//start poll
	pgood_timeout_start = 0;
	int poll_interval = atoi(cmd->argv[1]);
	int pgood_timeout = atoi(cmd->argv[2]);
	if(poll_interval < 1000 || pgood_timeout <5) {
		printf("ERROR PowerControl: poll_interval < 1000 or pgood_timeout < 5\n");
	} else {
		control_set_poll_interval(control,poll_interval);
		control_power_set_pgood_timeout(control_power,pgood_timeout);
		g_timeout_add(poll_interval, poll_pgood, object);
	}
}

static void
on_name_acquired(GDBusConnection *connection,
		 const gchar *name,
		 gpointer user_data)
{
}

static void
on_name_lost(GDBusConnection *connection,
	     const gchar *name,
	     gpointer user_data)
{
}

/*----------------------------------------------------------------*/
/* Main Event Loop                                                */

static void save_pid (void) {
    pid_t pid = 0;
    FILE *pidfile = NULL;
    pid = getpid();
    if (!(pidfile = fopen("/run/power_control_sthelens.pid", "w"))) {
        fprintf(stderr, "failed to open pidfile\n");
        return;
    }
    fprintf(pidfile, "%d\n", pid);
    fclose(pidfile);
}

gint
main(gint argc, gchar *argv[])
{
	GMainLoop *loop;
	cmdline cmd;
	cmd.argc = argc;
	cmd.argv = argv;

	guint id;
    save_pid();
	loop = g_main_loop_new(NULL, FALSE);

	id = g_bus_own_name(DBUS_TYPE,
			    dbus_name,
			    G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
			    G_BUS_NAME_OWNER_FLAGS_REPLACE,
			    on_bus_acquired,
			    on_name_acquired,
			    on_name_lost,
			    &cmd,
			    NULL);

	g_main_loop_run(loop);

	g_bus_unown_name(id);
	g_main_loop_unref(loop);
	return 0;
}
