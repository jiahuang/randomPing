# listen to an arduino over serial port

import serial
import os
arduino = serial.Serial('/dev/tty.usbmodem1411', 57600) # serial port# and baud rate

while 1:
  data = arduino.readline()
  print data