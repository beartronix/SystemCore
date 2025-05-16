#!/usr/bin/env python

import traceback
import serial
import struct
import sys
import socket
import select
from threading import Thread, Lock
from typing import List
from time import sleep
import datetime

from signal import signal, SIGINT

args = sys.argv

pathComPort = '/dev/ttyUSB0'
if len(args) > 1:
	pathComPort = args[1]

comPort = serial.Serial(pathComPort, 38400, timeout=0.2)
comPort.reset_input_buffer()


ID_TREE = 'T'
ID_LOG  = 'L'
ID_BIN  = 'B'

class Srv:
	def __init__(self, port):
		self.port = port

		self.shutdown = False

		self.actConns : List[socket.socket] = []

		# self.main()
		self.thr = Thread(target=self.main, name=str(self.port))
		self.thr.start()

	def main(self):
		self.sock = socket.socket(socket.AF_INET, socket.SocketKind.SOCK_STREAM)
		self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
		self.sock.setblocking(0)
		self.sock.bind(('0.0.0.0', self.port))
		self.sock.listen(2)

		print(f'Listening on port {self.port}...')
		while not self.shutdown:
			sockRdOk, sockWrOk, sockErr = select.select([self.sock], [], [], 0.005)
			if not self.sock in sockRdOk:
				continue

			conn, addr = self.sock.accept()
			print(f'Accepted Connection from {addr[0]}:{addr[1]}')
			self.actConns.append(conn)

		for conn in self.actConns:
			self.disconnect(conn)

	def disconnect(self, conn : socket.socket):
		self.actConns.remove(conn)
		conn.shutdown(socket.SHUT_RDWR)
		conn.close()

	def send(self, data):
		sockRdOk, sockWrOk, sockErr = select.select([], self.actConns, [], 0.005)
		for conn in sockWrOk:
			conn : socket.socket = conn
			try:
				conn.sendall(data)
			except Exception as e:
				print(f"Conn shutdown: {e}")
				self.disconnect(conn)

servers = {
	ID_TREE : Srv(3030),
	ID_LOG  : Srv(3031),
	ID_BIN  : Srv(3032)
}

comPort.reset_input_buffer()

data = b''
while True:
	try:
		data += comPort.read(2048) # somehow, this doesn't work for tree, only for binary and log
		# data += comPort.readall()

		if not data:
			continue

		msgId = data[0].to_bytes().decode()
		msg = b''


		if msgId == ID_LOG:
			msgEnd = data.find('\r\n'.encode())
			if msgEnd == -1:
				continue
			msg = b'\033[37m' + data[1:msgEnd+2]

		elif msgId == ID_TREE:
			msgEnd = data.find('\r\n\r\n'.encode())
			if msgEnd == -1:
				continue
			msg = b'\033\143\033[37m' + data[1:msgEnd+4]

		elif msgId == ID_BIN:
			size = struct.unpack('h', data[1:3])[0]
			msgEnd = 3+size
			msg = data[3:msgEnd]
			# print(f"Got {len} Bytes: {msg}")
		else:
			data = b''
			continue

		data = data[msgEnd:]

		while len(data) and ((data[0].to_bytes() == '\r'.encode()) or data[0].to_bytes() == '\n'.encode()):
			data = data[1:]

		servers[msgId].send(msg)

	except KeyboardInterrupt as k:
		for srv in servers.values():
			print(f"Shutdown server on port {srv.thr.name}")
			srv.shutdown = True
			srv.thr.join()
		sys.exit(1)
	except Exception as exc:
		print(f"{datetime.datetime.now().strftime("%H:%M:%S")} EXCEPTION (id {msgId}): {traceback.format_exc()}")
		data = b''
		# sleep(0.001)
