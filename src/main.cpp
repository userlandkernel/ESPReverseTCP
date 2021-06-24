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
#include "config.h"

const char* ssid = WLAN_SSID; // edit config.h
const char* password = WLAN_PASS;  //edit config.h

// connects to listener, forwards input to target
int createReverseTcpTunnel(const char* targetServer, int targetPort,  const char* listenerServer, int listenerPort)
{
  int targetSock = -1;
  int listenerSock = -1;
  char targetDataBuffer[256] = {};
  char listenerDataBuffer[256] = {};
  struct sockaddr_in targetServerAddr = {};
  struct sockaddr_in listenerServerAddr = {};
  struct hostent *targetServerHE = NULL;
  struct hostent *listenerServerHE = NULL;

  Serial.println("Resolving DNS names...");

  /* Try to resolve the DNS name of the target server */
  if ((targetServerHE = gethostbyname(targetServer)) == NULL) {
    Serial.println("DNS resolution failed for target server.\n");
    return -1;
  }

  memcpy(&targetServerAddr.sin_addr, targetServerHE->h_addr, sizeof(targetServerAddr));

  delay(100);

  /* Try to resolve the DNS name of the listener server */
  if ((listenerServerHE = gethostbyname(listenerServer)) == NULL) {
    Serial.println("DNS resolution failed for listener server.\n");
    return -1;
  }

  Serial.println("Setting up socket address.\n");
 
  memcpy(&listenerServerAddr.sin_addr, listenerServerHE->h_addr, sizeof(listenerServerAddr));

    // Setup socket addresses
  targetServerAddr.sin_family = AF_INET;
  targetServerAddr.sin_port = htons(targetPort);

  listenerServerAddr.sin_family = AF_INET;
  listenerServerAddr.sin_port = htons(listenerPort);

  // Print resolved IP addresses
  char targetIP[INET_ADDRSTRLEN] = {};
  char listenerIP[INET_ADDRSTRLEN] = {};
  inet_ntop(AF_INET, &(targetServerAddr.sin_addr), targetIP, INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &(listenerServerAddr.sin_addr), listenerIP, INET_ADDRSTRLEN);

  Serial.printf("Tunneling TCP %s:%d -> %s:%d\n", targetIP, targetPort, listenerIP, listenerPort);

  /* Create the sockets */
  targetSock = socket(AF_INET, SOCK_STREAM, 0);
  listenerSock = socket(AF_INET, SOCK_STREAM, 0);

  if(targetSock < 0) {
    Serial.println("Failed to create socket for target server.");
    return -1;
  }

  if(listenerSock < 0) {
    Serial.println("Failed to create socket for listener server.");
    return -1;
  }

  /* Connect to the lisenter server */
  int attempt = 0;
  Serial.println("Connecting to listener server...");
  while (true) {
    if( connect(listenerSock, (struct sockaddr*)&listenerServerAddr, sizeof(listenerServerAddr)) < 0 ) {
      Serial.println("Failed to connect to listener server.");
      attempt++;
      if(attempt < 3) {
        Serial.println("Retrying...");
        continue;
      }
      else {
        Serial.println("Failed to connect to listener server: Maximum retries exceeded, giving up.");
        close(targetSock);
        close(listenerSock);
        return -1;
      }
      delay(5000); // Wait 5 seconds, then retry
    }
    Serial.println("Connected to the listener server!");
    break;
  }

  /* Connect to the target server */
  attempt = 0;
  while (true) {
    if( connect(targetSock, (struct sockaddr*)&targetServerAddr, sizeof(targetServerAddr)) < 0 ) {
      Serial.println("Failed to connect to target server.");
      attempt++;
      if(attempt < 3) {
        Serial.println("Retrying...");
        continue;
      }
      else {
        Serial.println("Failed to connect to target server: Maximum retries exceeded, giving up.");
        close(targetSock);
        close(listenerSock);
        return -1;
      }
      delay(5000); // Wait 5 seconds, then retry
    }
    Serial.println("Connected to the target server!");
    break;
  }

  uint32_t packetSize = 0;
  while(read(listenerSock, &packetSize, sizeof(packetSize)) != sizeof(uint32_t)) {

  }
  Serial.printf("Got packet of %d bytes..\n", packetSize);
  unsigned int chunks = packetSize / sizeof(listenerDataBuffer);
  if(packetSize % sizeof(listenerDataBuffer)) {
    chunks++;
  }

  Serial.printf("Receiving and forwarding %d chunks...\n", chunks);
  for(int i = 0; i < chunks; i++) {
    bzero(listenerDataBuffer, sizeof(listenerDataBuffer));
    Serial.printf("Receiving chunk %d/%d...\n", i, chunks);
    read(listenerSock, listenerDataBuffer, sizeof(listenerDataBuffer));
    Serial.printf("Forwarding chunk %d/%d...\n", i, chunks);
    write(targetSock, listenerDataBuffer, sizeof(listenerDataBuffer));
  }

  // Keep reading from the target server until no data has been received
  while(read(targetSock, targetDataBuffer, sizeof(targetDataBuffer))) {
    //Forward the response to the listener
    Serial.println("Received response from target, forwarding it...");
    write(listenerSock, targetDataBuffer, sizeof(targetDataBuffer));
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

void loop() {
  Serial.println("Starting TCP tunnel.\n");
  int err = createReverseTcpTunnel("ah.nl", 443, "95.179.129.108", 1337);
  delay(1000);
}