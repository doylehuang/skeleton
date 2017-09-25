#!/usr/bin/python -u

import os
import sys
import glib
import subprocess
import dbus
import obmc_system_config as System
import time
import bmclogevent_ctl
import mac_guid

DBUS_NAME = 'org.openbmc.Sensors'
DBUS_INTERFACE = 'org.freedesktop.DBus.Properties'
SENSOR_VALUE_INTERFACE = 'org.openbmc.SensorValue'

watch_redfish_start_file = "/run/obmc/redfish_start" # 0: redfish prepare to start; 1: gevent prepare to create port443
g_bmchealth_obj_path = "/org/openbmc/sensors/bmc_health"
g_i2c_recovery = {}
g_reboot_flag = 0
g_watchdog_reset = 0
g_previous_log_rollover = -1
g_memory_utilization = -1
g_record_fru_status={}
g_record_bmchealth_severity={}
g_CPU_utilization = -1
g_previous_total = 0
g_previous_idle_cpu = 0
g_number_of_system = -1

def LogEventBmcHealthMessages(s_assert="", s_event_indicator="", \
                                         s_evd_desc="", data={}):
    try:
        result = bmclogevent_ctl.BmcLogEventMessages(g_bmchealth_obj_path, "BMC Health", \
                    s_assert,  s_event_indicator, s_evd_desc, data)
        s_severity_key = s_event_indicator + "-" + s_evd_desc
        if result['logid'] != 0:
            if s_assert == "Asserted":
                bmclogevent_ctl.bmclogevent_set_value(g_bmchealth_obj_path, 1, offset=(result['evd1']&0xf))
                g_record_bmchealth_severity[s_severity_key] = result['Severity']
            elif s_assert == "Deasserted":
                bmclogevent_ctl.bmclogevent_set_value(g_bmchealth_obj_path, 0, offset=(result['evd1']&0xf))
                del g_record_bmchealth_severity[s_severity_key]
            s_severity_critical = ''
            s_severity_warning = ''
            for item in g_record_bmchealth_severity:
                if g_record_bmchealth_severity[item] == 'Critical':
                    s_severity_critical = 'Critical'
                if g_record_bmchealth_severity[item] == 'Warning':
                    s_severity_warning = 'Warning'
            s_bmchealth_severity = 'OK'
            if s_severity_critical != '':
                s_bmchealth_severity = s_severity_critical
            elif s_severity_warning != '':
                s_bmchealth_severity = s_severity_warning

            bmclogevent_ctl.bmclogevent_set_property_with_dbus(g_bmchealth_obj_path,
                                        bmclogevent_ctl.HWMON_INTERFACE,
                                        'severity_health', s_bmchealth_severity)  
    except:
        print "LogEventBmcHealthMessages error!! " + s_event_indicator

