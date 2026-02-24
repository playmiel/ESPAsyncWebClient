#include "HttpHelpers.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

String normalizeDomainForStorage(const String& domain) {
    String cleaned = domain;
    cleaned.trim();
    if (cleaned.startsWith("."))
        cleaned.remove(0, 1);
    cleaned.toLowerCase();
    return cleaned;
}

bool equalsIgnoreCase(const String& a, const char* b) {
    if (!b)
        return false;
    size_t lenA = a.length();
    size_t lenB = strlen(b);
    if (lenA != lenB)
        return false;
    for (size_t i = 0; i < lenA; ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

int64_t currentTimeSeconds() {
    time_t now = time(nullptr);
    if (now > 0)
        return static_cast<int64_t>(now);
    // Fallback to millis-based monotonic clock when wall time is not set
    return static_cast<int64_t>(millis() / 1000);
}

int monthFromAbbrev(const char* mon) {
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (!mon || strlen(mon) < 3)
        return -1;
    for (int i = 0; i < 12; ++i) {
        if (tolower((unsigned char)mon[0]) == tolower((unsigned char)kMonths[i][0]) &&
            tolower((unsigned char)mon[1]) == tolower((unsigned char)kMonths[i][1]) &&
            tolower((unsigned char)mon[2]) == tolower((unsigned char)kMonths[i][2])) {
            return i;
        }
    }
    return -1;
}

int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    // Howard Hinnant's days_from_civil, offset so 1970-01-01 yields 0
    y -= m <= 2 ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);           // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
    return era * 146097 + static_cast<int>(doe) - 719468;
}

bool makeUtcTimestamp(int year, int month, int day, int hour, int minute, int second, int64_t* outEpoch) {
    if (!outEpoch)
        return false;
    if (month < 1 || month > 12 || day < 1 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
        second > 60)
        return false;
    static const uint8_t kMonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    uint8_t maxDay = kMonthDays[month - 1] + ((leap && month == 2) ? 1 : 0);
    if (static_cast<uint8_t>(day) > maxDay)
        return false;
    int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + second;
    *outEpoch = seconds;
    return true;
}

bool parseHttpDate(const String& value, int64_t* outEpoch) {
    if (!outEpoch)
        return false;
    String date = value;
    date.trim();
    if (date.length() < 20) // Shorter than "01 Jan 1970 00:00:00 GMT"
        return false;
    int comma = date.indexOf(',');
    if (comma != -1)
        date = date.substring(comma + 1);
    date.trim();

    int day = 0, year = 0, hour = 0, minute = 0, second = 0;
    char monthBuf[4] = {0};
    char tzBuf[4] = {0};
    int matched = sscanf(date.c_str(), "%d %3s %d %d:%d:%d %3s", &day, monthBuf, &year, &hour, &minute, &second, tzBuf);
    if (matched < 6)
        return false;
    if (matched == 6)
        strncpy(tzBuf, "GMT", sizeof(tzBuf));
    if (!(equalsIgnoreCase(String(tzBuf), "GMT") || equalsIgnoreCase(String(tzBuf), "UTC")))
        return false;
    int month = monthFromAbbrev(monthBuf);
    if (month < 0)
        return false;
    int64_t epoch = 0;
    if (!makeUtcTimestamp(year, month + 1, day, hour, minute, second, &epoch))
        return false;
    *outEpoch = epoch;
    return true;
}
