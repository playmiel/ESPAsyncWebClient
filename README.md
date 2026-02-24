# ESPAsyncWebClient

[![Build Examples](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/build.yml/badge.svg)](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/build.yml)
[![Library Tests](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/test.yml/badge.svg)](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/test.yml)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![PlatformIO Registry](https://badges.registry.platformio.org/packages/playmiel/library/ESPAsyncWebClient.svg)](https://registry.platformio.org/libraries/playmiel/ESPAsyncWebClient)

An asynchronous HTTP client library for ESP32 microcontrollers, built on top of AsyncTCP. This library provides a simple and efficient way to make HTTP requests without blocking your main program execution.

> 🔐 **HTTPS Ready**: TLS/HTTPS is available via AsyncTCP + mbedTLS. Load a CA certificate or fingerprint before talking to real servers. `client.setTlsInsecure(true)` is intended for debug/pinning scenarios; fully insecure TLS requires an explicit build-time opt-in (see the *HTTPS / TLS configuration* section below).

## Features

- ✅ **Asynchronous HTTP requests** - Non-blocking HTTP operations
- ✅ **HTTPS / TLS** - AsyncTCP + mbedTLS with CA, fingerprint and mutual-auth options
- ✅ **Multiple HTTP methods** - GET, POST, PUT, DELETE, HEAD, PATCH support
- ✅ **Custom headers** - Set global and per-request headers
- ✅ **Callback-based responses** - Success and error callbacks
- ✅ **Automatic cookies** - Captures `Set-Cookie` responses and replays them via `Cookie` on matching requests
- ✅ **ESP32 only** – Arduino-ESP32 core 3.x required (core 2.x dropped; ESP8266 removed since 1.0.1)
- ✅ **Simple API** - Easy to use with minimal setup
- ✅ **Configurable timeouts** - Set custom timeout values
- ✅ **Multiple simultaneous requests** - Handle multiple requests concurrently
- ✅ **Chunked transfer decoding** - Validates framing and exposes parsed trailers
- ✅ **Optional redirect following** - Follow 301/302/303 (converted to GET) and 307/308 (method preserved)
- ✅ **Header & body guards** - Limits buffered headers (~2.8 KiB) and body (8 KiB) by default to avoid runaway responses
- ✅ **Zero-copy streaming** - Combine `req->setNoStoreBody(true)` with `client.onBodyChunk(...)` to stream large payloads without heap spikes

> ⚠ Limitations: provide trust material for HTTPS (CA, fingerprint or insecure flag) and remember the full body is buffered in memory unless you opt into zero-copy streaming via `setNoStoreBody(true)`.

## Installation

### PlatformIO (Recommended)

Add to your `platformio.ini`:

```ini
lib_deps = 
    ESP32Async/ESPAsyncWebServer @ 3.10.0
    https://github.com/playmiel/ESPAsyncWebClient.git
platform_packages =
    framework-arduinoespressif32@^3
```

### Arduino IDE

1. Download this repository as ZIP
2. In Arduino IDE: Sketch → Include Library → Add .ZIP Library
3. Install the dependencies:
   - For ESP32: [AsyncTCP by ESP32Async](https://github.com/ESP32Async/AsyncTCP)
4. Make sure you have the ESP32 Arduino core 3.x installed from the Boards Manager (core 2.x is not supported)


## Quick Start

```cpp
#include <WiFi.h>
#include <ESPAsyncWebClient.h>

AsyncHttpClient client;

void setup() {
    Serial.begin(115200);
    
    // Connect to WiFi
    WiFi.begin("your-ssid", "your-password");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
    }
    
    // Make a simple GET request
    client.get("http://httpbin.org/get", 
        [](std::shared_ptr<AsyncHttpResponse> response) {
            Serial.printf("Success! Status: %d\n", response->getStatusCode());
            Serial.printf("Body: %s\n", response->getBody().c_str());
        },
        [](HttpClientError error, const char* message) {
            Serial.printf("Error: %s (%d)\n", httpClientErrorToString(error), (int)error);
        }
    );
}

void loop() {
#if !ASYNC_TCP_HAS_TIMEOUT
    // If your AsyncTCP does NOT provide native timeouts, you must drive timeouts manually
    // unless you build with -DASYNC_HTTP_ENABLE_AUTOLOOP .
    // Either:
    //   - Define ASYNC_HTTP_ENABLE_AUTOLOOP (ESP32): a tiny FreeRTOS task will call client.loop() for you; or
    //   - Call client.loop() periodically here yourself (recommended every ~10-20ms when busy).
    // client.loop();
#endif
}
```

On ESP32, if AsyncTCP lacks native timeout support, you have two options:

- Define `-DASYNC_HTTP_ENABLE_AUTOLOOP`: the library creates a tiny FreeRTOS task that periodically calls
    `client.loop()` in the background. This is convenient but introduces a background task; keep callbacks short.
- Do not define it: call `client.loop()` periodically yourself from your sketch `loop()` to drive timeouts.

If `ASYNC_TCP_HAS_TIMEOUT` is available in your AsyncTCP, neither is required for timeouts, but calling
`client.loop()` remains harmless.

## Migration v1 → v2

- `SuccessCallback` now receives `std::shared_ptr<AsyncHttpResponse>`.
- `request()` now takes `std::unique_ptr<AsyncHttpRequest>` and assumes ownership.
- `getBody()`, `getHeader()`, and `getStatusText()` return `String` by value.
- `HttpHeader` names are normalized to lowercase.
- Legacy void-return helpers (`*_legacy`, `ASYNC_HTTP_LEGACY_VOID_API`) were removed.
- `parseChunkSizeLine()` is now private.
- `BodyChunkCallback` data is only valid during the callback; copy if needed.

Example migration for advanced requests:

```cpp
std::unique_ptr<AsyncHttpRequest> request(new AsyncHttpRequest(HTTP_METHOD_GET, "http://example.com"));
client.request(std::move(request), onSuccess, onError);
```

## API Reference

### AsyncHttpClient Class

#### HTTP Methods

```cpp
// GET request
uint32_t get(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// POST request with data
uint32_t post(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// PUT request with data
uint32_t put(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// DELETE request
uint32_t del(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// HEAD request
uint32_t head(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// PATCH request (with data)
uint32_t patch(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// Advanced request (custom method, headers, streaming, etc.)
uint32_t request(std::unique_ptr<AsyncHttpRequest> request, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// Abort a request by its ID
bool abort(uint32_t requestId);
```

#### Configuration Methods

```cpp
// Set global default header
void setHeader(const char* name, const char* value);
void removeHeader(const char* name);
void clearHeaders();

// Set total request timeout (milliseconds)
void setTimeout(uint32_t timeout);

// Set connect phase timeout distinct from total timeout
void setDefaultConnectTimeout(uint32_t ms);

// Follow HTTP redirects (max hops clamps to >=1). Disabled by default.
void setFollowRedirects(bool enable, uint8_t maxHops = 3);

// Abort if response headers exceed this many bytes (default ~2.8 KiB, 0 = unlimited)
void setMaxHeaderBytes(size_t maxBytes);

// Soft limit for buffered response bodies (default 8192 bytes, 0 = unlimited)
void setMaxBodySize(size_t maxBytes);

// Limit simultaneous active requests (0 = unlimited, others queued)
void setMaxParallel(uint16_t maxParallel);

// Set User-Agent string
void setUserAgent(const char* userAgent);

// Keep-alive connection pooling (idle timeout in ms, clamped to >= 1000)
void setKeepAlive(bool enable, uint16_t idleMs = 5000);

// Cookie jar helpers
void clearCookies();
void setAllowCookieDomainAttribute(bool enable);
void addAllowedCookieDomain(const char* domain);
void clearAllowedCookieDomains();
void setCookie(const char* name, const char* value, const char* path = "/", const char* domain = nullptr,
               bool secure = false);

// Redirect header policy (when followRedirects is enabled)
void setRedirectHeaderPolicy(RedirectHeaderPolicy policy);
void addRedirectSafeHeader(const char* name);
void clearRedirectSafeHeaders();
```

Cookies are captured automatically from `Set-Cookie` responses and replayed on matching hosts/paths; call
`clearCookies()` to wipe the jar or `setCookie()` to pre-seed entries manually.

By default, cookies set without a `Domain=` attribute are treated as **host-only** (sent only to the exact host that
set them). `Domain=` attributes that would widen scope are ignored unless explicitly allowlisted via
`setAllowCookieDomainAttribute(true)` + `addAllowedCookieDomain("example.com")`.

Keep-alive pooling is off by default;
enable it with `setKeepAlive(true, idleMs)` to reuse TCP/TLS connections for the same host/port (respecting server
`Connection: close` requests).

#### Callback Types

```cpp
typedef std::function<void(std::shared_ptr<AsyncHttpResponse>)> SuccessCallback;
typedef std::function<void(HttpClientError, const char*)> ErrorCallback;
```

### AsyncHttpResponse Class

```cpp
// Response status
int getStatusCode() const;
String getStatusText() const;

// Response headers
String getHeader(const String& name) const;
const std::vector<HttpHeader>& getHeaders() const;
String getTrailer(const String& name) const;
const std::vector<HttpHeader>& getTrailers() const;

// Response body
String getBody() const;
size_t getContentLength() const;

// Status helpers
bool isSuccess() const;    // 2xx status codes
bool isRedirect() const;   // 3xx status codes
bool isError() const;      // 4xx+ status codes
```

Example of reading decoded chunk trailers:

```cpp
client.get("http://example.com/chunked", [](std::shared_ptr<AsyncHttpResponse> response) {
    for (const auto& trailer : response->getTrailers()) {
        Serial.printf("Trailer %s: %s\n", trailer.name.c_str(), trailer.value.c_str());
    }
});
```

### AsyncHttpRequest Class (Advanced Usage)

```cpp
// Create custom request
std::unique_ptr<AsyncHttpRequest> request(new AsyncHttpRequest(HTTP_METHOD_POST, "http://example.com/api"));

// Set headers
request->setHeader("Content-Type", "application/json");
request->setHeader("Authorization", "Bearer token");
request->removeHeader("Accept-Encoding");

// Set body
request->setBody("{\"key\":\"value\"}");

// Set timeout
request->setTimeout(10000);

// Execute
client.request(std::move(request), onSuccess, onError);
```

HTTP method enums are now prefixed (`HTTP_METHOD_GET`, `HTTP_METHOD_POST`, etc.) to avoid collisions with
`ESPAsyncWebServer`'s `HTTP_GET`/`HTTP_POST` values. Legacy aliases can be re-enabled by defining
`ASYNC_HTTP_ENABLE_LEGACY_METHOD_ALIASES` before including `ESPAsyncWebClient.h` (only do this if you are not also
including `ESPAsyncWebServer.h` in the same translation unit).

## Examples

### Simple GET Request

```cpp
client.get("http://api.example.com/data", 
    [](std::shared_ptr<AsyncHttpResponse> response) {
        if (response->isSuccess()) {
            Serial.println("Data received:");
            Serial.println(response->getBody());
        }
    }
);
```

### POST with JSON Data

```cpp
client.setHeader("Content-Type", "application/json");
String jsonData = "{\"sensor\":\"temperature\",\"value\":25.5}";

client.post("http://api.example.com/sensor", jsonData.c_str(),
    [](std::shared_ptr<AsyncHttpResponse> response) {
        Serial.printf("Posted data, status: %d\n", response->getStatusCode());
    }
);
```

### Multiple Simultaneous Requests

```cpp
// These requests will be made concurrently
client.get("http://api1.example.com/data", onSuccess1);
client.get("http://api2.example.com/data", onSuccess2);
client.post("http://api3.example.com/data", "payload", onSuccess3);
```

### Custom Headers

```cpp
// Set global headers (applied to all requests)
client.setHeader("X-API-Key", "your-api-key");
client.setUserAgent("MyDevice/1.0");

// Or set per-request headers
std::unique_ptr<AsyncHttpRequest> request(new AsyncHttpRequest(HTTP_METHOD_GET, "http://example.com"));
request->setHeader("Authorization", "Bearer token");
client.request(std::move(request), onSuccess);
```

### Following Redirects

```cpp
client.setFollowRedirects(true, 3); // follow at most 3 hops

client.post("http://example.com/login", "user=demo", [](std::shared_ptr<AsyncHttpResponse> response) {
    Serial.printf("Final location responded with %d\n", response->getStatusCode());
});
```

- 301/302/303 responses switch to `GET` automatically (body dropped).
- 307/308 keep the original method and body (stream bodies cannot be replayed automatically).
- Cross-origin redirects default to forwarding only a small safe set of headers (e.g. `User-Agent`, `Accept`, etc.).
  Use `setRedirectHeaderPolicy(...)` and `addRedirectSafeHeader(...)` if you need to forward additional headers.
- Redirects are triggered as soon as the headers arrive; the client skips downloading any subsequent 3xx body data.

See `examples/arduino/NoStoreToSD/NoStoreToSD.ino` for a full download example using `setNoStoreBody(true)` and a global `onBodyChunk` handler that streams chunked and non-chunked responses to an SD card.

## Error Handling

Error codes passed to error callbacks: see the single authoritative table in the “Error Codes” section below.

```cpp
client.get("http://example.com", onSuccess,
    [](HttpClientError error, const char* message) {
        switch(error) {
            case CONNECTION_FAILED:
                Serial.println("Connection failed");
                break;
            case REQUEST_TIMEOUT:
                Serial.println("Request timed out");
                break;
            default:
                Serial.printf("Network error: %s (%d)\n", httpClientErrorToString(error), (int)error);
        }
    }
);
```

## Configuration

### Global Settings

```cpp
// Set default timeout for all requests (10 seconds)
client.setTimeout(10000);

// Set default User-Agent
client.setUserAgent("ESP32-IoT-Device/1.0");

// Set default headers applied to all requests
client.setHeader("X-Device-ID", "esp32-001");
client.setHeader("Accept", "application/json");
```

### Per-Request Settings

```cpp
std::unique_ptr<AsyncHttpRequest> request(new AsyncHttpRequest(HTTP_METHOD_POST, url));
request->setTimeout(30000);  // 30 second timeout for this request
request->setHeader("Content-Type", "application/xml");
request->setBody(xmlData);
```

## Memory Management

- The library automatically manages memory for standard requests
- For advanced requests, pass a `std::unique_ptr<AsyncHttpRequest>` to `request()`; ownership transfers to the client
- Success callbacks receive a `std::shared_ptr<AsyncHttpResponse>`; keep a copy if you need the response after the callback
- No manual memory management required for typical usage

> IMPORTANT: Body chunk data is only valid during `onBodyChunk(...)`. Copy it if you need to keep it.

### Body Streaming (experimental)

Register a global streaming callback via:

```cpp
client.onBodyChunk([](const char* data, size_t len, bool final) {
    // data may be nullptr & len==0 when final==true and no trailing bytes
});
```

Parameters:

- `data`, `len`: received segment (for chunked: decoded chunk payload; for non-chunked: raw slice). When `final==true` and no extra bytes, `data` can be `nullptr`.
- `final`: true when the whole response body is complete.

Notes:

- Invoked for every segment (chunk or contiguous data block)
- `data` is only valid during the callback; copy it if you need to retain it
- Unless `req->setNoStoreBody(true)` is enabled, the full body is still accumulated internally
- `final` is invoked just before the success callback
- Keep it lightweight (avoid blocking operations)


### Content-Length and over / under delivery

If `Content-Length` is present, the response is considered complete once that many bytes have been received. Extra bytes (if a misbehaving server sends more) are ignored. Without `Content-Length`, completion is determined by connection close.

Configure `client.setMaxBodySize(maxBytes)` to abort early when the announced `Content-Length` or accumulated chunk data would exceed `maxBytes`, yielding `MAX_BODY_SIZE_EXCEEDED`. Pass `0` to disable the guard (this applies only when buffering the response body in memory).

Likewise, guard against oversized or malicious header blocks via `client.setMaxHeaderBytes(limit)`. When the cumulative response headers exceed `limit` bytes before completion of `\r\n\r\n`, the request aborts with `HEADERS_TOO_LARGE`.

### Transfer-Encoding: chunked

Chunked decoding validates frame boundaries and parses trailer headers for attachment to the response object.

Highlights / limitations:

- Trailer headers are parsed during chunked responses and available via `AsyncHttpResponse::getTrailers()`
- Chunk extensions are ignored but accepted
- Strict CRLF framing is required; malformed chunks raise `CHUNKED_DECODE_FAILED`

### HTTPS / TLS configuration

`https://` URLs now use the built-in AsyncTCP + mbedTLS transport. Supply trust material before making real requests:

- `client.setTlsCACert(caPem)` — load a PEM CA chain (null-terminated). Mandatory unless using fingerprint pinning (see `setTlsFingerprint(...)`).
- `client.setTlsClientCert(certPem, keyPem)` — optional mutual-TLS credentials (PEM).
- `client.setTlsFingerprint("AA:BB:...")` — 32-byte SHA-256 fingerprint pinning. Validated after the handshake in addition to CA checks.
- `client.setTlsInsecure(true)` — skips CA validation. By default this is only effective when a fingerprint is configured (pinning).
  To allow fully insecure TLS (MITM-unsafe) for local debugging, build with `-DASYNC_HTTP_ALLOW_INSECURE_TLS=1`.
- `client.setTlsInsecure(true)` — disable CA validation (development only; do not ship with this enabled).
- `client.setTlsHandshakeTimeout(ms)` — default is 12s; tune for slow networks.

Per-request overrides are available via `AsyncHttpRequest::setTlsConfig(const AsyncHttpTLSConfig&)` when a particular destination needs a different CA or timeout.

Common HTTPS errors:

- `TLS_HANDSHAKE_FAILED` — TCP issues or protocol alerts during the handshake.
- `TLS_CERT_INVALID` — CA verification failed (missing root, expired cert, wrong host).
- `TLS_FINGERPRINT_MISMATCH` — fingerprint pinning rejected the peer certificate.
- `TLS_HANDSHAKE_TIMEOUT` — handshake exceeded the configured timeout.
- `HTTPS_NOT_SUPPORTED` — only triggered if the binary is built without TLS support (non-ESP32 targets).

## Thread Safety

- The library is designed for single-threaded use (Arduino main loop)
- Callbacks are executed in the context of the network event loop
- Keep callback functions lightweight and non-blocking

## Dependencies

- **ESP32**: [AsyncTCP by ESP32Async](https://github.com/ESP32Async/AsyncTCP)
- **Arduino Core**: ESP32 (v3.0+)

> **Note**: ESP8266 was mentioned in early docs but is no longer supported as of 1.0.1. The code exclusively targets AsyncTCP (ESP32).

## Supported Platforms

- Current target: **ESP32** only
- ESP8266: removed (no conditional code path retained)

## Current Limitations (summary)

- TLS requires explicit trust material (CA certificate, fingerprint, or insecure mode)
- Chunked: trailers parsed and attached to `AsyncHttpResponse::getTrailers()`
- Full in-memory buffering (guard with `setMaxBodySize` or use no-store + chunk callback)
- Redirects disabled by default; opt-in via `client.setFollowRedirects(...)`
- Keep-alive pooling is disabled by default; enable it with `setKeepAlive(true, idleMs)`.
- Manual timeout loop required if AsyncTCP version lacks `setTimeout` (call `client.loop()` in `loop()`).
- No general content-encoding handling (br/deflate not supported); optional `gzip` decode is available via `ASYNC_HTTP_ENABLE_GZIP_DECODE`.

## Object lifecycle / Ownership

1. `AsyncHttpClient::makeRequest()` creates a dynamic `AsyncHttpRequest` (or you pass yours to `request()`).
2. `request()` allocates a `RequestContext`, an `AsyncHttpResponse` and an `AsyncTransport`.
3. Once connected the fully built HTTP request is written (`buildHttpRequest()`).
4. Reception: headers buffered until `\r\n\r\n`, then body accumulation (or chunk decoding).
5. On complete success: success callback invoked with `std::shared_ptr<AsyncHttpResponse>`.
6. On error or after success callback returns: `cleanup()` deletes the transport, `AsyncHttpRequest`, and `RequestContext`.
7. The response is freed when the last `shared_ptr` copy is released.

For very large bodies or future streaming options, a hook would be placed inside `handleData` after `headersComplete` before `appendBody`.

## Error Codes

Single authoritative list (kept in sync with `HttpCommon.h`):

| Code | Enum | Meaning |
|------|------|---------|
| -1 | CONNECTION_FAILED | Failed to initiate TCP connection or transport error mapped from AsyncTCP |
| -2 | HEADER_PARSE_FAILED | Invalid HTTP response headers |
| -3 | CONNECTION_CLOSED | Connection closed before headers received |
| -4 | REQUEST_TIMEOUT | Total request timeout exceeded |
| -5 | HTTPS_NOT_SUPPORTED | TLS/HTTPS transport unavailable (unsupported target) |
| -6 | CHUNKED_DECODE_FAILED | Failed to decode chunked body |
| -7 | CONNECT_TIMEOUT | Connect phase timeout |
| -8 | BODY_STREAM_READ_FAILED | Body streaming provider failed |
| -9 | ABORTED | Aborted by user |
| -10 | CONNECTION_CLOSED_MID_BODY | Connection closed after headers with body still missing bytes (truncated body) |
| -11 | MAX_BODY_SIZE_EXCEEDED | Body exceeds configured maximum (`setMaxBodySize`) |
| -12 | TOO_MANY_REDIRECTS | Redirect chain exceeded configured hop limit (`setFollowRedirects`) |
| -13 | HEADERS_TOO_LARGE | Response headers exceeded configured limit (`setMaxHeaderBytes`) |
| -14 | TLS_HANDSHAKE_FAILED | TLS handshake or channel failure |
| -15 | TLS_CERT_INVALID | TLS certificate validation failed |
| -16 | TLS_FINGERPRINT_MISMATCH | TLS fingerprint pinning rejected the peer certificate |
| -17 | TLS_HANDSHAKE_TIMEOUT | TLS handshake exceeded the configured timeout |
| -18 | GZIP_DECODE_FAILED | Failed to decode gzip body (`Content-Encoding: gzip`) |
| >0 | (AsyncTCP) | Not used: transport errors are mapped to CONNECTION_FAILED |

Example mapping in a callback:

```cpp
client.get("http://example.com", 
  [](std::shared_ptr<AsyncHttpResponse> r) {
      Serial.printf("OK %d %s\n", r->getStatusCode(), r->getStatusText().c_str());
  },
        [](HttpClientError e, const char* msg) {
      switch (e) {
          case CONNECTION_FAILED: Serial.println("TCP connect failed"); break;
          case HEADER_PARSE_FAILED: Serial.println("Bad HTTP header"); break;
          case CONNECTION_CLOSED: Serial.println("Closed before headers"); break;
          case CONNECTION_CLOSED_MID_BODY: Serial.println("Body truncated (closed mid-body)"); break;
          case REQUEST_TIMEOUT: Serial.println("Timeout"); break;
          case MAX_BODY_SIZE_EXCEEDED: Serial.println("Body exceeded guard"); break;
          case TOO_MANY_REDIRECTS: Serial.println("Redirect loop detected"); break;
          case HEADERS_TOO_LARGE: Serial.println("Headers exceeded guard"); break;
                    default: Serial.printf("Network error: %s (%d)\n", httpClientErrorToString(e), (int)e); break;
      }
  }
);
```

## Testing

### Dependency Testing

To test compatibility with different versions of AsyncTCP, use the provided test script:

```bash
./scripts/test-dependencies.sh
```

This script tests compilation with:

- AsyncTCP ESP32Async/main (development)
- AsyncTCP ESP32Async stable

### Manual Testing

You can also test individual environments:

```bash
# Test with development AsyncTCP
pio run -e esp32dev_asynctcp_dev

# Test with stable AsyncTCP
pio run -e test_asynctcp_stable

# Basic compilation test
pio run -e compile_test

# Chunk decoder regression tests
pio test -e esp32dev -f test_chunk_parse
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Support

- Streaming request body (no-copy) via setBodyStream
- Global body chunk callback (per-request callback removed for API simplicity)
- Basic Auth helper (request->setBasicAuth)
- Query param builder (addQueryParam/finalizeQueryParams)
- Optional Accept-Encoding: gzip (+ optional transparent decode via `ASYNC_HTTP_ENABLE_GZIP_DECODE`)
- Separate connect timeout and total timeout
- Optional request queue limiting parallel connections (setMaxParallel)
- Soft response buffering guard (`setMaxBodySize`) to fail fast on oversized payloads
- Request ID return (all helper methods now return a uint32_t identifier)
- Zero-copy streaming mode: call `req->setNoStoreBody(true)` and rely on `client.onBodyChunk(...)` to consume data without buffering (a final `(nullptr, 0, true)` event fires once)

### Gzip / Compression

Default: only the `Accept-Encoding: gzip` header can be added via `enableGzipAcceptEncoding(true)`.

Optional decode: build with `-DASYNC_HTTP_ENABLE_GZIP_DECODE=1` to transparently inflate `Content-Encoding: gzip` responses (both in-memory body and `client.onBodyChunk(...)` stream).

Notes:

- If you don't want compressed responses, simply don't enable the header.
- `enableGzipAcceptEncoding(false)` removes `Accept-Encoding` from the request's header list (or call `request.removeHeader("Accept-Encoding")`).
- `Content-Length` (when present) refers to the *compressed* payload size; completion detection still follows the wire length.
- RAM impact: enabling gzip decode allocates an internal 32KB sliding window per active gzip-decoded response (plus small state).
- Integrity: the gzip trailer is verified (CRC32 + ISIZE); corrupted payloads raise `GZIP_DECODE_FAILED`.

### HTTPS quick reference

- Call `client.setTlsCACert(caPem)` (or `request->setTlsConfig(...)`) before talking to production endpoints.
- Use `client.setTlsInsecure(true)` only during development when no CA/fingerprint is available.
- Fingerprint pinning (SHA-256) is optional via `client.setTlsFingerprint`.
- Mutual TLS is supported via `client.setTlsClientCert(certPem, keyPem)`.
- Errors are surfaced via `TLS_*` codes in the error callback; see the table below.

### Advanced Example

See Arduino sketch at `examples/arduino/StreamingUpload/StreamingUpload.ino` or the PlatformIO project at `examples/platformio/StreamingUpload/src/main.cpp` for a streaming (no-copy) upload demonstrating:

- `setBodyStream()`
- Basic Auth (`setBasicAuth`)
- Query params builder (`addQueryParam` / `finalizeQueryParams`)
- Connection limiting (`setMaxParallel`)

- Create an issue on GitHub for bug reports or feature requests
- Check the examples directory for usage patterns
- Review the API documentation above for detailed information

## Changelog

See the GitHub Releases page for version history and changes.
