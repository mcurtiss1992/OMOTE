#include <sstream>
#include "WiFi.h"
#include <PubSubClient.h>
#include "mqtt_hal_esp32.h"
#if (ENABLE_KEYBOARD_BLE == 1)
#include "keyboard_ble_hal_esp32.h"
#endif
#include "secrets.h"
#include "applicationInternal/omote_log.h"


#if (ENABLE_WIFI_AND_MQTT == 1)
WiFiClient espClient;
PubSubClient mqttClient(espClient);
bool isWifiConnected = false;

tAnnounceWiFiconnected_cb thisAnnounceWiFiconnected_cb = NULL;
void set_announceWiFiconnected_cb_HAL(tAnnounceWiFiconnected_cb pAnnounceWiFiconnected_cb) {
  thisAnnounceWiFiconnected_cb = pAnnounceWiFiconnected_cb;  
}

tAnnounceSubscribedTopics_cb thisAnnounceSubscribedTopics_cb = NULL;
void set_announceSubscribedTopics_cb_HAL(tAnnounceSubscribedTopics_cb pAnnounceSubscribedTopics_cb) {
  thisAnnounceSubscribedTopics_cb = pAnnounceSubscribedTopics_cb;  
}

bool getIsWifiConnected_HAL() {
  return isWifiConnected;
}

// mqttClient.connect() blocks the main loop (DNS lookup + TCP connect timeout) while the broker is not
// reachable. It used to be retried every 100 ms, and on every MQTT key press, which froze keys and GUI.
// Now failed attempts back off exponentially, and nothing is tried while no broker is configured.
static const unsigned long reconnectIntervalMin = 1000;
static const unsigned long reconnectIntervalMax = 60000;
static unsigned long reconnectInterval = reconnectIntervalMin;
static unsigned long nextReconnectAttempt = 0;
static uint8_t reconnectFails = 0;
static const uint8_t RECONNECT_FAILS_BEFORE_WIFI_RESET = 12;

static bool isMQTTbrokerConfigured() {
  return (strlen(MQTT_SERVER) > 0) && (strcmp(MQTT_SERVER, "IPAddressOfYourBroker") != 0);
}

// WiFi status event
void WiFiEvent(WiFiEvent_t event){
  //omote_log_i("[WiFi-event] event: %d\r\n", event);
  if(event == ARDUINO_EVENT_WIFI_STA_GOT_IP){
    // connection to MQTT server will be done in checkMQTTconnection()
    // mqttClient.setServer(MQTT_SERVER, 1883); // MQTT initialization
    // mqttClient.connect("OMOTE"); // Connect using a client id

  }

  // Set status bar icon based on WiFi status
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP || event == ARDUINO_EVENT_WIFI_STA_GOT_IP6) {
    isWifiConnected = true;
    // new connection: try MQTT right away
    reconnectInterval = reconnectIntervalMin;
    nextReconnectAttempt = millis();
    thisAnnounceWiFiconnected_cb(true);
    omote_log_i("WiFi connected, IP address: %s\r\n", WiFi.localIP().toString().c_str());

  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    isWifiConnected = false;
    thisAnnounceWiFiconnected_cb(false);
    // automatically try to reconnect
    omote_log_i("WiFi got disconnected. Will try to reconnect.\r\n");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  } else {
    // e.g. ARDUINO_EVENT_WIFI_STA_CONNECTED or many others
    // connected is not enough, will wait for IP
    isWifiConnected = false;
    thisAnnounceWiFiconnected_cb(false);

  }
}

void init_mqtt_HAL(void) {
  // Setup WiFi
  WiFi.setHostname("OMOTE"); //define hostname
  WiFi.onEvent(WiFiEvent);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(true);
}

std::string subscribeTopicOMOTEtest = "OMOTE/test";
std::string subscribeTopicOMOTEConfig = "OMOTE/config";
// For connecting to one or several BLE clients
std::string subscribeTopicOMOTE_BLEstartAdvertisingForAll        = "OMOTE/BLE/startAdvertisingForAll";
std::string subscribeTopicOMOTE_BLEstartAdvertisingWithWhitelist = "OMOTE/BLE/startAdvertisingWithWhitelist";
std::string subscribeTopicOMOTE_BLEstartAdvertisingDirected      = "OMOTE/BLE/startAdvertisingDirected";
std::string subscribeTopicOMOTE_BLEstopAdvertising               = "OMOTE/BLE/stopAdvertising";
std::string subscribeTopicOMOTE_BLEprintConnectedClients         = "OMOTE/BLE/printConnectedClients";
std::string subscribeTopicOMOTE_BLEdisconnectAllClients          = "OMOTE/BLE/disconnectAllClients";
std::string subscribeTopicOMOTE_BLEprintBonds                    = "OMOTE/BLE/printBonds";
std::string subscribeTopicOMOTE_BLEdeleteBonds                   = "OMOTE/BLE/deleteBonds";

