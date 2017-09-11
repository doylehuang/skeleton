# -*- coding: utf-8 -*-

# pylint: disable=attribute-defined-outside-init
# pylint: disable=missing-docstring

from os.path import join
from glob import glob

HOME_PATH = './'
CACHE_PATH = '/var/cache/obmc/'
FLASH_DOWNLOAD_PATH = "/tmp"
GPIO_BASE = 320
SYSTEM_NAME = "G50"

def find_gpio_base(path="/sys/class/gpio/"):
    pattern = "gpiochip*"
    for gpiochip in glob(join(path, pattern)):
        with open(join(gpiochip, "label")) as chipfile:
            label = chipfile.readline().strip()
        if label == "1e780000.gpio":
            with open(join(gpiochip, "base")) as chipfile:
                return int(chipfile.readline().strip())
    # trigger a file not found exception
    open(join(path, "gpiochip"))

GPIO_BASE = find_gpio_base()

## System states
##   state can change to next state in 2 ways:
##   - a process emits a GotoSystemState signal with state name to goto
##   - objects specified in EXIT_STATE_DEPEND have started
SYSTEM_STATES = [
    'BASE_APPS',
    'BMC_STARTING',
    'BMC_STARTING2',
    'BMC_READY',
    'HOST_POWERING_ON',
    'HOST_POWERED_ON',
    'HOST_BOOTING',
    'HOST_BOOTED',
    'HOST_POWERED_OFF',
]

EXIT_STATE_DEPEND = {
    'BASE_APPS' : {
        '/org/openbmc/sensors': 0,
    },
    'BMC_STARTING' : {
        '/org/openbmc/inventory/system/chassis/io_board' : 0,
    },
    'BMC_STARTING2' : {
        '/org/openbmc/control/chassis0': 0,
    },
}

## method will be called when state is entered
ENTER_STATE_CALLBACK = {
    'BMC_READY' : {
        'setBlinkSlow' : {
            'bus_name'   : 'org.openbmc.control.led',
            'obj_name'   : '/org/openbmc/control/led/heartbeat',
            'interface_name' : 'org.openbmc.Led',
        },
    }
}

APPS = {
    'startup_hacks' : {
        'system_state'    : 'BASE_APPS',
        'start_process'   : True,
        'monitor_process' : False,
        'process_name'    : 'startup_hacks.sh',
    },
    'set_hostname' : {
        'system_state'    : 'BASE_APPS',
        'start_process'   : True,
        'monitor_process' : False,
        'process_name'    : 'set_hostname.sh',
    },
    'inventory' : {
        'system_state'    : 'BMC_STARTING',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'inventory_items.py',
        'args'            : [SYSTEM_NAME]
    },
    'fan_control' : {
        'system_state'    : 'BMC_STARTING2',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'fan_control.py',
    },
    'hwmon' : {
        'system_state'    : 'BMC_STARTING',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'hwmon.py',
        'args'            : [SYSTEM_NAME]
    },
    'sensor_manager' : {
        'system_state'    : 'BASE_APPS',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'sensor_manager2.py',
        'args'            : [SYSTEM_NAME]
    },
    'host_watchdog' : {
        'system_state'    : 'BMC_STARTING',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'host_watchdog.exe',
    },
    'power_control' : {
        'system_state'    : 'BMC_STARTING',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name' : 'power_control_sthelens.exe',
        'args' : ['3000', '10']
    },
    'power_button' : {
        'system_state'    : 'BMC_STARTING',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'button_power.exe',
    },
    'led_control' : {
        'system_state'    : 'BMC_STARTING',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'led_controller.exe',
    },
    'bmc_flash_control' : {
        'system_state'    : 'BMC_STARTING',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'bmc_update.py',
    },
    'download_manager' : {
        'system_state'    : 'BMC_STARTING',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'download_manager.py',
        'args'            : [SYSTEM_NAME]
    },
    'chassis_control' : {
        'system_state'    : 'BMC_STARTING2',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'chassis_control.py',
    },
    'board_vpd' : {
        'system_state'    : 'BMC_STARTING2',
        'start_process'   : True,
        'monitor_process' : False,
        'process_name'    : 'phosphor-read-eeprom',
        'args'            : [
            '--eeprom', '/sys/bus/i2c/devices/4-0050/eeprom', '--fruid', '64'],
    },
    'restore' : {
        'system_state'    : 'BMC_READY',
        'start_process'   : True,
        'monitor_process' : False,
        'process_name'    : 'discover_system_state.py',
    },
    'fan_algorithm' : {
        'system_state'    : 'BMC_READY',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'fan_algorithm.exe',
    },
    'bmc_control' : {
        'system_state'    : 'BMC_STARTING',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'control_bmc.exe',
    },
    'id_button' : {
        'system_state'    : 'BMC_STARTING',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'button_id.exe',
    },
    'cable_led' : {
        'system_state'    : 'BMC_STARTING',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'cable_led.exe',
    },
    'netman' : {
        'system_state'    : 'BMC_STARTING2',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'netman.py',
    },
    'sync_mac' : {
        'system_state'    : 'BMC_READY',
        'start_process'   : True,
        'monitor_process' : False,
        'process_name'    : 'sync_inventory_items.py',
        'args'            : [
            '-t', 'DAUGHTER_CARD', '-n', 'io_board', '-p', 'Custom Field 2',
            '-s', 'mac'],
    },
    'fan_ctl' : {
        'system_state'    : 'BMC_STARTING',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'fan_generic_obj.exe',
    },
    'gpu_core' : {
        'system_state'    : 'BMC_READY',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'gpu_core.exe',
    },
    'node_init_sthelens' : {
        'system_state'    : 'BASE_APPS',
        'start_process'   : True,
        'monitor_process' : False,
        'process_name'    : 'node_init_sthelens.exe',
    },
    'bmchealth_service' : {
        'system_state'    : 'BMC_READY',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'bmchealth_handler.py',
    },
    'pex_service' : {
        'system_state'    : 'BMC_READY',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'pex_core.exe',
    },
    'pmbus_scanner' : {
        'system_state'    : 'BMC_READY',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'pmbus_scanner.exe',
    },
    'pcie-device-temperature' : {
        'system_state'	  : 'BMC_READY',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'	  : 'pcie-device-temperature.exe',
    },
    'sntp_sync_period' : {
        'system_state'    : 'BMC_STARTING',
        'start_process'   : True,
        'monitor_process' : True,
        'process_name'    : 'sntp_sync_period.exe',
    },
}

CACHED_INTERFACES = {
    "org.openbmc.InventoryItem" : True,
    "org.openbmc.control.Chassis" : True,
}
INVENTORY_ROOT = '/org/openbmc/inventory'

