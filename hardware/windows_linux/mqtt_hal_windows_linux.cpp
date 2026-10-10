#include <string>
#include "mqtt_hal_windows_linux.h"
#include "secrets.h"

#if (ENABLE_WIFI_AND_MQTT == 1)
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>

// example is mainly taken from .pio/libdeps/windows_linux_64bit/MQTT-C/tests.c, TEST__api__publish_subscribe__single
#if !defined(WIN32)
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#else
#include <ws2tcpip.h>

/* Some shortcuts to call winapi in a posix-like way */
#define close(sock)         closesocket(sock)
#define usleep(usec)        Sleep((usec) / 1000)
#endif

#include "lib/MQTT-C/include/mqtt.h"
#include "lib/MQTT-C/include/posix_sockets.h"

int sockfd = -1;
uint8_t sendmem1[4096];
uint8_t recvmem1[4096];
struct mqtt_client mqttClient;
std::string uniqueClientSuffix = "";
int state = 0;

tAnnounceWiFiconnected_cb thisAnnounceWiFiconnected_cb = NULL;
void set_announceWiFiconnected_cb_HAL(tAnnounceWiFiconnected_cb pAnnounceWiFiconnected_cb) {
  thisAnnounceWiFiconnected_cb = pAnnounceWiFiconnected_cb;  
}

tAnnounceSubscribedTopics_cb thisAnnounceSubscribedTopics_cb = NULL;
void set_announceSubscribedTopics_cb_HAL(tAnnounceSubscribedTopics_cb pAnnounceSubscribedTopics_cb) {
  thisAnnounceSubscribedTopics_cb = pAnnounceSubscribedTopics_cb;  
}

bool getIsWifiConnected_HAL() {
  return (sockfd != -1);
}

std::string subscribeTopicOMOTEtest = "OMOTE/test";
// For connecting to one or several BLE clients
std::string subscribeTopicOMOTE_BLEstartAdvertisingForAll        = "OMOTE/BLE/startAdvertisingForAll";
std::string subscribeTopicOMOTE_BLEstartAdvertisingWithWhitelist = "OMOTE/BLE/startAdvertisingWithWhitelist";
std::string subscribeTopicOMOTE_BLEstartAdvertisingDirected      = "OMOTE/BLE/startAdvertisingDirected";
std::string subscribeTopicOMOTE_BLEstopAdvertising               = "OMOTE/BLE/stopAdvertising";
std::string subscribeTopicOMOTE_BLEprintConnectedClients         = "OMOTE/BLE/printConnectedClients";
std::string subscribeTopicOMOTE_BLEdisconnectAllClients          = "OMOTE/BLE/disconnectAllClients";
std::string subscribeTopicOMOTE_BLEprintBonds                    = "OMOTE/BLE/printBonds";
std::string subscribeTopicOMOTE_BLEdeleteBonds                   = "OMOTE/BLE/deleteBonds";
// htpc bridge, forwarded to the commandHandler
std::string subscribeTopicHTPC_tiles                             = "htpc/movies/tiles";
std::string subscribeTopicHTPC_page                              = "htpc/movies/page";
std::string subscribeTopicHTPC_playerState                       = "htpc/player/state";
std::string subscribeTopicHTPC_bridgeOnline                      = "htpc/bridge/online";
std::string subscribeTopicHTPC_genres                            = "htpc/movies/genres";

void publish_callback(void** state, struct mqtt_response_publish *publish) {
    **(int**)state += 1;
    printf("message nr %d received\r\n", **(int**)state);

    std::string topic((const char*) (publish->topic_name), publish->topic_name_size);
    std::string payload((const char*) (publish->application_message), publish->application_message_size);

    printf("Received a PUBLISH(topic=%s, DUP=%d, QOS=%d, RETAIN=%d, pid=%d) from the broker. Data='%s'\r\n", 
           topic.c_str(), publish->dup_flag, publish->qos_level, publish->retain_flag, publish->packet_id,
           payload.c_str()
    );
    
    if (topic == subscribeTopicOMOTEtest) {
      // Do whatever you want here, if it is Windows/Linux hardware related.
      // ...

      // Or forward the topic to "void receiveMQTTmessage_cb" in the "commandHandler.cpp", if it is not Windows/Linux hardware related
      thisAnnounceSubscribedTopics_cb(topic, payload);

    } else {
      // forward all other topics to the commandHandler
      thisAnnounceSubscribedTopics_cb(topic, payload);
    }
}

