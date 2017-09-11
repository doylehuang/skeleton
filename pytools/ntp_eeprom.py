#!/usr/bin/env python

import getopt
import sys
import string
import os
import subprocess

g_eeprom_path = "/sys/devices/platform/ahb/ahb:apb/1e78a000.i2c/i2c-4/i2c-4/4-0050/eeprom"
g_ntp_offset = 0x2030

def readNTP():
  f=open(g_eeprom_path, 'r')
  f.seek(g_ntp_offset, 0)
  ntp = f.read(4)
  f.close()
  node = ntp.encode('hex')
  blocks = [str(int(node[x:x+2], 16)) for x in xrange(0, len(node), 2)]
  ntpServer = '.'.join(blocks)
  print "read ntp offset:0x2030 from eeprom: %s" %ntpServer
  return ntpServer

def checkNTP():
  ntp_addr = readNTP()
  if ntp_addr == "0.0.0.0" or ntp_addr == "255.255.255.255":
    print "ntp addr in eeprom is invalid, update default address in eeprom (%s)" %ntp_addr
    writeNTP("AC1100CA")
  else:
    print "found ntp addr in eeprom, restore it to ntp.conf"
  return 0

def writeNTP(ntpaddr):
  if ntpaddr == None:
    ntpaddr = raw_input("Please input NTP Address->")
    print("Your NTP:",ntpaddr)
  ntp =[]
  write_ntp=[]
  for i in range( 0, 8 ):
      if '0'<=ntpaddr[i] and ntpaddr[i] <= '9':
          ntp.append(int(ord(ntpaddr[i])-ord('0')))
      elif 'A'<=ntpaddr[i] and ntpaddr[i] <= 'F':
          ntp.append(int(ord(ntpaddr[i])-ord('A')+10))

  for i in range(0,8,2):
      j = (ntp[i]<<4 | ntp[i+1])
      write_ntp.append(j)

  data=bytearray(write_ntp)
  print "Write NTP Address to EEPROM !!"
  f = open(g_eeprom_path, "wb")
  f.seek(g_ntp_offset,0)
  f.write(data)
  f.seek(g_ntp_offset+len(data), 0)
  f.close()
  print "Done."

def usage():
  return str('Usage: ' + sys.argv[0] + ' [--help]' + ' [--check-ntp]' + ' [--write-ntp]'
                                     + ' [--read-ntp] \n'
                                     + '--help         : print help messages. \n'
                                     + '--check-ntp    : check ntp server address in eeprom. \n'
                                     + '--write-ntp    : write ntp server address to eeprom. \n'
                                     + '--read-ntp     : read ntp server address from eeprom.')

def main():
  try:
      opts, args=getopt.getopt(sys.argv[1:], "h", ["help", "check-ntp", "write-ntp", "read-ntp"])
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
      elif opt == "--check-ntp":
          checkNTP()
          sys.exit()
      elif opt == "--write-ntp":
          writeNTP(None)
          sys.exit()
      elif opt == "--read-ntp":
          readNTP()
          sys.exit()
      else:
          assert False, "unhandled option"

if __name__ == "__main__":
  main()
