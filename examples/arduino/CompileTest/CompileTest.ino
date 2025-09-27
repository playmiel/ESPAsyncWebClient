/**
 * Compilation Test for ESPAsyncWebClient (Arduino IDE)
 */
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebClient.h>

AsyncHttpClient client;

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ESPAsyncWebClient Compilation Test ===");

    // Basic config compile test
    client.setTimeout(5000);
    client.setUserAgent("ESPAsyncWebClient-CompileTest/1.0");
    client.setHeader("Content-Type", "application/json");
}

void loop() {
#if !ASYNC_TCP_HAS_TIMEOUT
    // Timeouts: If your AsyncTCP build doesn't provide native timeouts and you didn't enable auto-loop
    // (-DASYNC_HTTP_ENABLE_AUTOLOOP, ESP32 only), you must call client.loop() periodically to enforce
    // request timeouts. Uncomment the line below to enable request timeouts in this example.
    // client.loop();
#endif
    delay(1000);
}
