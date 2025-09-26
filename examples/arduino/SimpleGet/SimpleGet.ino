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

    client.get(
        "http://httpbin.org/get",
        [](AsyncHttpResponse* response) {
            Serial.println("Success!");
            Serial.printf("Status: %d\n", response->getStatusCode());
            Serial.printf("Body: %s\n", response->getBody().c_str());
        },
        [](HttpClientError error, const char* message) {
            Serial.printf("Error: %s (%d)\n", httpClientErrorToString(error), (int)error);
        });
}

void loop() {
#if !ASYNC_TCP_HAS_TIMEOUT
    // ESP32 fallback mode: the library auto-ticks timeouts via a FreeRTOS task.
    // If you define -DASYNC_HTTP_DISABLE_AUTOLOOP, call client.loop() periodically here.
    // client.loop();
#endif
}