void mqtt_subscribeTopics() {
  mqtt_subscribe(&mqttClient, subscribeTopicOMOTEtest.c_str(), 2);
  mqtt_subscribe(&mqttClient, subscribeTopicOMOTE_BLEstartAdvertisingForAll.c_str(), 2);
  mqtt_subscribe(&mqttClient, subscribeTopicOMOTE_BLEstartAdvertisingWithWhitelist.c_str(), 2);
  mqtt_subscribe(&mqttClient, subscribeTopicOMOTE_BLEstartAdvertisingDirected.c_str(), 2);
  mqtt_subscribe(&mqttClient, subscribeTopicOMOTE_BLEstopAdvertising.c_str(), 2);
  mqtt_subscribe(&mqttClient, subscribeTopicOMOTE_BLEprintConnectedClients.c_str(), 2);
  mqtt_subscribe(&mqttClient, subscribeTopicOMOTE_BLEdisconnectAllClients.c_str(), 2);
  mqtt_subscribe(&mqttClient, subscribeTopicOMOTE_BLEprintBonds.c_str(), 2);
  mqtt_subscribe(&mqttClient, subscribeTopicOMOTE_BLEdeleteBonds.c_str(), 2);
  mqtt_subscribe(&mqttClient, subscribeTopicHTPC_tiles.c_str(), 0);
  mqtt_subscribe(&mqttClient, subscribeTopicHTPC_page.c_str(), 0);
  mqtt_subscribe(&mqttClient, subscribeTopicHTPC_playerState.c_str(), 0);
  mqtt_subscribe(&mqttClient, subscribeTopicHTPC_bridgeOnline.c_str(), 0);
  mqtt_subscribe(&mqttClient, subscribeTopicHTPC_genres.c_str(), 0);

}

void reconnect_mqtt(struct mqtt_client *mqttClient, void**) {
  printf("MQTT: will reconnect ...\r\n");

  mqtt_reinit(mqttClient, sockfd, sendmem1, sizeof(sendmem1), recvmem1, sizeof(recvmem1));

  std::string mqttClientName = std::string(MQTT_CLIENTNAME) + uniqueClientSuffix;
  //                       client_id,              will_topic, will_message, will_message_size, user_name, password,  connect_flags, keep_alive
  mqtt_connect(mqttClient, mqttClientName.c_str(), NULL,       NULL,         0,                 MQTT_USER, MQTT_PASS, 0,             30);
  if (mqttClient->error != MQTT_OK) {
    printf("MQTT: connect error: %s\r\n", mqtt_error_str(mqttClient->error));
    // sockfd = -1;
    // return;
  }
}

#if !defined(WIN32) && !defined(__APPLE__)
std::string getMACaddress() {
  struct ifreq s;
  int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);

  strcpy(s.ifr_name, "eth0");
  if (0 == ioctl(fd, SIOCGIFHWADDR, &s)) {
    char buffer[6*3];
    int i;
    for (i = 0; i < 6; ++i) {
      sprintf(&buffer[i*3], "%02x:", (unsigned char) s.ifr_addr.sa_data[i]);
      // printf(" %02x", (unsigned char) s.ifr_addr.sa_data[i]);
    }
    //printf("\r\n");

    std::string MACaddress = std::string(buffer, 17);
    printf("  result in MACaddress(): %s\r\n", MACaddress.c_str());
    return MACaddress;
  }
  return "";
}
#endif

void init_mqtt_HAL(void) {
  #if defined(WIN32)
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR) {
        printf("Failed to init sockets: %i\r\n", iResult);
        return; // return iResult;
    }
  #endif

  char MACaddress[6*3];
  sockfd = open_nb_socket(MQTT_SERVER, std::to_string(MQTT_SERVER_PORT).c_str(), MACaddress);
  if (sockfd == -1) {
    printf("MQTT: Failed to open socket\r\n");
    return;
  }

  #if !defined(WIN32)
  // MAC address is not the best. You cannot start more than one instance like that, otherwise the MQTT broker will only keep the last connection.
  // printf("MQTT: received MAC address from posix_sockets.h is %s\r\n", MACaddress);
  // uniqueClientSuffix = std::string(MACaddress, 18);
  // simply use a random number
  srand(time(NULL));   // Initialization, should only be called once.
  int r = rand();      // Returns a pseudo-random integer between 0 and RAND_MAX.
  uniqueClientSuffix = "_linux_" + std::to_string(r);
  #else
  srand(time(NULL));   // Initialization, should only be called once.
  int r = rand();      // Returns a pseudo-random integer between 0 and RAND_MAX.
  uniqueClientSuffix = "_windows_" + std::to_string(r);
  #endif

  // printf("MQTT: MAC address from getMACaddress() in mqtt_hal_windows_linux.cpp is %s\r\n", getMACaddress().c_str());

  // printf("MQTT: will init ...\r\n");
  // mqtt_init(&mqttClient, sockfd, sendmem1, sizeof(sendmem1), recvmem1, sizeof(recvmem1), publish_callback);
  printf("MQTT: will init with reconnect ...\r\n");
  mqtt_init_reconnect(&mqttClient, reconnect_mqtt, NULL, publish_callback);
  reconnect_mqtt(&mqttClient, NULL);
  mqttClient.publish_response_callback_state = &state;

  mqtt_subscribeTopics();

  thisAnnounceWiFiconnected_cb(true);

}

