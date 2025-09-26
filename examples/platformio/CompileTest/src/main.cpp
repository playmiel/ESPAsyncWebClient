/**
 * Compilation Test for ESPAsyncWebClient (PlatformIO)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebClient.h>

AsyncHttpClient client;
void testHttpMethodsCompilation();

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ESPAsyncWebClient Compilation Test ===");

#ifndef COMPILE_TEST_ONLY
    Serial.println("Testing WiFi connection...");
    WiFi.begin("test-ssid", "test-password");

    // Timeout after 10 seconds for CI
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 100) {
        delay(100);
        Serial.print(".");
        timeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection timeout (expected in CI)");
    }
#else
    Serial.println("Compile-only test mode enabled");
#endif

    // Test that all library functions compile correctly
    Serial.println("Testing library API compilation...");

    // Test configuration methods
    client.setTimeout(5000);
    client.setUserAgent("ESPAsyncWebClient-CompileTest/1.0");
    client.setHeader("Content-Type", "application/json");
    client.setHeader("Accept", "application/json");
    client.setHeader("X-Test-Header", "compile-test");

    Serial.println("✓ Configuration methods compile OK");

    // Test HTTP method signatures compilation without making actual requests
    testHttpMethodsCompilation();

    Serial.println("✓ All library functions compile successfully!");
    Serial.println("=== Compilation Test Completed ===\n");
}

void testHttpMethodsCompilation() {
    // This function tests that all HTTP methods compile correctly
    Serial.println("Testing HTTP methods compilation...");

#ifndef COMPILE_TEST_ONLY
    // Test callback signatures compilation
    auto successCallback = [](AsyncHttpResponse* response) {
        Serial.printf("Success callback - Status: %d\n", response->getStatusCode());
        Serial.printf("Body length: %d\n", response->getBody().length());

        // Test response methods
        String body = response->getBody();
        int status = response->getStatusCode();
        String header = response->getHeader("Content-Type");

        (void)body;   // Suppress unused variable warning
        (void)status; // Suppress unused variable warning
        (void)header; // Suppress unused variable warning
    };

    auto errorCallback = [](HttpClientError error, const char* message) {
        Serial.printf("Error callback - Code: %d, Message: %s\n", (int)error, httpClientErrorToString(error));
    };

    // Test all HTTP methods compilation (won't execute in CI)
    if (WiFi.status() == WL_CONNECTED) {
        // These will compile but only execute with real WiFi connection
        client.get("http://httpbin.org/get", successCallback, errorCallback);
        client.post("http://httpbin.org/post", "{\"test\": \"data\"}", successCallback, errorCallback);
        client.put("http://httpbin.org/put", "{\"test\": \"data\"}", successCallback, errorCallback);
        client.del("http://httpbin.org/delete", successCallback, errorCallback);

        Serial.println("✓ HTTP methods will execute");
    } else {
        Serial.println("✓ HTTP methods compile OK (no WiFi for execution)");
    }
#else
    Serial.println("✓ HTTP methods compile OK (compile-only mode)");
#endif
}

void loop() {
    // On ESP32 fallback mode, the library auto-ticks timeouts via a FreeRTOS task.
    // If you define -DASYNC_HTTP_DISABLE_AUTOLOOP, call client.loop() periodically here.
    // #if !ASYNC_TCP_HAS_TIMEOUT
    // client.loop();
    // #endif
    // Simple heartbeat to show the program is running
    static unsigned long lastHeartbeat = 0;
    unsigned long now = millis();

    if (now - lastHeartbeat >= 10000) { // Every 10 seconds
        Serial.println("Heartbeat - Test program running...");
        lastHeartbeat = now;

#ifndef COMPILE_TEST_ONLY
        // Test runtime compilation occasionally
        if (WiFi.status() == WL_CONNECTED) {
            static bool testExecuted = false;
            if (!testExecuted) {
                Serial.println("Executing one-time HTTP test...");
                client.get(
                    "http://httpbin.org/get",
                    [](AsyncHttpResponse* response) {
                        Serial.printf("✓ GET request successful - Status: %d\n", response->getStatusCode());
                    },
                    [](HttpClientError error, const char* message) {
                        Serial.printf("✗ GET request failed - Error: %s (%d)\n", httpClientErrorToString(error),
                                      (int)error);
                    });
                testExecuted = true;
            }
        }
#endif
    }

    delay(100); // Small delay to prevent watchdog issues
}