def bmchealth_check_network():
    carrier_file_path = "/sys/class/net/eth0/carrier"
    operstate_file_path = "/sys/class/net/eth0/operstate"
    check_ipaddr_command ="ifconfig eth0"
    check_ipaddr_keywords = "inet addr"
    record_dhcp_file_path = "/tmp/bmchealth_record_dhcp_status.txt"
    record_down_file_path = "/tmp/bmchealth_record_down_status.txt"

    carrier = ""    #0 or 1
    operstate = ""  #up or down
    ipaddr = ""

    g_dhcp_status = 1
    g_net_down_status = 1

    try:
        with open(record_dhcp_file_path, 'r') as f:
            for line in f:
                g_dhcp_status = int(line.rstrip('\n'))
    except:
        pass
    try:
        with open(record_down_file_path, 'r') as f:
            for line in f:
                g_net_down_status = int(line.rstrip('\n'))
    except:
        pass

    org_dhcp_status = g_dhcp_status
    org_down_status = g_net_down_status
    try:
        cmd_data = subprocess.check_output(check_ipaddr_command, shell=True)
        if cmd_data.find(check_ipaddr_keywords) >=0:
            ipaddr = "1"
        else:
            ipaddr = "0"
    except:
        print "[bmchealth_check_network]Error conduct operstate!!!"
        return False

    try:
        with open(carrier_file_path, 'r') as f:
            for line in f:
                carrier = line.rstrip('\n')
    except:
        print "[bmchealth_check_network]Error conduct carrier!!!"
        return False

    try:
        with open(operstate_file_path, 'r') as f:
            for line in f:
                operstate = line.rstrip('\n')
    except:
        print "[bmchealth_check_network]Error conduct operstate!!!"
        return False

    #check dhcp fail status
    if ipaddr == "0" and carrier == "1" and operstate == "up":
        if g_dhcp_status == 1:
            print "bmchealth_check_network:  DHCP Fail"
            g_dhcp_status = 0
            LogEventBmcHealthMessages("Asserted", "Network Error", "DHCP Failure")
    else:
        if g_dhcp_status == 0:
            LogEventBmcHealthMessages("Deasserted", "Network Error", "DHCP Failure")
            time.sleep(60)
        g_dhcp_status = 1

    #check network down
    if carrier == "0" and operstate=="down":
        if g_net_down_status == 1:
            print "bmchealth_check_network:  network down Fail"
            g_net_down_status = 0
            LogEventBmcHealthMessages("Asserted", "Network Error", "Link Down")
    else:
        if g_net_down_status == 0:
            LogEventBmcHealthMessages("Deasserted", "Network Error", "Link Down")
            time.sleep(60)
        g_net_down_status = 1

    if org_dhcp_status != g_dhcp_status:
        with open(record_dhcp_file_path, 'w') as f:
            f.write(str(g_dhcp_status))
    if org_down_status != g_net_down_status:
        with open(record_down_file_path, 'w') as f:
            f.write(str(g_net_down_status))
    return True

def bmchealth_fix_and_check_mac():
    print "fix-mac & fix-guid start"
    fix_mac_status = mac_guid.fixMAC()
    fix_guid_status = mac_guid.fixGUID()

    print "bmchealth: check mac status:" + str(fix_mac_status)
    print "bmchealth: check guid status:" + str(fix_guid_status)
    #check bmchealth macaddress
    ret = 0
    if fix_mac_status == 0 or fix_guid_status == 0:
        LogEventBmcHealthMessages("Asserted", "No MAC address programmed")
    return True

def reboot_check_flag():
    reboot_file_path = "/var/lib/obmc/check_reboot"
    global g_reboot_flag
    if os.path.exists(reboot_file_path):
        g_reboot_flag = 1
        os.remove(reboot_file_path)
    return True

def bmchealth_check_watchdog():
    global g_watchdog_reset
    print "check watchdog timeout start"
    check_watchdog1_command = "devmem 0x1e785010"
    check_watchdog2_command = "devmem 0x1e785030"
    clear_watchdog1_command = "devmem 0x1e785014 w 0x76"
    clear_watchdog2_command = "devmem 0x1e785034 w 0x76"

    #read event counters
    try:
        watchdog1_str_data = subprocess.check_output(check_watchdog1_command, shell=True)
        watchdog1_timeout_counter = (  int(watchdog1_str_data, 16) >> 8) & 0xff
    except:
        print "[bmchealth_check_watchdog]Error conduct operstate!!!"
        return False

    try:
        watchdog2_str_data = subprocess.check_output(check_watchdog2_command, shell=True)
        watchdog2_timeout_counter = ( int(watchdog2_str_data, 16) >> 8) & 0xff
    except:
        print "[bmchealth_check_watchdog]Error conduct operstate!!!"
        return False

    if watchdog1_timeout_counter > 0 or watchdog2_timeout_counter > 0:
        if g_reboot_flag == 0:
            print "Log watchdog expired event"
            LogEventBmcHealthMessages("Asserted", "Hardware WDT expired")
            g_watchdog_reset = 1
        subprocess.check_output(clear_watchdog1_command, shell=True)
        subprocess.check_output(clear_watchdog2_command, shell=True)
    return True

