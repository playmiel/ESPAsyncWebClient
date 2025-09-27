// Example: Streaming upload + basic auth + query builder + global body chunk callback
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebClient.h>

static const char* WIFI_SSID = "YOUR_SSID";
static const char* WIFI_PASS = "YOUR_PASS";

AsyncHttpClient client;

struct PatternStream {
    size_t total;
    size_t sent;
    const char* pat = "abcdefghijklmnopqrstuvwxyz0123456789\n";
    size_t patLen = 37;
} pattern;

int patternProvider(uint8_t* buffer, size_t maxLen, bool* final) {
    if (pattern.sent >= pattern.total) { *final = true; return 0; }
    size_t remain = pattern.total - pattern.sent;
    size_t chunk = remain < maxLen ? remain : maxLen;
    for (size_t i = 0; i < chunk; ++i) buffer[i] = pattern.pat[(pattern.sent + i) % pattern.patLen];
    pattern.sent += chunk;
    if (pattern.sent >= pattern.total) *final = true;
    return (int)chunk;
}

void setup() {
    Serial.begin(115200);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(250); Serial.print('.'); }
    Serial.println();
    Serial.println("WiFi connected");

    client.setMaxParallel(1);
    client.setDefaultConnectTimeout(3000);
    client.setTimeout(15000);

    pattern.total = 10 * 1024;
    pattern.sent = 0;

    AsyncHttpRequest* req = new AsyncHttpRequest(HTTP_POST, "http://httpbin.org/post");
    req->addQueryParam("mode", "stream");
    req->addQueryParam("unit", "bytes");
    req->finalizeQueryParams();
    req->setHeader("Content-Type", "text/plain");
    req->setBasicAuth("user", "pass");
    client.onBodyChunk([](const char* data, size_t len, bool final) {
        if (data && len) Serial.printf("[CHUNK %u bytes]\n", (unsigned)len);
        if (final) Serial.println("[CHUNKS COMPLETE]");
    });
    req->setBodyStream(pattern.total, patternProvider);
    client.request(req, [](AsyncHttpResponse* resp) {
        Serial.printf("UPLOAD DONE status=%d len=%u\n", resp->getStatusCode(), (unsigned)resp->getBody().length());
    }, [](HttpClientError code, const char* msg) {
        Serial.printf("ERROR %d: %s\n", (int)code, msg);
    });
}

void loop() {
#if !ASYNC_TCP_HAS_TIMEOUT
    // ESP32 fallback mode: the library auto-ticks timeouts via a FreeRTOS task.
        // Call client.loop() periodically unless you build with -DASYNC_HTTP_ENABLE_AUTOLOOP (ESP32 only).
    // client.loop();
#endif
}
