/*
 * hdate_hdate.c: convert georgean and hebrew calendars.
 *
 * Author:
 *   Yaacov Zamir <kzamir@walla.co.il>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>

#include "hdate.h"

/* constants, in 1/18th of minute */
#define HOUR 1080
#define DAY  (24*HOUR)
#define WEEK (7*DAY)
#define M(h,p) ((h)*HOUR+p)
#define MONTH (DAY+M(12,793))

/**
 @brief Return the days from the start

 @param y The years
 @warning internal function.
*/
int
hdate_days_from_start (int y)
{
	int m, dw, s, l;
	gint64 nm;		/* Needs more than 32 bits for years past ~8120 */

	l = y * 7 + 1;		/* no. of leap months */
	m = y * 12 + l / 19;	/* total no. of months */
	l %= 19;
	nm = (gint64)m * MONTH + M (1 + 6, 779);	/* molad new year 3744 (16BC) + 6 hours */
	s = m * 28 + (int)(nm / DAY) - 2;

	nm %= WEEK;
	dw = nm / DAY;
	nm %= DAY;

	/* special cases of Molad Zaken */
	if ((l < 12 && dw == 3 && nm >= M (9 + 6, 204)) ||
	    (l < 7 && dw == 2 && nm >= M (15 + 6, 589)))
		s++, dw++;
	/* ADU */
	if (dw == 1 || dw == 4 || dw == 6)
		s++;
	return s;
}

/**
 @brief Check a (proleptic) Gregorian date

 @param d Day of month 1..31
 @param m Month 1..12
 @param y Year in 4 digits e.g. 2001

 @return TRUE if the date exists.
 */
gboolean
hdate_gdate_is_valid (int d, int m, int y)
{
	static const int days_in_month[12] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};
	int n;

	if (y <= 0 || m < 1 || m > 12 || d < 1)
		return FALSE;

	n = days_in_month[m - 1];
	if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
		n++;

	return d <= n;
}

/**
 @brief compute hebrew date from gregorian date

 @param d Day of month 1..31
 @param m Month 1..12
 @param y Year in 4 digits e.g. 2001

 @return 0 on success, 1 if the gregorian date does not exist.
 */
int
hdate_gdate_to_hdate (int d, int m, int y, int *hd, int *hm, int *hy)
{
	int jd;

	if (!hdate_gdate_is_valid (d, m, y))
		return 1;

	jd = hdate_gdate_to_jd (d, m, y);

	hdate_jd_to_hdate (jd, hd, hm, hy);

	return 0;
}

/* Number of days in hebrew month m (0..13) of a year with s days, or 0 */
/* if there is no such month in that kind of year.  */
static int
hdate_month_length (int m, int s)
{
	int leap = s > 365;

	switch (m) {
	case 0: return 30;			/* Tishri */
	case 1: return s % 10 > 4 ? 30 : 29;	/* Heshwan: long if year is 355/385 */
	case 2: return s % 10 < 4 ? 29 : 30;	/* Kislev: short if year is 353/383 */
	case 3: return 29;			/* Tebet */
	case 4: return 30;			/* Shebat */
	case 5: return leap ? 0 : 29;		/* Adar */
	case 6: return 30;			/* Nisan */
	case 7: return 29;			/* Iyar */
	case 8: return 30;			/* Sivan */
	case 9: return 29;			/* Tammuz */
	case 10: return 30;			/* Ab */
	case 11: return 29;			/* Elul */
	case 12: return leap ? 30 : 0;		/* Adar I */
	case 13: return leap ? 29 : 0;		/* Adar II */
	default: return 0;
	}
}

/**
 @brief compute gregorian date from hebrew date

 @param d Day of month 1..30
 @param m Month 0..13 (0 - tishre, 5 - adar, 12 - adar 1, 13 - adar 2).
          Adar is used in normal years, adar 1 and adar 2 in leap years.
 @param y Year in 4 digits e.g. 5731, at least 3744

 @return 0 on success, 1 if the hebrew date does not exist.
 */
int
hdate_hdate_to_gdate (int d, int m, int y, int *gd, int *gm, int *gy)
{
	int s, jd;

	if (y < 3744)
		return 1;

	s = hdate_days_from_start (y - 3744 + 1) - hdate_days_from_start (y - 3744);
	if (d < 1 || d > hdate_month_length (m, s))
		return 1;

	jd = hdate_hdate_to_jd (d, m, y);

	hdate_jd_to_gdate (jd, gd, gm, gy);

	return *gy <= 0;
}

