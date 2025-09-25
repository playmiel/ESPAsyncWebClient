# Copilot Project Instructions: ESPAsyncWebClient

Concise, project-specific guidance for AI coding agents. Focus on THESE patterns; avoid generic Arduino boilerplate.

## 1. Big Picture
- Purpose: Non-blocking HTTP client for ESP32 (AsyncTCP backend). Provides simple high-level methods plus an advanced request object.
- Core flow: `AsyncHttpClient` builds an `AsyncHttpRequest` → opens `AsyncClient` TCP connection → accumulates header + body → parses headers (`parseResponseHeaders`) → invokes success/error callback → cleans up (frees request/response/client).
- Ownership: Library owns dynamically allocated `AsyncHttpRequest` passed to `client.request()` and always deletes it in `cleanup()`.
- Timeout strategy: If `ASYNC_TCP_HAS_TIMEOUT` is defined, uses native `AsyncClient::setTimeout`; otherwise a manual polling timeout via `AsyncHttpClient::loop()` and `_activeRequests` vector.

## 2. Key Files
- `src/AsyncHttpClient.{h,cpp}`: Orchestrates request lifecycle, connection callbacks, timeout handling, memory cleanup.
- `src/HttpRequest.{h,cpp}`: URL parsing, header assembly, request string creation. Handles edge cases: query before path (e.g. `example.com?foo=bar`).
- `src/HttpResponse.{h,cpp}`: Stores status, headers, body. Provides helpers: `isSuccess()`, `isRedirect()`, `isError()`.
- `src/HttpCommon.h`: Shared types (`HttpHeader`, `HttpClientError`, `httpClientErrorToString`).
- `examples/*`: Canonical usage patterns (simple, multiple parallel, custom headers, POST with body, compile-only test mode).
- `platformio.ini`: Defines multiple environments (dev, compile test, AsyncTCP variants). Flags like `COMPILE_TEST_ONLY`, `TEST_ASYNCTCP_DEV` influence conditional code.

## 3. Architectural / Design Decisions
- Simplicity over full HTTP spec: Only HTTP/1.1, no chunked transfer decoding implemented (body accumulation assumes either closure or Content-Length).
- Single-pass header parse: Accumulates raw data until `\r\n\r\n`; after parse, remaining bytes treated as body.
- Memory: Uses `String` (dynamic heap). Response body concatenated; no streaming callbacks—be cautious adding very large downloads.
- Thread model: Assumes single-threaded Arduino event loop; callbacks must be lightweight.
- Error surface: Negative custom enum values; positive values bubble from underlying `AsyncClient` error codes.

## 4. URL Parsing Rules (Important Edge Cases)
Implemented in `AsyncHttpRequest::parseUrl()` and mirrored in `test_parse_url.py` for validation:
- Adds default protocol http/80 if none provided.
- Supports `https://` (secure=true, port=443) though TLS connect not yet implemented (no SSL handshake code present—future work if added).
- Query without path: `http://example.com?foo=bar` → host=`example.com`, path=`/?foo=bar`.
- Query before first `/` still treated as path: ensures request line always has leading `/`.

## 5. Callback & Lifecycle Contract
- Success callback receives owned `AsyncHttpResponse*` (valid only inside callback; freed immediately after callback returns).
- Error callback signature: `(HttpClientError code, const char* message)`; message is a short literal.
- After success or error: all internal allocations (`AsyncClient`, `AsyncHttpRequest`, `AsyncHttpResponse`, `RequestContext`) are released.
- Never store raw response pointer beyond callback; document this if adding new examples.

## 6. Adding Features Safely
When extending:
- Respect cleanup invariants: any new early-return path must call `triggerError()` or `processResponse()` (which both end in `cleanup()`).
- For streaming/chunked support: would need to detect `Transfer-Encoding: chunked` and parse incrementally before `processResponse()`.
- For HTTPS real support: replace `AsyncClient` with a secure variant or wrap; keep API unchanged.

## 7. Timeout Behavior
- With native timeout: relies on `AsyncClient` invoking `onTimeout` → calls `triggerError(REQUEST_TIMEOUT, ...)`.
- Manual mode: User must call `client.loop()` regularly; examples show this guarded by `#if !ASYNC_TCP_HAS_TIMEOUT`.
- If adding periodic tasks, do not block > few ms or timeouts become inaccurate.

## 8. Headers & Defaults
- Global defaults stored in `_defaultHeaders`; applied when building a new request in `makeRequest()` before user-specified body sets a `Content-Type` fallback (`application/x-www-form-urlencoded`).
- Per-request headers may override global ones (later set wins inside request object itself).
- Default User-Agent appears twice if not careful: request constructor sets `ESPAsyncWebClient/1.0.1`; client may overwrite via `setUserAgent()` when making the request.

## 9. Testing & CI Workflows
- Python tests (`test_parse_url.py`) replicate C++ URL parsing logic—keep them in sync if parser changes.
- `test_fix.py` validates dependency URL formation (matrix simulation). Update if adding new GitHub Actions matrix dimensions.
- Compile-only mode via `COMPILE_TEST_ONLY` macro prevents network attempts in CI but still validates API surface.
- Suggested local commands:
```bash
pio run -e compile_test
pio run -e esp32dev_asynctcp_dev
pio run -e test_asynctcp_stable
```

## 10. Common Pitfalls to Avoid
- Holding onto response pointer after callback (will be freed).
- Forgetting `client.loop()` when manual timeout path active.
- Adding blocking code in callbacks (starves async processing).
- Assuming body completeness without Content-Length: if server closes early before headers parsed → `CONNECTION_CLOSED`.

## 11. Style / Conventions
- Prefer updating existing header entries (see pattern in `setHeader` methods) instead of duplicating.
- Use `String.concat(data,len)` for appends; avoid manual loops.
- Error strings kept short, reused via `httpClientErrorToString`.
- No exceptions; control flow with early returns & explicit cleanup.

## 12. Extension Examples
- New HTTP method (e.g. PATCH): Add helper in `AsyncHttpClient` calling `makeRequest(HTTP_PATCH, ...)`; ensure enum already has `HTTP_PATCH`.
- Streaming hook: Introduce optional `std::function<void(const char*, size_t)>` in `RequestContext` called inside `handleData` after header parse when appending body.

## 13. When Editing
Before PR:
- If touching URL parser → update python mirror + add new edge-case test.
- If adding headers logic → verify no double insertion & adjust examples if default changes.
- Run at least compile test envs (above commands).

---
Feedback welcome: clarify unclear lifecycle rules, memory ownership, or planned HTTPS scope.
