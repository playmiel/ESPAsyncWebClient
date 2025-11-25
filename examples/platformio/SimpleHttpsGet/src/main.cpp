#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebClient.h>

// Set your SSID/Password
static const char* WIFI_SSID = "YOUR_SSID";
static const char* WIFI_PASS = "YOUR_PASSWORD";

// HTTPS test URL (returns JSON)
static const char* TEST_URL = "https://httpbin.org/get";

AsyncHttpClient client;

// Option 1 (Recommended for production): CA Certificate (paste PEM content into caPem)
// You can retrieve the server's CA using openssl or your browser.
// Example: openssl s_client -showcerts -connect httpbin.org:443 <NUL | openssl x509 -outform PEM > ca.pem
// Then paste the content of ca.pem below.
static const char caPem[] PROGMEM = ""; // "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n";

// Option 2 (Development only): Insecure mode
static const bool USE_INSECURE = true; // set to false if you provide caPem above

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println();
    Serial.println("[HTTPS demo] Boot");

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }
    Serial.printf("\nWiFi connected, IP=%s\n", WiFi.localIP().toString().c_str());

    // Global TLS configuration
    client.setTimeout(15000);
    client.setUserAgent("ESPAsyncWebClient-HTTPS-Demo/1.0");

    if (!USE_INSECURE && strlen(caPem) > 0) {
        client.setTlsCACert(caPem);
        Serial.println("TLS: CA loaded (verification enabled)");
    } else {
        client.setTlsInsecure(true);
        Serial.println("TLS: INSECURE mode enabled (DEV ONLY)");
    }

    // HTTPS GET request
    client.get(
        TEST_URL,
        [](AsyncHttpResponse* response) {
            Serial.printf("Success! Status: %d %s\n", response->getStatusCode(), response->getStatusText().c_str());
            Serial.println("Body:");
            Serial.println(response->getBody());
        },
        [](HttpClientError error, const char* message) {
            Serial.printf("Error: %s (%d)\n", message ? message : httpClientErrorToString(error), (int)error);
            switch (error) {
                case TLS_CERT_INVALID: Serial.println("Cause: Missing/Wrong CA, expired cert or host mismatch"); break;
                case TLS_FINGERPRINT_MISMATCH: Serial.println("Cause: SHA-256 fingerprint mismatch"); break;
                case TLS_HANDSHAKE_TIMEOUT: Serial.println("Cause: Handshake too long (slow network?)"); break;
                case TLS_HANDSHAKE_FAILED: Serial.println("Cause: TLS failure (parameters, ciphers)"); break;
                default: break;
            }
        }
    );
}

void loop() {
#if !ASYNC_TCP_HAS_TIMEOUT
    // If your AsyncTCP does not natively handle timeouts AND you haven't
    // defined -DASYNC_HTTP_ENABLE_AUTOLOOP, call client.loop() regularly.
    // client.loop();
#endif
}
