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

DBUS_NAME = 'org.openbmc.Sensors'
DBUS_INTERFACE = 'org.freedesktop.DBus.Properties'
SENSOR_VALUE_INTERFACE = 'org.openbmc.SensorValue'
HWMON_INTERFACE = 'org.openbmc.HwmonSensor'

_EVENT_MANAGER = None

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
    evd1 = 0
    evd2 = 0
    evd3 = 0
    serverity = Event.SEVERITY_INFO
    b_assert = 0
    event_type = 0
    result = {'logid':0}
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
    return result
