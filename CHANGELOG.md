# Changelog

All notable changes to this project are documented in this file.

## [Unreleased]
- (TBD)

## [2.1.2] - 2026-03-17
- Version bump and release metadata synchronization.

## [2.1.0] - 2026-02-26
- **Fix**: Use-after-free of `RequestContext` — transport callbacks now capture `shared_ptr<RequestContext>` instead of raw `void*` pointer, preventing dangling access after `cleanup()`.
- **Fix**: `handleData()` now checks `cancelled` flag at entry, matching `handleDisconnect`/`handleTransportError`.
- **Fix**: Race condition between `loop()` timeouts and AsyncTCP callbacks — `triggerError()` and `loop()` now check `atomic<bool> cancelled` before acting.
- **Fix**: `Content-Length` parsing uses `strtoul()` with `endptr` validation instead of `toInt()` (which returns 0 for invalid input).
- **Fix**: `Transfer-Encoding` multi-value support — `gzip, chunked` is now correctly detected via substring search instead of exact comparison.
- **Fix**: `setMaxBodySize()` is now enforced in streaming mode (`setNoStoreBody(true)`) to protect against unbounded data from malicious servers.
- **Perf**: Chunked transfer decoding eliminates double buffering — chunk body bytes are delivered directly from the incoming data buffer instead of being copied through `responseBuffer`.
- **Perf**: `_pendingQueue` changed from `std::vector` to `std::deque` for O(1) dequeue instead of O(n).
- **Feature**: `Content-Type` auto-detection for POST/PUT/PATCH — bodies starting with `{` or `[` default to `application/json`; user-set `Content-Type` headers take precedence.
- Internal: `_activeRequests` now uses `shared_ptr<RequestContext>` instead of `unique_ptr`.

## [2.0.0] - 2026-02-20
- **Breaking**: `SuccessCallback` now receives `std::shared_ptr<AsyncHttpResponse>`.
- **Breaking**: `request()` now takes `std::unique_ptr<AsyncHttpRequest>`.
- **Breaking**: `getBody()`, `getHeader()`, and `getStatusText()` return `String` by value.
- **Breaking**: removed legacy void-return helpers (`*_legacy`, `ASYNC_HTTP_LEGACY_VOID_API`).
- **Breaking**: `parseChunkSizeLine()` is now private.
- Normalize `HttpHeader` names to lowercase for faster lookups.
- Refactor request context state into sub-structs and store active/pending requests as `std::unique_ptr`.
- Document `BodyChunkCallback` lifetime guarantees.
- HTTPS/TLS transport: AsyncTCP + mbedTLS, CA/fingerprint/mutual-auth configuration, TLS error codes, and gated insecure TLS.
- Redirects: default safer cross-origin header forwarding, allow HTTP→HTTPS redirects.
- Cookies: host-only by default, allowlist required for parent-domain `Domain=` attributes.

## [1.0.7] - 2025-11-13
- Align `library.json`, `library.properties`, and `ESP_ASYNC_WEB_CLIENT_VERSION` with release 1.0.7 so CI/release scripts pass.
- Document contributor expectations in `AGENTS.md` and reconfirm README error codes plus HTTPS warning stay in sync with `HttpCommon.h`.
- Verified required Arduino/PlatformIO example skeletons for CI (SimpleGet, PostWithData, MultipleRequests, CustomHeaders, StreamingUpload, CompileTest) remain present.
