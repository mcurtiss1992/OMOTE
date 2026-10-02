#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <lvgl.h>
#include <ArduinoJson.h>
#include "webserver_hal_esp32.h"
#include "secrets.h"
#include "dynamicConfig/devices_dynamic.h"
#include "dynamicConfig/scenes_dynamic.h"
#include "dynamicConfig/guis_dynamic.h"
#include "applicationInternal/omote_log.h"

#define FORMAT_SPIFFS_IF_FAILED true

WebServer server(80);

//---------------------------------------------------------------------
// Helper function: sends the provided data in 500-byte chunks
//---------------------------------------------------------------------
void sendChunkedResponse(const char* contentType, const String& data) {
  // Setting content length to unknown enables chunked encoding.
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, contentType, "");  // Send headers with no payload

  size_t dataLength = data.length();
  for (size_t i = 0; i < dataLength; i += 500) {
    // Get a substring of up to 500 bytes.
    String chunk = data.substring(i, min(i + 500, dataLength));
    server.sendContent(chunk);
    delay(1);  // small delay to allow background processing (optional)
  }
  server.sendContent("");  // Indicate end of response
  server.client().stop();  // Close the connection
}

//---------------------------------------------------------------------
// Existing helper functions remain unchanged...
//---------------------------------------------------------------------

String listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    String data = F("Listing directory: ");
    data += dirname;
    data += F("\r\n");

    File root = fs.open(dirname);
    if (!root)
    {
        data += F("- failed to open directory");
        return data;
    }
    if (!root.isDirectory())
    {
        data += F(" - not a directory");
        return data;
    }

    File file = root.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            data += F("  DIR : ");
            data += file.name();
            data += F("\r\n");
            if (levels)
            {
                data += listDir(fs, file.path(), levels - 1);
            }
        }
        else
        {
            data += F("  FILE: ");
            data += file.name();
            data += F("\tSIZE: ");
            data += String(file.size());
            data += F("\r\n");
        }
        file = root.openNextFile();
    }
    return data;
}

// Helper function to insert a value into a JSON document based on a path
void insertJsonValue(JsonDocument &root, const String &path, const String &value)
{
    int startIndex = 0;
    int endIndex = path.indexOf('[');
    JsonVariant current = root;

    while (endIndex != -1)
    {
        String key = path.substring(startIndex, endIndex);
        int arrayIndex = path.substring(endIndex + 1, path.indexOf(']', endIndex)).toInt();
        current = current[key][arrayIndex];

        startIndex = path.indexOf('[', endIndex) + 1;
        endIndex = path.indexOf('[', startIndex);
    }

    String finalKey = path.substring(startIndex);
    if (finalKey != "")
    {
        current[finalKey] = value;
    }
}

// Use file.readString() to simplify file reading and reduce extra allocations
String readFile(fs::FS &fs, const char *path)
{
    String outContent = "";
    Serial.print(F("Reading file: "));
    omote_log_i("%s", path);
    File file = fs.open(path);
    if (!file || file.isDirectory())
    {
        omote_log_e("- failed to open file for reading");
        outContent = F("- failed to open file for reading");
        return outContent;
    }
    omote_log_i("- read from file:");
    outContent = file.readString();
    // omote_log_i(outContent); // (optional debug output)
    file.close();
    return outContent;
}

void writeFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.print(F("Writing file: "));
    omote_log_i("%s", path);

    File file = fs.open(path, FILE_WRITE);
    if (!file)
    {
        omote_log_i("- failed to open file for writing");
        return;
    }
    if (file.print(message))
    {
        omote_log_i("- file written");
    }
    else
    {
        omote_log_i("- write failed");
    }
    file.close();
}

void renameFile(fs::FS &fs, const char *path1, const char *path2)
{
    Serial.print(F("Renaming file "));
    Serial.print(path1);
    Serial.print(F(" to "));
    omote_log_i("%s", path2);
    if (fs.rename(path1, path2))
    {
        omote_log_i("- file renamed");
    }
    else
    {
        omote_log_i("- rename failed");
    }
}

void deleteFile(fs::FS &fs, const char *path)
{
    Serial.print(F("Deleting file: "));
    omote_log_i("%s", path);
    if (fs.remove(path))
    {
        omote_log_i("- file deleted");
    }
    else
    {
        omote_log_i("- delete failed");
    }
}

//---------------------------------------------------------------------
// Webserver setup and route handlers
//---------------------------------------------------------------------

