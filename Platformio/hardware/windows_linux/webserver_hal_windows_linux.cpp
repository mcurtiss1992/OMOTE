#if (ENABLE_WIFI_AND_MQTT == 1)

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <map>
#include <string>
#include <vector>
#include <ArduinoJson.h>
#include "webserver_hal_windows_linux.h"
#include "configFiles_hal_windows_linux.h"

#if defined(WIN32)
  #define NOMINMAX
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <process.h>
  typedef SOCKET socket_t;
  #define closeSocket(s) closesocket(s)
  #define SHUTDOWN_SEND SD_SEND
  #define SEND_FLAGS 0
#else
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #if defined(__APPLE__)
    #include <mach-o/dyld.h>
    #define SEND_FLAGS 0 // SO_NOSIGPIPE is set on the socket instead
  #else
    #define SEND_FLAGS MSG_NOSIGNAL
  #endif
  typedef int socket_t;
  #define INVALID_SOCKET (-1)
  #define closeSocket(s) close(s)
  #define SHUTDOWN_SEND SHUT_WR
#endif

// keep in sync with webserver_hal_esp32.cpp
#define OMOTE_WEB_API_VERSION 2
// the remote stores the files in SPIFFS, which allows 31 characters per path including the leading "/"
#define MAX_SPIFFS_PATH_LEN 31
// size of the SPIFFS partition of the remote, to show a realistic storage usage in the config app
#define SIMULATED_FS_SIZE 512000
#define MAX_REQUEST_SIZE (4 * 1024 * 1024)
#define DEFAULT_HTTP_PORT 8081
static const char* RESTART_ENV = "OMOTE_RESTART_INTO_SETUP";

static socket_t listenSocket = INVALID_SOCKET;
static int httpPort = DEFAULT_HTTP_PORT;
static bool restartRequested = false;
static std::chrono::steady_clock::time_point restartRequestedAt;
static const std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();

static tSendConfiguredCommand_cb thisSendConfiguredCommand_cb = NULL;
void set_sendConfiguredCommand_cb_HAL(tSendConfiguredCommand_cb pSendConfiguredCommand_cb) {
  thisSendConfiguredCommand_cb = pSendConfiguredCommand_cb;
}

//---------------------------------------------------------------------
// HTTP parsing helpers
//---------------------------------------------------------------------

struct Request {
  std::string method;
  std::string path;
  std::map<std::string, std::string> headers; // names in lower case
  std::map<std::string, std::string> args;    // query string, url encoded body and multipart fields
  bool hasFile = false;                       // multipart part with a filename
  std::string fileContent;

  std::string arg(const std::string& key) const {
    std::map<std::string, std::string>::const_iterator it = args.find(key);
    return (it == args.end()) ? std::string() : it->second;
  }
};

static std::string toLower(std::string s) {
  for (size_t i = 0; i < s.size(); i++) s[i] = (char)tolower((unsigned char)s[i]);
  return s;
}

static std::string trim(const std::string& s) {
  size_t start = s.find_first_not_of(" \t");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t");
  return s.substr(start, end - start + 1);
}

static bool endsWith(const std::string& s, const std::string& suffix) {
  return (s.size() >= suffix.size()) && (s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0);
}

static std::string urlDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '+') {
      out += ' ';
    } else if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
      out += (char)strtol(s.substr(i + 1, 2).c_str(), NULL, 16);
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

static void parseUrlEncoded(const std::string& s, std::map<std::string, std::string>& args) {
  size_t pos = 0;
  while (pos < s.size()) {
    size_t amp = s.find('&', pos);
    if (amp == std::string::npos) amp = s.size();
    std::string pair = s.substr(pos, amp - pos);
    if (!pair.empty()) {
      size_t eq = pair.find('=');
      args[urlDecode(pair.substr(0, eq))] = (eq == std::string::npos) ? "" : urlDecode(pair.substr(eq + 1));
    }
    pos = amp + 1;
  }
}

// value of param="..." (or param=...) in a header like Content-Disposition or Content-Type
static std::string headerParam(const std::string& header, const std::string& param) {
  size_t pos = 0;
  while ((pos = header.find(param + "=", pos)) != std::string::npos) {
    // "name=" must not match the end of "filename="
    if (pos == 0 || header[pos - 1] == ';' || header[pos - 1] == ' ') {
      size_t start = pos + param.size() + 1;
      if (start < header.size() && header[start] == '"') {
        size_t end = header.find('"', start + 1);
        return header.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
      }
      return trim(header.substr(start, header.find(';', start) - start));
    }
    pos += param.size();
  }
  return "";
}

