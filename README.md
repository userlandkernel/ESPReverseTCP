# ESPReverseTCP
A reverse TCP tunnel for the ESP32

## How to use
1. Edit config.h to match your SSID details and public listener (This is the vps the ESP connects to).  
2. Edit TCPImplantBackend.py (in lisenter folder) to match the LAN IP + PORT you want to reach remotely.    
3. Run the TCPImplantBackend.py on your VPS (Preferably with the screen command).  
4. Upload the code to your ESP32, see the serial log as it connects to your VPS. (Make sure port is reachable by VPS firewall rules).  
5. On your VPS traffic you send to localhost:8080 will be forwarded to the ESP, thus to the server on the LAN and you will get a response back.  
6. The ESP tunnels all TCP traffic, not UDP. You can use curl for http requests (ex: curl http://localhost:8080/pwn?cmd=ls)  

## How to detect
1. Monitor connected MAC addresses to your corporate LAN, force employees to register their devices.  
2. Detect reverse TCP tunnels with an IDS (How? data send to server on LAN equals data sent to server REMOTE thus data is being exfiltrated!).  
3. Need a better implant? Hire me.  
