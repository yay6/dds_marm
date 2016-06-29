#!/usr/bin/env python

import argparse
import os
import socket
import struct
import sys

DDS_DATA_FORMATS = ['8bit', '12bit_LEFT', '12bit_RIGHT']
DDS_MODES = ['independent', 'single_trigger', 'dual']

DDS_HEADER_STR = '<4cIIB'
DDS_CHCONFIG_STR = '<BBIIIH'

def create_header(mode, size):
    return struct.pack(DDS_HEADER_STR,
                       b'M', b'A', b'R', b'M', 
                       0, 
                       size,
                       mode)
    
def create_chconfig(enabled, format=0, offset=0, size=0, period=0, prescaler=0):
    return struct.pack(DDS_CHCONFIG_STR,
                       enabled,
                       format,
                       offset,
                       size,
                       period,
                       prescaler)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='MARM_DDS client.')
    parser.add_argument('address', help='IP address of MARM_DDS device')
    parser.add_argument('file',  type=argparse.FileType('rb'), help='file with samples')
    parser.add_argument('--mode', choices=DDS_MODES,
                        default=DDS_MODES[1], help='DDS mode')
    parser.add_argument('--format', choices=DDS_DATA_FORMATS, 
                        default=DDS_DATA_FORMATS[0], help='samples format')
    parser.add_argument('--period', type=int, default=1, help='DAC period')
    parser.add_argument('--prescaler', type=int, default=1, help='DAC prescaler')
    
    args = parser.parse_args()
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    server_address = (args.address, 1234)
    sock.connect(server_address)
    
    file1_size = os.fstat(args.file.fileno()).st_size
    frame_size = file1_size + struct.calcsize(DDS_HEADER_STR) + struct.calcsize(DDS_CHCONFIG_STR) 
    
    frame = []   
    frame.append(create_header(1, frame_size))
    frame.append(create_chconfig(1, DDS_DATA_FORMATS.index(args.format), 0, file1_size, args.period, args.prescaler))
    frame.append(create_chconfig(0))
    frame.append(args.file.read())
    
    try:  
        sock.sendall(b''.join(frame))
        data = sock.recv(128)
        print(data)

    finally:
        sock.close()