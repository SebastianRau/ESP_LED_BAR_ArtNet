#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <SPI.h>
#include <ArtnetLayer.h>

#define SERIAL_DEBUG

#define NUM_LEDS      135
#define DMX_DATA_SIZE 512
#define EEPROM_START_ADDRESS 0
#define PIN_LED         2


const unsigned long crc_table[16] = {
  0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
  0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
  0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
  0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
};

//Callbacks from Artnet
void onArtUpdSend(uint8_t ip[4], uint16_t port, uint8_t* packetData, size_t len, uint8_t broadcast);
void onNetworkRestart();
void onConfigChange(NODE_CONFIGURATION_T* config);
void onArtDmxFrame(uint8_t* data, uint16_t length, uint8_t sequence);
void updateDisplay();


char    ssid[]      = "DMXLED";
char    password[]  = "AT7Bnty5OxajWMVqUdHuHPSjJPq5LT7gXMTZNBXeK5K0uLGlDGuQEBCzi4gwLri";
char    host_name[] = "LEDBAR-AABBCC";

uint8_t dmxData[DMX_DATA_SIZE];
uint8_t lastSequence = 0;
uint8_t updateFlag = 1;

ArtnetLayer artnet(onArtUpdSend, onNetworkRestart);
WiFiUDP Udp;

void setup() {
  delay(2000);              // sanity check delay - allows reprogramming if accidently blowing power w/leds
  EEPROM.begin(512);
  dmxData[0] = 0x40;        // Set First LED to Red
  SPI.begin();
  SPI.setFrequency(10000000);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, 0);

  WiFi.mode(WIFI_STA);
  WiFi.macAddress(artnet.nodeConfig.mac);
  sprintf(host_name,"LEDBAR-%02X%02X%02X",artnet.nodeConfig.mac[3],artnet.nodeConfig.mac[4],artnet.nodeConfig.mac[5]);

  
  
  
  WiFi.hostname();

  
  updateDisplay();          //SET LEDS to 0x40
#ifdef SERIAL_DEBUG
  Serial.begin(115200);
  while (!Serial);
  Serial.println();
  Serial.println();
  Serial.println(host_name);
  
  Serial.print("MAC: ");
  Serial.print(artnet.nodeConfig.mac[0],HEX);
  Serial.print(":");
  Serial.print(artnet.nodeConfig.mac[1],HEX);
  Serial.print(":");
  Serial.print(artnet.nodeConfig.mac[2],HEX);
  Serial.print(":");
  Serial.print(artnet.nodeConfig.mac[3],HEX);
  Serial.print(":");
  Serial.print(artnet.nodeConfig.mac[4],HEX);
  Serial.print(":");
  Serial.println(artnet.nodeConfig.mac[5],HEX);
#endif
 
  
  
  
  loadConfig();

  
  while(ConnectWifi() == 0){
    delay(100);  
  }

  
  
  artnet.setConfigChangedCallback(onConfigChange);
  artnet.setArtDmxCallback(onArtDmxFrame);
  artnet.begin();

  dmxData[0] = 0x00;        // Set First LED red off
  dmxData[1] = 0x40;        // Set First LED to Green  
  
}

void loop() {
  int packetSize = Udp.parsePacket();
  if (packetSize > 0 && packetSize <= ART_NET_BUFFER_SIZE) {
    Udp.read(artnet.getPacketBuffer(), ART_NET_BUFFER_SIZE);
    artnet.read(packetSize);
  }


  if (updateFlag == 1) {
    updateDisplay();
    updateFlag = 0;
  }

  if (!WiFi.isConnected()) {
    digitalWrite(PIN_LED, 0);
    //Leave last LED state - as configured in Artnet Failsafe mode
    //memset(dmxData, 0, DMX_DATA_SIZE);    //Reset DMX Channels
    //updateDisplay();                      //Update Device to all off
    updateFlag = 0;                       //Reset update Flag
    ConnectWifi();
  }
}

uint8_t ConnectWifi(void){
  int connectionCount = 0;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
#ifdef SERIAL_DEBUG
  Serial.println("");
  Serial.println("Connecting to WiFi");

  Serial.print("Connecting");
#endif

  while (!WiFi.isConnected()) {
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    delay(500);
#ifdef SERIAL_DEBUG
    Serial.print(".");
#endif
    connectionCount ++;
    if (connectionCount > 30) {
      return 0;
    }
  }
  digitalWrite(PIN_LED, 1);
#ifdef SERIAL_DEBUG
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
#endif
  return 1;
}

void onArtUpdSend(uint8_t ip[4], uint16_t port, uint8_t* packetData, size_t len, uint8_t broadcast) {
 IPAddress ipAddress(ip[0], ip[1], ip[2], ip[3]);
  if (broadcast) {
    ipAddress = (~(uint32_t)WiFi.subnetMask()) | ((uint32_t)WiFi.localIP());
  }
  Udp.beginPacket(ipAddress, port);
  Udp.write(packetData, len);
  Udp.endPacket();
}

