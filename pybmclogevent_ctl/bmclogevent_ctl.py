#!/usr/bin/python -u

from obmc.events import EventManager, Event
import obmc_system_config as System
import os
import sys
import dbus
import dbus.service
import dbus.mainloop.glib
import obmc.dbuslib.propertycacher as PropertyCacher
from obmc.dbuslib.bindings import get_dbus, DbusProperties, DbusObjectManager
from obmc.sensors import HwmonSensor as HwmonSensor
import time
import subprocess
import json

DBUS_NAME = 'org.openbmc.Sensors'
DBUS_INTERFACE = 'org.freedesktop.DBus.Properties'
SENSOR_VALUE_INTERFACE = 'org.openbmc.SensorValue'
HWMON_INTERFACE = 'org.openbmc.HwmonSensor'
RECORD_CONTROL_LED_FILE = '/run/obmc/record_control_led'

_EVENT_MANAGER = None

#light: 1, light on; 0:light off
def bmchealth_set_status_led(light):
    if 'GPIO_CONFIG' not in dir(System) or 'BLADE_ATT_LED_N' not in System.GPIO_CONFIG:
        return
    try:
        data_reg_addr = System.GPIO_CONFIG["BLADE_ATT_LED_N"]["data_reg_addr"]
        offset = System.GPIO_CONFIG["BLADE_ATT_LED_N"]["offset"]
        inverse = "no"
        if "inverse" in  System.GPIO_CONFIG["BLADE_ATT_LED_N"]:
            inverse = System.GPIO_CONFIG["BLADE_ATT_LED_N"]["inverse"]
        cmd_data = subprocess.check_output("devmem  " + hex(data_reg_addr) , shell=True)
        cmd_data = cmd_data.rstrip("\n")
        cur_data = int(cmd_data, 16)
        if (inverse == "yes"):
            if (light == 1):
                cur_data = cur_data & ~(1<<offset)
            else:
                cur_data = cur_data | (1<<offset)
        else:
            if (light == 1):
                cur_data = cur_data | (1<<offset)
            else:
                cur_data = cur_data & ~(1<<offset)

        set_led_cmd = "devmem  " + hex(data_reg_addr) + " 32 " + hex(cur_data)[:10]
        os.system(set_led_cmd)
    except:
        pass


def bmchealth_push_record_control_led(key_str):
    record_control_led = {}
    try:
        with open(RECORD_CONTROL_LED_FILE, "r") as readfile:
            record_control_led=json.load(readfile)
    except:
        pass
    record_control_led[key_str]  = 0
    with open(RECORD_CONTROL_LED_FILE, 'w') as writefile:
        json.dump(record_control_led, writefile)
    return len(record_control_led)

def bmchealth_pop_record_control_led(key_str):
    record_control_led = {}
    try:
        with open(RECORD_CONTROL_LED_FILE, "r") as readfile:
            record_control_led=json.load(readfile)
    except:
        pass
    if key_str in record_control_led:
        del record_control_led[key_str]
        with open(RECORD_CONTROL_LED_FILE, 'w') as writefile:
            json.dump(record_control_led, writefile)
    return len(record_control_led)

def bmchealth_control_status_led(severity = Event.SEVERITY_CRIT, sensor_number = 0, event_dir = 0, evd1 = 0, evd2 = 0, evd3 = 0):
    if severity !=  Event.SEVERITY_CRIT and event_dir == 0:
        return

    key_str = str(sensor_number)
    key_str += "-" + str(evd1)
    key_str += "-" + str(evd2)
    key_str += "-" + str(evd3)

    len_control_led = 0
    if event_dir == 0:
        len_control_led = bmchealth_push_record_control_led(key_str)
    else:
        len_control_led = bmchealth_pop_record_control_led(key_str)

    if len_control_led > 0:
        bmchealth_set_status_led(1)
    else:
        bmchealth_set_status_led(0)

def _get_event_manager():
    '''
    Defer instantiation of _EVENT_MANAGER to the moment it is really needed.
    '''
    global _EVENT_MANAGER
    if _EVENT_MANAGER is None:
        _EVENT_MANAGER = EventManager()
    return _EVENT_MANAGER

def bmclogevent_get_log_rollover():
    return _get_event_manager().rollover_count()

def bmclogevent_set_property_with_dbus(obj_path, intf, property, val):
    try:
        b_bus = get_dbus()
        b_obj= b_bus.get_object(DBUS_NAME, obj_path)
        b_interface = dbus.Interface(b_obj,  DBUS_INTERFACE)
        b_interface.Set(intf, property, val)
    except:
        print "bmclogevent_set_value_with_dbus Error!!! " + obj_path
        return -1
    return 0

