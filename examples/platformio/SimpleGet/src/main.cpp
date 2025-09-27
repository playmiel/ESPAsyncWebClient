#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebClient.h>

AsyncHttpClient client;

void setup() {
    Serial.begin(115200);

    // Connect to WiFi
    WiFi.begin("your-ssid", "your-password");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    // Make a simple GET request
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
    // Timeouts: If your AsyncTCP build doesn't provide native timeouts and you didn't enable auto-loop
    // (-DASYNC_HTTP_ENABLE_AUTOLOOP, ESP32 only), you must call client.loop() periodically to enforce
    // request timeouts. Uncomment the line below when running on hardware to enable timeouts.
    // client.loop();
#endif
}