def bmchealth_check_i2c(NUM_BUS):
    global g_i2c_recovery
    i2c_recovery_event_map = {
        1: 0, # recover done
        2: 5, #0101b-Bus Busy
        4: 3, #0011b-Abnormal Start/Stop condition
        8: 4, #0100b-Aribitration Lost
        16:2, #0010b-SCL low timed out
        32:1, #0001b-Recovery Performed
    }

    i2c_recovery_base = "/proc/i2c_recovery_bus"
    for bus_id in range(NUM_BUS):
        i2c_recovery_path = i2c_recovery_base + str(bus_id)
        try:
            if i2c_recovery_path not in g_i2c_recovery:
                g_i2c_recovery[i2c_recovery_path]  = 0
            with open(i2c_recovery_path, 'r') as f:
                i2c_sts = int(f.readline())
            if (i2c_sts == 0) or (i2c_sts == (1<<6)):
                continue

            if ((i2c_sts&0x1) != 0x1) and ((i2c_sts& (1<<6)) != (1<<6)):
                if g_i2c_recovery[i2c_recovery_path] == 0: #i2c recovery assert
                    for event in i2c_recovery_event_map:
                        if (i2c_sts & event) == event:
                            LogEventBmcHealthMessages("Asserted", "I2C bus hang", data={'i2c_bus_id':bus_id, 'i2c_error_code':i2c_recovery_event_map[event]})
                            g_i2c_recovery[i2c_recovery_path] = 1
            if ((i2c_sts&0x1) == 0x1) or ((i2c_sts& (1<<6)) == (1<<6)):
                if g_i2c_recovery[i2c_recovery_path] == 1:
                     LogEventBmcHealthMessages("Deasserted", "I2C bus hang", data={'i2c_bus_id':bus_id, 'i2c_error_code':0})
                     g_i2c_recovery[i2c_recovery_path] = 0
        except:
            print "[bmchealth_check_i2c]exception !!!"
    return True

def bmchealth_check_fw_update_start():
    fw_update_start_check = "/var/lib/obmc/fw_update_start"
    psu_fw_update_start_check = "/var/lib/obmc/psu_fwupdate_record"
    fpga_fw_update_start_check =  "/var/lib/obmc/fpga_fwupdate_record"
    #check BMC fw update start
    if os.path.exists(fw_update_start_check):
        print "Log BMC FW update start"
        try:
            with open(fw_update_start_check, 'r') as f:
                partition = int(f.readline())
        except:
            print "[bmchealth_check_fw_update_start]exception !!!"
        LogEventBmcHealthMessages("Asserted", "Firmware Update Started","BMC Firmware Update Started",data={'index':partition})
        os.rename(fw_update_start_check, "/var/lib/obmc/fw_update_complete")
    #check PSU fw update start
    if os.path.exists(psu_fw_update_start_check):
        try:
            with open(psu_fw_update_start_check, 'r') as f:
                psu_id = int(f.readline())
                print"log psu_fwupdate_record psu_id = "+str(psu_id)
                LogEventBmcHealthMessages("Asserted", "Firmware Update Started","PSU Firmware Update Started",data={'index':psu_id})
                os.rename(psu_fw_update_start_check, "/var/lib/obmc/psu_fwupdate_complete")
        except:
                print "[bmchealth_check_fw_update_start]exception !!!"
    #check FPGA fw update start
    if os.path.exists(fpga_fw_update_start_check):
        try:
            with open(fpga_fw_update_start_check, 'r') as f:
                fpga_id = int(f.readline())
                print"log fpga_fwupdate_record fpga_id = "+str(fpga_id)
                LogEventBmcHealthMessages("Asserted", "Firmware Update Started","FPGA Firmware Update Started",data={'index':fpga_id})
                os.rename(fpga_fw_update_start_check, "/var/lib/obmc/fpga_fwupdate_complete")
        except:
                print "[bmchealth_check_fw_update_start]exception !!!"
    return True

