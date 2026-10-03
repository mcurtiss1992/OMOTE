#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_attr.h>
#include <esp_system.h>
#include "webserver_hal_esp32.h"
#include "preferencesStorage_hal_esp32.h"
#include "dynamicConfig/devices_dynamic.h"
#include "applicationInternal/commandHandler.h"
#include "applicationInternal/omote_log.h"

#if ENABLE_WIFI_AND_MQTT == 1

// Version of the HTTP API, reported by /status so the config app can tell what the remote supports
#define OMOTE_WEB_API_VERSION 2
// SPIFFS object names are limited to 31 characters including the leading "/".
// Longer names cannot be created, so they are rejected with an error instead of failing silently.
#define MAX_SPIFFS_PATH_LEN 31
#define UPLOAD_TMP_PATH "/upload.tmp"

WebServer server(80);

static bool mdnsStarted = false;
static unsigned long restartRequestedAt = 0;
static bool restartRequested = false;

// Survives ESP.restart() (but not a power cycle), so that a restart requested from the
// config app comes back up in setup mode.
#define RESTART_INTO_SETUP_MAGIC 0x5E7095E7
RTC_NOINIT_ATTR static uint32_t restartIntoSetup;

//---------------------------------------------------------------------
// Helpers
//---------------------------------------------------------------------

// Builds "/<filename>" and checks it. Returns false and sets error if the name cannot be used.
static bool resolveJsonPath(String filename, String& path, String& error) {
  if (filename.startsWith("/")) {
    filename = filename.substring(1);
  }
  if (filename.length() == 0) {
    error = F("Missing filename");
    return false;
  }
  if ((filename.indexOf('/') >= 0) || (filename.indexOf('\\') >= 0) || (filename.indexOf("..") >= 0)) {
    error = F("Invalid filename");
    return false;
  }
  if (!filename.endsWith(".json")) {
    error = F("Only .json files can be accessed");
    return false;
  }
  path = "/" + filename;
  if (path.length() > MAX_SPIFFS_PATH_LEN) {
    error = String(F("File name too long: ")) + filename + F(" has ") + String(filename.length()) +
            F(" characters, the remote supports at most ") + String(MAX_SPIFFS_PATH_LEN - 1);
    return false;
  }
  return true;
}

static bool writeFile(const String& path, const String& content) {
  File file = SPIFFS.open(path, FILE_WRITE);
  if (!file) {
    omote_log_e("Failed to open %s for writing\r\n", path.c_str());
    return false;
  }
  size_t written = file.print(content);
  file.close();
  if (written != content.length()) {
    omote_log_e("Writing %s failed (%u of %u bytes)\r\n", path.c_str(), written, content.length());
    SPIFFS.remove(path);
    return false;
  }
  return true;
}

static bool deleteFile(const String& path) {
  if (!SPIFFS.exists(path)) {
    return true; // nothing to do
  }
  return SPIFFS.remove(path);
}

static void sendText(int code, const String& text) {
  server.send(code, F("text/plain; charset=UTF-8"), text);
}

static void scheduleRestartIntoSetup() {
  restartIntoSetup = RESTART_INTO_SETUP_MAGIC;
  restartRequested = true;
  restartRequestedAt = millis();
}

//---------------------------------------------------------------------
// Route handlers
//---------------------------------------------------------------------

static void handleRoot() {
  String html = F("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
                  "<title>OMOTE</title></head><body style='font-family:sans-serif;max-width:40em;margin:2em auto;padding:0 1em'>"
                  "<h1>OMOTE setup mode</h1>"
                  "<p>Open the OMOTE Config app and set the remote address to <b>http://" OMOTE_HOSTNAME ".local</b> or <b>http://");
  html += WiFi.localIP().toString();
  html += F("</b>.</p><p><a href='/listJson'>Configuration files</a> &middot; <a href='/status'>Status</a></p></body></html>");
  server.send(200, F("text/html; charset=UTF-8"), html);
}

static void handleStatus() {
  JsonDocument doc;
  doc["name"] = "OMOTE";
  doc["api"] = OMOTE_WEB_API_VERSION;
  #ifdef OMOTE_HARDWARE_REV
  doc["hardwareRevision"] = OMOTE_HARDWARE_REV;
  #endif
  doc["hostname"] = OMOTE_HOSTNAME ".local";
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["uptimeMs"] = millis();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["minFreeHeap"] = ESP.getMinFreeHeap();
  doc["maxAllocHeap"] = ESP.getMaxAllocHeap();
  doc["fsTotal"] = SPIFFS.totalBytes();
  doc["fsUsed"] = SPIFFS.usedBytes();
  doc["maxFilenameLength"] = MAX_SPIFFS_PATH_LEN - 1;
  String out;
  serializeJson(doc, out);
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.send(200, F("application/json"), out);
}