// In your webserver setup, register the /postJson route with both the main and upload handlers
void webserver_setup() {
    Serial.begin(115200);
    delay(100);
    if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
      omote_log_i("SPIFFS Mount Failed");
      return;
    }
    omote_log_i("%s", listDir(SPIFFS, "/", 0).c_str());
  
    omote_log_i("Connecting to ");
    omote_log_i("%s", WIFI_SSID);
  
    // Connect to WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.print(F("."));
    }
    omote_log_i("WiFi connected..!");
    Serial.print(F("Got IP: "));
    omote_log_i("%s", WiFi.localIP().toString().c_str());
  
    // Other route setups...
    server.on("/", handle_OnConnect);
    server.on("/finishSetup", handle_FinishSetup);
    server.on("/wifi", handle_WiFiSettings);
    server.on("/devices", handle_DeviceSettings);
    server.on("/scenes", handle_SceneSettings);
    server.on("/editJson", HTTP_ANY, handleEditJson);
    server.on("/listJson", HTTP_GET, handleListJsonFiles);
    server.on("/getJson", HTTP_GET, handleGetJson);
  
    // Register the /postJson route with both main and upload handlers
    server.on("/postJson", HTTP_POST, handlePostJson, handlePostJsonUpload);
    server.on("/smallUpload", HTTP_POST, handlePutJson);
  
    server.on("/registerDynamicDevices", HTTP_GET, handleDynamicDeviceRegistration);
    server.on("/registerDynamicScenes", HTTP_GET, handleDynamicSceneRegistration);
    server.on("/registerDynamicGuis", HTTP_GET, handleDynamicGuiRegistration);
    server.onNotFound(handle_NotFound);
    server.begin();
    omote_log_i("HTTP server started");
  }

void webserverHandleClient()
{
    server.handleClient();
}

void handle_WiFiSettings()
{
    server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
    server.send(200, F("text/html"), SendWifiPage());
    server.sendHeader("Connection", "close");
}

void handle_FinishSetup()
{
    server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
    server.send(200, F("text/html"), SendFinishSetupPage());
    server.sendHeader("Connection", "close");
}

void handle_DeviceSettings()
{
    server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
    server.send(200, F("text/html"), SendDevicePage());
    server.sendHeader("Connection", "close");
}

void handle_SceneSettings()
{
    server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
    server.send(200, F("text/html"), SendScenePage());
    server.sendHeader("Connection", "close");
}

void handle_OnConnect()
{
    server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
    server.send(200, F("text/html"), SendHomepage());
    server.sendHeader("Connection", "close");
}

void handleDynamicDeviceRegistration()
{
    register_dynamic_devices();
    server.send(200, F("text/html"), F("Registered Your Devices"));
    server.sendHeader("Connection", "close");
}

void handleDynamicSceneRegistration()
{
    register_dynamic_scenes();
    server.send(200, F("text/html"), F("Registered Your Scenes"));
    server.sendHeader("Connection", "close");
}

void handleDynamicGuiRegistration()
{
    register_dynamic_guis();
    server.send(200, F("text/html"), F("Registered Your Guis"));
    server.sendHeader("Connection", "close");
}

//---------------------------------------------------------------------
// Modified handleEditJson: GET branch uses chunked response to send file content
//---------------------------------------------------------------------
void handleEditJson()
{
    if (server.method() == HTTP_GET)
    {
        if (!server.hasArg("filename"))
        {
            server.send(400, F("text/html"), F("Missing filename"));
            server.sendHeader("Connection", "close");
            return;
        }
        String filename = server.arg("filename");
        String filePath = "/" + filename;
        String fileContent = SPIFFS.exists(filePath.c_str()) ? readFile(SPIFFS, filePath.c_str()) : F("{}");
        JsonDocument jsonData;
        deserializeJson(jsonData, fileContent);
        fileContent = "";
        String stringData;
        serializeJsonPretty(jsonData, stringData);

        // Build HTML form using += to append flash strings safely
        String htmlForm = String(F("<textarea name='jsonContent' rows='10' cols='50'>"));
        htmlForm += stringData;
        htmlForm += F("</textarea>");

        String htmlPage = String(F("<!DOCTYPE html><html><body>"));
        htmlPage += F("<h2>Edit JSON File</h2>"
                      "<form action='/editJson' method='post'>"
                      "<input type='hidden' name='filename' value='");
        htmlPage += filename;
        htmlPage += F("'>");
        htmlPage += htmlForm;
        htmlPage += F("<input type='submit' name='action' value='Save'>"
                      "<input type='submit' name='action' value='Delete' onclick=\"return confirm('Are you sure?');\">"
                      "</form></body></html>");
        server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
        // Send the HTML page in 500-byte chunks:
        sendChunkedResponse("text/html; charset=UTF-8", htmlPage);
        return;
    }
    else if (server.method() == HTTP_POST)
    {
        if (!server.hasArg("filename") || !server.hasArg("action"))
        {
            server.send(400, F("text/html"), F("Missing data"));
            server.sendHeader("Connection", "close");
            return;
        }
        String filename = server.arg("filename");
        String action = server.arg("action");

        if (action.equals(F("Save")))
        {
            if (!server.hasArg("jsonContent"))
            {
                server.send(400, F("text/html"), F("Missing JSON content"));
                server.sendHeader("Connection", "close");
                return;
            }
            String jsonContent = server.arg("jsonContent");
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, jsonContent);
            jsonContent = "";
            if (error)
            {
                server.send(400, F("text/html"), F("Invalid JSON data"));
                server.sendHeader("Connection", "close");
                return;
            }
            String updatedJson;
            serializeJson(doc, updatedJson);
            String filePath = "/" + filename;
            writeFile(SPIFFS, filePath.c_str(), updatedJson.c_str());
            server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
            server.send(200, F("text/html"), F("<h2>File Updated</h2><a href='/listJson'>JSON List</a>"));
            doc.clear();
            updatedJson.clear();
            server.sendHeader("Connection", "close");
            return;
        }
        else if (action.equals(F("Delete")))
        {
            String filePath = "/" + filename;
            deleteFile(SPIFFS, filePath.c_str());
            server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
            server.send(200, F("text/html"), F("<h2>File Deleted</h2><a href='/listJson'>JSON List</a>"));
            server.sendHeader("Connection", "close");
            return;
        }
    }
    else
    {
        server.send(405, F("text/html"), F("Method Not Allowed"));
        server.sendHeader("Connection", "close");
    }
}

