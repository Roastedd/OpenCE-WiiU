/*
HTTP_DATE.H

An HTTP "Date" header's time (the IMF-fixdate of RFC 9110, "Fri, 06 Nov 2026 19:09:11 GMT") as Unix seconds in UTC. The Wii U's clock is the console's own and is
often hours off (a time zone set as if it were UTC): a list signed by a master server is checked against the time the server's own TLS answer gives, which the
console's clock cannot spoil, when the two differ by more than the list's freshness. Header only, so that the test can compile it alone.
*/

#ifndef HTTP_DATE_H
#define HTTP_DATE_H

#include <stdio.h>
#include <string.h>

/* Unix seconds of the date, or 0 when the text is not an IMF-fixdate with a year from 2000 to 2100 */
static inline double http_date_parse(const char *text)
{
	static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
	char month_name[4] = { 0 };
	int day, year, hour, minute, second, month, adjusted_year, era, year_of_era, day_of_year, day_of_era;
	const char *comma = text ? strchr(text, ',') : NULL;
	char const *found;
	double days;

	if (!comma)
		return 0.0;
	if (sscanf(comma + 1, " %d %3s %d %d:%d:%d", &day, month_name, &year, &hour, &minute, &second) != 6)
		return 0.0;
	found = strstr(months, month_name);
	if (!found || (found - months) % 3 != 0)
		return 0.0;
	month = (int)((found - months) / 3) + 1;
	if (day < 1 || day > 31 || year < 2000 || year > 2100 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60)
		return 0.0;
	/* days from 1970-01-01 (Howard Hinnant's days_from_civil) */
	adjusted_year = month <= 2 ? year - 1 : year;
	era = adjusted_year / 400;
	year_of_era = adjusted_year - era * 400;
	day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
	days = (double)(era * 146097 + day_of_era - 719468);
	return days * 86400.0 + hour * 3600.0 + minute * 60.0 + second;
}

#endif