void mqtt_loop_HAL() {
  if (sockfd != -1) {
    mqtt_sync(&mqttClient);
  }

}

bool publishMQTTMessage_HAL(const char *topic, const char *payload) {

    // like on the ESP32: no attempts while MQTT_SERVER is the placeholder, and at most one connect attempt
    // every 10 seconds, the connect (DNS lookup) blocks the GUI
    static time_t lastConnectAttempt = 0;
    if ((sockfd == -1) && (strcmp(MQTT_SERVER, "IPAddressOfYourBroker") != 0) && (time(NULL) - lastConnectAttempt >= 10)) {
      lastConnectAttempt = time(NULL);
      init_mqtt_HAL();
    }
    if (sockfd == -1) {
      // no broker: mqttClient was never initialized, publishing would crash the simulator
      printf("MQTT: not connected, cannot publish %s %s\r\n", topic, payload);
      return false;
    }
  
    mqtt_publish(&mqttClient, topic, payload, strlen(payload), MQTT_PUBLISH_QOS_0);
    if (mqttClient.error != MQTT_OK) {
      printf("MQTT: publish error %s\r\n", mqtt_error_str(mqttClient.error));
      sockfd = -1;
      return false;
    }

  return true;
}

bool httpGet_HAL(const char *url, std::string *body) {
  // the simulator does not talk to devices, only the emulator service in the config stack does
  printf("HTTP: GET %s (not sent by the simulator)\r\n", url);
  if (body != nullptr) body->clear();
  return true;
}

// minimal HTTP/1.0 client for downloading data, only http://host[:port]/path is supported
bool httpDownload_HAL(const char *url, std::string *body) {
  body->clear();
  std::string u(url);
  if (u.compare(0, 7, "http://") != 0) {return false;}
  u.erase(0, 7);
  size_t slash = u.find('/');
  std::string hostPort = u.substr(0, slash);
  std::string path = (slash == std::string::npos) ? "/" : u.substr(slash);
  std::string host = hostPort;
  std::string port = "80";
  size_t colon = hostPort.find(':');
  if (colon != std::string::npos) {
    host = hostPort.substr(0, colon);
    port = hostPort.substr(colon + 1);
  }

  struct addrinfo hints = {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* result = NULL;
  if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) {return false;}
  int sock = -1;
  for (struct addrinfo* p = result; p != NULL; p = p->ai_next) {
    sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sock == -1) {continue;}
    #if !defined(WIN32)
    struct timeval timeout = {2, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    #endif
    if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) {break;}
    close(sock);
    sock = -1;
  }
  freeaddrinfo(result);
  if (sock == -1) {return false;}

  std::string request = "GET " + path + " HTTP/1.0\r\nHost: " + hostPort + "\r\nConnection: close\r\n\r\n";
  send(sock, request.c_str(), request.size(), 0);
  std::string response;
  char buffer[2048];
  int n;
  while ((n = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
    response.append(buffer, n);
    if (response.size() > 64 * 1024) {break;}
  }
  close(sock);

  size_t headerEnd = response.find("\r\n\r\n");
  size_t firstLineEnd = response.find("\r\n");
  if ((headerEnd == std::string::npos) || (response.substr(0, firstLineEnd).find(" 200") == std::string::npos)) {
    printf("HTTP: GET %s failed\r\n", url);
    return false;
  }
  body->assign(response, headerEnd + 4, std::string::npos);
  return true;
}

void wifi_shutdown_HAL() {
  /* disconnect */
  if (sockfd != -1) {
    mqtt_disconnect(&mqttClient);
    mqtt_sync(&mqttClient);
  }

  #if defined(WIN32)
  WSACleanup();
  #endif

}

#endif