FRU_INSTANCES = {
    '<inventory_root>/system' : {
        'fru_type' : 'SYSTEM',
        'is_fru' : True,
        'present' : "True"
        },
    '<inventory_root>/system/misc' : {
        'fru_type' : 'SYSTEM',
        'is_fru' : False,
        },
    '<inventory_root>/system/chassis' : {
        'fru_type' : 'SYSTEM',
        'is_fru' : True,
        'present' : "True"
        },
    '<inventory_root>/system/chassis/motherboard' : {
        'fru_type' : 'MAIN_PLANAR',
        'is_fru' : True,
        },
    '<inventory_root>/system/systemevent'                  : {
        'fru_type' : 'SYSTEM_EVENT',
        'is_fru' : False,
        },
    '<inventory_root>/system/chassis/motherboard/refclock' : {
        'fru_type' : 'MAIN_PLANAR',
        'is_fru' : False,
        },
    '<inventory_root>/system/chassis/motherboard/pcieclock': {
        'fru_type' : 'MAIN_PLANAR',
        'is_fru' : False,
        },
    '<inventory_root>/system/chassis/motherboard/todclock' : {
        'fru_type' : 'MAIN_PLANAR',
        'is_fru' : False,
        },
    '<inventory_root>/system/chassis/motherboard/apss'     : {
        'fru_type' : 'MAIN_PLANAR',
        'is_fru' : False,
        },
    '<inventory_root>/system/chassis/motherboard/bmc' : {
        'fru_type' : 'BMC',
        'is_fru' : False,
        'manufacturer' : 'ASPEED'
        },
    '<inventory_root>/system/chassis/io_board' : {
        'fru_type' : 'DAUGHTER_CARD',
        'is_fru' : True,
        'Custom Field 2': '00:00:00:00:00:00',
        },
}

ID_LOOKUP = {
    'FRU' : {
        0x40 : '<inventory_root>/system/chassis/io_board',
    },
    'FRU_STR' : {
        'BOARD_100'  : '<inventory_root>/system/chassis/io_board',
    },
    'FRU_SLAVE' : {
        'BOARD_100'  : {'I2C_BUS':4 , 'I2C_SLAVE': 0x50}
    },
    'SENSOR' : {
    },
    'GPIO_PRESENT' : {
    }
}

GPIO_CONFIG = {}
GPIO_CONFIG['UBB_SMB_RST_N'] = {'gpio_pin': 'C6', 'direction': 'out', 'inverse': 'yes'}
GPIO_CONFIG['PDB_SMB_RST_N'] = {'gpio_pin': 'C7', 'direction': 'out', 'inverse': 'yes'}
GPIO_CONFIG['BLADE_ATT_LED_N'] = {'gpio_pin': 'F0', 'direction': 'out', 'inverse': 'yes', 'data_reg_addr': 0x1e780020, 'offset': 8}
GPIO_CONFIG['PWR_BTN_N'] = {'gpio_pin': 'F1', 'direction': 'in'}
GPIO_CONFIG['PWR_STA_LED_N'] = {'gpio_pin': 'F2', 'direction': 'out', 'inverse': 'yes'}
GPIO_CONFIG['UID_BTN_N'] = {'gpio_pin': 'F3', 'direction': 'in'}
GPIO_CONFIG['UID_LED_N'] = {'gpio_pin': 'F4', 'direction': 'out', 'inverse': 'yes'}
GPIO_CONFIG['SYS_THROTTLE_N'] = {'gpio_pin': 'F5', 'direction': 'in', 'inverse': 'yes'}
GPIO_CONFIG['FM_GPIO4'] = {'gpio_pin': 'F6', 'direction': 'out'}
GPIO_CONFIG['GPU_OVERT_N'] = {'gpio_pin': 'F7', 'direction': 'out'}
GPIO_CONFIG['RM_SYS_THROTTLE_N'] = {'gpio_pin': 'M0', 'direction': 'in', 'inverse': 'yes'}
GPIO_CONFIG['FIO_RM_SYS_THROTTLE_N'] = {'gpio_pin': 'M1', 'direction': 'in', 'inverse': 'yes'}
GPIO_CONFIG['PWR_ON_REQ_N'] = {'gpio_pin': 'M2', 'direction': 'in', 'inverse': 'yes'}
GPIO_CONFIG['SYS_FORCE_PWR_OFF'] = {'gpio_pin': 'M3', 'direction': 'out'}
GPIO_CONFIG['SYS_READY_N'] = {'gpio_pin': 'M4', 'direction': 'out', 'inverse': 'yes'}
GPIO_CONFIG['FM_GPIO1'] = {'gpio_pin': 'M5', 'direction': 'out'}
GPIO_CONFIG['FM_GPIO2'] = {'gpio_pin': 'M6', 'direction': 'out'}
GPIO_CONFIG['FM_GPIO3'] = {'gpio_pin': 'M7', 'direction': 'out'}
GPIO_CONFIG['BMC_BSC_ALT_N'] = {'gpio_pin': 'Q6', 'direction': 'in', 'inverse': 'yes'}
GPIO_CONFIG['BMC_SMB_4_RST_N'] = {'gpio_pin': 'Q7', 'direction': 'out', 'inverse': 'yes'}
GPIO_CONFIG['BMC_HB_LED'] = {'gpio_pin': 'R4', 'direction': 'out'}


def convertGpio(name):
    offset = int(filter(str.isdigit, name))
    port = filter(str.isalpha, name.upper())
    port_num = ord(port[-1]) - ord('A')
    if len(port) > 1:
        port_num += 26
    base = port_num * 8 + GPIO_BASE
    return base + offset