// Returns the content of a JSON file. 404 if it does not exist (it is no longer created as a side effect).
static void handleGetJson() {
  String path, error;
  if (!resolveJsonPath(server.arg("filename"), path, error)) {
    sendText(400, error);
    return;
  }
  if (!SPIFFS.exists(path)) {
    sendText(404, F("File not found"));
    return;
  }
  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    sendText(500, F("Failed to open file"));
    return;
  }
  // streamFile sends the file in large blocks with a Content-Length header.
  // The old 500 byte chunk loop needed a delay(1) per chunk and read past the end of its buffer.
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.streamFile(file, F("application/json"));
  file.close();
}

static void handleListJsonFiles() {
  bool asJson = server.hasArg("configTool");
  JsonDocument doc;
  JsonArray files = doc["files"].to<JsonArray>();
  String html = F("<!DOCTYPE html><html><head><title>JSON Files</title></head><body><h2>List of JSON Files</h2><ul>");

  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    String fname = file.name();
    if (fname.startsWith("/")) {
      fname = fname.substring(1);
    }
    if (fname.endsWith(".json")) {
      if (asJson) {
        JsonObject entry = files.add<JsonObject>();
        entry["name"] = fname;
        entry["size"] = file.size();
      } else {
        html += F("<li><a href='/editJson?filename=");
        html += fname;
        html += F("'>");
        html += fname;
        html += F("</a></li>");
      }
    }
    file = root.openNextFile();
  }
  root.close();

  if (asJson) {
    String out;
    serializeJson(doc, out);
    server.sendHeader(F("Cache-Control"), F("no-store"));
    server.send(200, F("application/json"), out);
  } else {
    html += F("</ul></body></html>");
    server.send(200, F("text/html; charset=UTF-8"), html);
  }
}

// Shared by /smallUpload and the form of /editJson: action=Save with jsonContent, or action=Delete
static void handleSaveOrDelete(bool htmlResponse) {
  String path, error;
  if (!resolveJsonPath(server.arg("filename"), path, error)) {
    sendText(400, error);
    return;
  }
  String action = server.arg("action");

  if (action.equals(F("Save"))) {
    if (!server.hasArg("jsonContent")) {
      sendText(400, F("Missing JSON content"));
      return;
    }
    JsonDocument doc;
    DeserializationError jsonError = deserializeJson(doc, server.arg("jsonContent"));
    if (jsonError) {
      sendText(400, String(F("Invalid JSON data: ")) + jsonError.c_str());
      return;
    }
    String updatedJson;
    serializeJson(doc, updatedJson);
    doc.clear();
    if (!writeFile(path, updatedJson)) {
      sendText(500, F("Could not write the file. Is the flash full?"));
      return;
    }
  } else if (action.equals(F("Delete"))) {
    if (!deleteFile(path)) {
      sendText(500, F("Could not delete the file"));
      return;
    }
  } else {
    sendText(400, F("Invalid or missing action"));
    return;
  }

  if (htmlResponse) {
    server.send(200, F("text/html; charset=UTF-8"), action.equals(F("Save")) ?
      F("<h2>File Updated</h2><a href='/listJson'>JSON List</a>") : F("<h2>File Deleted</h2><a href='/listJson'>JSON List</a>"));
  } else {
    sendText(200, action.equals(F("Save")) ? F("File Updated") : F("File Deleted"));
  }
}

static void handlePutJson() {
  handleSaveOrDelete(false);
}

// Minimal built-in editor, usable without the config app
static void handleEditJson() {
  if (server.method() == HTTP_POST) {
    handleSaveOrDelete(true);
    return;
  }
  String path, error;
  if (!resolveJsonPath(server.arg("filename"), path, error)) {
    sendText(400, error);
    return;
  }
  JsonDocument jsonData;
  if (!loadJsonFile(path.c_str(), jsonData)) {
    jsonData.to<JsonObject>();
  }
  String stringData;
  serializeJsonPretty(jsonData, stringData);
  jsonData.clear();
  stringData.replace("&", "&amp;");
  stringData.replace("<", "&lt;");

  String filename = path.substring(1);
  String htmlPage = F("<!DOCTYPE html><html><body><h2>Edit JSON File</h2>"
                      "<form action='/editJson' method='post'><input type='hidden' name='filename' value='");
  htmlPage += filename;
  htmlPage += F("'><textarea name='jsonContent' rows='20' cols='60'>");
  htmlPage += stringData;
  htmlPage += F("</textarea><br><input type='submit' name='action' value='Save'>"
                "<input type='submit' name='action' value='Delete' onclick=\"return confirm('Are you sure?');\">"
                "</form></body></html>");
  server.send(200, F("text/html; charset=UTF-8"), htmlPage);
}

// /postJson: multipart upload of a JSON file (action=Save, field "filename" before the file part)
// or action=Delete. The upload is written to a temporary file first, so an aborted upload
// cannot leave a truncated config file behind.
static File uploadFile;
static String uploadTargetPath;
static String uploadError;
static bool uploadReceived = false;