def bmchealth_check_fw_update_complete():
    fw_update_complete_check = "/var/lib/obmc/fw_update_complete"
    psu_fw_update_complete_check = "/var/lib/obmc/psu_fwupdate_complete"
    fpga_fw_update_complete_check =  "/var/lib/obmc/fpga_fwupdate_complete"
    global g_reboot_flag
    #check BMC fw update complete
    if os.path.exists(fw_update_complete_check) and g_reboot_flag == 1:
        try:
            with open(fw_update_complete_check, 'r') as f:
                partition = int(f.readline())
        except:
                print "[bmchealth_check_fw_updata_complete]exception !!!"
        os.remove(fw_update_complete_check)
        print "Log BMC FW update completed"
        LogEventBmcHealthMessages("Asserted", "Firmware Update completed","BMC Firmware Update completed",data={'index':partition})
    #check PSU fw update complete
    if os.path.exists(psu_fw_update_complete_check):
        try:
            with open(psu_fw_update_complete_check, 'r') as f:
                psu_id = int(f.readline())
                print"log psu_fwupdate_record psu_id = "+str(psu_id)
                LogEventBmcHealthMessages("Asserted", "Firmware Update completed","PSU Firmware Update completed",data={'index':psu_id})
        except:
                print "[bmchealth_check_fw_updata_complete]exception !!!"
        os.remove(psu_fw_update_complete_check)
    #check FPGA fw update complete
    if os.path.exists(fpga_fw_update_complete_check):
        try:
            with open(fpga_fw_update_complete_check, 'r') as f:
                fpga_id = int(f.readline())
                print"log fpga_fwupdate_record fpga_id = "+str(fpga_id)
                LogEventBmcHealthMessages("Asserted", "Firmware Update completed","FPGA Firmware Update completed",data={'index':fpga_id})
        except:
                print "[bmchealth_check_fw_updata_complete]exception !!!"
        os.remove(fpga_fw_update_complete_check)
    return True

#To skip fwu case, it should be invoked before fw_update_complete removed
def bmchealth_check_bmc_reset():
    fw_update_complete_check = "/var/lib/obmc/fw_update_complete"
    reset_flag_file = '/var/lib/obmc/redfish_reset'
    check_scu3c_command = "devmem 0x1e6e203c 8"
    unlock_scu_command = "devmem 0x1e6e2000 32 0x1688a8a8"
    #lock_scu_command = "devmem 0x1e6e203c 32 0"
    reset_flag = redfish_reset = 0

    print "check system reset control/status register"

    if os.path.exists(reset_flag_file):
        redfish_reset = 1
        os.remove(reset_flag_file)

    try:
        scu3c_str_data = subprocess.check_output(check_scu3c_command, shell=True)
        reset_flag = int(scu3c_str_data, 16) & 0xe
        if g_watchdog_reset == 1:
            reset_flag = reset_flag & 0xc
        if reset_flag != 0:
            if os.path.exists(fw_update_complete_check):
                return True
            subprocess.check_output(unlock_scu_command, shell=True)
            clear_scu3c = "devmem 0x1e6e203c 32 %d" % (int(scu3c_str_data, 16) & 1)
            subprocess.check_output(clear_scu3c, shell=True)
            #subprocess.check_output(lock_scu_command, shell=True)
    except:
        print "[bmchealth_check_bmc_reset] exception!!!"
        return False

    if reset_flag == 0:
        return True;

    if redfish_reset == 1:
        LogEventBmcHealthMessages("Asserted", "BMC Reset", "Redfish Reset")
    else:
        LogEventBmcHealthMessages("Asserted", "BMC Reset", "Register/Pin Reset")
    return True

def bmchealth_check_log_rollover():
    current_log_rollover =  bmclogevent_ctl.bmclogevent_get_log_rollover()
    global g_previous_log_rollover
    if g_previous_log_rollover == -1:
        g_previous_log_rollover =0
    if current_log_rollover > g_previous_log_rollover:
        print "Log Log Rollover event"
        LogEventBmcHealthMessages("Asserted", "Log Rollover","Log Rollover",data={'log_rollover_count':current_log_rollover})
        g_previous_log_rollover = current_log_rollover
    if current_log_rollover == 0 and g_previous_log_rollover != 0:
        bmclogevent_ctl.bmclogevent_set_value("/org/openbmc/sensors/bmc_health", 0, offset=0xb)
        g_previous_log_rollover = current_log_rollover
    return True