/**
 @brief Compute Julian day (jd from Gregorian day, month and year (d, m, y)
 Algorithm from 'Julian and Gregorian Day Numbers' by Peter Meyer
 @author Yaacov Zamir ( algorithm from Henry F. Fliegel and Thomas C. Van Flandern ,1968)

 @param d Day of month 1..31
 @param m Month 1..12
 @param y Year in 4 digits e.g. 2001
 */
int
hdate_gdate_to_jd (int d, int m, int y)
{
	int jd;

	jd = (1461 * (y + 4800 + (m - 14) / 12)) / 4 +
		(367 * (m - 2 - 12 * ((m - 14) / 12))) / 12 -
		(3 * ((y + 4900 + (m - 14) / 12) / 100)) / 4 + d - 32075;

	return jd;
}

/**
 @brief Converting from the Julian day to the Gregorian day
 Algorithm from 'Julian and Gregorian Day Numbers' by Peter Meyer
 @author Yaacov Zamir ( Algorithm, Henry F. Fliegel and Thomas C. Van Flandern ,1968)

 @param jd Julian day
 @param d Return Day of month 1..31
 @param m Return Month 1..12
 @param y Return Year in 4 digits e.g. 2001
 */
void
hdate_jd_to_gdate (int jd, int *d, int *m, int *y)
{
	int l, n, i, j;

	l = jd + 68569;
	n = (4 * l) / 146097;
	l = l - (146097 * n + 3) / 4;
	i = (4000 * (l + 1)) / 1461001;	/* that's 1,461,001 */
	l = l - (1461 * i) / 4 + 31;
	j = (80 * l) / 2447;
	*d = l - (2447 * j) / 80;
	l = j / 11;
	*m = j + 2 - (12 * l);
	*y = 100 * (n - 49) + i + l;	/* that's a lower-case L */
}

/**
 @brief Converting from the Julian day to the Hebrew day
 @author Amos Shapir 1984 (rev. 1985, 1992) Yaacov Zamir 2003-2005

 @param jd Julian day
 @param d Return Day of month 1..31
 @param m Return Month 1..14
 @param y Return Year in 4 digits e.g. 2001
 */
void
hdate_jd_to_hdate (int jd, int *d, int *m, int *y)
{
	int s;
	int l, n, i, j;

	l = jd + 68569;
	n = (4 * l) / 146097;
	l = l - (146097 * n + 3) / 4;
	i = (4000 * (l + 1)) / 1461001;	/* that's 1,461,001 */
	l = l - (1461 * i) / 4 + 31;
	j = (80 * l) / 2447;
	l = j / 11;
	*y = 100 * (n - 49) + i + l;	/* that's a lower-case L */

	*d = jd - 1715119;	/* julean to days since 1 Tishrei 3744 */

	*y += 16;
	s = hdate_days_from_start (*y);
	*m = hdate_days_from_start (*y + 1);

	while (*d >= *m)
	{			/* computed year was underestimated */
		s = *m;
		(*y)++;
		*m = hdate_days_from_start (*y + 1);
	}
	*d -= s;
	s = *m - s;		/* size of current year */
	*y += 3744;

	/* compute day and month */
	if (*d >= s - 236)
	{			/* last 8 months are regular */
		*d -= s - 236;
		*m = *d * 2 / 59;
		*d -= (*m * 59 + 1) / 2;
		*m += 4;
		if (s > 365 && *m <= 5)	/* Adar of Meuberet */
			*m += 8;
	}
	else
	{
		/* first 4 months have 117-119 days */
		s = 114 + s % 10;
		*m = *d * 4 / s;
		*d -= (*m * s + 3) / 4;
	}
}

/**
 @brief Compute Julian day (jd from Hebrew day, month and year (d, m, y)
 @author Amos Shapir 1984 (rev. 1985, 1992) Yaacov Zamir 2003-2005

 @param d Day of month 1..30 (note: hdate_jd_to_hdate returns 0..29)
 @param m Month 0..13 (0 - tishre, 5 - adar, 12 - adar 1, 13 - adar 2)
 @param y Year in 4 digits e.g. 5753, at least 3744

 @attention no sanity check; see hdate_hdate_to_gdate.
 */
int
hdate_hdate_to_jd (int d, int m, int y)
{
	static const int normal_order[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
	static const int leap_order[13] = { 0, 1, 2, 3, 4, 12, 13, 6, 7, 8, 9, 10, 11 };
	int start = hdate_days_from_start (y - 3744);
	int s = hdate_days_from_start (y - 3744 + 1) - start;
	int leap = s > 365;
	const int *order = leap ? leap_order : normal_order;
	int n = leap ? 13 : 12;
	int i, offset = 0;

	for (i = 0; i < n && order[i] != m; i++)
		offset += hdate_month_length (order[i], s);

	/* 1715119 is the julian day of 1 Tishrei 3744 */
	return 1715119 + start + offset + (d - 1);
}
