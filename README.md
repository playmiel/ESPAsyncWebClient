# ESPAsyncWebClient

[![Build Examples](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/build.yml/badge.svg)](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/build.yml)
[![Library Tests](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/test.yml/badge.svg)](https://github.com/playmiel/ESPAsyncWebClient/actions/workflows/test.yml)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![PlatformIO Registry](https://badges.registry.platformio.org/packages/playmiel/library/ESPAsyncWebClient.svg)](https://registry.platformio.org/libraries/playmiel/ESPAsyncWebClient)

An asynchronous HTTP client library for ESP32 microcontrollers, built on top of AsyncTCP. This library provides a simple and efficient way to make HTTP requests without blocking your main program execution.

> ⚠️ **HTTPS Warning**: Real TLS/HTTPS is NOT implemented yet. `https://` URLs are rejected with `HTTPS_NOT_SUPPORTED`. Do not use this library for sensitive data until TLS support is added.

## Features

- ✅ **Asynchronous HTTP requests** - Non-blocking HTTP operations
- ✅ **Multiple HTTP methods** - GET, POST, PUT, DELETE, HEAD, PATCH support
- ✅ **Custom headers** - Set global and per-request headers
- ✅ **Callback-based responses** - Success and error callbacks
- ✅ **ESP32 only** – (ESP8266 support removed since 1.0.1)
- ✅ **Simple API** - Easy to use with minimal setup
- ✅ **Configurable timeouts** - Set custom timeout values
- ✅ **Multiple simultaneous requests** - Handle multiple requests concurrently
- ⚠️ **Chunked transfer decoding** - Validates chunk framing but still discards trailers

> ⚠ Limitations: HTTPS not implemented; chunked decoding validates frames but still discards trailers; full body is buffered in memory (no zero-copy streaming yet).

## Installation

### PlatformIO (Recommended)

Add to your `platformio.ini`:

```ini
lib_deps = 
    https://github.com/ESP32Async/AsyncTCP.git
    https://github.com/playmiel/ESPAsyncWebClient.git
```

### Arduino IDE

1. Download this repository as ZIP
2. In Arduino IDE: Sketch → Include Library → Add .ZIP Library
3. Install the dependencies:
   - For ESP32: [AsyncTCP by ESP32Async](https://github.com/ESP32Async/AsyncTCP)


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
        [](AsyncHttpResponse* response) {
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
    // unless you build with -DASYNC_HTTP_ENABLE_AUTOLOOP (ESP32 only).
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

## API Reference

### AsyncHttpClient Class

#### HTTP Methods

```cpp
// GET request
void get(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// POST request with data
void post(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// PUT request with data
void put(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// DELETE request
uint32_t del(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// HEAD request
uint32_t head(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// PATCH request (with data)
uint32_t patch(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// Advanced request (custom method, headers, streaming, etc.)
uint32_t request(AsyncHttpRequest* request, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

// Abort a request by its ID
bool abort(uint32_t requestId);
```

#### Configuration Methods

```cpp
// Set global default header
void setHeader(const char* name, const char* value);

// Set total request timeout (milliseconds)
void setTimeout(uint32_t timeout);

// Set connect phase timeout distinct from total timeout
void setDefaultConnectTimeout(uint32_t ms);

// Soft limit for buffered response bodies (bytes, 0 = unlimited)
void setMaxBodySize(size_t maxBytes);

// Limit simultaneous active requests (0 = unlimited, others queued)
void setMaxParallel(uint16_t maxParallel);

// Set User-Agent string
void setUserAgent(const char* userAgent);
```

#### Callback Types

```cpp
typedef std::function<void(AsyncHttpResponse*)> SuccessCallback;
typedef std::function<void(HttpClientError, const char*)> ErrorCallback;
```

### AsyncHttpResponse Class

```cpp
// Response status
int getStatusCode() const;
const String& getStatusText() const;

// Response headers
const String& getHeader(const String& name) const;
const std::vector<HttpHeader>& getHeaders() const;

// Response body
const String& getBody() const;
size_t getContentLength() const;

// Status helpers
bool isSuccess() const;    // 2xx status codes
bool isRedirect() const;   // 3xx status codes
bool isError() const;      // 4xx+ status codes
```

### AsyncHttpRequest Class (Advanced Usage)

```cpp
// Create custom request
AsyncHttpRequest request(HTTP_POST, "http://example.com/api");

// Set headers
request.setHeader("Content-Type", "application/json");
request.setHeader("Authorization", "Bearer token");

// Set body
request.setBody("{\"key\":\"value\"}");

// Set timeout
request.setTimeout(10000);

// Execute
client.request(&request, onSuccess, onError);
```

## Examples

### Simple GET Request

```cpp
client.get("http://api.example.com/data", 
    [](AsyncHttpResponse* response) {
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
    [](AsyncHttpResponse* response) {
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
AsyncHttpRequest* request = new AsyncHttpRequest(HTTP_GET, "http://example.com");
request->setHeader("Authorization", "Bearer token");
client.request(request, onSuccess);
```

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
AsyncHttpRequest* request = new AsyncHttpRequest(HTTP_POST, url);
request->setTimeout(30000);  // 30 second timeout for this request
request->setHeader("Content-Type", "application/xml");
request->setBody(xmlData);
```

## Memory Management

- The library automatically manages memory for standard requests
- For advanced `AsyncHttpRequest` objects, the library takes ownership and will delete them
- Response objects are automatically cleaned up after callbacks complete
- No manual memory management required for typical usage

> IMPORTANT: The `AsyncHttpResponse*` pointer passed to the success callback is ONLY valid during that callback. Do not store it or references to its internal `String` objects. Copy what you need.

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
- The full body is still accumulated internally (future option may allow disabling accumulation)
- `final` is invoked just before the success callback
- Keep it lightweight (avoid blocking operations)


### Content-Length and over / under delivery

If `Content-Length` is present, the response is considered complete once that many bytes have been received. Extra bytes (if a misbehaving server sends more) are ignored. Without `Content-Length`, completion is determined by connection close.

Configure `client.setMaxBodySize(maxBytes)` to abort early when the announced `Content-Length` or accumulated chunk data would exceed `maxBytes`, yielding `MAX_BODY_SIZE_EXCEEDED`. Pass `0` (default) to disable the guard.

### Transfer-Encoding: chunked

Chunked decoding validates frame boundaries and discards trailer headers quietly.

Highlights / limitations:

- Trailer headers are skipped (not exposed to user callbacks)
- Chunk extensions are ignored but accepted
- Strict CRLF framing is required; malformed chunks raise `CHUNKED_DECODE_FAILED`

### HTTPS (not supported)

`https://` URLs return `HTTPS_NOT_SUPPORTED`. To add TLS later, wrap or replace `AsyncClient` with a secure implementation.

## Thread Safety

- The library is designed for single-threaded use (Arduino main loop)
- Callbacks are executed in the context of the network event loop
- Keep callback functions lightweight and non-blocking

## Dependencies

- **ESP32**: [AsyncTCP by ESP32Async](https://github.com/ESP32Async/AsyncTCP)
- **Arduino Core**: ESP32 (v2.0+)

> **Note**: ESP8266 was mentioned in early docs but is no longer supported as of 1.0.1. The code exclusively targets AsyncTCP (ESP32).

## Supported Platforms

- Current target: **ESP32** only
- ESP8266: removed (no conditional code path retained)

## Current Limitations (summary)

- No TLS (HTTPS rejected)
- Chunked: trailers discarded (not exposed to callbacks)
- Full in-memory buffering (guard with `setMaxBodySize` or use no-store + chunk callback)
- No automatic redirects (3xx not followed)
- No long-lived keep-alive: default header `Connection: close`; no connection reuse currently.
- Manual timeout loop required if AsyncTCP version lacks `setTimeout` (call `client.loop()` in `loop()`).
- No specific content-encoding handling (gzip/deflate ignored if sent).

## Object lifecycle / Ownership

1. `AsyncHttpClient::makeRequest()` creates a dynamic `AsyncHttpRequest` (or you pass yours to `request()`).
2. `request()` allocates a `RequestContext`, an `AsyncHttpResponse` and an `AsyncClient`.
3. Once connected the fully built HTTP request is written (`buildHttpRequest()`).
4. Reception: headers buffered until `\r\n\r\n`, then body accumulation (or chunk decoding).
5. On complete success: success callback invoked with `AsyncHttpResponse*` (valid only during the callback).
6. On error or after success callback returns: `cleanup()` deletes `AsyncClient`, `AsyncHttpRequest`, `AsyncHttpResponse`, `RequestContext`.
7. Do **not** keep any pointer/reference after callback return (it will dangle).

For very large bodies or future streaming options, a hook would be placed inside `handleData` after `headersComplete` before `appendBody`.

## Error Codes

Single authoritative list (kept in sync with `HttpCommon.h`):

| Code | Enum | Meaning |
|------|------|---------|
| -1 | CONNECTION_FAILED | Failed to initiate TCP connection or transport error mapped from AsyncTCP |
| -2 | HEADER_PARSE_FAILED | Invalid HTTP response headers |
| -3 | CONNECTION_CLOSED | Connection closed before headers received |
| -4 | REQUEST_TIMEOUT | Total request timeout exceeded |
| -5 | HTTPS_NOT_SUPPORTED | HTTPS not supported yet |
| -6 | CHUNKED_DECODE_FAILED | Failed to decode chunked body |
| -7 | CONNECT_TIMEOUT | Connect phase timeout |
| -8 | BODY_STREAM_READ_FAILED | Body streaming provider failed |
| -9 | ABORTED | Aborted by user |
| -10 | CONNECTION_CLOSED_MID_BODY | Connection closed after headers with body still missing bytes (truncated body) |
| -11 | MAX_BODY_SIZE_EXCEEDED | Body exceeds configured maximum (`setMaxBodySize`) |
| >0 | (AsyncTCP) | Not used: transport errors are mapped to CONNECTION_FAILED |

Example mapping in a callback:

```cpp
client.get("http://example.com", 
  [](AsyncHttpResponse* r) {
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

- Added: HEAD, PATCH

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Support

- Streaming request body (no-copy) via setBodyStream
- Global body chunk callback (per-request callback removed for API simplicity)
- Basic Auth helper (request->setBasicAuth)
- Query param builder (addQueryParam/finalizeQueryParams)
- Optional Accept-Encoding: gzip (no automatic decompression yet)
- Separate connect timeout and total timeout
- Optional request queue limiting parallel connections (setMaxParallel)
- Soft response buffering guard (`setMaxBodySize`) to fail fast on oversized payloads
- Request ID return (all helper methods now return a uint32_t identifier)
- No-store body mode: `req->setNoStoreBody(true)` to avoid buffering body when a chunk callback is used (final `(nullptr, 0, true)` event fired once)

### Gzip / Compression

Current: only the `Accept-Encoding: gzip` header can be added via `enableGzipAcceptEncoding(true)`.
The library DOES NOT yet decompress gzip payloads. If you don't want compressed responses, simply don't enable the header.

Important: calling `enableGzipAcceptEncoding(false)` does not remove the header if it was already added earlier on the same request instance. Create a new request without enabling it to avoid sending the header.
A future optional flag (`ASYNC_HTTP_ENABLE_GZIP_DECODE`) may add a tiny inflater (miniz/zlib) after flash/RAM impact is evaluated.

### HTTPS (Not Supported Yet)

HTTPS is not implemented. Any `https://` URL returns `HTTPS_NOT_SUPPORTED`. A future drop-in TLS client (replacing `AsyncClient`) is planned without breaking the public API.

<!-- Note: per-request chunk callback removed; use global client.onBodyChunk -->

### API Change: Request ID Return (Breaking)

All convenience request methods (get/post/put/delete/head/patch/request) now return a `uint32_t` request ID.

Pros:

- Enables precise cancellation with `abort(id)`.
- Easier correlation of logs/metrics to in-flight requests.
- Foundation for retry/backoff orchestration or tracing layers.
- Allows future per-request state lookups (timings) without storing pointers.

Cons / Migration impact:

- Existing sketches expecting `void` will fail to compile (signature mismatch).
- Wrapper libraries must update their own forwarders.
- Users ignoring the value may see an unused result warning (can cast to `(void)` if desired).

Potential compatibility shim (not included by default):

```cpp
#ifdef ASYNC_HTTP_LEGACY_VOID_API
inline void get_legacy(AsyncHttpClient& c, const char* url,
        AsyncHttpClient::SuccessCallback ok,
        AsyncHttpClient::ErrorCallback err = nullptr) {
    (void)c.get(url, ok, err);
}
#endif
```

Request if you would like these legacy inline adapters added to the library.

If you define `ASYNC_HTTP_LEGACY_VOID_API` (e.g. via build flags), the class exposes helper wrappers like `get_legacy()`, `post_legacy()`, etc., that reproduce the old `void` signatures while discarding the new request ID.

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
