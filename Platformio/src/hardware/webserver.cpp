#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <lvgl.h>
#include <ArduinoJson.h>
#include "hardware/webserver.h"
#include "gui_general_and_keys/guiBase.h"
#include "secrets.h"
#include "commandHandler.h"

#define FORMAT_SPIFFS_IF_FAILED true

WebServer server(80);


String dirs = "";
bool setupEnabled = false;

String listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    String data = "";
    data += "Listing directory: %s\r\n";
    data += dirname;

    File root = fs.open(dirname);
    if (!root)
    {
        data += "- failed to open directory";
        return data;
    }
    if (!root.isDirectory())
    {
        data += " - not a directory";
        return data;
    }

    File file = root.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            data += "  DIR : ";
            data += file.name();
            if (levels)
            {
                listDir(fs, file.path(), levels - 1);
            }
        }
        else
        {
            data += "  FILE: ";
            data += file.name();
            data += "\tSIZE: ";
            data += file.size();
        }
        file = root.openNextFile();
    }
    return data;
}

// Helper function to insert a value into a JSON document based on a path
void insertJsonValue(JsonDocument &root, const String &path, const String &value)
{
    // Split the path into components
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

String readFile(fs::FS &fs, const char *path)
{
    String outContent = "";
    Serial.printf("Reading file: %s\r\n", path);
    File file = fs.open(path);
    if (!file || file.isDirectory())
    {
        Serial.println("- failed to open file for reading");
        outContent = "- failed to open file for reading";
        return outContent;
    }
    Serial.println("- read from file:");
    // Create a buffer to hold file content + 1 for null terminator
    size_t fileSize = file.size();
    std::unique_ptr<char[]> buf(new char[fileSize + 1]);

    file.readBytes(buf.get(), fileSize);
    // Null-terminate the string
    buf[fileSize] = '\0';
    outContent = String(buf.get());
    file.close();
    Serial.println(outContent);
    return outContent;
}

void writeFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("Writing file: %s\r\n", path);

    File file = fs.open(path, FILE_WRITE);
    if (!file)
    {
        Serial.println("- failed to open file for writing");
        return;
    }
    if (file.print(message))
    {
        Serial.println("- file written");
    }
    else
    {
        Serial.println("- write failed");
    }
    file.close();
}

void renameFile(fs::FS &fs, const char *path1, const char *path2)
{
    Serial.printf("Renaming file %s to %s\r\n", path1, path2);
    if (fs.rename(path1, path2))
    {
        Serial.println("- file renamed");
    }
    else
    {
        Serial.println("- rename failed");
    }
}

void deleteFile(fs::FS &fs, const char *path)
{
    Serial.printf("Deleting file: %s\r\n", path);
    if (fs.remove(path))
    {
        Serial.println("- file deleted");
    }
    else
    {
        Serial.println("- delete failed");
    }
}

void webserver_setup()
{
    Serial.begin(115200);
    delay(100);
    if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED))
    {
        Serial.println("SPIFFS Mount Failed");
        return;
    }
    listDir(SPIFFS, "/", 0);



    Serial.println("Connecting to ");
    Serial.println(WIFI_SSID);

    // connect to your local wi-fi network
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // check wi-fi is connected to wi-fi network
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected..!");
    Serial.print("Got IP: ");
    Serial.println(WiFi.localIP());

    server.on("/", handle_OnConnect);
    server.on("/finishSetup", handle_FinishSetup);
    server.on("/wifi", handle_WiFiSettings);
    server.on("/devices", handle_DeviceSettings);
    server.on("/scenes", handle_SceneSettings);
    server.on("/editJson", HTTP_ANY, handleEditJson); // Handle both GET and POST
    server.on("/listJson", HTTP_GET, handleListJsonFiles);
    server.onNotFound(handle_NotFound);
    server.begin();
    Serial.println("HTTP server started");
}
void webserverHandleClient()
{
    server.handleClient();
}
void handle_WiFiSettings()
{
    server.send(200, "text/html", SendWifiPage());
}
void handle_FinishSetup()
{
    server.send(200, "text/html", SendFinishSetupPage());
}
void handle_DeviceSettings()
{
    server.send(200, "text/html", SendDevicePage());
}
void handle_SceneSettings()
{
    server.send(200, "text/html", SendScenePage());
}
void handle_OnConnect()
{

    server.send(200, "text/html", SendHomepage());
}