static void parseMultipart(const std::string& body, const std::string& boundary, Request& req) {
  const std::string delimiter = "--" + boundary;
  size_t pos = body.find(delimiter);
  while (pos != std::string::npos) {
    pos += delimiter.size();
    if (body.compare(pos, 2, "--") == 0) break; // closing delimiter
    if (body.compare(pos, 2, "\r\n") == 0) pos += 2;
    size_t headerEnd = body.find("\r\n\r\n", pos);
    if (headerEnd == std::string::npos) break;
    std::string partHeaders = body.substr(pos, headerEnd - pos);
    size_t contentStart = headerEnd + 4;
    size_t next = body.find("\r\n" + delimiter, contentStart);
    if (next == std::string::npos) break;

    std::string disposition;
    size_t lineStart = 0;
    while (lineStart < partHeaders.size()) {
      size_t lineEnd = partHeaders.find("\r\n", lineStart);
      if (lineEnd == std::string::npos) lineEnd = partHeaders.size();
      std::string line = partHeaders.substr(lineStart, lineEnd - lineStart);
      size_t colon = line.find(':');
      if (colon != std::string::npos && toLower(trim(line.substr(0, colon))) == "content-disposition") {
        disposition = line.substr(colon + 1);
      }
      lineStart = lineEnd + 2;
    }
    std::string content = body.substr(contentStart, next - contentStart);
    if (disposition.find("filename=") != std::string::npos) {
      req.hasFile = true;
      req.fileContent = content;
    } else {
      req.args[headerParam(disposition, "name")] = content;
    }
    pos = next + 2;
  }
}

static bool readRequest(socket_t client, Request& req) {
  std::string data;
  char buffer[4096];
  size_t headerEnd = std::string::npos;
  while (headerEnd == std::string::npos) {
    int n = recv(client, buffer, sizeof(buffer), 0);
    if (n <= 0 || data.size() > 64 * 1024) return false;
    data.append(buffer, n);
    headerEnd = data.find("\r\n\r\n");
  }

  size_t lineEnd = data.find("\r\n");
  std::string requestLine = data.substr(0, lineEnd);
  size_t sp1 = requestLine.find(' ');
  size_t sp2 = requestLine.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) return false;
  req.method = requestLine.substr(0, sp1);
  std::string target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);

  size_t pos = lineEnd + 2;
  while (pos < headerEnd) {
    size_t end = data.find("\r\n", pos);
    std::string line = data.substr(pos, end - pos);
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
      req.headers[toLower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }
    pos = end + 2;
  }

  size_t contentLength = req.headers.count("content-length") ? strtoul(req.headers["content-length"].c_str(), NULL, 10) : 0;
  if (contentLength > MAX_REQUEST_SIZE) return false;
  std::string body = data.substr(headerEnd + 4);
  while (body.size() < contentLength) {
    int n = recv(client, buffer, sizeof(buffer), 0);
    if (n <= 0) return false;
    body.append(buffer, n);
  }
  body.resize(contentLength);

  size_t question = target.find('?');
  req.path = urlDecode(target.substr(0, question));
  if (question != std::string::npos) {
    parseUrlEncoded(target.substr(question + 1), req.args);
  }
  std::string contentType = req.headers["content-type"];
  std::string contentTypeLower = toLower(contentType);
  if (contentTypeLower.find("application/x-www-form-urlencoded") == 0) {
    parseUrlEncoded(body, req.args);
  } else if (contentTypeLower.find("multipart/form-data") == 0) {
    parseMultipart(body, headerParam(contentType, "boundary"), req);
  }
  return true;
}

static void sendAll(socket_t client, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    int n = send(client, data.data() + sent, (int)(data.size() - sent), SEND_FLAGS);
    if (n <= 0) return;
    sent += n;
  }
}

static void respond(socket_t client, int code, const std::string& contentType, const std::string& body, const std::string& extraHeaders = "") {
  const char* reason = code == 200 ? "OK" : code == 204 ? "No Content" : code == 400 ? "Bad Request" :
                       code == 404 ? "Not Found" : code == 405 ? "Method Not Allowed" : "Internal Server Error";
  // same CORS headers as WebServer::enableCORS() on the remote
  std::string head = "HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\n" +
                     "Content-Type: " + contentType + "\r\n" +
                     "Content-Length: " + std::to_string(body.size()) + "\r\n" +
                     "Access-Control-Allow-Origin: *\r\n"
                     "Access-Control-Allow-Methods: *\r\n"
                     "Access-Control-Allow-Headers: *\r\n"
                     "Cache-Control: no-store\r\n"
                     "Connection: close\r\n" + extraHeaders + "\r\n";
  sendAll(client, head + body);
}

