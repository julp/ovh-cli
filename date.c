#include <string.h>
#include "date.h"

#define SECOND (1)
#define MINUTE (60 * SECOND)
#define HOUR (60 * MINUTE)
#define DAY (24 * HOUR)

bool date_parse_simple(const char *date, const char *format, time_t *tm, error_t **error)
{
    char *endptr;
    struct tm ltm = { 0 };

    if (NULL == (endptr = strptime(date, NULL == format ? "%F" : format, &ltm))) {
        error_set(error, NOTICE, "unable to parse date: %s", date);
    } else {
        *tm = mktime(&ltm);
    }

    return NULL != endptr;
}

bool date_parse(const char *date, const char *format, struct tm *tm, error_t **error)
{
    char *endptr;

    if (NULL == (endptr = strptime(date, format, tm))) {
        error_set(error, NOTICE, "unable to parse date: %s", date);
    }

    return NULL != endptr;
}

int date_diff_in_days(time_t a, time_t b)
{
    return difftime(a, b) / DAY;
}

enum {
    SECONDS_DONE,
    MINUTES_DONE,
    HOURS_DONE,
    DAYS_DONE,
    NOTHING_DONE
};

// illimited | (?:\d *days?)? *(?:\d *hours?)? *(?:\d *minutes?)? *(?:\d *seconds?)?
bool parse_duration(const char *duration, time_t *value)
{
    if ('\0' == *duration) {
        return FALSE;
    } else if (0 == strcasecmp(duration, "illimited")) {
        *value = 0;
        return TRUE;
    } else {
        long v;
        int part;
        const char *p;

        v = 0;
        *value = 0;
        part = NOTHING_DONE;
        for (p = duration; '\0' != *p; /*p++*/) {
            switch (*p) {
                case ' ':
                    ++p;
                    continue;
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                {
                    char *endptr;

                    if (0 != v) { // 2 consecutive numbers
                        return FALSE;
                    }
                    v = strtol(p, &endptr, 10);
                    p = endptr;
                    break;
                }
                case 'd':
                case 'D':
                    if (part <= DAYS_DONE || (0 != strncasecmp(p, "days", STR_LEN("days")) && 0 != strncasecmp(p, "day", STR_LEN("day"))) || 0 == v) {
                        return FALSE;
                    } else {
                        p += 0 == strncasecmp(p, "days", STR_LEN("days")) ? STR_LEN("days") : STR_LEN("day");
                        *value += v * DAY;
                        part = DAYS_DONE;
                        v = 0;
                    }
                    break;
                case 'h':
                case 'H':
                    if (part <= HOURS_DONE || (0 != strncasecmp(p, "hours", STR_LEN("hours")) && 0 != strncasecmp(p, "hour", STR_LEN("hour"))) || 0 == v) {
                        return FALSE;
                    } else {
                        p += 0 == strncasecmp(p, "hours", STR_LEN("hours")) ? STR_LEN("hours") : STR_LEN("hour");
                        *value += v * HOUR;
                        part = HOURS_DONE;
                        v = 0;
                    }
                    break;
                case 'm':
                case 'M':
                    if (part <= MINUTES_DONE || (0 != strncasecmp(p, "minutes", STR_LEN("minutes")) && 0 != strncasecmp(p, "minute", STR_LEN("minute"))) || 0 == v) {
                        return FALSE;
                    } else {
                        p += 0 == strncasecmp(p, "minutes", STR_LEN("minutes")) ? STR_LEN("minutes") : STR_LEN("minute");
                        *value += v * MINUTE;
                        part = MINUTES_DONE;
                        v = 0;
                    }
                    break;
                case 's':
                case 'S':
                    if (part <= SECONDS_DONE || (0 != strncasecmp(p, "seconds", STR_LEN("seconds")) && 0 != strncasecmp(p, "second", STR_LEN("second"))) || 0 == v) {
                        return FALSE;
                    } else {
                        p += 0 == strncasecmp(p, "seconds", STR_LEN("seconds")) ? STR_LEN("seconds") : STR_LEN("second");
                        *value += v * SECOND;
                        part = SECONDS_DONE;
                        v = 0;
                    }
                    break;
                default:
                    return FALSE;
            }
        }
        return 0 != *value && 0 == v; // nothing really parsed (eg: 0 days) && no pending number (eg: 3 days 12)
    }
}
