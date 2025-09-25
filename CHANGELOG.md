# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2025-06-XX

### Added (Unreleased)

- Initial release of ESPAsyncWebClient library
- Asynchronous HTTP client for ESP32 and ESP8266
- Support for GET, POST, PUT, DELETE HTTP methods
- Callback-based response handling (success and error callbacks)
- Custom header support (global and per-request)
- Configurable request timeouts
- Support for multiple simultaneous requests
- Built on AsyncTCP (ESP32) and ESPAsyncTCP (ESP8266)
- Complete API documentation and examples
- Arduino library format with proper metadata

### Features

- `AsyncHttpClient` main class with simple API
- `AsyncHttpRequest` for advanced request configuration
- `AsyncHttpResponse` for accessing response data
- Automatic memory management
- URL parsing and HTTP request building
- Response header parsing
- Content-Length handling
- Connection management with proper cleanup

### Examples

- SimpleGet - Basic GET request example
- PostWithData - POST request with form data
- MultipleRequests - Multiple simultaneous requests
- CustomHeaders - Custom headers and advanced usage

### Dependencies

- AsyncTCP (for ESP32)
- ESPAsyncTCP (for ESP8266)
- Arduino Core (ESP32 v2.0+ or ESP8266 v3.0+)

### Compatibility (unchanged)

- ESP32 (all variants)
- ESP8266
- Arduino IDE and PlatformIO
- Arduino library format compliant

## [1.0.1] - 2025-09-25

Changes since 1.0.0

### Fixed

- Fixed compilation warnings

### Removed

- Dropped ESP8266 related documentation/claims (library now targets ESP32 only)

### Added (since 1.0.0)

- Workflow GitHub Actions for CI
- CompileTest examples
- HttpClientError enumeration and error string utility

### Changed

- Error callbacks now use HttpClientError enumeration instead of raw integers

### Compatibility

- ESP32 (all variants)
- Arduino IDE and PlatformIO
- Arduino library format compliant

## [Unreleased]

### Planned

- (placeholder)

### Removed

- Per-request body chunk callback API (unstable / deferred). Global `onBodyChunk` remains.

## [1.0.2] - 2025-09-25

### Added

- HEAD & PATCH helper methods
- Separate connect timeout (`setDefaultConnectTimeout`) distinct from total timeout
- Request queue limiting via `setMaxParallel()`
- Abort support returning `ABORTED (-9)` error code
- Extended error codes: `CONNECT_TIMEOUT (-7)`, `BODY_STREAM_READ_FAILED (-8)`, `ABORTED (-9)`

### Changed

- `loop()` iteration made safe against vector mutation during callbacks (index-based)
- Improved chunked decoding: strict `endptr` validation for chunk size
- `handleDisconnect` now detects truncated bodies (Content-Length short or chunked incomplete)
- README updated: new methods, error table, documented abort & streaming

### Fixed

- Potential invalid iteration when triggering cleanup inside timeout / error paths
- Chunk size parsing failure cases now yield `CHUNKED_DECODE_FAILED`

### Internal

- Unified abort semantics (distinct `ABORTED` code instead of reusing `CONNECTION_CLOSED`)
- Clear separation of connect vs total timeout logic

