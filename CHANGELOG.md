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

### Added

- Basic chunked transfer decoding (no trailers support)
- HTTPS_NOT_SUPPORTED (-5) and CHUNKED_DECODE_FAILED (-6) error codes

### Documented

- Lifetime of `AsyncHttpResponse*` pointer restricted to success callback
- Limitations: HTTPS unimplemented, chunked minimal, full in-memory buffering

### Internal

- Track all RequestContext objects (even with native timeout) for potential future abort()