def bmclogevent_get_property_with_dbus(obj_path, intf, property):
    val = 0
    try:
        b_bus = get_dbus()
        b_obj= b_bus.get_object(DBUS_NAME, obj_path)
        b_interface = dbus.Interface(b_obj,  DBUS_INTERFACE)
        val = b_interface.Get(intf, property)
    except:
        print "bmclogevent_get_value_with_dbus Error!!! " + obj_path
        return -1
    return val

def bmclogevent_set_value_with_dbus(obj_path, val):
    return bmclogevent_set_property_with_dbus(obj_path, SENSOR_VALUE_INTERFACE, 'value', val)

def bmclogevent_get_value_with_dbus(obj_path):
    return bmclogevent_get_property_with_dbus(obj_path, SENSOR_VALUE_INTERFACE, 'value')

def bmclogevent_set_value(obj_path, val, mask=0xFFFF, offset=-1):
    retry = 20
    data = bmclogevent_get_value_with_dbus(obj_path)
    while( data == -1):
        if (retry <=0):
            return -1
        data = bmclogevent_get_value_with_dbus(obj_path)
        retry = retry -1
        time.sleep(1)

    if offset != -1:
        offset_mask = (1<<offset)
        mask = mask & offset_mask
        val = val << offset

    data = data & ~(mask)
    data = data | val;
    bmclogevent_set_value_with_dbus(obj_path, data)
    return 0

def BmcLogEventMessages(objpath = "", s_event_identify="", s_assert="", \
                                    s_event_indicator="", s_evd_desc="", data={}):
    evd1 = 0xff
    evd2 = 0xff
    evd3 = 0xff
    serverity = Event.SEVERITY_INFO
    b_assert = 0
    event_type = 0
    result = {'logid':0}
    led_notify = 0
    try:
        if 'BMC_LOGEVENT_CONFIG' not in dir(System) and \
          s_event_identify not in System.BMC_HEALTH_LOGEVENT_CONFIG:
            return result
        event_type = System.BMC_LOGEVENT_CONFIG[s_event_identify]['Event Type']

        if s_assert == "Deasserted":
            b_assert = (1<<7)

        evd_data = System.BMC_LOGEVENT_CONFIG[s_event_identify]['Event Data Table'][s_event_indicator]

        if (s_evd_desc == ""):
            s_evd_desc = s_event_indicator

        if (evd_data['Severity'] == 'Critical'):
            serverity = Event.SEVERITY_CRIT
        elif (evd_data['Severity'] == 'Warning'):
            serverity = Event.SEVERITY_WARN
        elif (evd_data['Severity'] == 'OK'):
            serverity = Event.SEVERITY_OKAY

        if 'led_notify' in evd_data:
            led_notify =  evd_data['led_notify']

        evd_data_info = evd_data['Event Data Information'][s_evd_desc]
        if evd_data_info[0] != None:
            if isinstance(evd_data_info[0], basestring):
                if evd_data_info[0] in data:
                    evd1 =data[evd_data_info[0]]
            else:
                evd1 =evd_data_info[0]
        if evd_data_info[1] != None:
            if isinstance(evd_data_info[1], basestring):
                if evd_data_info[1] in data:
                    evd2 =data[evd_data_info[1]]
            else:
                evd2 =evd_data_info[1]
        if evd_data_info[2] != None:
            if isinstance(evd_data_info[2], basestring):
                if evd_data_info[2] in data:
                    evd3 =data[evd_data_info[2]]
            else:
                evd3 =evd_data_info[2]
    except:
        return result

    bus = get_dbus()
    obj = bus.get_object(DBUS_NAME, objpath, introspect=False)
    intf = dbus.Interface(obj, dbus.PROPERTIES_IFACE)
    sensortype = int(intf.Get(HwmonSensor.IFACE_NAME, 'sensor_type'), 16)
    sensor_number = intf.Get(HwmonSensor.IFACE_NAME, 'sensornumber')
    if isinstance(sensor_number, basestring):
        sensor_number =  int(sensor_number , 16)
    log = Event.from_binary(serverity, sensortype, sensor_number, event_type | b_assert, evd1, evd2, evd3)
    logid=_get_event_manager().create(log)
    print('BmcLogEventMessages added log with record ID 0x%04X' % logid)
    result['logid'] = logid
    if s_event_identify == "BMC Health":
        result['evd1'] = evd1
        result['Severity'] = evd_data['Severity']
    if led_notify == 1 and logid != 0:
        bmchealth_control_status_led(serverity, sensor_number, b_assert, evd1, evd2, evd3)
    return result

if __name__ == '__main__':
    if len(sys.argv) == 3:
        sensor_number = int(sys.argv[1])
        event_dir =  int(sys.argv[2])
        bmchealth_control_status_led(sensor_number = sensor_number, event_dir = event_dir)