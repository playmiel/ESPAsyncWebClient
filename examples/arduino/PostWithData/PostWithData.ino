#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebClient.h>

AsyncHttpClient client;

void setup() {
    Serial.begin(115200);

    WiFi.begin("your-ssid", "your-password");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    String postData = "name=ESP32&value=123&sensor=temperature";

    client.post(
        "http://httpbin.org/post", postData.c_str(),
        [](AsyncHttpResponse* response) {
            Serial.println("POST Success!");
            Serial.printf("Status: %d\n", response->getStatusCode());
            Serial.printf("Content-Type: %s\n", response->getHeader("Content-Type").c_str());
            Serial.printf("Body length: %d\n", response->getBody().length());
        },
        [](HttpClientError error, const char* message) {
            Serial.printf("POST Error: %s (%d)\n", httpClientErrorToString(error), (int)error);
        });
}

void loop() {
#if !ASYNC_TCP_HAS_TIMEOUT
    // Call client.loop() periodically unless you build with -DASYNC_HTTP_ENABLE_AUTOLOOP (ESP32 only).
    client.loop();
#endif
}
