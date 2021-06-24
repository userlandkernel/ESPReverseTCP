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


// connects to listener, forwards input to target
int createReverseTcpTunnel(const char* targetDomain, int targetPort,  const char* listenerDomain, int listenerPort)
{
  int targetSock = -1;
  int listenerSock = -1;
  char targetDataBuffer[256] = {};
  char listenerDataBuffer[256] = {};
  
  targetSock = createIPv4Socket(targetDomain, targetPort);
  if(targetSock < 0) {
    Serial.println("Failed to create socket for target server.");
    return -1;
  }

  listenerSock = createIPv4Socket(listenerDomain, listenerPort);
  if(listenerSock < 0) {
    Serial.println("Failed to create socket for listener server.");
    return -1;
  }


  
  // FORWARD REQUEST TO TARGET
  Serial.println("Sending data from listener -> target...");
  ssize_t count = 0;
  while( (count = recv(listenerSock, listenerDataBuffer, sizeof(listenerDataBuffer), MSG_WAITALL)) ) {
    if (count == -1) {
      Serial.println("An error has occured.\n");
      break;
    }
    Serial.printf("Got packet of %d bytes, forwarding it to target...\n", count);
    write(targetSock, listenerDataBuffer, count);
    bzero(listenerDataBuffer, sizeof(listenerDataBuffer));
  }

  Serial.println("Sending data from target -> listener...");
  // FORWARD RESPONSE TO LISTENER
  count = 0;
  while( (count = recv(targetSock, targetDataBuffer, sizeof(targetDataBuffer), MSG_WAITALL)) ) {
    if (count == -1) {
      Serial.println("An error has occured.\n");
      break;
    }
    Serial.printf("Got packet of %d bytes, forwarding it to listener...\n", count);
    write(listenerSock, targetDataBuffer, count);
    bzero(targetDataBuffer, sizeof(targetDataBuffer));
  }

  Serial.println("Tunnel communication has completed, cleaning up...");
  bzero(targetDataBuffer, sizeof(targetDataBuffer));
  bzero(listenerDataBuffer, sizeof(listenerDataBuffer));
  
  close(listenerSock);
  close(targetSock);

  return 0;
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
  int err = createReverseTcpTunnel("ah.nl", 443, "kernelium.com", 1337);
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