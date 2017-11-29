#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#define LOG_CREATED_MAX_LENGTH 32
#define LOG_ID_MAX_LENGTH 20
#define LOG_MESSAGE_MAX_LENGTH 256
#define LOG_SEVERITY_MAX_LENGTH 16
#define PATH_MAX_LENGTH 64

static const char *BUS_NAME = "org.openbmc.records.events";
static const char *BUS_PATH = "/org/openbmc/records/events";
static const char *BUS_INTERFACE = "org.openbmc.recordlog";
static const char *EVENT_DIR_PATH = "/var/lib/obmc/events";
static const uint8_t EVENT_LOG_SENSOR_NUMBER = 0x80;
static const char *LOCK_PATH = "/var/lib/obmc/events.lock";

static int dbus_open(sd_bus** bus)
{
    int err = 0;
    if ((err = sd_bus_open_system(bus)) < 0) {
        fprintf(stderr, "ERROR: failed to open dbus: %s\n",
                strerror(-err));
        return -1;
    }
    return 0;
}

static void dbus_close(sd_bus* bus)
{
    sd_bus_unref(bus);
}

static int event_clear(sd_bus* bus)
{
    int err = 0;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *res = NULL;
    err = sd_bus_call_method(
            bus,
            BUS_NAME,
            BUS_PATH,
            BUS_INTERFACE,
            "clear",
            &error,
            &res,
            "y",
            EVENT_LOG_SENSOR_NUMBER);
    if (err < 0) {
        fprintf(stderr, "ERROR: failed to clear events\n");
        return -1;
    }
    sd_bus_message_unref(res);
    return 0;
}

static int event_get_record_ids_and_timestamps(
        sd_bus* bus,
        uint16_t** record_ids,
        uint64_t** timestamps,
        size_t* count)
{
    int err = 0;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *res = NULL;
    const uint16_t *res_record_ids = NULL;
    const uint64_t *res_timestamps = NULL;
    size_t res_count = 0;
    err = sd_bus_call_method(
            bus,
            BUS_NAME,
            BUS_PATH,
            BUS_INTERFACE,
            "get_record_ids_and_logical_timestamps",
            &error,
            &res,
            NULL);
    if (err < 0) {
        fprintf(stderr,
                "ERROR: failed to get record ids and timestamps: DBus API\n");
        return -1;
    }
    err = sd_bus_message_read_array(
            res,
            SD_BUS_TYPE_UINT16,
            (const void**) &res_record_ids,
            &res_count);
    if (err < 0) {
        fprintf(stderr,
                "ERROR: failed to get record ids and timestamps: record_ids\n");
        sd_bus_message_unref(res);
        return -2;
    }
    err = sd_bus_message_read_array(
            res,
            SD_BUS_TYPE_UINT64,
            (const void**) &res_timestamps,
            &res_count);
    if (err < 0) {
        fprintf(stderr,
                "ERROR: failed to get record ids and timestamps: timestamps\n");
        sd_bus_message_unref(res);
        return -3;
    }
    res_count /= sizeof(uint64_t);
    if ((*record_ids = calloc(res_count, sizeof(uint16_t))) != NULL) {
        memcpy(*record_ids, res_record_ids, sizeof(uint16_t) * res_count);
    }
    else {
        fprintf(stderr,
                "ERROR: failed to get record_ids and timestamps: oom\n");
        sd_bus_message_unref(res);
        return -4;
    }
    if ((*timestamps = calloc(res_count, sizeof(uint64_t))) != NULL) {
        memcpy(*timestamps, res_timestamps, sizeof(uint64_t) * res_count);
    }
    else {
        fprintf(stderr,
                "ERROR: failed to get record_ids and timestamps: oom\n");
        sd_bus_message_unref(res);
        return -5;
    }
    *count = res_count;
    sd_bus_message_unref(res);
    return 0;
}

