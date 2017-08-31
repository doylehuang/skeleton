#!/usr/bin/python -u

import sys
import os
import glib
import glob
import dbus
import dbus.service
import dbus.mainloop.glib
import re
from obmc.dbuslib.bindings import get_dbus
from obmc.events import Event
from obmc.events import EventManager
from obmc.sensors import SensorValue as SensorValue
from obmc.sensors import HwmonSensor as HwmonSensor
from obmc.sensors import SensorThresholds as SensorThresholds
import obmc.sensor_data_record_pool as sdr_tool
import obmc_system_config as System
import bmclogevent_ctl
import traceback
from time import sleep

SENSOR_BUS = 'org.openbmc.Sensors'
# sensors include /org/openbmc/sensors and /org/openbmc/control
SENSORS_OBJPATH = '/org/openbmc'
SENSOR_PATH = '/org/openbmc/sensors'
DIR_POLL_INTERVAL = 30
HWMON_PATH = '/sys/class/hwmon'
KICK_WATCHDOG_INTERVAL = 10
WATCHDOG_FILE_PATH = "/run/obmc/watch_hwmon"

## static define which interface each property is under
## need a better way that is not slow
IFACE_LOOKUP = {
	'units' : SensorValue.IFACE_NAME,
	'scale' : HwmonSensor.IFACE_NAME,
	'offset' : HwmonSensor.IFACE_NAME,
	'critical_upper' : SensorThresholds.IFACE_NAME,
	'warning_upper' : SensorThresholds.IFACE_NAME,
	'critical_lower' : SensorThresholds.IFACE_NAME,
	'warning_lower' : SensorThresholds.IFACE_NAME,
	'emergency_enabled' : SensorThresholds.IFACE_NAME,
	'positive_hysteresis' : SensorThresholds.IFACE_NAME,
	'negative_hysteresis' : SensorThresholds.IFACE_NAME,
	'sensornumber': HwmonSensor.IFACE_NAME,
	'sensor_name': HwmonSensor.IFACE_NAME,
	'sensor_type': HwmonSensor.IFACE_NAME,
	'reading_type': HwmonSensor.IFACE_NAME,
	'min_reading': HwmonSensor.IFACE_NAME,
	'max_reading': HwmonSensor.IFACE_NAME,
	'standby_monitor': HwmonSensor.IFACE_NAME,
	'firmware_update': HwmonSensor.IFACE_NAME,
	'severity_health': HwmonSensor.IFACE_NAME,
	'extra_data': HwmonSensor.IFACE_NAME,
	'ready': HwmonSensor.IFACE_NAME,
}
IFACE_MAPPING = {
	'value': SensorValue.IFACE_NAME,
	'threshold_state': SensorThresholds.IFACE_NAME,
	'thresholds_enabled': SensorThresholds.IFACE_NAME,
	'critical_upper' : SensorThresholds.IFACE_NAME,
	'warning_upper' : SensorThresholds.IFACE_NAME,
	'critical_lower' : SensorThresholds.IFACE_NAME,
	'warning_lower' : SensorThresholds.IFACE_NAME,
	'positive_hysteresis' : SensorThresholds.IFACE_NAME,
	'negative_hysteresis' : SensorThresholds.IFACE_NAME,
	'firmware_update': HwmonSensor.IFACE_NAME,
	'severity_health': HwmonSensor.IFACE_NAME,
}

# The bit is not supported if not mentioned
PMBUS_STATUS_BYTES = {
	0x8000: 0x01,
	0x4000: 0x01,
	0x2000: 0x01,
	0x0800: 0x04,
	0x0400: 0x01,
	0x40: 0x01,
	0x20: 0x01,
	0x10: 0x01,
	0x08: 0x01,
	0x04: 0x01,
	0x02: 0x01
}

