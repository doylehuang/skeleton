#!/usr/bin/env python

# **************************************************************
# *                                                            *
# *   Copyright (C) Microsoft Corporation. All rights reserved.*
# *                                                            *
# **************************************************************

import getopt
import sys
import string
import uuid
import os

g_allchars = "".join(chr(a) for a in range(256))
g_delchars = set(g_allchars) - set(string.hexdigits)
g_eeprom_path = "/sys/devices/platform/ahb/ahb:apb/1e78a000.i2c/i2c-4/i2c-4/4-0050/eeprom"
g_mac_offset = 0x2000
g_guid_offset = 0x2010

def checkMAC(s):
  mac = s.translate("".join(g_allchars),"".join(g_delchars))
  if len(mac) != 12:
      raise ValueError, ("Ethernet MACs are always 12 hex characters, you entered %s",mac)
  return mac.upper()

def calChecksum(cal_data, data_len):
  sum = 0
  for i in range (0, data_len, 1):
      sum = sum + cal_data[i]

  sum = (~sum & 0xff) + 1;
  print "8-bit 2's complement checksum: %s" %(hex(sum))

  return sum

def writeMAC(macaddr):
  if macaddr == None:
    macaddr = raw_input("Please input MAC Address->")
    macaddr = checkMAC(macaddr)
    print("Your MAC:",macaddr)
  mac =[]
  write_mac=[]
  for i in range( 0, 12 ):
      if '0'<=macaddr[i] and macaddr[i] <= '9':
          mac.append(int(ord(macaddr[i])-ord('0')))
      elif 'A'<=macaddr[i] and macaddr[i] <= 'F':
          mac.append(int(ord(macaddr[i])-ord('A')+10))

  for i in range(0,12,2):
      j = (mac[i]<<4 | mac[i+1])
      write_mac.append(j)

  data=bytearray(write_mac)
  checksum = calChecksum(data, len(data))
  print "Write MAC Address to EEPROM !!"
  f = open(g_eeprom_path, "wb")
  f.seek(g_mac_offset,0)
  f.write(data)
  f.seek(g_mac_offset+len(data), 0)
  f.write(chr(checksum))
  f.close()
  print "Done."

def writeGUID():
  f = open(g_eeprom_path, 'rb+')
  f.seek(g_mac_offset, 0)
  mac = f.read(6)
  node = int(mac.encode('hex'), 16)
  guid = uuid.uuid1(node)
  print "v1 UUID: %s" %(guid)
  data = bytearray(guid.bytes)
  checksum = calChecksum(data, len(data))
  print "Write UUID to EEPROM !!"
  f.seek(g_guid_offset, 0)
  f.write(data)
  f.seek(g_guid_offset+len(data), 0)
  f.write(chr(checksum))
  f.close()
  print "Done."

def genGUID():
  print uuid.uuid1()

def readMAC(count):
  f=open(g_eeprom_path, 'r')
  f.seek(g_mac_offset, 0)
  mac = f.read(count)
  f.close()
  node = mac.encode('hex')
  blocks = [node[x:x+2] for x in xrange(0, len(node), 2)]
  macFormatted = ':'.join(blocks)
  print macFormatted
  return macFormatted

def readGUID():
  f=open(g_eeprom_path, 'r')
  f.seek(g_guid_offset, 0)
  guid = f.read(16)
  f.close()
  guid_hex = guid.encode('hex')
  print uuid.UUID(guid_hex)

def fixMAC():
  data = readMAC(7)
  mac_addr_sp = data[:17].split(":")
  checksum = int(data[18:], 16)
  mac_addr = []
  for i in range(len(mac_addr_sp)):
    mac_addr.append(int(mac_addr_sp[i], 16))
  calc_checksum = calChecksum(mac_addr, len(mac_addr))
  if checksum != calc_checksum :
    writeMAC("0003FF000000")
    writeGUID()
    return 0
  else:
    return 1


def fixGUID():
  f=open(g_eeprom_path, 'r')
  f.seek(g_guid_offset, 0)
  data = f.read(17)
  f.close()
  data_hex = data.encode('hex')
  guid_str = data_hex[:32]
  checksum = int(data_hex[32:], 16)
  guid = []
  for i in range(16):
    guid.append(int(guid_str[i*2:i*2+2], 16))
  calc_checksum = calChecksum(guid, len(guid))
  if checksum != calc_checksum:
    writeGUID()
    return 0
  else:
    return 1

def usage():
  return str('Usage: ' + sys.argv[0] + ' [--help]' + ' [--write-mac]' + ' [--write-guid]'
                                     + ' [--read-mac]' + ' [--read-guid]' + ' [--guid]\n'
                                     + '--help         : print help messages. \n'
                                     + '--write-mac    : write bmc mac address to eeprom. \n'
                                     + '--write-guid   : write version 1 uuid to eeprom. \n'
                                     + '--read-mac     : print mac address from eeprom. \n'
                                     + '--read-guid    : print guid from eerpom. \n'
                                     + '--guid         : generate veriosn 1 uuid.\n'
                                     + '--fix-mac      : inspect checksum. if checksum error, set default mac address: 00:03:FF:00:00:00\n'
                                     + '--fix-guid      : inspect checksum. if checksum error, set default guid')

def main():
  try:
      opts, args=getopt.getopt(sys.argv[1:], "h", ["help", "write-mac", "write-guid", "read-mac", "read-guid", "guid", "fix-mac", "fix-guid"])
  except getopt.GetoptError as err:
      print usage()
      sys.exit(2)
  if len(opts) < 1:
      print usage()
      sys.exit(2)

  for opt, arg in opts:
      if opt in ("-h", "--help"):
          print usage()
          sys.exit()
      elif opt == "--write-mac":
          writeMAC(None)
          sys.exit()
      elif opt == "--write-guid":
          writeGUID()
          sys.exit()
      elif opt == "--read-mac":
          readMAC(6)
          sys.exit()
      elif opt == "--read-guid":
          readGUID()
          sys.exit()
      elif opt == "--guid":
          genGUID()
          sys.exit()
      elif opt == "--fix-mac":
          fixMAC()
          sys.exit()
      elif opt == "--fix-guid":
          fixGUID()
          sys.exit()
      else:
          assert False, "unhandled option"

if __name__ == "__main__":
  main()
