#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>

#include <ESPAsyncWebClient.h>

// Replace with your network credentials
constexpr const char* WIFI_SSID = "YOUR_WIFI";
constexpr const char* WIFI_PASS = "YOUR_PASSWORD";

// Replace with HTTP (non-HTTPS) endpoints that serve chunked and plain responses
constexpr const char* CHUNKED_URL = "http://example.com/chunked.bin";
constexpr const char* PLAIN_URL = "http://example.com/plain.bin";

AsyncHttpClient client;
File outputFile;
String currentPath;
bool plainQueued = false;

static bool beginDownload(const char* url, const char* destinationPath) {
    if (outputFile) {
        outputFile.close();
    }

    outputFile = SD.open(destinationPath, FILE_WRITE);
    if (!outputFile) {
        Serial.printf("Failed to open %s on SD card\r\n", destinationPath);
        return false;
    }

    currentPath = destinationPath;

    AsyncHttpRequest* request = new AsyncHttpRequest(HTTP_GET, url);
    request->setNoStoreBody(true); // only stream via onBodyChunk

    uint32_t id = client.request(
        request,
        [](AsyncHttpResponse* response) {
            Serial.printf("Download complete (%d). Reported length: %u\r\n", response->getStatusCode(),
                          static_cast<unsigned int>(response->getContentLength()));
            if (!plainQueued) {
                plainQueued = true;
                if (!beginDownload(PLAIN_URL, "/plain.bin")) {
                    Serial.println("Second download could not start. Check SD card and URLs.");
                }
            }
        },
        [](HttpClientError error, const char* message) {
            Serial.printf("Download failed (%d): %s\r\n", error, message ? message : "");
        });

    Serial.printf("Started request %u for %s\r\n", id, destinationPath);
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("Mounting SD card...");
    if (!SD.begin()) {
        Serial.println("SD.begin() failed; aborting example.");
        return;
    }

    Serial.printf("Connecting to WiFi %s...\r\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print(".");
    }
    Serial.println("\r\nWiFi connected.");

    client.setFollowRedirects(true, 3);

    client.onBodyChunk([](const char* data, size_t len, bool final) {
        if (data && len > 0 && outputFile) {
            outputFile.write(reinterpret_cast<const uint8_t*>(data), len);
        }
        if (final && outputFile) {
            outputFile.flush();
            outputFile.close();
            Serial.printf("Saved file to %s\r\n", currentPath.c_str());
        }
    });

    if (!beginDownload(CHUNKED_URL, "/chunked.bin")) {
        Serial.println("First download could not start. Check SD card and URLs.");
    }
}

void loop() {
    client.loop();
}
