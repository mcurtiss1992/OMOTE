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

#define FORMAT_SPIFFS_IF_FAILED true

WebServer server(80);

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
    Serial.println(path);
    File file = fs.open(path);
    if (!file || file.isDirectory())
    {
        Serial.println(F("- failed to open file for reading"));
        outContent = F("- failed to open file for reading");
        return outContent;
    }
    Serial.println(F("- read from file:"));
    outContent = file.readString();
    // Serial.println(outContent);
    file.close();
    return outContent;
}

void writeFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.print(F("Writing file: "));
    Serial.println(path);

    File file = fs.open(path, FILE_WRITE);
    if (!file)
    {
        Serial.println(F("- failed to open file for writing"));
        return;
    }
    if (file.print(message))
    {
        Serial.println(F("- file written"));
    }
    else
    {
        Serial.println(F("- write failed"));
    }
    file.close();
}

void renameFile(fs::FS &fs, const char *path1, const char *path2)
{
    Serial.print(F("Renaming file "));
    Serial.print(path1);
    Serial.print(F(" to "));
    Serial.println(path2);
    if (fs.rename(path1, path2))
    {
        Serial.println(F("- file renamed"));
    }
    else
    {
        Serial.println(F("- rename failed"));
    }
}

void deleteFile(fs::FS &fs, const char *path)
{
    Serial.print(F("Deleting file: "));
    Serial.println(path);
    if (fs.remove(path))
    {
        Serial.println(F("- file deleted"));
    }
    else
    {
        Serial.println(F("- delete failed"));
    }
}

void webserver_setup()
{
    Serial.begin(115200);
    delay(100);
    if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED))
    {
        Serial.println(F("SPIFFS Mount Failed"));
        return;
    }
    Serial.println(listDir(SPIFFS, "/", 0));

    Serial.println(F("Connecting to "));
    Serial.println(WIFI_SSID);

    // Connect to WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        Serial.print(F("."));
    }
    Serial.println();
    Serial.println(F("WiFi connected..!"));
    Serial.print(F("Got IP: "));
    Serial.println(WiFi.localIP());

    // Setup routes
    server.on("/", handle_OnConnect);
    server.on("/finishSetup", handle_FinishSetup);
    server.on("/wifi", handle_WiFiSettings);
    server.on("/devices", handle_DeviceSettings);
    server.on("/scenes", handle_SceneSettings);
    server.on("/editJson", HTTP_ANY, handleEditJson);
    server.on("/listJson", HTTP_GET, handleListJsonFiles);
    server.on("/getJson", HTTP_GET, handleGetJson);
    server.on("/postJson", HTTP_POST, handlePutJson);
    server.on("/registerDynamicDevices", HTTP_GET, handleDynamicDeviceRegistration);
    server.on("/registerDynamicScenes", HTTP_GET, handleDynamicSceneRegistration);
    server.on("/registerDynamicGuis", HTTP_GET, handleDynamicGuiRegistration);
    server.onNotFound(handle_NotFound);
    server.begin();
    Serial.println(F("HTTP server started"));
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
        server.send(200, F("text/html; charset=UTF-8"), htmlPage);
        server.sendHeader("Connection", "close");
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
            jsonContent.clear();
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
        jsonPayload += F("]}");
        server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
        server.send(200, F("application/json"), jsonPayload);
        server.sendHeader("Connection", "close");
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
    }
}

void handle_NotFound()
{
    server.send(404, F("text/plain"), F("Not found"));
    server.sendHeader("Connection", "close");
}

void handleGetJson()
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
        server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
        server.send(200, F("text/json; charset=UTF-8"), stringData);
        fileContent.clear();
        stringData.clear();
        jsonData.clear();
        server.sendHeader("Connection", "close");
        return;
    }
    else
    {
        server.send(405, F("text/html"), F("Method Not Allowed"));
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
    Serial.println(F("Reading JSON from SPIFFS: "));
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