def bmchealth_check_CPU_utilization():
    CPU_stat_path = "/proc/stat"
    global g_CPU_utilization
    global g_previous_total
    global g_previous_idle_cpu

    try:
        with open(CPU_stat_path, 'r') as f:
            cpu = f.readline()
            usr_cpu = float(cpu.split(' ')[2])
            nice_cpu = float(cpu.split(' ')[3])
            system_cpu = float(cpu.split(' ')[4])
            idle_cpu = float(cpu.split(' ')[5])
            iowait_cpu = float(cpu.split(' ')[6])
            irq_cpu = float(cpu.split(' ')[7])
            softirq_cpu = float(cpu.split(' ')[8])
            current_total = usr_cpu + nice_cpu + system_cpu + idle_cpu + iowait_cpu + irq_cpu + softirq_cpu
            if g_CPU_utilization == -1:
                g_CPU_utilization =0
                g_previous_total = current_total
                g_previous_idle_cpu = idle_cpu
                return True
            total = current_total-g_previous_total
            idle = idle_cpu-g_previous_idle_cpu
            cpu_usage =  (total - idle)/total
            g_previous_total = current_total
            g_previous_idle_cpu = idle_cpu
            cpu_utilization = int(cpu_usage*100)
            if cpu_utilization >= 80 and g_CPU_utilization == 0:
                print "Log Asserted CPU utilization"
                LogEventBmcHealthMessages("Asserted", "BMC CPU utilization","BMC CPU utilization",data={'cpu_utilization':cpu_utilization})
                g_CPU_utilization = 1
            elif cpu_utilization < 80 and g_CPU_utilization == 1:
                print "Log Deasserted CPU utilization"
                LogEventBmcHealthMessages("Deasserted", "BMC CPU utilization","BMC CPU utilization",data={'cpu_utilization':cpu_utilization})
                g_CPU_utilization = 0
                time.sleep(60)
    except:
            print "[bmchealth_check_CPU_utilization]exception !!!"
    return True

def bmchealth_check_memory_utilization():
    meminfo_path = "/proc/meminfo"
    global g_memory_utilization
    if g_memory_utilization == -1:
        g_memory_utilization = 0
    try:
        with open(meminfo_path, 'r') as f:
            line1 = f.readline()
            line2 = f.readline()
            memory_total = float(line1.replace(' ', '').split(':')[1].split('k')[0])
            memory_free = float(line2.replace(' ', '').split(':')[1].split('k')[0])
            memory_used = memory_total - memory_free
            memory_usage = memory_used / memory_total
            memory_utilization = int(memory_usage*100)
            memory_utilization_in_MB = int(memory_used/1000)
            if memory_utilization >= 80 and g_memory_utilization == 0:
                print "Log Asserted memory utilization"
                LogEventBmcHealthMessages("Asserted", "BMC Memory utilization","BMC Memory utilization",data={'memory_utilization':memory_utilization_in_MB})
                g_memory_utilization = 1
            elif memory_utilization < 80 and g_memory_utilization == 1:
                print "Log Deasserted memory utilization"
                LogEventBmcHealthMessages("Deasserted", "BMC Memory utilization","BMC Memory utilization",data={'memory_utilization':memory_utilization_in_MB})
                g_memory_utilization = 0
                time.sleep(60)
    except:
            print "[bmchealth_check_memory_utilization]exception !!!"
    return True