void callback(char* topic, byte* payload, unsigned int length) {
  // handle message arrived
  std::string topicReceived(topic);
  std::string strPayload(reinterpret_cast<const char *>(payload), length);
  omote_log_i("MQTT: received topic %s with payload %s\r\n", topicReceived.c_str(), strPayload.c_str());

  if(topicReceived == subscribeTopicOMOTEConfig){
    
  }
  else if (topicReceived == subscribeTopicOMOTEtest) {
    // Do whatever you want here, if it is ESP32 hardware related.
    // ...

    // Or forward the topic to "void receiveMQTTmessage_cb" in the "commandHandler.cpp", if it is not ESP32 hardware related
    thisAnnounceSubscribedTopics_cb(topicReceived, strPayload);

  #if (ENABLE_KEYBOARD_BLE == 1)
  } else if (topicReceived == subscribeTopicOMOTE_BLEstartAdvertisingForAll) {
    keyboardBLE_startAdvertisingForAll_HAL();  
  } else if (topicReceived == subscribeTopicOMOTE_BLEstartAdvertisingWithWhitelist) {
    keyboardBLE_startAdvertisingWithWhitelist_HAL(strPayload);  
  } else if (topicReceived == subscribeTopicOMOTE_BLEstartAdvertisingDirected) {
    // the payload are two values, separated by comma: peerAddress and isRandomAddress 
    std::stringstream ss(strPayload);
    if (ss.good())  {
      std::string peerAddress;
      std::getline(ss, peerAddress, ',');
      
      if (ss.good())  {
        std::string isRandomAddressStr;
        std::getline(ss, isRandomAddressStr, ',');
        bool isRandomAddress = false;
        if (isRandomAddressStr == "true") {
          isRandomAddress = true;  
        }
        keyboardBLE_startAdvertisingDirected_HAL(peerAddress, isRandomAddress);  
      }
    }
  } else if (topicReceived == subscribeTopicOMOTE_BLEstopAdvertising) {
    keyboardBLE_stopAdvertising_HAL();  
  } else if (topicReceived == subscribeTopicOMOTE_BLEprintConnectedClients) {
    keyboardBLE_printConnectedClients_HAL();
  } else if (topicReceived == subscribeTopicOMOTE_BLEdisconnectAllClients) {
    keyboardBLE_disconnectAllClients_HAL();  
  } else if (topicReceived == subscribeTopicOMOTE_BLEprintBonds) {
    keyboardBLE_printBonds_HAL();  
  } else if (topicReceived == subscribeTopicOMOTE_BLEdeleteBonds) {
    keyboardBLE_deleteBonds_HAL();  
  #endif

  } else {
    // forward all other topics to the commandHandler
    thisAnnounceSubscribedTopics_cb(topicReceived, strPayload);

  }
}

void mqtt_subscribeTopics() {
  mqttClient.setCallback(&callback);

  mqttClient.subscribe(subscribeTopicOMOTEtest.c_str());
  mqttClient.subscribe(subscribeTopicOMOTE_BLEstartAdvertisingForAll.c_str());
  mqttClient.subscribe(subscribeTopicOMOTE_BLEstartAdvertisingWithWhitelist.c_str());
  mqttClient.subscribe(subscribeTopicOMOTE_BLEstartAdvertisingDirected.c_str());
  mqttClient.subscribe(subscribeTopicOMOTE_BLEstopAdvertising.c_str());
  mqttClient.subscribe(subscribeTopicOMOTE_BLEprintConnectedClients.c_str());
  mqttClient.subscribe(subscribeTopicOMOTE_BLEdisconnectAllClients.c_str());
  mqttClient.subscribe(subscribeTopicOMOTE_BLEprintBonds.c_str());
  mqttClient.subscribe(subscribeTopicOMOTE_BLEdeleteBonds.c_str());
  omote_log_i("  Successfully subscribed to MQTT topics\r\n");

}