def _add_gpu_temperature_sensor(configs, index, sensornumber):
    objpath = '/org/openbmc/sensors/gpu/gpu_temp'
    config = {
        'critical_upper': 81,
        'positive_hysteresis': 2,
        'device_node': '/tmp/gpu/gpu%d_temp' % index,
        'object_path': 'sensors/gpu/gpu_temp',
        'poll_interval': 5000,
        'reading_type': 0x01,
        'scale': 1,
        'sensor_name': 'GPU%d Temp' % index,
        'sensor_type': '0x01',
        'sensornumber': sensornumber,
        'standby_monitor': False,
        'units': 'C',
        'index': index,
        'value': -1,
        'mapping': '/org/openbmc/control/gpu/slot%d' % index,
        'status_change_count': 0,
        'reading_error_count': 0,
		'ready': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_gpu_mem_temperature_sensor(configs, index, sensornumber):
    objpath = '/org/openbmc/sensors/gpu/gpu_mem_temp'
    config = {
        'critical_upper': 85,
        'positive_hysteresis': 2,
        'device_node': '/tmp/gpu/gpu%d_mem_temp' % index,
        'object_path': 'sensors/gpu/gpu_mem_temp',
        'poll_interval': 5000,
        'reading_type': 0x01,
        'scale': 1,
        'sensor_name': 'GPU%d Mem Temp' % index,
        'sensor_type': '0x01',
        'sensornumber': sensornumber,
        'standby_monitor': False,
        'units': 'C',
        'index': index,
        'value': -1,
        'status_change_count': 0,
        'reading_error_count': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_m2_temperature_sensor(configs, index, sensornumber):
    objpath = '/org/openbmc/sensors/M2/M2_TMP'
    config = {
        'critical_upper': 85,
        'positive_hysteresis': 2,
        'device_node': '/tmp/pcie/mdot2_%d_temp' % index,
        'poll_interval': 5000,
        'reading_type': 0x01,
        'scale': 1,
        'sensor_name': 'M.2 %d Temp' % index,
        'sensor_type': '0x01',
        'sensornumber': sensornumber,
        'standby_monitor': False,
        'units': 'C',
        'index': index,
        'value': -1,
        'status_change_count': 0,
        'reading_error_count': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_fan_pwm_sensor(configs, index, sensornumber):
    objpath = '/org/openbmc/control/fan/pwm'
    config = {
        'device_node':
            '/sys/devices/platform/ast_pwm_tacho.0/pwm%d_falling' % index,
        'object_path': 'control/fan/pwm',
        'critical_lower': 18,
        'poll_interval': 10000,
        'reading_type': 0x01,
        'scale': 1,
        'sensor_name': 'PWM %d' % index,
        'sensor_type': '0x04',
        'sensornumber': sensornumber,
        'standby_monitor': False,
        'units': '%',
        'value': -1,
        'status_change_count': 0,
        'reading_error_count': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_fan_tach_sensor(configs, index, sensornumber):
    objpath = '/org/openbmc/sensors/fan/fan_tacho'
    config = {
        'critical_lower': 3800,
        'device_node':
            '/sys/devices/platform/ast_pwm_tacho.0/tacho%d_rpm' % index,
        'object_path': 'sensors/fan/fan_tacho',
        'poll_interval': 10000,
        'reading_type': 0x01,
        'scale': 1,
        'sensor_name': 'Fan Tach %d' % index,
        'sensor_type': '0x04',
        'sensornumber': sensornumber,
        'standby_monitor': False,
        'units': 'rpm',
        'value': -1,
        'status_change_count': 0,
        'reading_error_count': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_psu_temperature_sensor(configs, index, sensornumber, bus_number):
    objpath = '/org/openbmc/sensors/pmbus/pmbus/temp_02'
    config = {
        'bus_number': bus_number,
        'critical_upper': 95,
        'positive_hysteresis': 2,
        'device_node': 'temp2_input',
        'object_path': 'sensors/pmbus/pmbus/temp_02',
        'poll_interval': 10000,
        'reading_type': 0x01,
        'scale': 1000,
        'sensor_name': 'PSU%d Temp 2' % index,
        'sensor_type': '0x09',
        'sensornumber': sensornumber,
        'index': index,
        'standby_monitor': False,
        'units': 'C',
        'value': -1,
        'firmware_update': 0, # 0: normal, 1:firmware_update working
        'status_change_count': 0,
        'reading_error_count': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_psu_voltage_sensor(configs, index, sensornumber, bus_number):
    objpath = '/org/openbmc/sensors/pmbus/pmbus/Voltage_vout'
    config = {
        'bus_number': bus_number,
        'critical_lower': 10.5,
        'critical_upper': 14.25,
        'device_node': 'in2_input',
        'max_reading': '20',
        'min_reading': '0',
        'object_path': 'sensors/pmbus/pmbus/Voltage_vout',
        'poll_interval': 10000,
        'reading_type': 0x01,
        'scale': 1000,
        'sensor_name': 'PSU%d Voltage Output' % index,
        'sensor_type': '0x09',
        'sensornumber': sensornumber,
        'index': index,
        'standby_monitor': False,
        'units': 'V',
        'value': -1,
        'index': index,
        'firmware_update': 0, # 0: normal, 1:firmware_update working
        'status_change_count': 0,
        'reading_error_count': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_psu_power_sensor(configs, index, sensornumber, bus_number):
    objpath = '/org/openbmc/sensors/pmbus/pmbus/Power_pout'
    config = {
        'bus_number': bus_number,
        'critical_upper': 1760,
        'device_node': 'power2_input',
        'object_path': 'sensors/pmbus/pmbus/Power_pout',
        'poll_interval': 10000,
        'reading_type': 0x01,
        'scale': 1000000,
        'sensor_name': 'PSU%d Power Output' % index,
        'sensor_type': '0x09',
        'sensornumber': sensornumber,
        'index': index,
        'standby_monitor': False,
        'units': 'W',
        'value': -1,
        'firmware_update': 0, # 0: normal, 1:firmware_update working
        'status_change_count': 0,
        'reading_error_count': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_cable_led(configs, index, gpio):
    config = ['/org/openbmc/control/cable_led/led%d' % index, {
        'device_node': '/sys/class/gpio/gpio%d/value' % gpio,
        'object_path': 'control/cable_led/led%d' % index,
        'poll_interval': 10000,
        'scale': 1,
        'standby_monitor': True,
        'units': '',
        'entity': 0x1F,
        'index': index+1,
        'inverse': 0,
        }]
    configs.append(config)

def _add_pex9797(configs, index, sensornumber):
    objpath = '/org/openbmc/sensors/pex/pex'
    config = {
        'device_node': '/tmp/pex/pex%d_temp' % index,
        'critical_upper': 111,
        'positive_hysteresis': 2,
        'object_path': 'sensors/pex/pex',
        'poll_interval': 5000,
        'scale': 1,
        'sensornumber': sensornumber,
        'sensor_type': '0x01',
        'reading_type': 0x01,
        'sensor_name': 'PLX Switch %d Temp' % (index+1),
        'standby_monitor': False,
        'units': 'C',
        'index': index,
        'status_change_count': 0,
        'reading_error_count': 0,
        'ready': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_bmc_health_sensor(configs, sensornumber):
    config = ['/org/openbmc/sensors/bmc_health', {
        'device_node': '',
        'object_path': 'sensors/bmc_health',
        'reading_type': 0x70,
        'sensor_name': 'BMC Health',
        'sensor_type': '0x28',
        'sensornumber': sensornumber,
        'standby_monitor': True,
        'value': 0,
        'severity_health': 'OK',
        }]
    configs.append(config)

def _add_event_log_sensor(configs, sensornumber):
    config = ['/org/openbmc/sensors/event_log', {
        'device_node': '',
        'object_path': 'sensors/event_log',
        'reading_type': 0x6F,
        'sensor_name': 'Event log',
        'sensor_type': '0x10',
        'sensornumber': sensornumber,
        'standby_monitor': True,
        'value': 0,
        }]
    configs.append(config)

def _add_ntp_status_sensor(configs, sensornumber):
    config = ['/org/openbmc/sensors/ntp_status', {
        'device_node': '',
        'object_path': 'sensors/ntp_status',
        'reading_type': 0x71,
        'sensor_name': 'NTP Status',
        'sensor_type': '0x12',
        'sensornumber': sensornumber,
        'standby_monitor': True,
        'value': 0,
        }]
    configs.append(config)

def _add_psu_status_sensor(configs, index, sensornumber, bus_number):
    objpath = '/org/openbmc/sensors/pmbus/pmbus/status'
    config = {
        'bus_number': bus_number,
        'device_node': 'pmbus_status_word',
        'object_path': 'sensors/pmbus/pmbus/status',
        'poll_interval': 5000,
        'reading_type': 0x6F,
        'scale': 1,
        'sensor_name': 'PSU%d Status' % index,
        'sensor_type': '0x08',
        'sensornumber': sensornumber,
        'index': index,
        'standby_monitor': True,
        'units': '',
        'value': -1,
        'firmware_update': 0, # 0: normal, 1:firmware_update working
        'status_change_count': 0,
        'reading_error_count': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_hsc_temperature_sensor(configs, index, sensornumber, sensor_name, bus_number, standby):
    objpath = '/org/openbmc/sensors/HSC/HSC_TMP'
    config = {
        'bus_number': bus_number,
        'critical_upper':125,
        'positive_hysteresis': 2,
        'device_node': 'temp1_input',
        'object_path' : 'sensors/HSC/HSC_TMP',
        'poll_interval' : 5000,
        'reading_type': 0x01,
        'scale': 1000,
        'sensor_name': sensor_name,
        'sensor_type' : '0x01',
        'sensornumber': sensornumber,
        'index': index,
        'standby_monitor': standby,
        'emergency_enabled' : False,
        'units': 'C',
        'value': -1,
        'min_reading':'0',
        'max_reading':'20',
        'status_change_count': 0,
        'reading_error_count': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_hsc_voltage_sensor(configs, index, sensornumber, sensor_name, bus_number, standby):
    objpath = '/org/openbmc/sensors/HSC/HSC_VOUT'
    config = {
        'bus_number': bus_number,
        'critical_lower':10.6,
        'critical_upper':13.8,
        'device_node': 'in3_input',
        'object_path' : 'sensors/HSC/HSC_VOUT',
        'poll_interval' : 5000,
        'reading_type': 0x01,
        'scale': 1000,
        'sensor_name': sensor_name,
        'sensor_type' : '0x02',
        'sensornumber': sensornumber,
        'index': index,
        'standby_monitor': standby,
        'emergency_enabled' : False,
        'units' : 'V',
        'value': -1,
        'min_reading':'0',
        'max_reading':'20',
        'status_change_count': 0,
        'reading_error_count': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_temp_sensor(configs, index, sensornumber, sensor_name, bus_number):
    objpath = '/org/openbmc/sensors/temperature/TMP'
    config = {
        'bus_number': bus_number,
        'critical_upper':37,
        'positive_hysteresis': 2,
        'device_node': 'temp1_input',
        'object_path' : 'sensors/temperature/TMP',
        'poll_interval' : 5000,
        'reading_type': 0x01,
        'scale': 1000,
        'sensor_name': sensor_name,
        'sensor_type' : '0x01',
        'sensornumber': sensornumber,
        'index': index,
        'standby_monitor': False,
        'emergency_enabled' : True,
        'units' : 'C',
        'value': -1,
        'status_change_count': 0,
        'reading_error_count': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_fpga1_sensor(configs, index, sensornumber, sensor_name, bus_number):
    objpath = '/org/openbmc/sensors/temperature/FPGA1_TMP'
    config = {
        'bus_number': bus_number,
        'critical_upper':83,
        'positive_hysteresis': 2,
        'device_node': 'temp1_input',
        'object_path' : 'sensors/temperature/FPGA1_TMP',
        'poll_interval' : 5000,
        'reading_type': 0x01,
        'scale': 1000,
        'sensor_name': sensor_name,
        'sensor_type' : '0x01',
        'sensornumber': sensornumber,
        'index': index,
        'standby_monitor': False,
        'emergency_enabled' : True,
        'units' : 'C',
        'value': -1,
        'status_change_count': 0,
        'reading_error_count': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_fpga2_sensor(configs, index, sensornumber, sensor_name, bus_number):
    objpath = '/org/openbmc/sensors/temperature/FPGA2_TMP'
    config = {
        'bus_number': bus_number,
        'critical_upper':83,
        'positive_hysteresis': 2,
        'device_node': 'temp2_input',
        'object_path' : 'sensors/temperature/FPGA2_TMP',
        'poll_interval' : 5000,
        'reading_type': 0x01,
        'scale': 1000,
        'sensor_name': sensor_name,
        'sensor_type' : '0x01',
        'sensornumber': sensornumber,
        'index': index,
        'standby_monitor': False,
        'emergency_enabled' : True,
        'units' : 'C',
        'value': -1,
        'status_change_count': 0,
        'reading_error_count': 0,
        }
    if objpath in configs:
        configs[objpath].append(config)
    else:
        configs[objpath] = []
        configs[objpath].append(config)

def _add_entity_presence(configs, sensornumber):
    config = ['/org/openbmc/sensors/entity_presence', {
        'device_node': '',
        'object_path': 'sensors/entity_presence',
        'reading_type': 0x6F,
        'sensor_name': 'Entity Presence',
        'sensor_type': '0x25',
        'sensornumber': sensornumber,
        'standby_monitor': True,
        'value': 0,
        }]
    configs.append(config)

def _add_management_subsystem_health(configs, sensornumber):
    config = ['/org/openbmc/sensors/management_subsystem_health', {
        'device_node': '',
        'object_path': 'sensors/management_subsystem_health',
        'reading_type': 0x6F,
        'sensor_name': 'Management Subsystem Health',
        'sensor_type': '0x28',
        'sensornumber': sensornumber,
        'standby_monitor': True,
        'value': 0,
        }]
    configs.append(config)

def _add_pcie_slot(configs, index, gpio):
    config = ['/org/openbmc/control/pcie/slot%d' % index, {
        'device_node': '/sys/class/gpio/gpio%d/value' % gpio,
        'object_path': 'control/pcie/slot%d' % index,
        'poll_interval': 10000,
        'scale': 1,
        'standby_monitor': True,
        'units': '',
        'entity': 0xB,
        'index': index,
        'inverse': 1,
        }]
    configs.append(config)

def _add_gpu_slot(configs, index, gpio):
    config = ['/org/openbmc/control/gpu/slot%d' % index, {
        'device_node': '/sys/class/gpio/gpio%d/value' % gpio,
        'object_path': 'control/gpu/slot%d' % index,
        'poll_interval': 10000,
        'scale': 1,
        'standby_monitor': True,
        'units': '',
        'entity': 0x3,
        'index': index,
        'inverse': 1,
        }]
    configs.append(config)

def _add_sys_throttle_gpio(configs, sensornumber, gpio, extra_gpio):
    config = ['/org/openbmc/sensors/system_throttle', {
        'device_node': '/sys/class/gpio/gpio%d/value' % gpio,
        'object_path': 'sensors/system_throttle',
        'poll_interval': 1000,
        'reading_type': 0x72,
        'scale': 1,
        'sensor_name': 'System Throttle',
        'sensor_type': '0xc0',
        'sensornumber': sensornumber,
        'standby_monitor': True,
        'units': '',
        'value': 0,
        'inverse': 1,
        'extra_data':'/sys/class/gpio/gpio%d/value' % extra_gpio,
        }]
    configs.append(config)

def _add_session_audit(configs, sensornumber):
    config = ['/org/openbmc/sensors/session_audit', {
        'device_node': '',
        'object_path': 'sensors/session_audit',
        'poll_interval': 5000,
        'reading_type': 0x6F,
        'sensor_name': 'Session Audit',
        'sensor_type': '0x2a',
        'sensornumber': sensornumber,
        'standby_monitor': True,
        'units': '',
        'value': 0,
        }]
    configs.append(config)

def _add_system_event(configs, sensornumber):
    config = ['/org/openbmc/sensors/system_event', {
        'device_node': '',
        'object_path': 'sensors/system_event',
        'reading_type': 0x72,
        'sensor_name': 'System Event',
        'sensor_type': '0x12',
        'sensornumber': sensornumber,
        'standby_monitor': True,
        'value': 0,
        }]
    configs.append(config)

HWMON_SENSOR_CONFIG = {}
SENSOR_MONITOR_CONFIG = []
_add_temp_sensor(HWMON_SENSOR_CONFIG, 9, 0x05, 'FIO Inlet Temp 1', '0-0049')
_add_temp_sensor(HWMON_SENSOR_CONFIG, 10, 0x06, 'FIO Inlet Temp 2', '0-004a')
_add_hsc_temperature_sensor(HWMON_SENSOR_CONFIG, 1, 0x24, 'HSC1 Temp', '0-0010', False)
_add_hsc_temperature_sensor(HWMON_SENSOR_CONFIG, 2, 0x26, 'HSC2 STBY Temp', '0-0011', True)
_add_hsc_temperature_sensor(HWMON_SENSOR_CONFIG, 3, 0x28, 'HSC3 GPU1 Temp', '0-0040', False)
_add_hsc_temperature_sensor(HWMON_SENSOR_CONFIG, 4, 0x2a, 'HSC4 GPU2 Temp', '0-0041', False)
_add_hsc_temperature_sensor(HWMON_SENSOR_CONFIG, 5, 0x2c, 'HSC5 GPU3 Temp', '0-0042', False)
_add_hsc_temperature_sensor(HWMON_SENSOR_CONFIG, 6, 0x2e, 'HSC6 GPU4 Temp', '0-0043', False)
_add_hsc_temperature_sensor(HWMON_SENSOR_CONFIG, 7, 0x30, 'HSC7 GPU5 Temp', '0-0044', False)
_add_hsc_temperature_sensor(HWMON_SENSOR_CONFIG, 8, 0x32, 'HSC8 GPU6 Temp', '0-0045', False)
_add_hsc_temperature_sensor(HWMON_SENSOR_CONFIG, 9, 0x34, 'HSC9 GPU7 Temp', '0-0046', False)
_add_hsc_temperature_sensor(HWMON_SENSOR_CONFIG, 10, 0x36, 'HSC10 GPU8 Temp', '0-0047', False)
_add_hsc_voltage_sensor(HWMON_SENSOR_CONFIG, 1, 0x23, 'HSC1 VOUT', '0-0010', False)
_add_hsc_voltage_sensor(HWMON_SENSOR_CONFIG, 2, 0x25, 'HSC2 STBY VOUT', '0-0011', True)
_add_hsc_voltage_sensor(HWMON_SENSOR_CONFIG, 3, 0x27, 'HSC3 GPU1 VOUT', '0-0040', False)
_add_hsc_voltage_sensor(HWMON_SENSOR_CONFIG, 4, 0x29, 'HSC4 GPU2 VOUT', '0-0041', False)
_add_hsc_voltage_sensor(HWMON_SENSOR_CONFIG, 5, 0x2b, 'HSC5 GPU3 VOUT', '0-0042', False)
_add_hsc_voltage_sensor(HWMON_SENSOR_CONFIG, 6, 0x2d, 'HSC6 GPU4 VOUT', '0-0043', False)
_add_hsc_voltage_sensor(HWMON_SENSOR_CONFIG, 7, 0x2f, 'HSC7 GPU5 VOUT', '0-0044', False)
_add_hsc_voltage_sensor(HWMON_SENSOR_CONFIG, 8, 0x31, 'HSC8 GPU6 VOUT', '0-0045', False)
_add_hsc_voltage_sensor(HWMON_SENSOR_CONFIG, 9, 0x33, 'HSC9 GPU7 VOUT', '0-0046', False)
_add_hsc_voltage_sensor(HWMON_SENSOR_CONFIG, 10, 0x35, 'HSC10 GPU8 VOUT', '0-0047', False)
_add_gpu_temperature_sensor(HWMON_SENSOR_CONFIG, 1, 0x41)
_add_gpu_temperature_sensor(HWMON_SENSOR_CONFIG, 2, 0x42)
_add_gpu_temperature_sensor(HWMON_SENSOR_CONFIG, 3, 0x43)
_add_gpu_temperature_sensor(HWMON_SENSOR_CONFIG, 4, 0x44)
_add_gpu_temperature_sensor(HWMON_SENSOR_CONFIG, 5, 0x45)
_add_gpu_temperature_sensor(HWMON_SENSOR_CONFIG, 6, 0x46)
_add_gpu_temperature_sensor(HWMON_SENSOR_CONFIG, 7, 0x47)
_add_gpu_temperature_sensor(HWMON_SENSOR_CONFIG, 8, 0x48)
_add_m2_temperature_sensor(HWMON_SENSOR_CONFIG, 1, 0x70)
_add_m2_temperature_sensor(HWMON_SENSOR_CONFIG, 2, 0x71)
_add_m2_temperature_sensor(HWMON_SENSOR_CONFIG, 3, 0x72)
_add_m2_temperature_sensor(HWMON_SENSOR_CONFIG, 4, 0x73)
_add_fpga1_sensor(HWMON_SENSOR_CONFIG, 1, 0x74, 'FPGA Die Temp', '24-004c')
_add_fpga2_sensor(HWMON_SENSOR_CONFIG, 1, 0x75, 'FPGA Ambient Temp', '24-004c')
_add_fan_pwm_sensor(HWMON_SENSOR_CONFIG, 1, 0x1D)
_add_fan_pwm_sensor(HWMON_SENSOR_CONFIG, 2, 0x1E)
_add_fan_pwm_sensor(HWMON_SENSOR_CONFIG, 3, 0x1F)
_add_fan_pwm_sensor(HWMON_SENSOR_CONFIG, 4, 0x20)
_add_fan_pwm_sensor(HWMON_SENSOR_CONFIG, 5, 0x21)
_add_fan_pwm_sensor(HWMON_SENSOR_CONFIG, 6, 0x22)
_add_fan_tach_sensor(HWMON_SENSOR_CONFIG, 1, 0x11)
_add_fan_tach_sensor(HWMON_SENSOR_CONFIG, 2, 0x12)
_add_fan_tach_sensor(HWMON_SENSOR_CONFIG, 3, 0x13)
_add_fan_tach_sensor(HWMON_SENSOR_CONFIG, 4, 0x14)
_add_fan_tach_sensor(HWMON_SENSOR_CONFIG, 5, 0x15)
_add_fan_tach_sensor(HWMON_SENSOR_CONFIG, 6, 0x16)
_add_fan_tach_sensor(HWMON_SENSOR_CONFIG, 7, 0x17)
_add_fan_tach_sensor(HWMON_SENSOR_CONFIG, 8, 0x18)
_add_fan_tach_sensor(HWMON_SENSOR_CONFIG, 9, 0x19)
_add_fan_tach_sensor(HWMON_SENSOR_CONFIG, 10, 0x1A)
_add_fan_tach_sensor(HWMON_SENSOR_CONFIG, 11, 0x1B)
_add_fan_tach_sensor(HWMON_SENSOR_CONFIG, 12, 0x1C)
_add_psu_temperature_sensor(HWMON_SENSOR_CONFIG, 1, 0x52, '8-0058')
_add_psu_voltage_sensor(HWMON_SENSOR_CONFIG, 1, 0x51, '8-0058')
_add_psu_power_sensor(HWMON_SENSOR_CONFIG, 1, 0x50, '8-0058')
_add_psu_temperature_sensor(HWMON_SENSOR_CONFIG, 2, 0x55, '9-0058')
_add_psu_voltage_sensor(HWMON_SENSOR_CONFIG, 2, 0x54, '9-0058')
_add_psu_power_sensor(HWMON_SENSOR_CONFIG, 2, 0x53, '9-0058')
_add_psu_temperature_sensor(HWMON_SENSOR_CONFIG, 3, 0x58, '10-0058')
_add_psu_voltage_sensor(HWMON_SENSOR_CONFIG, 3, 0x57, '10-0058')
_add_psu_power_sensor(HWMON_SENSOR_CONFIG, 3, 0x56, '10-0058')
_add_psu_temperature_sensor(HWMON_SENSOR_CONFIG, 4, 0x5B, '11-0058')
_add_psu_voltage_sensor(HWMON_SENSOR_CONFIG, 4, 0x5A, '11-0058')
_add_psu_power_sensor(HWMON_SENSOR_CONFIG, 4, 0x59, '11-0058')
_add_psu_temperature_sensor(HWMON_SENSOR_CONFIG, 5, 0x5E, '12-0058')
_add_psu_voltage_sensor(HWMON_SENSOR_CONFIG, 5, 0x5D, '12-0058')
_add_psu_power_sensor(HWMON_SENSOR_CONFIG, 5, 0x5C, '12-0058')
_add_psu_temperature_sensor(HWMON_SENSOR_CONFIG, 6, 0x61, '13-0058')
_add_psu_voltage_sensor(HWMON_SENSOR_CONFIG, 6, 0x60, '13-0058')
_add_psu_power_sensor(HWMON_SENSOR_CONFIG, 6, 0x5F, '13-0058')
_add_psu_status_sensor(HWMON_SENSOR_CONFIG, 1, 0x83, '8-0058')
_add_psu_status_sensor(HWMON_SENSOR_CONFIG, 2, 0x84, '9-0058')
_add_psu_status_sensor(HWMON_SENSOR_CONFIG, 3, 0x85, '10-0058')
_add_psu_status_sensor(HWMON_SENSOR_CONFIG, 4, 0x86, '11-0058')
_add_psu_status_sensor(HWMON_SENSOR_CONFIG, 5, 0x87, '12-0058')
_add_psu_status_sensor(HWMON_SENSOR_CONFIG, 6, 0x88, '13-0058')
_add_pex9797(HWMON_SENSOR_CONFIG, 0, 0x37)
_add_pex9797(HWMON_SENSOR_CONFIG, 1, 0x38)
_add_pex9797(HWMON_SENSOR_CONFIG, 2, 0x39)
_add_pex9797(HWMON_SENSOR_CONFIG, 3, 0x3A)

_add_event_log_sensor(SENSOR_MONITOR_CONFIG, 0x80)
_add_ntp_status_sensor(SENSOR_MONITOR_CONFIG, 0x81)
_add_bmc_health_sensor(SENSOR_MONITOR_CONFIG, 0x82)
_add_entity_presence(SENSOR_MONITOR_CONFIG, 0x8A)
_add_management_subsystem_health(SENSOR_MONITOR_CONFIG, 0x89)
_add_pcie_slot(SENSOR_MONITOR_CONFIG, 1, 252)
_add_pcie_slot(SENSOR_MONITOR_CONFIG, 2, 253)
_add_pcie_slot(SENSOR_MONITOR_CONFIG, 3, 254)
_add_pcie_slot(SENSOR_MONITOR_CONFIG, 4, 255)
_add_gpu_slot(SENSOR_MONITOR_CONFIG, 1, 236)
_add_gpu_slot(SENSOR_MONITOR_CONFIG, 2, 237)
_add_gpu_slot(SENSOR_MONITOR_CONFIG, 3, 238)
_add_gpu_slot(SENSOR_MONITOR_CONFIG, 4, 239)
_add_gpu_slot(SENSOR_MONITOR_CONFIG, 5, 240)
_add_gpu_slot(SENSOR_MONITOR_CONFIG, 6, 241)
_add_gpu_slot(SENSOR_MONITOR_CONFIG, 7, 242)
_add_gpu_slot(SENSOR_MONITOR_CONFIG, 8, 243)
_add_sys_throttle_gpio(SENSOR_MONITOR_CONFIG, 0x8B, 388, 389)
_add_session_audit(SENSOR_MONITOR_CONFIG, 0x8C)
_add_system_event(SENSOR_MONITOR_CONFIG, 0x8D)
_add_gpu_mem_temperature_sensor(HWMON_SENSOR_CONFIG, 1, 0x62)
_add_gpu_mem_temperature_sensor(HWMON_SENSOR_CONFIG, 2, 0x63)
_add_gpu_mem_temperature_sensor(HWMON_SENSOR_CONFIG, 3, 0x64)
_add_gpu_mem_temperature_sensor(HWMON_SENSOR_CONFIG, 4, 0x65)
_add_gpu_mem_temperature_sensor(HWMON_SENSOR_CONFIG, 5, 0x66)
_add_gpu_mem_temperature_sensor(HWMON_SENSOR_CONFIG, 6, 0x67)
_add_gpu_mem_temperature_sensor(HWMON_SENSOR_CONFIG, 7, 0x68)
_add_gpu_mem_temperature_sensor(HWMON_SENSOR_CONFIG, 8, 0x69)



HWMON_CONFIG = {
    '21-004b' :  {
        'names' : {
            'temp1_input' : {
                'object_path' : 'sensors/temperature/TMP4',
                'poll_interval' : 5000,
                'scale' : 1, #for thermal request, need to accurate decimal point for fan algorithm
                'units' : 'C',
                'sensor_type' : '0x01',
                'sensornumber' : '',
                'sensor_name':'',
                'reading_type' : 0x01,
                'emergency_enabled' : True,
                'offset':-7,
                'standby_monitor': False,
                }, #Thermal team suggest temp4 (-7) offset
        }
    },
}

# Miscellaneous non-poll sensor with system specific properties.
# The sensor id is the same as those defined in ID_LOOKUP['SENSOR'].
MISC_SENSORS = {
}

# Set fan_algorithm config
#   FAN_DBUS_INTF_LOOKUP: {keys:[dbus bus name, dbus interface name]}
#   CHASSIS_POWER_STATE: set chassis power state object path
#   FAN_INPUT_OBJ: set fan input object path
#       - ["fan tach obj path", "pwm index mapping with fan tach"]
#   FAN_OUTPUT_OBJ: set fan out object path , eg: hwmon control fan speed or
#   directly use pwm control pwm
#   OPEN_LOOP_PARAM: set openloop parameter
#       - paramA
#       - paramB
#       - paramC
#       - Low_Amb
#       - Up_Amb
#       - Low_Speed
#       - High_Speed
#   OPEN_LOOP_GROUPS_1: set openloop object path
#   CLOSE_LOOP_PARAM_1: set closeloop parameter
#       - g_Kp
#       - g_Ki
#       - g_Kd
#       - g_CPUVariable
#       - g_DIMMVariable
#       - sensor_tracking
#       - highest warning temp
#   CLOSE_LOOP_GROUPS_1: set closeloop  group001 object path, eg: CPU0/CPU1
#   CLOSE_LOOP_PARAM_2: set closeloop parameter
#       - g_Kp
#       - g_Ki
#       - g_Kd
#       - g_CPUVariable
#       - g_DIMMVariable
#       - sensor_tracking
#       - highest warning temp
#   CLOSE_LOOP_GROUPS_2: set closeloop  group002 object path, eg: DIMM
#   FAN_LED_OFF: set fan led command: off
#   FAN_LED_PORT0_ALL_BLUE: set fan led port0 command: all blue
#   FAN_LED_PORT1_ALL_BLUE: set fan led port1 command: all blue
#   FAN_LED_PORT0_ALL_RED: set fan led port0 command: all red
#   FAN_LED_PORT1_ALL_RED: set fan led port1 command: all red
#   PORT0_FAN_LED_RED_MASK: set fan led port0 register mask with red
#   PORT0_FAN_LED_BLUE_MASK: set fan led port0 register mask with blue
#   PORT1_FAN_LED_RED_MASK: set fan led port1 register mask with red
#   PORT1_FAN_LED_BLUE_MASK: set fan led port1 register mask with blue
#   FAN_LED_I2C_BUS: set fan led i2c bus
#   FAN_LED_I2C_SLAVE_ADDRESS: set fan led i2c slave address
FAN_ALGORITHM_CONFIG = {
    'FAN_DBUS_INTF_LOOKUP':
    {
        'CHASSIS_POWER_STATE': [
            'org.openbmc.control.Chassis', 'org.openbmc.control.Chassis'],
        'FAN_INPUT_OBJ' : ['org.openbmc.Sensors', 'org.openbmc.SensorValue'],
        'FAN_OUTPUT_OBJ' : ['org.openbmc', 'org.openbmc.Fan'],
        'OPEN_LOOP_GROUPS_1' : [
            'org.openbmc.Sensors', 'org.openbmc.SensorValue'],
        'CLOSE_LOOP_GROUPS_1' : [
            'org.openbmc.Sensors', 'org.openbmc.SensorValue'],
        'CLOSE_LOOP_GROUPS_2' : [
            'org.openbmc.Sensors', 'org.openbmc.SensorValue'],
        'CLOSE_LOOP_GROUPS_3' : [
            'org.openbmc.Sensors', 'org.openbmc.SensorValue'],
        'CLOSE_LOOP_GROUPS_4' : [
            'org.openbmc.Sensors', 'org.openbmc.SensorValue'],
        'CLOSE_LOOP_GROUPS_5' : [
            'org.openbmc.Sensors', 'org.openbmc.SensorValue'],
        'CLOSE_LOOP_GROUPS_6' : [
            'org.openbmc.Sensors', 'org.openbmc.SensorValue'],
    },

    'CHASSIS_POWER_STATE': ['/org/openbmc/control/chassis0'],
    'FAN_INPUT_OBJ':
        [
            "/org/openbmc/sensors/fan/fan_tacho",
            "SensorNumberList", #notfity following setting about SensorNumberList
            "0x11", #base sensor number
            "12", #releate sensor list size
        ],
    'FAN_OUTPUT_OBJ':
        [
            "/org/openbmc/control/fan/pwm",
            "SensorNumberList", #notfity following setting about SensorNumberList
            "0x1D", #base sensor number
            "6", #releate sensor list size
        ],
#Profile #1 FIO inlet sensor 1
    'OPEN_LOOP_PARAM':
        [
            '0',
            '1',
            '-5',
            '25',
            '38',
            '20',
            '100',
        ],
    'OPEN_LOOP_GROUPS_1':
        [
            "/org/openbmc/sensors/temperature/TMP",
            "SensorNumberList", #notfity following setting about SensorNumberList
            "0x05", #base sensor number
            "1", #releate sensor list size
            #Thermal team only watch temp4 ambinet
        ],
#Profile #2 GPU1~8, Treal
    'CLOSE_LOOP_PARAM_1' :
        [
            '0.35',
            '-0.015',
            '0.4',
            '70',
            '81',
        ],
    'CLOSE_LOOP_GROUPS_1':
        [
            "/org/openbmc/sensors/gpu/gpu_temp",
            "SensorNumberList", #notfity following setting about SensorNumberList
            "0x41", #base sensor number
            "8", #releate sensor list size
        ],
#Profile #3 PEX9797 1~4, Treal
    'CLOSE_LOOP_PARAM_2' :
        [
            '0.35',
            '-0.015',
            '0.4',
            '90',
            '111',
        ],
    'CLOSE_LOOP_GROUPS_2':
        [
            "/org/openbmc/sensors/pex/pex",
            "SensorNumberList", #notfity following setting about SensorNumberList
            "0x37", #base sensor number
            "4", #releate sensor list size
        ],
#Profile #4 HBM1~8, Treal
    'CLOSE_LOOP_PARAM_3' :
        [
            '0.7',
            '-0.03',
            '0.4',
            '75',
            '85',
        ],
    'CLOSE_LOOP_GROUPS_3':
        [
            "/org/openbmc/sensors/gpu/gpu_mem_temp",
            "SensorNumberList", #notfity following setting about SensorNumberList
            "0x62", #base sensor number
            "8", #releate sensor list size
        ],
#Profile #5 Storey Peak sensor, Treal
    'CLOSE_LOOP_PARAM_4' :
        [
            '0.35',
            '-0.015',
            '0.4',
            '75',
            '86',
        ],
    'CLOSE_LOOP_GROUPS_4':
        [
            "/org/openbmc/sensors/temperature/FPGA1_TMP",
            "SensorNumberList", #notfity following setting about SensorNumberList
            "0x74", #base sensor number
            "1", #releate sensor list size
        ],
#Profile #5 Storey Peak sensor, Treal
    'CLOSE_LOOP_PARAM_5' :
        [
            '0.35',
            '-0.015',
            '0.4',
            '75',
            '86',
        ],
    'CLOSE_LOOP_GROUPS_5':
        [
            "/org/openbmc/sensors/temperature/FPGA2_TMP",
            "SensorNumberList", #notfity following setting about SensorNumberList
            "0x75", #base sensor number
            "1", #releate sensor list size
        ],
#Profile #6 AVA card sensor, Treal
    'CLOSE_LOOP_PARAM_6' :
        [
            '0.35',
            '-0.015',
            '0.4',
            '64',
            '78',
        ],
    'CLOSE_LOOP_GROUPS_6':
        [
            "/org/openbmc/sensors/M2/M2_TMP",
            "SensorNumberList", #notfity following setting about SensorNumberList
            "0x70", #base sensor number
            "4", #releate sensor list size
        ],

    'FAN_LED_OFF': ["0xFF"],
    'FAN_LED_PORT0_ALL_BLUE': ["0xAA"],
    'FAN_LED_PORT1_ALL_BLUE': ["0x55"],
    'FAN_LED_PORT0_ALL_RED': ["0x55"],
    'FAN_LED_PORT1_ALL_RED': ["0xAA"],
    'PORT0_FAN_LED_RED_MASK': ["0x02"],
    'PORT0_FAN_LED_BLUE_MASK': ["0x01"],
    'PORT1_FAN_LED_RED_MASK': ["0x40"],
    'PORT1_FAN_LED_BLUE_MASK': ["0x80"],
    'FAN_LED_SPEED_LIMIT': ["30"],

    'FAN_LED_I2C_BUS': [],
    'FAN_LED_I2C_SLAVE_ADDRESS': [],
}

BMC_LOGEVENT_CONFIG = {
	'BMC Health': {
	    'Record ID': 0,
	    'Record Type': 0x2,
	    'Timestamp': 0,
	    'Generator Id': 0x20,
	    'Evm Rev': 0x04,
	    'Event Type': 0x70,
	    'Event Data Table': {
			'Network Error': {
				'Severity': 'Critical',
				'Event Data Information': {
					'Link Down':	[0xA1, 0x1, None],
					'DHCP Failure':	[0xA1, 0x2, None],
				},
			},
			'Firmware Boot SPI Flash Primary': {
				'Severity': 'OK',
				'Event Data Information': {
					'Firmware Boot SPI Flash Primary':	[0xA2, 0x1, None],
				},
			},
			'Firmware Boot SPI Flash Secondary': {
				'Severity': 'Critical',
				'Event Data Information': {
					'Firmware Boot SPI Flash Secondary':	[0xA2, 0x2, None],
				},
			},
			'Hardware WDT expired': {
				'Severity': 'Critical',
				'Event Data Information': {
					'Hardware WDT expired':	[0xA3, None, None],
				},
			},
			'Alignment Traps': {
				'Severity': 'Critical',
				'Event Data Information': {
					'Alignment Traps':	[0xA5, 'alignment_msb', 'alignment_lsb'],
				},
			},
			'BMC CPU utilization': {
				'Severity': 'Warning',
				'Event Data Information': {
					'BMC CPU utilization':	[0xA6, 'cpu_utilization', None],
				},
			},
			'BMC Memory utilization': {
				'Severity': 'Warning',
				'Event Data Information': {
					'BMC Memory utilization':	[0xA7, 'memory_utilization', None],
				},
			},
			'BMC Reset': {
				'Severity': 'Critical',
				'Event Data Information': {
					'Register/Pin Reset': [0xA8, 0x1, None],
					'Redfish Reset':      [0xA8, 0x2, None],
				},
			},
			'DRAM ECC errors': {
				'Severity': 'Warning',
				'Event Data Information': {
					'DRAM ECC errors':	[0xA9, 0xFF, 0xFF],
				},
			},
			'I2C bus hang': {
				'Severity': 'Warning',
				'Event Data Information': {
					'I2C bus hang':	[0xAA, 'i2c_bus_id', 'i2c_error_code'],
				},
			},
			'Log Rollover': {
				'Severity': 'OK',
				'Event Data Information': {
					'Log Rollover':	[0xAB, 'log_rollover_count', None],
				},
			},
			'No MAC address programmed': {
				'Severity': 'Critical',
				'Event Data Information': {
					'No MAC address programmed':	[0xAC, None, None],
				},
			},
			'Firmware Update Started': {
				'Severity': 'OK',
				'Event Data Information': {
					'BMC Firmware Update Started':	[0xAD, 0x1, 'index'],
					'PSU Firmware Update Started':	[0xAD, 0x2, 'index'],
					'FPGA Firmware Update Started':	[0xAD, 0x3, 'index'],
				},
			},
			'Firmware Update completed': {
				'Severity': 'OK',
				'Event Data Information': {
					'BMC Firmware Update completed':	[0xAE, 0x1, 'index'],
					'PSU Firmware Update completed':	[0xAE, 0x2, 'index'],
					'FPGA Firmware Update completed':	[0xAE, 0x3, 'index'],
				},
			},
			'Empty Invalid FRU': {
				'Severity': 'Critical',
				'Event Data Information': {
					'Empty Invalid FRU':	[0xA4, 'fru_id', None],
				},
			},
			'BMC Service Restarted': {
				'Severity': 'Critical',
				'Event Data Information': {
					'BMC Service Restarted':	[0xA0, 'service_id_low', 'service_id_high'],
				},
			},
		},
	},
	'Entity Presence': {
		'Record ID': 0,
		'Record Type': 0x2,
		'Timestamp': 0,
		'Generator Id': 0x20,
		'Evm Rev': 0x04,
		'Event Type': 0x6F,
		'Event Data Table': {
			'Entity Presence': {
				'Severity': 'Critical',
				'Event Data Information': {
					'Entity Presence':	[0xA1, 'entity_device', 'entity_index'],
				},
			},
		},
	},
	'Management Subsystem Health': {
		'Record ID': 0,
		'Record Type': 0x2,
		'Timestamp': 0,
		'Generator Id': 0x20,
		'Evm Rev': 0x04,
		'Event Type': 0x6F,
		'Event Data Table': {
			'Management Subsystem Health': {
				'Severity': 'Critical',
				'Event Data Information': {
					'Management Subsystem Health':	['event_status', 'sensor_number', 0xFF],
				},
			},
		},
	},
	'System Event': {
		'Record ID': 0,
		'Record Type': 0x2,
		'Timestamp': 0,
		'Generator Id': 0x20,
		'Evm Rev': 0x04,
		'Event Type': 0x72,
		'Event Data Table': {
			'System Event PowerOn': {
				'Severity': 'OK',
				'Event Data Information': {
					'System Event PowerOn':	[0x00, None, None],
				},
			},
			'System Event PowerOff': {
				'Severity': 'Critical',
				'Event Data Information': {
					'System Event PowerOff': [0x01, None, None],
				},
			},
		},
	},
	'Session Audit Event': {
		'Record ID': 0,
		'Record Type': 0x2,
		'Timestamp': 0,
		'Generator Id': 0x20,
		'Evm Rev': 0x04,
		'Event Type': 0x6F,
		'Event Data Table': {
			'SSH Activated': {
				'Severity': 'OK',
				'Event Data Information': {
					'SSH Activated': [0x00, 0x01, 0x02],
				},
			},
			'SSH Failed Password': {
				'Severity': 'Critical',
				'Event Data Information': {
					'SSH Failed Password': [0x02, 0x01, 0x02],
				},
			},
			'SSH Invalid User': {
				'Severity': 'Critical',
				'Event Data Information': {
					'SSH Invalid User': [0x02, 0x01, 0x02],
				},
			},
			'SSH Closed Session By Command': {
				'Severity': 'OK',
				'Event Data Information': {
					'SSH Closed Session By Command': [0x01, 0x01, 0x12],
				},
			},
			'SSH Closed Session By Timeout': {
				'Severity': 'OK',
				'Event Data Information': {
					'SSH Closed Session By Timeout': [0x01, 0x01, 0x22],
				},
			},
			'Redfish Failed Login': {
				'Severity': 'Critical',
				'Event Data Information': {
					'Redfish Failed Login': [0x02, 0x01, 0x01],
				},
			},
		},
	},
}