//---------------------------------------------------------------------
// Modified handleGetJson: returns JSON file content in 500-byte chunks
//---------------------------------------------------------------------
void handleGetJson() {
    if (server.method() == HTTP_GET) {
      if (!server.hasArg("filename")) {
        server.send(400, "text/html", "Missing filename");
        server.sendHeader("Connection", "close");
        return;
      }
      String filename = server.arg("filename");
      String filePath = "/" + filename;
      if (!SPIFFS.exists(filePath.c_str())) {
        writeFile(SPIFFS, filePath.c_str(), "{}");
      }
      File file = SPIFFS.open(filePath.c_str(), "r");
      if (!file) {
        server.send(500, "text/html", "Failed to open file");
        server.sendHeader("Connection", "close");
        return;
      }
  
      server.sendHeader("Access-Control-Allow-Origin", "*");
      // Set content length to unknown to trigger chunked encoding
      server.setContentLength(CONTENT_LENGTH_UNKNOWN);
      server.send(200, "text/json; charset=UTF-8", "");
  
      const size_t chunkSize = 500;
      uint8_t buffer[chunkSize];
      while (file.available()) {
        size_t bytesRead = file.read(buffer, chunkSize);
        // Send exactly the number of bytes read
        server.sendContent(String((char*)buffer).substring(0, bytesRead));
        delay(1);  // Small delay to allow background processing
      }
      file.close();
      server.sendContent(""); // Indicate end of response
      server.client().stop();
    }
    else {
      server.send(405, "text/html", "Method Not Allowed");
      server.sendHeader("Connection", "close");
    }
  }

void handleListJsonFiles()
{
    if (server.hasArg("configTool"))
    {
        String jsonPayload = F("{\"files\":[");
        bool first = true;
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        while (file)
        {
            String fname = file.name();
            if (fname.endsWith(".json"))
            {
                if (!first)
                {
                    jsonPayload += F(",");
                }
                jsonPayload += F("{\"name\":\"");
                jsonPayload += fname;
                jsonPayload += F("\",\"size\":");
                jsonPayload += String(file.size());
                jsonPayload += F("}");
                first = false;
            }
            file = root.openNextFile();
        }
        file.close();
        jsonPayload += F("]}");
        server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
        server.send(200, F("application/json"), jsonPayload);
        server.sendHeader("Connection", "close");
        jsonPayload = "";
        root.close();
        return;
    }
    else
    {
        String html = String(F("<!DOCTYPE html><html><head><title>JSON Files</title></head><body>"));
        html += F("<h2>List of JSON Files</h2>");
        html += F("<ul>");

        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        while (file)
        {
            String fname = file.name();
            if (fname.endsWith(".json"))
            {
                html += F("<li><a href='/editJson?filename=");
                html += fname;
                html += F("'>");
                html += fname;
                html += F("</a></li>");
            }
            file = root.openNextFile();
        }

        html += F("</ul>");
        html += F("</body></html>");
        server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
        server.send(200, F("text/html"), html);
        server.sendHeader("Connection", "close");
        root.close();
    }
}

void handle_NotFound()
{
    server.send(404, F("text/plain"), F("Not found"));
    server.sendHeader("Connection", "close");
}



// Global file handle for /postJson uploads
File fsPostJsonFile;