bool checkMQTTconnection() {

  if (WiFi.isConnected()) {
    if (mqttClient.connected()) {
      return true;
    } else if (!isMQTTbrokerConfigured() || ((long)(millis() - nextReconnectAttempt) < 0)) {
      // no broker configured, or the last attempt failed recently
      return false;
    } else {
      // try to connect to mqtt server
      mqttClient.setBufferSize(512);   // default is 256
      //mqttClient.setKeepAlive(15);     // default is 15   Client will send MQTTPINGREQ to keep connection alive
      //mqttClient.setSocketTimeout(15); // default is 15   This determines how long the client will wait for incoming data when it expects data to arrive - for example, whilst it is in the middle of reading an MQTT packet.
      mqttClient.setServer(MQTT_SERVER, MQTT_SERVER_PORT); // MQTT initialization
      
      std::string mqttClientName = std::string(MQTT_CLIENTNAME) + "_esp32_" + std::string(WiFi.macAddress().c_str());
      if (mqttClient.connect(mqttClientName.c_str(), MQTT_USER, MQTT_PASS)) {
        omote_log_i("  Successfully connected to MQTT broker\r\n");
        reconnectInterval = reconnectIntervalMin;
        reconnectFails = 0;
    
        mqtt_subscribeTopics();

      } else {
        omote_log_e("  MQTT connection failed (but WiFi is available). Will try again in %lu ms\r\n", reconnectInterval);
        nextReconnectAttempt = millis() + reconnectInterval;
        reconnectInterval = min(reconnectInterval * 2, reconnectIntervalMax);
        // broker unreachable for a long time while WiFi claims to be connected: assume the link is stuck and rebuild it
        if (++reconnectFails >= RECONNECT_FAILS_BEFORE_WIFI_RESET) {
          reconnectFails = 0;
          omote_log_e("MQTT unreachable for a long time, resetting WiFi\r\n");
          WiFi.disconnect();
          WiFi.reconnect();
        }

      }
      return mqttClient.connected();
    }
  } else {
    // omote_log_e("  No connection to MQTT server, because WiFi ist not connected.\r\n");
    return false;
  }  
}

void mqtt_loop_HAL() {
  if (!mqttClient.connected()) {
    // Attempt to reconnect, if an attempt is due
    checkMQTTconnection();
  }  

  if (mqttClient.connected()) {
    mqttClient.loop();
  }
}

// Plain HTTP/1.0 GET over WiFiClient. HTTPClient pulls in the TLS stack and overflows IRAM on the ESP32.
bool httpGet_HAL(const char *url, std::string *body) {
  if (!isWifiConnected) {
    omote_log_w("HTTP GET %s: WiFi is not connected\r\n", url);
    return false;
  }
  std::string u(url);
  if (u.compare(0, 7, "http://") != 0) {
    omote_log_e("HTTP GET: url has to start with http://: %s\r\n", url);
    return false;
  }
  u.erase(0, 7);
  size_t slash = u.find('/');
  std::string hostPort = u.substr(0, slash);
  std::string path = (slash == std::string::npos) ? "/" : u.substr(slash);
  uint16_t port = 80;
  size_t colon = hostPort.find(':');
  std::string host = hostPort.substr(0, colon);
  if (colon != std::string::npos) port = atoi(hostPort.c_str() + colon + 1);

  WiFiClient client;
  client.setTimeout(2);
  if (!client.connect(host.c_str(), port, 1500)) {
    omote_log_e("HTTP GET %s: connect failed\r\n", url);
    return false;
  }
  client.printf("GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path.c_str(), host.c_str());
  String status = client.readStringUntil('\n');
  if (body != nullptr) {
    body->clear();
    while (client.connected() || client.available()) {
      String line = client.readStringUntil('\n');
      if (line.length() == 0 || line == "\r") break;
    }
    while ((client.connected() || client.available()) && body->size() < 4096) {
      int c = client.read();
      if (c < 0) {
        if (!client.connected()) break;
        delay(1);
        continue;
      }
      body->push_back((char)c);
    }
  }
  client.stop();
  int code = 0;
  int sp = status.indexOf(' ');
  if (sp > 0) code = status.substring(sp + 1).toInt();
  if (code >= 200 && code < 300) {
    omote_log_i("HTTP GET %s: %d\r\n", url, code);
    return true;
  }
  omote_log_e("HTTP GET %s failed: '%s'\r\n", url, status.c_str());
  return false;
}

bool publishMQTTMessage_HAL(const char *topic, const char *payload){

  if (checkMQTTconnection()) {
    // omote_log_i("Sending mqtt payload to topic \"%s\": %s\r\n", topic, payload);
      
    if (mqttClient.publish(topic, payload)) {
      // omote_log_i("Publish ok\r\n");
      return true;
    }
    else {
      omote_log_e("Publish failed\r\n");
    }
  } else {
    omote_log_e("  Cannot publish mqtt message, because checkMQTTconnection failed (WiFi or mqtt is not connected)\r\n");
  }
  return false;
}

void wifi_shutdown_HAL() {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
}

#endif
