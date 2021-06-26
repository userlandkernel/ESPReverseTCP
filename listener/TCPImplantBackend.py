import _thread
import socket
import struct
import sys
import time
import select

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