def bmchealth_check_empty_invalid_fru():
    global g_record_fru_status
    if 'ID_LOOKUP' not in dir(System):
        return False
    if 'FRU_STR' not in System.ID_LOOKUP  or 'FRU_SLAVE' not in System.ID_LOOKUP:
        return False

    check_fru_empty_item = [
        'Chassis_Info_Area_Length',
        'Board_Area_Length',
        'Product_Area_Length',
    ]

    check_fru_invalid_item = [
        'Chassis_CheckSum',
        'Board_Area_Checksum',
        'Product_Area_Checksum',
    ]

    for fur_item in System.ID_LOOKUP['FRU_STR']:
        if fur_item in System.ID_LOOKUP['FRU_SLAVE']:
            i2c_bus = System.ID_LOOKUP['FRU_SLAVE'][fur_item]['I2C_BUS']
            i2c_slave = System.ID_LOOKUP['FRU_SLAVE'][fur_item]['I2C_SLAVE']
            fru_id = int(fur_item.split("_")[1])

            # read fru to check fru data is correct with 'ocs-fru' or 'phosphor-read-eeprom'
            fru_chk_status = 0
            fru_chk_len_status = -1
            fru_chk_cmd = 'ocs-fru -c '+str(i2c_bus)+' -s '+ hex(i2c_slave)+' -r'
            cmd_msg = subprocess.check_output(fru_chk_cmd, shell=True)
            sp_cmd = cmd_msg.split("\n")
            for cmd_line in sp_cmd:
                sp_cmd_line = cmd_line.rstrip('\n').split(":")
                if len(sp_cmd_line) == 2:
                    #check if fru is invalid
                    ocs_fru_item = sp_cmd_line[0]
                    ocs_fru_val = sp_cmd_line[1]
                    try:
                        index = check_fru_invalid_item.index(ocs_fru_item)
                        if ocs_fru_val == 'ERROR':
                            fru_chk_status = -1
                            break
                    except:
                        pass
                    #check if fru is empty
                    try:
                        index = check_fru_empty_item.index(ocs_fru_item)
                        if int(ocs_fru_val) > 0:
                            fru_chk_len_status = 0
                    except:
                        pass
            if fru_chk_len_status != 0:
                fru_chk_status = -1
            if fur_item not in g_record_fru_status:
                g_record_fru_status[fur_item]  = 0
            if fru_chk_status != 0 and g_record_fru_status[fur_item] == 0: #assert
                LogEventBmcHealthMessages("Asserted", "Empty Invalid FRU",data={'fru_id':fru_id})
                g_record_fru_status[fur_item] = 1
            elif fru_chk_status == 0 and g_record_fru_status[fur_item] == 1: #deassert
                LogEventBmcHealthMessages("Deasserted", "Empty Invalid FRU",data={'fru_id':fru_id})
                g_record_fru_status[fur_item] = 0
    return True

def bmchealth_check_alignment_traps():
    alignment_traps_check_path = "/proc/cpu/alignment"
    global g_number_of_system

    #read alignment proc file content
    try:
        with open(alignment_traps_check_path, 'r') as f:
            content = f.read()
            sp_line = content.split("\n")
            #parse user faults
            fault_state = int(sp_line[7].split(":")[1].lstrip()[0])
            #parse system NO.
            number_of_system = int(sp_line[0].split(":")[1].lstrip())
            number_of_system_lsb = ( number_of_system >> 8) & 0xff
            number_of_system_msb = number_of_system & 0xff
            #log event if fault states and process id change
            if fault_state != 0 and number_of_system > g_number_of_system:
                print "Log alignment traps event"
                LogEventBmcHealthMessages("Asserted", "Alignment Traps",data={'alignment_msb':number_of_system_msb, 'alignment_lsb':number_of_system_lsb})
                g_number_of_system = number_of_system
    except:
            print "[bmchealth_check_alignment_traps]exception !!!"
    return True

def watch_redfish():
    flag_redfish_start = 0
    watchdog_file_path = "/run/obmc/watch_redfish"
    if os.path.exists(watch_redfish_start_file):
        with open(watch_redfish_start_file, "r") as f:
            try:
                flag_redfish_start=int(f.readline().rstrip('\n'))
            except:
                flag_redfish_start = 0
    if flag_redfish_start != 0 and os.path.exists(watchdog_file_path):
        cmd = 'netstat -nl | grep ":443 "'
        cmd_msg = subprocess.check_output(cmd, shell=True)
        if len(cmd_msg) > 0:
            os.remove(watchdog_file_path)
    return True