// Upload handler for the /postJson route
void handlePostJsonUpload() {

  HTTPUpload& upload = server.upload();


  if (upload.status == UPLOAD_FILE_START) {
    // Determine the filename from the POST field "filename"
    String filename = server.arg("filename");
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    Serial.printf("PostJson Upload Start: %s\n", filename.c_str());
    fsPostJsonFile = SPIFFS.open(filename, FILE_WRITE);
    if (!fsPostJsonFile) {
      Serial.println("Failed to open file for writing");
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
    // Write incoming data in chunks. Adjust chunk size if needed.
    if (fsPostJsonFile) {
      fsPostJsonFile.write(upload.buf, upload.currentSize);
    }
  }
  else if (upload.status == UPLOAD_FILE_END) {
    // Close the file once the upload is complete
    if (fsPostJsonFile) {
      fsPostJsonFile.close();
      Serial.printf("PostJson Upload End: %s, %u bytes\n", server.arg("filename").c_str(), upload.totalSize);
    }
    // Optionally, you could validate the JSON here before sending a response.
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "File Uploaded Successfully");
    // Note: Do not call server.client().stop() here as the WebServer library handles it.
  }
}

// Main handler for /postJson route
void handlePostJson() {
  // For the Delete action, handle it here
  if (server.hasArg("action") && server.arg("action").equals("Delete")) {
    String filename = server.arg("filename");
    String filePath = "/" + filename;
    deleteFile(SPIFFS, filePath.c_str());
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/html", "File Deleted");
    server.sendHeader("Connection", "close");
  }
  // For Save action, the response is already sent from the upload handler.
  // If no file is uploaded, you can send an error response here.
  else if (!server.hasArg("action") || !server.arg("action").equals("Save")) {
    server.send(400, "text/html", "Invalid or missing action");
    server.sendHeader("Connection", "close");
  }
}




void handlePutJson()
{
    if (server.method() == HTTP_POST)
    {
        if (!server.hasArg("filename") || !server.hasArg("action"))
        {
            server.send(400, F("text/html"), F("Missing data"));
            server.sendHeader("Connection", "close");
            return;
        }
        String filename = server.arg("filename");
        String action = server.arg("action");

        if (action.equals(F("Save")))
        {
            if (!server.hasArg("jsonContent"))
            {
                server.send(400, F("text/html"), F("Missing JSON content"));
                server.sendHeader("Connection", "close");
                return;
            }
            String jsonContent = server.arg("jsonContent");
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, jsonContent);
            jsonContent = "";
            if (error)
            {
                server.send(400, F("text/html"), F("Invalid JSON data"));
                server.sendHeader("Connection", "close");
                return;
            }
            String updatedJson;
            serializeJson(doc, updatedJson);
            String filePath = "/" + filename;
            writeFile(SPIFFS, filePath.c_str(), updatedJson.c_str());
            server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
            server.send(200, F("text/html"), F("File Updated"));
            doc.clear();
            updatedJson.clear();
            server.sendHeader("Connection", "close");
            return;
        }
        else if (action.equals(F("Delete")))
        {
            String filePath = "/" + filename;
            deleteFile(SPIFFS, filePath.c_str());
            server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
            server.send(200, F("text/html"), F("File Deleted"));
            server.sendHeader("Connection", "close");
            return;
        }
    }
    else
    {
        server.send(405, F("text/html"), F("Method Not Allowed"));
        server.sendHeader("Connection", "close");
    }
}

String SendWifiPage()
{
    // Assuming siteHeader is defined as a constant string
    String ptr = String(siteHeader);
    ptr += F("<h3>Wifi Details and Config (Be Careful)</h3>\n");
    ptr += F("</body>\n</html>\n");
    return ptr;
}

String SendDevicePage()
{
    JsonDocument readData;
    omote_log_i("Reading JSON from SPIFFS: ");
    String json = readFile(SPIFFS, "/test.json");
    deleteFile(SPIFFS, "/test.json");
    deleteFile(SPIFFS, "/hello.txt");

    deserializeJson(readData, json);
    String ptr = String(siteHeader);
    ptr += F("<h3>Device Details and Config</h3>\n<p>");
    ptr += readData.as<String>();
    ptr += F("</p>\n</body>\n</html>\n");
    return ptr;
}

String SendScenePage()
{
    String ptr = String(siteHeader);
    ptr += F("<h3>Scene Details and Config</h3>\n");
    ptr += F("</body>\n</html>\n");
    return ptr;
}

String SendHomepage()
{
    String ptr = String(siteHeader);
    ptr += F("<h3>Welcome to OMOTE Config</h3>\n");
    ptr += F("</body>\n</html>\n");
    return ptr;
}

String SendFinishSetupPage()
{
    String ptr = String(siteHeader);
    ptr += F("<h3>Successfully Finished Setup. If you'd like to return to setup, please throw the toggle on your remote.</h3>\n");
    ptr += F("</body>\n</html>\n");
    return ptr;
}