static void handlePostJsonUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    uploadReceived = true;
    uploadError = "";
    String filename = server.hasArg("filename") ? server.arg("filename") : upload.filename;
    if (!resolveJsonPath(filename, uploadTargetPath, uploadError)) {
      return;
    }
    uploadFile = SPIFFS.open(UPLOAD_TMP_PATH, FILE_WRITE);
    if (!uploadFile) {
      uploadError = F("Failed to open file for writing");
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && (uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize)) {
      uploadError = F("Could not write the file. Is the flash full?");
      uploadFile.close();
      SPIFFS.remove(UPLOAD_TMP_PATH);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      deleteFile(uploadTargetPath);
      if (!SPIFFS.rename(UPLOAD_TMP_PATH, uploadTargetPath)) {
        uploadError = F("Could not save the file");
      }
      omote_log_i("Upload of %s finished, %u bytes\r\n", uploadTargetPath.c_str(), upload.totalSize);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) {
      uploadFile.close();
    }
    SPIFFS.remove(UPLOAD_TMP_PATH);
    uploadError = F("Upload aborted");
  }
}

static void handlePostJson() {
  String action = server.arg("action");
  if (action.equals(F("Delete"))) {
    String path, error;
    if (!resolveJsonPath(server.arg("filename"), path, error)) {
      sendText(400, error);
    } else if (!deleteFile(path)) {
      sendText(500, F("Could not delete the file"));
    } else {
      sendText(200, F("File Deleted"));
    }
  } else if (action.equals(F("Save"))) {
    if (!uploadReceived) {
      sendText(400, F("No file uploaded"));
    } else if (uploadError.length() > 0) {
      sendText(uploadError.startsWith(F("File name")) || uploadError.startsWith(F("Invalid")) ? 400 : 500, uploadError);
    } else {
      sendText(200, F("File Uploaded Successfully"));
    }
  } else {
    sendText(400, F("Invalid or missing action"));
  }
  uploadReceived = false;
  uploadError = "";
}

// Executes a configured command, so the config app can test it. Only commands known since the last restart are found.
static void handleSendCommand() {
  String device = server.arg("device");
  String command = server.arg("command");
  uint16_t commandId = resolveDynamicCommand(device.c_str(), command.c_str());
  if (commandId == DYNAMIC_COMMAND_NOT_FOUND) {
    sendText(404, F("Command not found on the remote. Save it and restart the remote first."));
    return;
  }
  executeCommand(commandId, std::string(server.arg("payload").c_str()));
  sendText(200, F("Command sent"));
}

// The remote reads its configuration only during startup, so changes are applied with a restart.
static void handleRestart() {
  sendText(200, F("Restarting"));
  scheduleRestartIntoSetup();
}

static void handleNotFound() {
  if (server.method() == HTTP_OPTIONS) {
    // CORS preflight. The Access-Control-Allow-* headers are added by enableCORS().
    server.sendHeader(F("Access-Control-Allow-Private-Network"), F("true"));
    server.sendHeader(F("Access-Control-Max-Age"), F("600"));
    server.send(204);
    return;
  }
  sendText(404, F("Not found"));
}

//---------------------------------------------------------------------
// Webserver setup and loop
//---------------------------------------------------------------------

void webserver_setup() {
  mountConfigFS();

  // Answer every request, including errors, with CORS headers. The config app is served from another origin.
  server.enableCORS(true);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/listJson", HTTP_GET, handleListJsonFiles);
  server.on("/getJson", HTTP_GET, handleGetJson);
  server.on("/editJson", HTTP_ANY, handleEditJson);
  server.on("/smallUpload", HTTP_POST, handlePutJson);
  server.on("/postJson", HTTP_POST, handlePostJson, handlePostJsonUpload);
  server.on("/sendCommand", HTTP_POST, handleSendCommand);
  server.on("/restart", HTTP_POST, handleRestart);
  // Re-registering at runtime duplicated scenes and commands. A restart applies the configuration cleanly.
  server.on("/registerDynamicDevices", HTTP_GET, handleRestart);
  server.on("/registerDynamicScenes", HTTP_GET, handleRestart);
  server.on("/registerDynamicGuis", HTTP_GET, handleRestart);
  server.onNotFound(handleNotFound);

  // Listening does not need a WiFi connection. The old code waited here for WiFi
  // (forever, if the network was not available), which blocked the whole boot.
  server.begin();
  omote_log_i("HTTP server started\r\n");
}

void webserverHandleClient() {
  if (!mdnsStarted && WiFi.isConnected()) {
    if (MDNS.begin(OMOTE_HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      omote_log_i("mDNS started: http://%s.local\r\n", OMOTE_HOSTNAME);
    }
    mdnsStarted = true;
  }

  server.handleClient();

  if (restartRequested && (millis() - restartRequestedAt > 500)) {
    // give the response time to leave, keep the active scene/gui
    save_preferences_HAL();
    ESP.restart();
  }
}

bool webserver_consumeRestartIntoSetup() {
  bool requested = (restartIntoSetup == RESTART_INTO_SETUP_MAGIC) && (esp_reset_reason() == ESP_RST_SW);
  restartIntoSetup = 0;
  return requested;
}

std::string webserver_getAddress() {
  if (!WiFi.isConnected()) {
    return "";
  }
  return std::string(OMOTE_HOSTNAME ".local / ") + WiFi.localIP().toString().c_str();
}

#endif