def watch_event_service():
    watchdog_file_path = "/run/obmc/watch_event_service"
    try:
        bmclogevent_ctl.bmclogevent_get_log_rollover()
    except:
        print 'Unexpected error:', sys.exc_info()[0]
    else:
        if os.path.exists(watchdog_file_path):
            os.remove(watchdog_file_path)
    return True

def bmchealth_kick_watchdog():

    watch_redfish()
    watch_event_service()

    return True

def bmchealth_check_boot_spi():
    check_spi_command = "devmem 0x1e785030"
    try:
        watchdog_timeout_status = subprocess.check_output(check_spi_command, shell=True)
        boot_spi = (  int(watchdog_timeout_status, 16) >> 1) & 0x1
        if boot_spi == 0:
            print "Log Boot SPI Primary"
            LogEventBmcHealthMessages("Asserted", "Firmware Boot SPI Flash Primary")
        elif boot_spi == 1:
            print "Log Boot SPI Secondary"
            LogEventBmcHealthMessages("Asserted", "Firmware Boot SPI Flash Secondary")
    except:
            print "[bmchealth_check_boot_spi]exception !!!"
    return True

def bmchealth_check_ecc_errors():
    check_ecc_error_command = "devmem 0x1e6e0050"

    try:
        ecc_status = subprocess.check_output(check_ecc_error_command, shell=True)
        re_count = (  int(ecc_status, 16) >> 16) & 0xff
        ue_count = (  int(ecc_status, 16) >> 12) & 0xf
        if re_count+ue_count > 0:
            print "Log DRAM ECC Errors"
            LogEventBmcHealthMessages("Asserted", "DRAM ECC errors")
            clear_ecc_counts_command = "devmem 0x1e6e0050 32 %d" % (int(ecc_status, 16) | (1<<31))
            subprocess.check_output(clear_ecc_counts_command, shell=True)
            reset_ecc_status_command = "devmem 0x1e6e0050 32 %d" % (int(ecc_status, 16))
            subprocess.check_output(reset_ecc_status_command, shell=True)
    except:
            print "[bmchealth_check_ecc_errors]exception !!!"
    return True

def save_pid():
    pid = os.getpid()
    try:
        with open('/run/bmchealth_handler.pid', 'w') as pidfile:
            print >>pidfile, pid
    except IOError:
        print >>sys.stderr, 'failed to open pidfile'

if __name__ == '__main__':
    save_pid()
    mainloop = glib.MainLoop()
    #set bmchealth default value
    bmclogevent_ctl.bmclogevent_set_value(g_bmchealth_obj_path, 0)
    reboot_check_flag()
    bmchealth_fix_and_check_mac()
    bmchealth_check_watchdog()
    bmchealth_check_bmc_reset() # Before check fwu, after check watchdog
    bmchealth_check_fw_update_complete()
    bmchealth_check_boot_spi()
    glib.timeout_add_seconds(5,bmchealth_check_network)
    glib.timeout_add_seconds(1,bmchealth_check_fw_update_start)
    glib.timeout_add_seconds(5,bmchealth_check_i2c, 5)
    glib.timeout_add_seconds(5,bmchealth_check_log_rollover)
    glib.timeout_add_seconds(5,bmchealth_check_memory_utilization)
    glib.timeout_add_seconds(20,bmchealth_check_empty_invalid_fru)
    glib.timeout_add_seconds(5,bmchealth_check_CPU_utilization)
    glib.timeout_add_seconds(5,bmchealth_check_alignment_traps)
    glib.timeout_add_seconds(5,bmchealth_check_ecc_errors)
    glib.timeout_add_seconds(10,bmchealth_kick_watchdog)
    print "bmchealth_handler control starting"
    mainloop.run()

