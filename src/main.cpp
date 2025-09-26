// Exclude this Arduino stub when running PlatformIO unit tests to avoid
// multiple definitions of setup()/loop() with test files.
#ifndef UNIT_TEST
#include <Arduino.h>
// Minimal stub to satisfy PlatformIO build for library self-test environments.
void setup() {}
void loop() {}
#endif
