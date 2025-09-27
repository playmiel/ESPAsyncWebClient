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
    // ESP32 fallback mode: the library auto-ticks timeouts via a FreeRTOS task.
    // Call client.loop() periodically unless you build with -DASYNC_HTTP_ENABLE_AUTOLOOP (ESP32 only).
    // client.loop();
#endif
    delay(1000);
}