void handleEditJson()
{
    if (server.method() == HTTP_GET)
    {
        if (!server.hasArg("filename"))
        {
            server.send(400, "text/html", "Missing filename");
            return;
        }
        String filename = server.arg("filename");
        // Check if the file exists. If not, initialize content as empty.
        String fileContent = SPIFFS.exists(("/" + filename).c_str()) ? readFile(SPIFFS, ("/" + filename).c_str()) : "{}";
        JsonDocument jsonData; // Adjust size as needed
        deserializeJson(jsonData, fileContent);
        fileContent.clear();
        // Serialize JSON document for display
        String stringData;
        serializeJsonPretty(jsonData, stringData); // Correctly serialize JSON to string

        // Your existing code to construct HTML form...
        String htmlForm = "<textarea name='jsonContent' rows='10' cols='50'>" + stringData + "</textarea>";
        stringData.clear();
        String htmlPage = "<!DOCTYPE html><html><body>";

        htmlPage +=       "<h2>Edit JSON File</h2>"
                          "<form action='/editJson' method='post'>"
                          "<input type='hidden' name='filename' value='" +
                          filename + "'>" + htmlForm +
                          "<input type='submit' name='action' value='Save'>"
                          "<input type='submit' name='action' value='Delete' onclick=\"return confirm('Are you sure?');\">"
                          "</form></body></html>";
        server.send(200, "text/html; charset=UTF-8", htmlPage);
        server.begin();
        htmlPage.clear();
        htmlForm.clear();
        jsonData.clear();
    }
    else if (server.method() == HTTP_POST)
    {
        if (!server.hasArg("filename") || !server.hasArg("action"))
        {
            server.send(400, "text/html", "Missing data");
            return;
        }
        String filename = server.arg("filename");
        String action = server.arg("action");

        if (action == "Save")
        {
            if (!server.hasArg("jsonContent"))
            {
                server.send(400, "text/html", "Missing JSON content");
                return;
            }
            String jsonContent = server.arg("jsonContent");

            JsonDocument doc; // Adjust size as needed
            DeserializationError error = deserializeJson(doc, jsonContent);
            jsonContent.clear();
            if (error)
            {
                // Respond with an error message if JSON parsing fails
                server.send(400, "text/html", "Invalid JSON data");
                return;
            }

            // Serialize the validated JSON document back to a string
            String updatedJson;
            serializeJson(doc, updatedJson);

            writeFile(SPIFFS, ("/" + filename).c_str(), updatedJson.c_str()); // Save validated and updated JSON
            updatedJson.clear();
            server.send(200, "text/html", "<h2>File Updated</h2><a href='/listJson'>JSON List</a>");
            server.begin();
            doc.clear();
        }
        else if (action == "Delete")
        {
            deleteFile(SPIFFS, ("/" + filename).c_str());
            server.send(200, "text/html", "<h2>File Deleted</h2><a href='/listJson'>JSON List</a>");
        }
    }
    else
    {
        server.send(405, "text/html", "Method Not Allowed");
    }
}
void handleListJsonFiles()
{
    String html = "<!DOCTYPE html><html><head><title>JSON Files</title></head><body>";
    html += "<h2>List of JSON Files</h2>";
    html += "<ul>";

    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file)
    {
        if (String(file.name()).endsWith(".json"))
        {
            String fileName = String(file.name());
            // Create a link to the edit page for each JSON file
            html += "<li><a href='/editJson?filename=" + fileName + "'>" + fileName + "</a></li>";
        }
        file = root.openNextFile();
    }

    html += "</ul>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}
void handle_NotFound()
{
    server.send(404, "text/plain", "Not found");
}

String SendWifiPage()
{
    String ptr = siteHeader;
    ptr += "<h3>Wifi Details and Config (Be Careful)</h3>\n";

    ptr += "</body>\n";
    ptr += "</html>\n";
    return ptr;
}

String SendDevicePage()
{
    JsonDocument readData;
    Serial.println("Reading JSON from SPIFFS: ");
    String json = readFile(SPIFFS, "/test.json");
    deleteFile(SPIFFS, "/test.json");
    deleteFile(SPIFFS, "/hello.txt");

    deserializeJson(readData, json);
    String ptr = siteHeader;
    ptr += "<h3>Device Details and Config</h3>\n";
    ptr += "<p>";
    ptr += readData.as<String>();
    ptr += "</p>\n";

    ptr += "</body>\n";
    ptr += "</html>\n";
    readData.clear();
    return ptr;
}

String SendScenePage()
{
    String ptr = siteHeader;
    ptr += "<h3>Scene Details and Config</h3>\n";

    ptr += "</body>\n";
    ptr += "</html>\n";
    return ptr;
}

String SendHomepage()
{
    String ptr = siteHeader;
    ptr += "<h3>Welcome to OMOTE Config</h3>\n";
    ptr += "</body>\n";
    ptr += "</html>\n";
    return ptr;
}

String SendFinishSetupPage()
{
    setupEnabled = false;
    String ptr = siteHeader;
    ptr += "<h3>Successfully Finished Setup. If you'd like to return to setup, please throw the toggle on your remote.</h3>\n";
    ptr += "</body>\n";
    ptr += "</html>\n";
    return ptr;
}