static void sendText(socket_t client, int code, const std::string& text) {
  respond(client, code, "text/plain; charset=UTF-8", text);
}

static std::string htmlEscape(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '&') out += "&amp;";
    else if (s[i] == '<') out += "&lt;";
    else if (s[i] == '>') out += "&gt;";
    else if (s[i] == '\'') out += "&#39;";
    else out += s[i];
  }
  return out;
}

//---------------------------------------------------------------------
// API, same behaviour as on the remote
//---------------------------------------------------------------------

static bool resolveJsonName(std::string filename, std::string& name, std::string& error) {
  if (!filename.empty() && filename[0] == '/') filename.erase(0, 1);
  if (filename.empty()) { error = "Missing filename"; return false; }
  if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos || filename.find("..") != std::string::npos) {
    error = "Invalid filename";
    return false;
  }
  if (!endsWith(filename, ".json")) { error = "Only .json files can be accessed"; return false; }
  if (filename.size() + 1 > MAX_SPIFFS_PATH_LEN) {
    error = "File name too long: " + filename + " has " + std::to_string(filename.size()) +
            " characters, the remote supports at most " + std::to_string(MAX_SPIFFS_PATH_LEN - 1);
    return false;
  }
  name = filename;
  return true;
}

static unsigned long uptimeMs() {
  return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
}

static void handleStatus(socket_t client) {
  size_t used = 0;
  std::vector<ConfigFileInfo> files = listConfigFiles_simulator();
  for (size_t i = 0; i < files.size(); i++) used += files[i].size;
  JsonDocument doc;
  doc["name"] = "OMOTE";
  doc["api"] = OMOTE_WEB_API_VERSION;
  doc["simulator"] = true;
  doc["hostname"] = "localhost:" + std::to_string(httpPort);
  doc["ip"] = "127.0.0.1";
  doc["uptimeMs"] = uptimeMs();
  doc["fsTotal"] = SIMULATED_FS_SIZE;
  doc["fsUsed"] = used;
  doc["maxFilenameLength"] = MAX_SPIFFS_PATH_LEN - 1;
  doc["configDir"] = configDir_simulator();
  std::string out;
  serializeJson(doc, out);
  respond(client, 200, "application/json", out);
}

static void handleListJson(socket_t client, const Request& req) {
  std::vector<ConfigFileInfo> files = listConfigFiles_simulator();
  if (req.args.count("configTool")) {
    JsonDocument doc;
    JsonArray list = doc["files"].to<JsonArray>();
    for (size_t i = 0; i < files.size(); i++) {
      JsonObject entry = list.add<JsonObject>();
      entry["name"] = files[i].name;
      entry["size"] = files[i].size;
    }
    std::string out;
    serializeJson(doc, out);
    respond(client, 200, "application/json", out);
  } else {
    std::string html = "<!DOCTYPE html><html><head><title>JSON Files</title></head><body><h2>List of JSON Files</h2><ul>";
    for (size_t i = 0; i < files.size(); i++) {
      std::string name = htmlEscape(files[i].name);
      html += "<li><a href='/editJson?filename=" + name + "'>" + name + "</a></li>";
    }
    respond(client, 200, "text/html; charset=UTF-8", html + "</ul></body></html>");
  }
}

static void handleGetJson(socket_t client, const Request& req) {
  std::string name, error, content;
  if (!resolveJsonName(req.arg("filename"), name, error)) { sendText(client, 400, error); return; }
  if (!readConfigFile_HAL(name, content)) { sendText(client, 404, "File not found"); return; }
  respond(client, 200, "application/json", content);
}

// /smallUpload and the form of /editJson: action=Save with jsonContent, or action=Delete
static void handleSaveOrDelete(socket_t client, const Request& req, bool htmlResponse) {
  std::string name, error;
  if (!resolveJsonName(req.arg("filename"), name, error)) { sendText(client, 400, error); return; }
  std::string action = req.arg("action");
  if (action == "Save") {
    if (!req.args.count("jsonContent")) { sendText(client, 400, "Missing JSON content"); return; }
    JsonDocument doc;
    DeserializationError jsonError = deserializeJson(doc, req.arg("jsonContent"));
    if (jsonError) { sendText(client, 400, std::string("Invalid JSON data: ") + jsonError.c_str()); return; }
    std::string updatedJson;
    serializeJson(doc, updatedJson);
    if (!writeConfigFile_simulator(name, updatedJson)) { sendText(client, 500, "Could not write the file"); return; }
  } else if (action == "Delete") {
    if (!deleteConfigFile_simulator(name)) { sendText(client, 500, "Could not delete the file"); return; }
  } else {
    sendText(client, 400, "Invalid or missing action");
    return;
  }
  if (htmlResponse) {
    respond(client, 200, "text/html; charset=UTF-8", std::string("<h2>File ") + (action == "Save" ? "Updated" : "Deleted") + "</h2><a href='/listJson'>JSON List</a>");
  } else {
    sendText(client, 200, action == "Save" ? "File Updated" : "File Deleted");
  }
}