static void event_show(uint64_t timestamp)
{
    char path[PATH_MAX_LENGTH] = {0};
    FILE *fp = NULL;
    int lineno = 0;
    char id[LOG_ID_MAX_LENGTH] = {0};
    char severity[LOG_SEVERITY_MAX_LENGTH] = {0};
    char created[LOG_CREATED_MAX_LENGTH] = {0};
    char message[LOG_MESSAGE_MAX_LENGTH] = {0};
    char dummy[256] = {0};
    char *line = NULL;
    size_t len = 0;
    if (PATH_MAX_LENGTH <= snprintf(path, PATH_MAX_LENGTH, "%s/%llu",
                EVENT_DIR_PATH,
                timestamp)) {
        fprintf(stderr,
                "WARN: failed to show %s/%llu: path too long\n",
                EVENT_DIR_PATH,
                timestamp);
        return;
    }
    if ((fp = fopen(path, "r")) == NULL) {
        fprintf(stderr,
                "WARN: failed to show %s/%llu: file not exist\n",
                EVENT_DIR_PATH,
                timestamp);
        return;
    }
    while (!feof(fp)) {
        lineno++;
        switch (lineno) {
            case 1:
                if (fgets(id, LOG_ID_MAX_LENGTH, fp) == NULL) {
                    fprintf(stderr,
                            "WARN: failed to show %s/%llu: id\n",
                            EVENT_DIR_PATH,
                            timestamp);
                    fclose(fp);
                    return;
                }
                line = id;
                break;
            case 4:
                if (fgets(severity, LOG_SEVERITY_MAX_LENGTH, fp) == NULL) {
                    fprintf(stderr,
                            "WARN: failed to show %s/%llu: severity\n",
                            EVENT_DIR_PATH,
                            timestamp);
                    fclose(fp);
                    return;
                }
                line = severity;
                break;
            case 5:
                if (fgets(created, LOG_CREATED_MAX_LENGTH, fp) == NULL) {
                    fprintf(stderr,
                            "WARN: failed to show %s/%llu: created\n",
                            EVENT_DIR_PATH,
                            timestamp);
                    fclose(fp);
                    return;
                }
                line = created;
                break;
            case 10:
                if (fgets(message, LOG_MESSAGE_MAX_LENGTH, fp) == NULL) {
                    fprintf(stderr,
                            "WARN: failed to show %s/%llu: message\n",
                            EVENT_DIR_PATH,
                            timestamp);
                    fclose(fp);
                    return;
                }
                line = message;
                break;
            default:
                fgets(dummy, 256, fp);
                break;
        }
        len = strlen(line);
        if (line[len-1] == '\n') {
            line[len-1] = '\0';
        }
    }
    fclose(fp);
    printf("%04s | %19s | %8s | %s\n", id, created, severity, message);
}

static void event_show_header(void)
{
    printf("%04s | %16s | %8s | %s\n", "ID", "Time", "Severity", "Message");
}

static int lock_acquire(void)
{
    int fd = 0;
    if ((fd = open(LOCK_PATH, O_RDONLY)) == -1) {
        fprintf(stderr, "ERROR: failed to acquire lock: open\n");
        return -1;
    }
    if (flock(fd, LOCK_SH) == -1) {
        fprintf(stderr, "ERROR: failed to acquire lock: flock\n");
        close(fd);
        return -2;
    }
    return fd;
}

static void lock_release(int fd)
{
    close(fd);
}

static void print_usage(void)
{
    printf("Usage:\n");
    printf("\n");
    printf("\teventctl add {severity} {sensor_type} {sensor_number} "
           "{event_dir_type} {event_data_1} [--event_data_2 event_data_2] "
           "[--event_data_3 event_data_3]\n");
    printf("\n");
    printf("\tseverity - one of Critical, OK, Info, Warning\n");
    printf("\tall other fields - in hexadecimal\n");
    printf("\n");
    printf("\teventctl clear\n");
    printf("\n");
    printf("\teventctl list\n");
    printf("\n");
}

static int request_add(int argc, char** argv)
{
    /* Delegate adding event to eventctl.py to re-use its message generation
     * functions.
     */
    char buff[1024] = "";
    char cmd[1024] = "/usr/bin/eventctl.py add";
    int i = 0;
    for (i = 0 ; i < argc ; i++) {
        if (1024 <= snprintf(buff, 1024, "%s %s", cmd, argv[i])) {
            fprintf(stderr, "ERROR: failed to handle request 'add'\n");
            return -1;
        }
        strcpy(cmd, buff);
    }
    return system(cmd);
}

static int request_clear(void)
{
    sd_bus *bus = NULL;
    if (dbus_open(&bus) != 0) {
        fprintf(stderr, "ERROR: failed to handle request 'clear'\n");
        return -1;
    }
    if (event_clear(bus) != 0) {
        fprintf(stderr, "ERROR: failed to handle request 'clear'\n");
        dbus_close(bus);
        return -2;
    }
    dbus_close(bus);
    return 0;
}

static int request_list(void)
{
    int fd = 0;
    sd_bus *bus = NULL;
    int err = 0;
    uint16_t *record_ids = NULL;
    uint64_t *timestamps = NULL;
    size_t count = 0;
    size_t i = 0;
    if ((fd = lock_acquire()) == -1) {
        return -1;
    }
    if (dbus_open(&bus) != 0) {
        fprintf(stderr, "ERROR: failed to handle request 'list'\n");
        lock_release(fd);
        return -2;
    }
    err = event_get_record_ids_and_timestamps(
            bus,
            &record_ids,
            &timestamps,
            &count);
    if (err < 0) {
        fprintf(stderr, "ERROR: failed to handle request 'list'\n");
        dbus_close(bus);
        lock_release(fd);
        return -3;
    }
    event_show_header();
    for (i = 0 ; i < count ; i++) {
        event_show(timestamps[i]);
    }
    free(record_ids);
    free(timestamps);
    dbus_close(bus);
    lock_release(fd);
    return 0;
}

int main(int argc, char** argv)
{
    if (!(2 <= argc)) {
        print_usage();
        return -1;
    }
    if (strcmp(argv[1], "add") == 0) {
        return request_add(argc - 2, argv + 2);
    }
    else if (strcmp(argv[1], "clear") == 0) {
        return request_clear();
    }
    else if (strcmp(argv[1], "list") == 0) {
        return request_list();
    }
    else {
        print_usage();
        return -1;
    }
}
