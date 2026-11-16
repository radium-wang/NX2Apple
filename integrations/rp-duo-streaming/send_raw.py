#!/usr/bin/env python3
"""Diagnostic snapshots, not a game adapter and not a motor playback test."""
import argparse
import os
import socket
import struct
import time

p=argparse.ArgumentParser()
p.add_argument('--socket', default=f'/private/tmp/rpduo-stream-{os.getuid()}/haptics.sock')
a=p.parse_args()
s=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM)
# Distinct left/right bands reveal any accidental mixing or field reordering.
for seq, hz in enumerate((130.81,146.83,164.81,174.61,196.00,220.00,246.94,261.63),1):
    bands=(0.25,hz,0.5,hz*2,0.75,hz+7,1.0,hz*2+7)
    frame=struct.pack('<4sHHIQQI8f',b'RPH1',1,0,seq,1,time.monotonic_ns()//1000,200000,*bands)
    s.sendto(frame,a.socket)
    time.sleep(0.05)
stop=struct.pack('<4sHHIQQI8f',b'RPH1',1,0,9,1,time.monotonic_ns()//1000,200000,*([0.0]*8))
s.sendto(stop,a.socket)
print('Sent 8 distinct raw snapshots and a stop; verify RpRawHaptics on the client.')
