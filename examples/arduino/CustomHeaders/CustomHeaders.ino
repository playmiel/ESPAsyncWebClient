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

    client.setHeader("X-API-Key", "your-api-key-here");
    client.setHeader("Authorization", "Bearer your-token-here");
    client.setUserAgent("ESP32-CustomClient/1.0");
    client.setTimeout(15000);

    client.get(
        "http://httpbin.org/headers",
        [](const std::shared_ptr<AsyncHttpResponse>& response) {
            Serial.println("Request with custom headers successful!");
            Serial.printf("Status: %d\n", response->getStatusCode());
        },
        [](HttpClientError error, const char* message) {
            Serial.printf("Error: %s (%d)\n", httpClientErrorToString(error), (int)error);
        });
}

void loop() {
#if !ASYNC_TCP_HAS_TIMEOUT
    // Timeouts: If your AsyncTCP build doesn't provide native timeouts and you didn't enable auto-loop
    // (-DASYNC_HTTP_ENABLE_AUTOLOOP), call client.loop() periodically to enforce request timeouts.
    client.loop();
#endif
}