static void handleEditJson(socket_t client, const Request& req) {
  if (req.method == "POST") {
    handleSaveOrDelete(client, req, true);
    return;
  }
  std::string name, error, content, pretty;
  if (!resolveJsonName(req.arg("filename"), name, error)) { sendText(client, 400, error); return; }
  JsonDocument doc;
  if (!readConfigFile_HAL(name, content) || deserializeJson(doc, content)) {
    doc.to<JsonObject>();
  }
  serializeJsonPretty(doc, pretty);
  respond(client, 200, "text/html; charset=UTF-8",
    "<!DOCTYPE html><html><body><h2>Edit JSON File</h2><form action='/editJson' method='post'>"
    "<input type='hidden' name='filename' value='" + htmlEscape(name) + "'>"
    "<textarea name='jsonContent' rows='20' cols='60'>" + htmlEscape(pretty) + "</textarea><br>"
    "<input type='submit' name='action' value='Save'>"
    "<input type='submit' name='action' value='Delete' onclick=\"return confirm('Are you sure?');\">"
    "</form></body></html>");
}

// multipart upload of a file (action=Save) or action=Delete
static void handlePostJson(socket_t client, const Request& req) {
  std::string name, error;
  if (!resolveJsonName(req.arg("filename"), name, error)) { sendText(client, 400, error); return; }
  std::string action = req.arg("action");
  if (action == "Delete") {
    if (!deleteConfigFile_simulator(name)) { sendText(client, 500, "Could not delete the file"); return; }
    sendText(client, 200, "File Deleted");
  } else if (action == "Save") {
    if (!req.hasFile) { sendText(client, 400, "No file uploaded"); return; }
    if (!writeConfigFile_simulator(name, req.fileContent)) { sendText(client, 500, "Could not save the file"); return; }
    sendText(client, 200, "File Uploaded Successfully");
  } else {
    sendText(client, 400, "Invalid or missing action");
  }
}

static void handleSendCommand(socket_t client, const Request& req) {
  if ((thisSendConfiguredCommand_cb == NULL) || !thisSendConfiguredCommand_cb(req.arg("device"), req.arg("command"), req.arg("payload"))) {
    sendText(client, 404, "Command not found on the remote. Save it and restart the remote first.");
    return;
  }
  sendText(client, 200, "Command sent");
}

static void handleRestart(socket_t client) {
  sendText(client, 200, "Restarting");
  restartRequested = true;
  restartRequestedAt = std::chrono::steady_clock::now();
}

static void handleRoot(socket_t client) {
  std::string address = "http://localhost:" + std::to_string(httpPort);
  respond(client, 200, "text/html; charset=UTF-8",
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'><title>OMOTE simulator</title></head>"
    "<body style='font-family:sans-serif;max-width:40em;margin:2em auto;padding:0 1em'><h1>OMOTE simulator, setup mode</h1>"
    "<p>Set the remote address in the OMOTE Config app to <b>" + address + "</b>.</p>"
    "<p>Config files: <code>" + htmlEscape(configDir_simulator()) + "</code></p>"
    "<p><a href='/listJson'>Configuration files</a> &middot; <a href='/status'>Status</a></p></body></html>");
}

static void route(socket_t client, const Request& req) {
  const std::string& p = req.path;
  const bool get = (req.method == "GET");
  const bool post = (req.method == "POST");
  if (req.method == "OPTIONS") {
    // CORS preflight
    respond(client, 204, "text/plain", "", "Access-Control-Allow-Private-Network: true\r\nAccess-Control-Max-Age: 600\r\n");
  } else if (get && p == "/") {
    handleRoot(client);
  } else if (get && p == "/status") {
    handleStatus(client);
  } else if (get && p == "/listJson") {
    handleListJson(client, req);
  } else if (get && p == "/getJson") {
    handleGetJson(client, req);
  } else if (p == "/editJson") {
    handleEditJson(client, req);
  } else if (post && p == "/smallUpload") {
    handleSaveOrDelete(client, req, false);
  } else if (post && p == "/postJson") {
    handlePostJson(client, req);
  } else if (post && p == "/sendCommand") {
    handleSendCommand(client, req);
  } else if ((post && p == "/restart") ||
             (get && (p == "/registerDynamicDevices" || p == "/registerDynamicScenes" || p == "/registerDynamicGuis"))) {
    handleRestart(client);
  } else {
    sendText(client, 404, "Not found");
  }
}

