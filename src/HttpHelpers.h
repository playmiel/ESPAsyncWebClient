#ifndef HTTP_HELPERS_H
#define HTTP_HELPERS_H

#include <Arduino.h>
#include <cstdint>

String normalizeDomainForStorage(const String& domain);
bool equalsIgnoreCase(const String& a, const char* b);
int64_t currentTimeSeconds();
int monthFromAbbrev(const char* mon);
int64_t daysFromCivil(int y, unsigned m, unsigned d);
bool makeUtcTimestamp(int year, int month, int day, int hour, int minute, int second, int64_t* outEpoch);
bool parseHttpDate(const String& value, int64_t* outEpoch);

#endif // HTTP_HELPERS_H