void onNetworkRestart() {

  int32_t dhcpWaitTime = 2000;   //waiting 5ms * 1000 = 5s
  IPAddress zeroIp((uint32_t)0x00000000);

  if (!WiFi.isConnected()) {
    digitalWrite(PIN_LED, 0);
    ConnectWifi();
  }

  Udp.stop();
  if (artnet.nodeConfig.dhcpEnable) {

    WiFi.config(zeroIp, zeroIp, zeroIp);


    while (((uint32_t)WiFi.localIP()) == 0) {
      delay(5);
      dhcpWaitTime--;
      if (dhcpWaitTime <= 0) {
        artnet.nodeConfig.dhcpEnable = 0;
#ifdef SERIAL_DEBUG
        Serial.println("DHCP Failed.");
#endif
        onNetworkRestart();
        return;
      }
    }
    artnet.nodeConfig.ip[0] = WiFi.localIP()[0];          //get IP
    artnet.nodeConfig.ip[1] = WiFi.localIP()[1];          //get IP
    artnet.nodeConfig.ip[2] = WiFi.localIP()[2];          //get IP
    artnet.nodeConfig.ip[3] = WiFi.localIP()[3];          //get IP

    artnet.nodeConfig.subnet[0] = WiFi.subnetMask()[0];   //get Subnet
    artnet.nodeConfig.subnet[1] = WiFi.subnetMask()[1];   //get Subnet
    artnet.nodeConfig.subnet[2] = WiFi.subnetMask()[2];   //get Subnet
    artnet.nodeConfig.subnet[3] = WiFi.subnetMask()[3];   //get Subnet
  } else {
    IPAddress local_ip(artnet.nodeConfig.ip);
    IPAddress subnet(artnet.nodeConfig.subnet);
    IPAddress gateway(artnet.nodeConfig.gateway);
    IPAddress dns1(artnet.nodeConfig.dnsServer);
    WiFi.config(local_ip, gateway, subnet, dns1);
  }

  Serial.println(WiFi.localIP());
  Serial.println(WiFi.subnetMask());
  Udp.begin(ART_NET_PORT);
#ifdef SERIAL_DEBUG
        Serial.println("Network config done.");
#endif
  
}


void onArtDmxFrame(uint8_t* data, uint16_t length, uint8_t sequence) {
  memcpy(&dmxData, data, length);
  updateFlag = 1;
  digitalWrite(PIN_LED, !digitalRead(PIN_LED));
}


void onConfigChange(NODE_CONFIGURATION_T* config) {
  writeConfigEEPROM(config, sizeof(NODE_CONFIGURATION_T));
  Serial.println("writing config");
}



void loadConfig() {
  readConfigEEPROM(&artnet.nodeConfig, sizeof(NODE_CONFIGURATION_T) );
  WiFi.macAddress(artnet.nodeConfig.mac);
  Serial.println(artnet.nodeConfig.crc32, HEX);
  if (!artnet.isConfigCrcValid()) {
    loadDefaultConfig();
    Serial.println(F("CONFIG LOAD FAILED. RESTORE DEFAULT."));
  } else {
    Serial.println(F("CONFIG LOAD SUCCESSFUL."));
  }
  
}

void loadDefaultConfig() {
  memcpy_P(&artnet.nodeConfig, &defaultConfig, sizeof(artnet.nodeConfig) );
  WiFi.macAddress(artnet.nodeConfig.mac);
  onConfigChange(&artnet.nodeConfig);
}


int writeConfigEEPROM(NODE_CONFIGURATION_T* config, size_t len ){
#ifdef SERIAL_DEBUG
  Serial.println("Storing values");
#endif
  const byte* data = (const byte*)config;
  unsigned int i = 0;
  unsigned int eeprom_Address = EEPROM_START_ADDRESS;

  for (i = 0; i < len; i++) {
    EEPROM.write(eeprom_Address++, *data++);
  }
  EEPROM.commit();
  return i;
}

int readConfigEEPROM(NODE_CONFIGURATION_T* config, size_t len){
  uint8_t* data = (uint8_t*)config;
  unsigned int i = 0;
  unsigned int eeprom_Address = EEPROM_START_ADDRESS;

  for (i = 0; i < len; i++) {
    *data++ = EEPROM.read(eeprom_Address++);
  }
  return i;
}

void updateDisplay() {
  APA_Start();
  for (int i = NUM_LEDS; i > 0; i--)
  {
    APA_LED(dmxData[i * 3], dmxData[i * 3 + 1], dmxData[i * 3 + 2]);
  }
  APA_Stop();
}

void APA_Start(){
  SPI.transfer(0);
  SPI.transfer(0);
  SPI.transfer(0);
  SPI.transfer(0);
}

void APA_Stop(){
  for (int i = 0; i < (NUM_LEDS / 2); i++) {
    SPI.transfer(0xFF); // one stop frame for every 32 leds
  }
}

void APA_LED(uint8_t R, uint8_t G, uint8_t B){
  SPI.transfer(0xFF);
  SPI.transfer(B);
  SPI.transfer(G);
  SPI.transfer(R);
}
