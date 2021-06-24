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


/*
char m_server[255];
char m_client[256];
int m_port;
int m_port_client;

char server_buffer[256];
char client_buffer[256];
int sockfd_server = -1;
int sockfd_client = -1;


TaskHandle_t server_client_task = NULL;
TaskHandle_t client_server_task = NULL;
bool running;

int create_socket(char *_server, int _port) {
  int sockfd;
  struct sockaddr_in serverSockAddr = {};
  struct hostent *he = NULL;
  if ( (he = gethostbyname(_server)) == NULL) 
  {
    return -1;
  }
  memcpy(&serverSockAddr.sin_addr, he->h_addr, sizeof(serverSockAddr));

  char str[INET_ADDRSTRLEN];
  // now get it back and print it
  inet_ntop(AF_INET, &(serverSockAddr.sin_addr), str, INET_ADDRSTRLEN);

  Serial.println(str);

  serverSockAddr.sin_port = htons(_port);
  serverSockAddr.sin_family = AF_INET;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if(sockfd < 0) {
    Serial.println("Fock-et.");
    return -1;
  }

  if(connect(sockfd, (struct sockaddr*)&serverSockAddr, sizeof(serverSockAddr)) < 0) {
    Serial.println("Connection failed.");
    return -1;
  }

  Serial.println("BANANEN BOOT!");
  Serial.println(sockfd);

  return sockfd;

}

*/
const char* ssid = "Ziggo8152006";
const char* password = "wxNdqksw4xfc";

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
/*
   // put your setup code here, to run once:
  int kerneliumSock = create_socket((char*)"95.179.129.108", 1337);
  int albertheijn = create_socket("captive.apple.com", 80);
  if (kerneliumSock == -1) {
    Serial.println("Failed to connect to kernelium.com.");
    delay(60000);
    return;
  }
  if(albertheijn == -1) {
    Serial.println("Failed to connect to albert heijn network.");
     delay(60000);
    return;
  }

  uint32_t packetSize = 0;
  while(read(kerneliumSock, &packetSize, sizeof(packetSize)) != sizeof(uint32_t) && packetSize != 0) {

  }

  unsigned int chunks = packetSize / 4096;
  if(packetSize % 4096) {
    chunks++;
  }

  for(int i = 0; i < chunks; i++) {
    char* buffer = (char*)malloc(4096);
    bzero(buffer, 4096);
    read(kerneliumSock, buffer, 4096);
    write(albertheijn, buffer, 4096);
    free(buffer);
    buffer = NULL;
  }

    

  close(kerneliumSock);
  close(albertheijn);

  delay(60000);
 */

  int err = createReverseTcpTunnel("ah.nl", 443, "95.179.129.108", 1337);

}