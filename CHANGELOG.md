# Changelog

All notable changes to this project are documented in this file.

## [Unreleased]
- Add asynchronous TLS transport (AsyncTCP + mbedTLS) with configurable CA/fingerprint/mutual-auth plus HTTPS request support.
- Extend error enum/table with TLS-specific codes and expose client/request-level TLS configuration helpers.
- Allow HTTP→HTTPS redirects, update docs/tests to reflect the new HTTPS capability.

## [1.0.7] - 2025-11-13
- Align `library.json`, `library.properties`, and `ESP_ASYNC_WEB_CLIENT_VERSION` with release 1.0.7 so CI/release scripts pass.
- Document contributor expectations in `AGENTS.md` and reconfirm README error codes plus HTTPS warning stay in sync with `HttpCommon.h`.
- Verified required Arduino/PlatformIO example skeletons for CI (SimpleGet, PostWithData, MultipleRequests, CustomHeaders, StreamingUpload, CompileTest) remain present.