//---------------------------------------------------------------------
// sockets
//---------------------------------------------------------------------

static void setNonBlocking(socket_t s, bool nonBlocking) {
  #if defined(WIN32)
  u_long mode = nonBlocking ? 1 : 0;
  ioctlsocket(s, FIONBIO, &mode);
  #else
  int flags = fcntl(s, F_GETFL, 0);
  fcntl(s, F_SETFL, nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
  fcntl(s, F_SETFD, FD_CLOEXEC); // not inherited by the restarted simulator
  #endif
}

static void prepareClientSocket(socket_t s) {
  setNonBlocking(s, false);
  #if defined(WIN32)
  DWORD timeout = 3000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
  #else
  struct timeval timeout;
  timeout.tv_sec = 3;
  timeout.tv_usec = 0;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  #if defined(__APPLE__)
  int on = 1;
  setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
  #endif
  #endif
}

void init_webserver_HAL() {
  #if defined(WIN32)
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
  #endif
  const char* portFromEnv = getenv("OMOTE_HTTP_PORT");
  if (portFromEnv != NULL && atoi(portFromEnv) > 0) {
    httpPort = atoi(portFromEnv);
  }

  socket_t s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == INVALID_SOCKET) {
    printf("Web config server: cannot create socket\r\n");
    return;
  }
  #if !defined(WIN32)
  // the restarted simulator binds the port again right away
  int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  #endif
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(httpPort);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 8) != 0) {
    printf("Web config server: cannot listen on port %d, set another one with OMOTE_HTTP_PORT\r\n", httpPort);
    closeSocket(s);
    return;
  }
  setNonBlocking(s, true);
  listenSocket = s;
  printf("Web config server: http://localhost:%d (answers while \"Web config\" is switched on in the settings)\r\n", httpPort);
}

static void restartSimulator() {
  // like ESP.restart() on the remote: start again, so the config files are read again
  printf("Restarting the simulator to apply the configuration ...\r\n");
  fflush(stdout);
  closeSocket(listenSocket);
  listenSocket = INVALID_SOCKET;
  #if defined(WIN32)
  _putenv((std::string(RESTART_ENV) + "=1").c_str());
  char path[MAX_PATH];
  GetModuleFileNameA(NULL, path, MAX_PATH);
  std::string quoted = std::string("\"") + path + "\"";
  const char* args[] = {quoted.c_str(), NULL};
  _execv(path, args);
  #else
  setenv(RESTART_ENV, "1", 1);
  char path[4096] = {0};
  #if defined(__APPLE__)
  uint32_t size = sizeof(path);
  if (_NSGetExecutablePath(path, &size) != 0) path[0] = '\0';
  #else
  ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
  path[n > 0 ? n : 0] = '\0';
  #endif
  // close everything else (display connection, MQTT socket), the new process opens its own
  for (int fd = 3; fd < 1024; fd++) {
    close(fd);
  }
  char* args[] = {path, NULL};
  execv(path, args);
  #endif
  printf("Restart failed (%s). Please start the simulator again.\r\n", strerror(errno));
  exit(1);
}

void webserver_handleClient_HAL() {
  if (listenSocket != INVALID_SOCKET) {
    // a few requests per loop, the config app sends them one after another
    for (int i = 0; i < 4; i++) {
      socket_t client = accept(listenSocket, NULL, NULL);
      if (client == INVALID_SOCKET) break;
      prepareClientSocket(client);
      Request req;
      if (readRequest(client, req)) {
        route(client, req);
      }
      shutdown(client, SHUTDOWN_SEND);
      closeSocket(client);
    }
  }

  if (restartRequested && (std::chrono::steady_clock::now() - restartRequestedAt > std::chrono::milliseconds(300))) {
    restartSimulator();
  }
}

bool webserver_consumeRestartIntoSetup_HAL() {
  const char* value = getenv(RESTART_ENV);
  bool requested = (value != NULL) && (strcmp(value, "1") == 0);
  #if defined(WIN32)
  _putenv((std::string(RESTART_ENV) + "=").c_str());
  #else
  unsetenv(RESTART_ENV);
  #endif
  return requested;
}

std::string webserver_getAddress_HAL() {
  return (listenSocket != INVALID_SOCKET) ? "localhost:" + std::to_string(httpPort) : "";
}

#endif
