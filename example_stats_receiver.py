#!/usr/bin/env python3

import logging
logging.basicConfig(level=logging.DEBUG)
import sys
import os
import os.path
import socket
import argparse
import yaml

parser = argparse.ArgumentParser(
    description="Example Stats UNIX Datagram Socket Receiver")
parser.add_argument('-s', '--socket', default="/tmp/stats", type=str,
        help='Full path of the socket',
        required=False)

cli_args = parser.parse_args()

sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)

if os.path.exists(cli_args.socket):
    try:
        os.unlink(cli_args.socket)
    except OSError:
        logging.warning("Could not unlink socket %s", cli_args.socket)

sock.bind(cli_args.socket)

logging.info("Starting receiver using socket '{}'".format(cli_args.socket))


while True:
    data, addr = sock.recvfrom(256)

    logging.info("RX from {}". format(addr))
    data = yaml.load(data)
    print(data)