class Hwmons():
	def __init__(self,bus):
		self.sensors = { }
		self.hwmon_root = { }
		self.threshold_state = {}
		self.psu_state = {}
		self.throttle_state = {}
		self.session_audit = {}
		self.pgood_obj = bus.get_object('org.openbmc.control.Power', '/org/openbmc/control/power0', introspect=False)
		self.pgood_intf = dbus.Interface(self.pgood_obj,dbus.PROPERTIES_IFACE)
		self.path_mapping = {}
		self.event_manager = EventManager()
		self.check_entity_presence = {}
		self.check_subsystem_health = {}
		self.retry_subsystem_health = {}
		self.check_entity_mapping =  {}
		self.scanDirectory()
		self.pmbus1_hwmon = ""
		self.pmbus2_hwmon = ""
		self.pmbus3_hwmon = ""
		self.pmbus4_hwmon = ""
		self.pmbus5_hwmon = ""
		self.pmbus6_hwmon = ""
		self.record_pgood = 0
		glib.timeout_add_seconds(DIR_POLL_INTERVAL, self.scanDirectory)
		glib.timeout_add_seconds(KICK_WATCHDOG_INTERVAL, self.kickWatchdog)

	def readAttribute(self,filename):
		val = "-1"
		try:
			with open(filename, 'r') as f:
				for line in f:
					val = line.rstrip('\n')
		except (OSError, IOError):
			if filename != "N/A":
				print "Cannot read attributes:", filename
		return val

	def writeAttribute(self,filename,value):
		with open(filename, 'w') as f:
			f.write(str(value)+'\n')

	def check_thresholds(self, threshold_props, value, hwmon):
		sn = hwmon['sensornumber']
		thresholds = ['critical_upper', 'critical_lower', \
						'warning_upper', 'warning_lower']
		if hwmon['thresholds_enabled'] is False:
			return "NORMAL"
		for threshold in thresholds:
			try:
				hwmon[threshold] = threshold_props[threshold+'_'+str(sn)]
			except KeyError:
				pass
		current_state = hwmon['threshold_state']
		if current_state.find("NORMAL") != -1:
			if (hwmon['critical_upper'] != None) and \
					(value >= hwmon['critical_upper']):
				current_state = "UPPER_CRITICAL"
			elif (hwmon['critical_lower'] != None) and \
					(value <= hwmon['critical_lower']):
				current_state = "LOWER_CRITICAL"
			elif (hwmon['warning_upper'] != None) and \
					(value >= hwmon['warning_upper']):
				current_state = "UPPER_WARNING"
			elif (hwmon['warning_lower'] != None) and \
					(value <= hwmon['warning_lower']):
				current_state = "LOWER_WARNING"
		elif current_state.find("UPPER_CRITICAL") >= 0:
			if (hwmon['critical_upper'] != None) and \
					(value <= hwmon['critical_upper'] -
							(hwmon['positive_hysteresis'] + 1)):
				current_state = "NORMAL"
		elif current_state.find("LOWER_CRITICAL") >= 0:
			if (hwmon['critical_lower'] != None) and \
					(value >= hwmon['critical_lower'] +
							(hwmon['negative_hysteresis'] + 1)):
				current_state = "NORMAL"
		elif current_state.find("UPPER_WARNING") >= 0:
			if (hwmon['warning_upper'] != None) and \
					(value <= hwmon['warning_upper'] -
							(hwmon['positive_hysteresis'] + 1)):
				current_state = "NORMAL"
		elif current_state.find("LOWER_WARNING") >= 0:
			if (hwmon['warning_lower'] != None) and \
					(value >= hwmon['warning_lower'] +
							(hwmon['negative_hysteresis'] + 1)):
				current_state = "NORMAL"

		hwmon['threshold_state'] = current_state
		worst = hwmon['worst_threshold_state']
		if (current_state.find("CRITICAL") != -1 or
				(current_state.find("WARNING") != -1 and worst.find("CRITICAL") == -1)):
			hwmon['worst_threshold_state'] = current_state

		return current_state

	def entity_presence_check(self,objpath,hwmon,raw_value):
		entity_presence_obj_path = "/org/openbmc/sensors/entity_presence"
		if objpath not in self.check_entity_presence:
			self.check_entity_presence[objpath] = 1
		if hwmon.has_key('entity'):
			if objpath not in self.check_entity_mapping:
				self.check_entity_mapping[objpath] = 0
			if raw_value == hwmon['inverse'] and self.check_entity_presence[objpath] == 1:
				bmclogevent_ctl.BmcLogEventMessages(entity_presence_obj_path, \
						"Entity Presence" ,"Asserted", "Entity Presence" , \
						data={'entity_device':hwmon['entity'], 'entity_index':hwmon['index']})
				bmclogevent_ctl.bmclogevent_set_value(entity_presence_obj_path ,1)
				self.check_entity_presence[objpath] = 0
				self.check_entity_mapping[objpath] = 1
			elif raw_value != hwmon['inverse']:
				if self.check_entity_presence[objpath] == 0:
					bmclogevent_ctl.BmcLogEventMessages(entity_presence_obj_path, \
						"Entity Presence" ,"Deasserted", "Entity Presence" , \
						data={'entity_device':hwmon['entity'], 'entity_index':hwmon['index']})
					bmclogevent_ctl.bmclogevent_set_value(entity_presence_obj_path, 0)
					self.check_entity_mapping[objpath] = 0
				self.check_entity_presence[objpath] = 1
		return True

	def subsystem_health_check(self,hwmon,raw_value):
		check_subsystem_health_obj_path = "/org/openbmc/sensors/management_subsystem_health"
		if hwmon.has_key('mapping'):
			if self.check_entity_mapping[hwmon['mapping']] == 1:
				return True
		if hwmon.has_key('ready') and hwmon['ready'] == 0:
			plx_obj = bus.get_object(SENSOR_BUS,"/org/openbmc/sensors/pex/pex",introspect=False)
			gpu_obj = bus.get_object(SENSOR_BUS,"/org/openbmc/sensors/gpu/gpu_temp",introspect=False)
			plx_intf = dbus.Interface(plx_obj,dbus.PROPERTIES_IFACE)
			gpu_intf = dbus.Interface(gpu_obj,dbus.PROPERTIES_IFACE)

			plx_ready = 0
			gpu_ready = 0
			try:
				plx_ready = plx_intf.Get(HwmonSensor.IFACE_NAME,'ready')
				gpu_ready = gpu_intf.Get(HwmonSensor.IFACE_NAME,'ready')
			except:
				pass
			if hwmon.has_key('mapping') and gpu_ready == 1:
				hwmon['ready'] = 1
			elif plx_ready == 1:
				hwmon['ready'] = 1
			else:
				return True
		if hwmon.has_key('sensornumber'):
			if hwmon['sensornumber'] >= 0xA1 and hwmon['sensornumber']<= 0xA8:
				return True
			if hwmon['sensornumber'] not in self.check_subsystem_health:
				self.check_subsystem_health[hwmon['sensornumber']] = 1
			if raw_value == -1 and self.check_subsystem_health[hwmon['sensornumber']] == 1:
				bmclogevent_ctl.BmcLogEventMessages(check_subsystem_health_obj_path, \
						"Management Subsystem Health" ,"Asserted", "Management Subsystem Health" , \
						data={'event_status':0xC4, 'sensor_number':hwmon['sensornumber']})
				bmclogevent_ctl.bmclogevent_set_value(check_subsystem_health_obj_path , 1)
				self.check_subsystem_health[hwmon['sensornumber']] = 0
			elif raw_value >= 0:
				if self.check_subsystem_health[hwmon['sensornumber']] == 0:
					bmclogevent_ctl.BmcLogEventMessages(check_subsystem_health_obj_path, \
					"Management Subsystem Health" ,"Deasserted", "Management Subsystem Health", \
						data={'event_status':0xC4, 'sensor_number':hwmon['sensornumber']})
					bmclogevent_ctl.bmclogevent_set_value(check_subsystem_health_obj_path, 0)
				self.check_subsystem_health[hwmon['sensornumber']] = 1
		return True

	def check_throttle_state(self, objpath, attribute, hwmon):
		obj = bus.get_object(SENSOR_BUS,objpath,introspect=False)
		intf = dbus.Interface(obj,dbus.PROPERTIES_IFACE)
		try:
			power_consum = 0
			power_consum += int(self.readAttribute(self.pmbus1_hwmon), 10)
			power_consum += int(self.readAttribute(self.pmbus2_hwmon), 10)
			power_consum += int(self.readAttribute(self.pmbus3_hwmon), 10)
			power_consum += int(self.readAttribute(self.pmbus4_hwmon), 10)
			power_consum += int(self.readAttribute(self.pmbus5_hwmon), 10)
			power_consum += int(self.readAttribute(self.pmbus6_hwmon), 10)
			power_consum = power_consum / 1000000
			power_LSB = hex(power_consum & 0xFF)
			power_MSB = hex(power_consum >> 8)
			raw_value = int(self.readAttribute(attribute), 16)
			extra_refer_gpio = 1
			if 'extra_data' in hwmon:
				extra_refer_gpio = int(self.readAttribute(hwmon['extra_data']), 16)
			if (extra_refer_gpio >=0):
				raw_value &= extra_refer_gpio
			if raw_value == 0 and self.throttle_state[objpath] == 0:
				event_dir = 0
				event_type = 0x3
				sensor_type = int(hwmon['sensor_type'], 0)
				sensor_number = hwmon['sensornumber']
				evd1 = 0x1
				evd2 = power_MSB
				evd3 = power_LSB
				severity = Event.SEVERITY_CRIT
				if int(power_MSB, 0) >= 0 and int(power_MSB, 0) <= 255 \
						and int(power_LSB, 0) >=0 and int(power_LSB, 0) <= 255:
							log = Event.from_binary(severity, sensor_type, sensor_number, \
									event_dir | event_type, evd1, int(evd2, 16), int(evd3, 16))
							self.event_manager.create(log)
							self.throttle_state[objpath] = 1
							intf.Set(SensorValue.IFACE_NAME, 'value', 1) #set value = 1 to notify system throttle is 'critical'
			elif raw_value == 1 and self.throttle_state[objpath] == 1:
				event_dir = 0
				event_type = 0x3
				sensor_type = int(hwmon['sensor_type'], 0)
				sensor_number = hwmon['sensornumber']
				evd1 = 0x0
				evd2 = power_MSB
				evd3 = power_LSB
				severity = Event.SEVERITY_OKAY
				if int(power_MSB, 0) >= 0 and int(power_MSB, 0) <= 255 \
						and int(power_LSB, 0) >=0 and int(power_LSB, 0) <= 255:
							log = Event.from_binary(severity, sensor_type, sensor_number, \
									event_dir | event_type, evd1, int(evd2, 16), int(evd3, 16))
							self.event_manager.create(log)
							self.throttle_state[objpath] = 0
							intf.Set(SensorValue.IFACE_NAME, 'value', 0) #set value = 0 to notify system throttle is 'ok'

		except Exception as e:
			print str(e)
		sleep(0.4)
		return True

	def sesson_audit_check(self, objpath, attribute, hwmon):
		try:
			sensor_type = int(hwmon['sensor_type'], 0)
			sensor_number = hwmon['sensornumber']
			severity = Event.SEVERITY_CRIT

			patch = '/var/log/auth.log'
			file = open(patch, 'r')

			for line in file:
				idPosition = -1
				fileString = line
				idFilter = 'Accepted password for admin'
				idPosition = fileString.find(idFilter)
				if idPosition != -1:
					event_dir = 0
					event_type = 0x6f
					evd1 = 0x0
					evd2 = 0x1
					evd3 = 0x2
					log = Event.from_binary(severity, sensor_type, sensor_number, \
								event_dir | event_type, evd1, evd2, evd3)
					self.event_manager.create(log)

				idFilter = 'Failed password for admin'
				idPosition = fileString.find(idFilter)
				if idPosition != -1:
					event_dir = 0
					event_type = 0x6f
					evd1 = 0x2
					evd2 = 0x1
					evd3 = 0x2
					log = Event.from_binary(severity, sensor_type, sensor_number, \
								event_dir | event_type, evd1, evd2, evd3)
					self.event_manager.create(log)

				idFilter = 'Invalid user'
				idPosition = fileString.find(idFilter)
				if idPosition != -1:
					event_dir = 0
					event_type = 0x6f
					evd1 = 0x2
					evd2 = 0x1
					evd3 = 0x2
					log = Event.from_binary(severity, sensor_type, sensor_number, \
								event_dir | event_type, evd1, evd2, evd3)
					self.event_manager.create(log)

			file.close()
			os.remove(patch)

		except Exception as e:
			print str(e)
		sleep(0.4)
		return True

	def check_pmbus_state(self, objpath, hwmons):
		obj = bus.get_object(SENSOR_BUS,objpath,introspect=False)
		intf = dbus.Interface(obj,dbus.PROPERTIES_IFACE)
		for hwmon in hwmons:
			try:
				if 'bus_number' in hwmon:
					if hwmon['bus_number'] in self.path_mapping:
						hwmon_path = self.path_mapping[hwmon['bus_number']] + hwmon['device_node']
					else:
						hwmon_path = None
				attribute = hwmon_path
				evd1 = 0xA0
				if 'firmware_update' in hwmon:
					firmware_update_status = intf.Get(HwmonSensor.IFACE_NAME,'firmware_update')
					if (firmware_update_status & (1 << (hwmon['index'] - 1))) > 0:
						return True
				if attribute:
					raw_value = int(self.readAttribute(attribute), 16)
				else:
					raw_value = -1
				if raw_value < -1 or raw_value > 0xFFFF:
					continue
				if raw_value == -1:
					hwmon['reading_error_count']+=1
					if hwmon['reading_error_count'] < 3:
						continue
				hwmon['reading_error_count'] = 0
				self.entity_presence_check(objpath,hwmon,raw_value)
				self.subsystem_health_check(hwmon,raw_value)
				intf.Set(SensorValue.IFACE_NAME, 'value_'+str(hwmon['sensornumber']), raw_value)
				if raw_value == -1:
					continue

				severity = Event.SEVERITY_OKAY
				event_dir = 0x0
				assertion_failure = False
				deassertion_failure = False
				assertion_power_lost = False
				deassertion_power_lost = False
				if raw_value != self.psu_state[hwmon['sensornumber']]:
					hwmon['status_change_count']+=1
					if hwmon['status_change_count'] <= 3:
						continue
				hwmon['status_change_count'] = 0

				for bitmap, event_type in PMBUS_STATUS_BYTES.iteritems():
					if bitmap & (raw_value) & ((self.psu_state[hwmon['sensornumber']]) ^ 0xFFFF):
						if event_type == 0x04:
							assertion_power_lost = True
						else:
							assertion_failure = True
					elif (not(bitmap & (raw_value)) and (bitmap&self.psu_state[hwmon['sensornumber']])):
						if event_type == 0x04:
							deassertion_power_lost = True
						else:
							deassertion_failure = True
				if assertion_failure:
					event_dir = 0x0
					severity = Event.SEVERITY_CRIT
					self.LogThresholdEventMessages(hwmon, severity, event_dir, evd1|0x01, raw_value>>8, raw_value&0xF)
				if assertion_power_lost:
					event_dir = 0x0
					severity = Event.SEVERITY_CRIT
					self.LogThresholdEventMessages(hwmon, severity, event_dir, evd1|0x04, raw_value>>8, raw_value&0xF)
				if deassertion_failure:
					severity = Event.SEVERITY_OKAY
					event_dir = 0x80
					self.LogThresholdEventMessages(hwmon, severity, event_dir, evd1|0x01, raw_value>>8, raw_value&0xF)
				if deassertion_power_lost:
					severity = Event.SEVERITY_OKAY
					event_dir = 0x80
					self.LogThresholdEventMessages(hwmon, severity, event_dir, evd1|0x04, raw_value>>8, raw_value&0xF)
				self.psu_state[hwmon['sensornumber']] = raw_value

			except Exception as e:
				print str(e)
		sleep(0.4)
		return True

	def check_system_event(self, current_pgood):
		try:
			system_event_objpath = "/org/openbmc/sensors/system_event"
			obj = bus.get_object(SENSOR_BUS,system_event_objpath,introspect=False)
			intf = dbus.Interface(obj,dbus.PROPERTIES_IFACE)
			if self.record_pgood != current_pgood:
				result = {'logid':0}
				if current_pgood == 1: #current poweroff->poweron condition
					intf.Set(HwmonSensor.IFACE_NAME, 'severity_health', 'OK')
					result = bmclogevent_ctl.BmcLogEventMessages(system_event_objpath, "System Event", \
						"Asserted",  "System Event PowerOn", "System Event PowerOn")
				elif current_pgood == 0: #current poweron->poweroff condition
					intf.Set(HwmonSensor.IFACE_NAME, 'severity_health', 'Critical')
					result = bmclogevent_ctl.BmcLogEventMessages(system_event_objpath, "System Event", \
						"Asserted",  "System Event PowerOff", "System Event PowerOff")

				if (result['logid'] !=0):
					self.record_pgood = current_pgood
		except:
			pass

	def sensor_polling(self, objpath, hwmons):
		obj = bus.get_object(SENSOR_BUS,objpath,introspect=False)
		intf = dbus.Interface(obj, dbus.PROPERTIES_IFACE)
		threshold_props = intf.GetAll(SensorThresholds.IFACE_NAME)
		for hwmon in hwmons:
			try:
				standby_monitor = True
				if hwmon.has_key('standby_monitor'):
					standby_monitor = hwmon['standby_monitor']
				# Skip monitor while DC power off if stand by monitor is False
				current_pgood = self.pgood_intf.Get('org.openbmc.control.Power', 'pgood')
				self.check_system_event(current_pgood)
				if not standby_monitor:
					if  current_pgood == 0:
						intf.Set(SensorValue.IFACE_NAME, 'value_'+str(hwmon['sensornumber']), -1)
						continue

				if 'firmware_update' in hwmon:
					firmware_update_status = intf.Get(HwmonSensor.IFACE_NAME,'firmware_update')
					if (firmware_update_status & (1 << (hwmon['index'] - 1))) > 0:
						continue

				# skip get sensor readings while dc on/off in progress
				dc_on_off = self.pgood_intf.Get('org.openbmc.control.Power', 'dc_on_off')
				if dc_on_off == 1:
					continue
				READING_VALUE = 'reading_value_'+str(hwmon['sensornumber'])
				scale = hwmon['scale']
				hwmon_path = None
				if 'bus_number' in hwmon:
					if hwmon['bus_number'] in self.path_mapping:
						hwmon_path = self.path_mapping[hwmon['bus_number']] + hwmon['device_node']
				if READING_VALUE in threshold_props and \
						threshold_props[READING_VALUE] != -1:
					raw_value = threshold_props[READING_VALUE]
					scale = 1
				else:
					if hwmon_path:
						raw_value = int(self.readAttribute(hwmon_path))
					else:
						raw_value = int(self.readAttribute(hwmon['device_node']))

				if raw_value == -1:
					hwmon['reading_error_count']+=1
					if hwmon['reading_error_count'] < 3:
						continue
					intf.Set(SensorValue.IFACE_NAME, 'value_'+str(hwmon['sensornumber']), -1)
				else:
					intf.Set(SensorValue.IFACE_NAME, 'value_'+str(hwmon['sensornumber']), raw_value / scale)
				hwmon['reading_error_count'] = 0


				if hwmon['sensornumber'] != '':
					self.entity_presence_check(objpath,hwmon,raw_value)
					self.subsystem_health_check(hwmon,raw_value)

				# do not check threshold while not reading
				if raw_value == -1:
					continue
				reading_value = raw_value / scale
				origin_threshold_state = hwmon['threshold_state']
				status_change = origin_threshold_state != self.check_thresholds(threshold_props, reading_value, hwmon)
				if status_change:
					hwmon['status_change_count']+=1
					if hwmon['status_change_count'] < 3:
						hwmon['threshold_state'] = origin_threshold_state
						continue
					hwmon['status_change_count'] = 0
					intf.Set(SensorThresholds.IFACE_NAME, \
							'threshold_state_'+str(hwmon['sensornumber']), hwmon['threshold_state'])
					severity = Event.SEVERITY_INFO
					event_type_code = 0x0
					dc_on_off = self.pgood_intf.Get('org.openbmc.control.Power', 'dc_on_off')
					if dc_on_off == 1:
						hwmon['threshold_state'] = 'NORMAL'
						continue

					if hwmon['threshold_state'].find("CRITICAL") != -1 or origin_threshold_state.find("CRITICAL") != -1:
						severity = Event.SEVERITY_CRIT
						if hwmon['threshold_state'].find("LOWER") != -1 or origin_threshold_state.find("LOWER") != -1:
							event_type_code = 0x02
							evd3 = hwmon['critical_lower']
						else:
							event_type_code = 0x09
							evd3 = hwmon['critical_upper']
					elif hwmon['threshold_state'].find("WARNING") != -1 or origin_threshold_state.find("WARNING") != -1:
						severity = Event.SEVERITY_WARN
						if hwmon['threshold_state'].find("LOWER") != -1 or origin_threshold_state.find("LOWER") != -1:
							event_type_code = 0x0
							evd3 = hwmon['warning_lower']
						else:
							event_type_code = 0x07
							evd3 = hwmon['warning_upper']
					# [7:6] Trigger reading, [5:4] trigger threshold value, [3:0] Event/Reading code
					evd1 = (0b0101 << 4)  | event_type_code
					evd2 = reading_value

					sdr = sdr_tool.SDRS.get_by_sensor_number(hwmon['sensornumber'])
					evd2 = sdr.compress_raw_reading(evd2)
					evd3 = sdr.compress_raw_reading(evd3)
					if hwmon['threshold_state'] == 'NORMAL':
						severity = Event.SEVERITY_OKAY
						event_dir = 0x80
					else:
						event_dir = 0x0
					self.LogThresholdEventMessages(hwmon, severity, event_dir,
											evd1, evd2, evd3)
				else:
					hwmon['status_change_count'] = 0

			except:
				traceback.print_exc()
				print "HWMON: Attibute no longer exists: "+hwmon['object_path']
		sleep(0.4)
		return True

	def poll(self,objpath,attribute,hwmon):
		try:
			standby_monitor = True
			if hwmon.has_key('standby_monitor'):
				standby_monitor = hwmon['standby_monitor']
			# Skip monitor while DC power off if stand by monitor is False
			obj = bus.get_object(SENSOR_BUS,objpath,introspect=False)
			intf_p = dbus.Interface(obj, dbus.PROPERTIES_IFACE)
			intf = dbus.Interface(obj,HwmonSensor.IFACE_NAME)
			current_pgood = self.pgood_intf.Get('org.openbmc.control.Power', 'pgood')
			self.check_system_event(current_pgood)
			if not standby_monitor:
				if  current_pgood == 0:
					rtn = intf.setByPoll(-1)
					if (rtn[0] == True):
						self.writeAttribute(attribute,rtn[1])
					return True

			if 'firmware_update' in hwmon:
				if intf_p.Get(HwmonSensor.IFACE_NAME,'firmware_update') == 1:
					return True

			# skip get sensor readings while dc on/off in progress
			dc_on_off = self.pgood_intf.Get('org.openbmc.control.Power', 'dc_on_off')
			if dc_on_off == 1:
				return True
			raw_value = int(self.readAttribute(attribute))
			rtn = intf.setByPoll(raw_value)
			if (rtn[0] == True):
				self.writeAttribute(attribute,rtn[1])

			self.entity_presence_check(objpath,hwmon,raw_value)
			self.subsystem_health_check(hwmon,raw_value)

			# do not check threshold while not reading
			if raw_value == -1:
				return True
			threshold_state = intf_p.Get(SensorThresholds.IFACE_NAME, 'threshold_state')
			if threshold_state != self.threshold_state[objpath]:
				severity = Event.SEVERITY_INFO
				event_type_code = 0x0
				origin_threshold_type = self.threshold_state[objpath]
				dc_on_off = self.pgood_intf.Get('org.openbmc.control.Power', 'dc_on_off')
				if dc_on_off == 1:
					intf_p.Set(SensorThresholds.IFACE_NAME, 'threshold_state', 'NORMAL')
					return True
				self.threshold_state[objpath]  = threshold_state

				if threshold_state.find("CRITICAL") != -1 or origin_threshold_type.find("CRITICAL") != -1:
					severity = Event.SEVERITY_CRIT
					if threshold_state.find("LOWER") != -1 or origin_threshold_type.find("LOWER") != -1:
						event_type_code = 0x02
						evd3 = intf_p.Get(SensorThresholds.IFACE_NAME,'critical_lower')
					else:
						event_type_code = 0x09
						evd3 = intf_p.Get(SensorThresholds.IFACE_NAME,'critical_upper')
				elif threshold_state.find("WARNING") != -1 or origin_threshold_type.find("WARNING") != -1:
					severity = Event.SEVERITY_WARN
					if threshold_state.find("LOWER") != -1 or origin_threshold_type.find("LOWER") != -1:
						event_type_code = 0x0
						evd3 = intf_p.Get(SensorThresholds.IFACE_NAME,'warning_lower')
					else:
						event_type_code = 0x07
						evd3 = intf_p.Get(SensorThresholds.IFACE_NAME,'warning_upper')
				# [7:6] Trigger reading, [5:4] trigger threshold value, [3:0] Event/Reading code
				scale = intf_p.Get(HwmonSensor.IFACE_NAME,'scale')
				evd1 = (0b0101 << 4)  | event_type_code
				evd2 = raw_value / scale

				sdr = sdr_tool.SDRS.get_by_sensor_number(hwmon['sensornumber'])
				evd2 = sdr.compress_raw_reading(evd2)
				evd3 = sdr.compress_raw_reading(evd3)
				if threshold_state == 'NORMAL':
					severity = Event.SEVERITY_OKAY
					event_dir = 0x80
				else:
					event_dir = 0x0
				self.LogThresholdEventMessages(hwmon, severity, event_dir,
										evd1, evd2, evd3)
		except:
			traceback.print_exc()
			print "HWMON: Attibute no longer exists: "+attribute
			self.sensors.pop(objpath,None)
			return False
		sleep(0.4)
		return True

	def LogThresholdEventMessages(self, hwmon, severity, event_dir, evd1, evd2=0xFF, evd3=0xFF):

		sensortype = int(hwmon['sensor_type'], 0)
		sensor_number = hwmon['sensornumber']
		sensor_name = hwmon['sensor_name']
		event_type = hwmon['reading_type']

		# Add event log
		log = Event.from_binary(severity, sensortype, sensor_number, event_dir | event_type, evd1, evd2, evd3)
		self.event_manager.create(log)

		return True

	def addObject(self,dpath,hwmon_path,hwmon):
		objsuf = hwmon['object_path']
		objpath = SENSORS_OBJPATH+'/'+objsuf

		if (self.sensors.has_key(objpath) == False):
			print "HWMON add: "+objpath+" : "+hwmon_path

			## register object with sensor manager
			obj = bus.get_object(SENSOR_BUS,SENSOR_PATH,introspect=False)
			intf = dbus.Interface(obj,SENSOR_BUS)
			intf.register("HwmonSensor",objpath)

			## set some properties in dbus object
			obj = bus.get_object(SENSOR_BUS,objpath,introspect=False)
			intf = dbus.Interface(obj,dbus.PROPERTIES_IFACE)
			intf.Set(HwmonSensor.IFACE_NAME,'filename',hwmon_path)

			## check if one of thresholds is defined to know
			## whether to enable thresholds or not
			if (hwmon.has_key('critical_upper') or hwmon.has_key('critical_lower')):
				intf.Set(SensorThresholds.IFACE_NAME,'thresholds_enabled',True)

			for prop in hwmon.keys():
				if (IFACE_LOOKUP.has_key(prop)):
					intf.Set(IFACE_LOOKUP[prop],prop,hwmon[prop])
					print "Setting: "+prop+" = "+str(hwmon[prop])

			self.sensors[objpath]=True
			self.hwmon_root[dpath].append(objpath)
			self.threshold_state[objpath] = "NORMAL"

			glib.timeout_add_seconds(hwmon['poll_interval']/1000,self.poll,objpath,hwmon_path,hwmon)

	def addSensorMonitorObject(self):
		if "SENSOR_MONITOR_CONFIG" not in dir(System):
			return

		for i in range(len(System.SENSOR_MONITOR_CONFIG)):
			objpath = System.SENSOR_MONITOR_CONFIG[i][0]
			hwmon = System.SENSOR_MONITOR_CONFIG[i][1]

			if 'device_node' not in hwmon:
				print "Warnning[addSensorMonitorObject]: Not correct set [device_node]"
				continue

			if 'bus_number' in hwmon:
				if hwmon['bus_number'] in self.path_mapping:
					hwmon_path = self.path_mapping[hwmon['bus_number']] + hwmon['device_node']
				else:
					hwmon_path = 'N/A'
			else:
				hwmon_path = hwmon['device_node']
			if (self.sensors.has_key(objpath) == False):
				## register object with sensor manager
				obj = bus.get_object(SENSOR_BUS,SENSOR_PATH,introspect=False)
				intf = dbus.Interface(obj,SENSOR_BUS)
				intf.register("HwmonSensor",objpath)

				## set some properties in dbus object
				obj = bus.get_object(SENSOR_BUS,objpath,introspect=False)
				intf = dbus.Interface(obj,dbus.PROPERTIES_IFACE)
				intf.Set(HwmonSensor.IFACE_NAME,'filename',hwmon_path)
				# init value as
				val = -1
				if hwmon.has_key('value'):
					val = hwmon['value']
					intf_h = dbus.Interface(obj,HwmonSensor.IFACE_NAME)
					intf_h.setByPoll(val)

				## check if one of thresholds is defined to know
				## whether to enable thresholds or not
				if (hwmon.has_key('critical_upper') or hwmon.has_key('critical_lower')):
					intf.Set(SensorThresholds.IFACE_NAME,'thresholds_enabled',True)

				for prop in hwmon.keys():
					if (IFACE_LOOKUP.has_key(prop)):
						intf.Set(IFACE_LOOKUP[prop],prop,hwmon[prop])

				self.sensors[objpath]=True
				self.threshold_state[objpath] = "NORMAL"
				if 'sensornumber' in hwmon and hwmon['sensornumber'] >= 0x83 and hwmon['sensornumber'] <= 0x88:
					self.psu_state[objpath] = 0x0
					glib.timeout_add_seconds(hwmon['poll_interval']/1000,self.check_pmbus_state,objpath, hwmon_path, hwmon)
				elif 'sensornumber' in hwmon and hwmon['sensornumber'] == 0x81:
					self.check_ntp_init_status(hwmon)
				elif 'sensornumber' in hwmon and hwmon['sensornumber'] == 0x8B:
					self.throttle_state[objpath] = 0
					glib.timeout_add_seconds(hwmon['poll_interval']/1000,self.check_throttle_state,objpath, hwmon_path, hwmon)
				elif 'sensornumber' in hwmon and hwmon['sensornumber'] == 0x8C:
					glib.timeout_add_seconds(hwmon['poll_interval']/1000,self.sesson_audit_check,objpath, hwmon_path, hwmon)
				else:
					if hwmon.has_key('poll_interval'):
						glib.timeout_add_seconds(hwmon['poll_interval']/1000,self.poll,objpath,hwmon_path,hwmon)

	def addSensorMonitor(self):
		if "HWMON_SENSOR_CONFIG" not in dir(System):
			return

		for objpath, hwmons in System.HWMON_SENSOR_CONFIG.iteritems():

			last_sensor_number = None
			if (self.sensors.has_key(objpath) == False):
				for hwmon in hwmons:
					if 'device_node' not in hwmon:
						print "Warnning[addSensorMonitorObject]: Not correct set [device_node]"
						continue

					if (self.sensors.has_key(objpath) == False):
						## register object with sensor manager
						sensor_obj = bus.get_object(SENSOR_BUS,SENSOR_PATH,introspect=False)
						sensor_intf = dbus.Interface(sensor_obj,SENSOR_BUS)
						sensor_intf.register("HwmonSensor",objpath)

					## check if one of thresholds is defined to know
					## whether to enable thresholds or not
					hwmon['thresholds_enabled'] = False
					if (hwmon.has_key('critical_upper') or hwmon.has_key('critical_lower') or \
						hwmon.has_key('warning_upper') or hwmon.has_key('warning_lower')):
						hwmon['thresholds_enabled'] = True

					obj = bus.get_object(SENSOR_BUS,objpath,introspect=False)
					intf = dbus.Interface(obj,dbus.PROPERTIES_IFACE)
					if (self.sensors.has_key(hwmon['sensornumber']) == False):
						for prop in hwmon.keys():
							if (IFACE_MAPPING.has_key(prop)):
								if prop == 'firmware_update':
									intf.Set(IFACE_MAPPING[prop],prop,hwmon[prop])
								else:
									intf.Set(IFACE_MAPPING[prop],prop+'_'+str(hwmon['sensornumber']),hwmon[prop])
						self.sensors[hwmon['sensornumber']]=True
					# init threshold state
					intf.Set(IFACE_MAPPING['threshold_state'],'threshold_state_'+str(hwmon['sensornumber']),"NORMAL")

					if 'critical_upper' not in hwmon:
						hwmon['critical_upper'] = None
					if 'critical_lower' not in hwmon:
						hwmon['critical_lower'] = None
					if 'warning_upper' not in hwmon:
						hwmon['warning_upper'] = None
					if 'warning_lower' not in hwmon:
						hwmon['warning_lower'] = None
					if 'positive_hysteresis' not in hwmon:
						hwmon['positive_hysteresis'] = -1
					if 'negative_hysteresis' not in hwmon:
						hwmon['negative_hysteresis'] = -1
					hwmon['worst_threshold_state'] = "NORMAL"
					hwmon['threshold_state'] = "NORMAL"
					last_sensor_number = hwmon['sensornumber']
					if hwmon['sensornumber'] >= 0x83 and hwmon['sensornumber'] <= 0x88:
						self.psu_state[hwmon['sensornumber']] = 0x0

				if last_sensor_number >= 0x83 and last_sensor_number <= 0x88:
					glib.timeout_add_seconds(hwmon['poll_interval']/1000,self.check_pmbus_state,objpath, hwmons)
				elif last_sensor_number == 0x81:
					self.check_ntp_init_status(hwmon)
				else:
					if hwmon.has_key('poll_interval'):
						glib.timeout_add_seconds(hwmon['poll_interval']/1000,self.sensor_polling,objpath, hwmons)
				self.sensors[objpath]=True

	def check_ntp_init_status(self, hwmon):
		status = None
		ntp_init_status = '/var/tmp/ntp_init_status'
		try:
			with open(ntp_init_status, 'r') as f:
				status = int(f.readline())
		except IOError as e:
			print e
			return
		except ValueError as e:
			print e
			return

		event_dir = 0
		event_type = 0x71
		sensor_type = int(hwmon['sensor_type'], 0)
		sensor_number = hwmon['sensornumber']
		if status == 0:
			evd1 = 0x01
			severity = Event.SEVERITY_INFO
		else:
			evd1 = 0x03
			severity = Event.SEVERITY_CRIT
		log = Event.from_binary(severity, sensor_type, sensor_number, \
				event_dir | event_type, evd1)
		self.event_manager.create(log)

		os.remove(ntp_init_status)

	def checkPmbusHwmon(self, instance_name, dpath):
		if instance_name == "8-0058":
			self.pmbus1_hwmon = dpath+"power2_input"
		elif instance_name == "9-0058":
			self.pmbus2_hwmon = dpath+"power2_input"
		elif instance_name == "10-0058":
			self.pmbus3_hwmon = dpath+"power2_input"
		elif instance_name == "11-0058":
			self.pmbus4_hwmon = dpath+"power2_input"
		elif instance_name == "12-0058":
			self.pmbus5_hwmon = dpath+"power2_input"
		elif instance_name == "13-0058":
			self.pmbus6_hwmon = dpath+"power2_input"

	def scanDirectory(self):
		check_subsystem_health_obj_path = "/org/openbmc/sensors/management_subsystem_health"
	 	devices = os.listdir(HWMON_PATH)
		found_hwmon = {}
		regx = re.compile('([a-z]+)\d+\_')
		self.path_mapping = {}
		obj_mapping = []
		self.addSensorMonitorObject()
		for d in devices:
			dpath = HWMON_PATH+'/'+d+'/'
			found_hwmon[dpath] = True
			if (self.hwmon_root.has_key(dpath) == False):
				self.hwmon_root[dpath] = []
			## the instance name is a soft link
			instance_name = os.path.realpath(dpath+'device').split('/').pop()
			self.checkPmbusHwmon(instance_name, dpath)
			self.path_mapping[instance_name] = dpath
			if (System.HWMON_CONFIG.has_key(instance_name)):
				hwmon = System.HWMON_CONFIG[instance_name]
				if (hwmon.has_key('labels')):
					label_files = glob.glob(dpath+'/*_label')
					for f in label_files:
						label_key = self.readAttribute(f)
						if (hwmon['labels'].has_key(label_key)):
							namef = f.replace('_label','_input')
							self.addObject(dpath,namef,hwmon['labels'][label_key])
						else:
							pass
							#print "WARNING - hwmon: label ("+label_key+") not found in lookup: "+f

				if hwmon.has_key('names'):
					for attribute in hwmon['names'].keys():
						obj_mapping.append(hwmon['names'][attribute]['object_path'])
						self.addObject(dpath,dpath+attribute,hwmon['names'][attribute])

			else:
				print "WARNING - hwmon: Unhandled hwmon: "+dpath
		self.addSensorMonitor()

		for dpath in System.HWMON_CONFIG:
			for attribute in System.HWMON_CONFIG[dpath]['names']:
				objpath = System.HWMON_CONFIG[dpath]['names'][attribute]['object_path']
				if (System.HWMON_CONFIG[dpath]['names'][attribute].has_key('sensornumber')):
					if (System.HWMON_CONFIG[dpath]['names'][attribute]['sensornumber'] != ''):
						if objpath not in self.check_subsystem_health:
							self.check_subsystem_health[objpath] = 1
						if System.HWMON_CONFIG[dpath]['names'][attribute]['object_path'] not in obj_mapping:
							if self.check_subsystem_health[objpath] == 1:
								try:
									bmclogevent_ctl.BmcLogEventMessages(check_subsystem_health_obj_path, \
											"Management Subsystem Health" ,"Asserted", "Management Subsystem Health", \
											data={'event_status':0x4, 'sensor_number':System.HWMON_CONFIG[dpath]['names'][attribute]['sensornumber']})
									bmclogevent_ctl.bmclogevent_set_value(check_subsystem_health_obj_path, 1)
									self.check_subsystem_health[objpath] = 0
								except:
									pass

		for k in self.hwmon_root.keys():
			if (found_hwmon.has_key(k) == False):
				## need to remove all objects associated with this path
				print "Removing: "+k
				for objpath in self.hwmon_root[k]:
					if (self.sensors.has_key(objpath) == True):
						print "HWMON remove: "+objpath
						self.sensors.pop(objpath,None)
						obj = bus.get_object(SENSOR_BUS,SENSOR_PATH,introspect=False)
						intf = dbus.Interface(obj,SENSOR_BUS)
						intf.delete(objpath)

				self.hwmon_root.pop(k,None)

		return True

	def kickWatchdog(self):
		if os.path.exists(WATCHDOG_FILE_PATH):
			os.remove(WATCHDOG_FILE_PATH)
		return True

if __name__ == '__main__':

	os.nice(-19)
	dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
	bus = get_dbus()
	root_sensor = Hwmons(bus)
	mainloop = glib.MainLoop()

	print "Starting HWMON sensors"
	mainloop.run()

