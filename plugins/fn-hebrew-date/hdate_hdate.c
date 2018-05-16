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
	int m, nm, dw, s, l;

	l = y * 7 + 1;		/* no. of leap months */
	m = y * 12 + l / 19;	/* total no. of months */
	l %= 19;
	nm = m * MONTH + M (1 + 6, 779);	/* molad new year 3744 (16BC) + 6 hours */
	s = m * 28 + nm / DAY - 2;

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
 @brief compute hebrew date from gregorian date

 @param d Day of month 1..31
 @param m Month 1..12 ,  if m or d is 0 return current date.
 @param y Year in 4 digits e.g. 2001
 */
int
hdate_gdate_to_hdate (int d, int m, int y, int *hd, int *hm, int *hy)
{
	int jd;

	/* sanity checks */
	if (!(m >= 1 && m <= 12) ||
	    !((d >= 1)
	      && ((y >= 3000 && m == 6 && d <= 59)
		  || (d <= 31))) || !(y > 0))
		return 1;

	/* end of cheking */

	jd = hdate_gdate_to_jd (d, m, y);

	hdate_jd_to_hdate (jd, hd, hm, hy);

	return 0;

}

/**
 @brief compute general date structure from hebrew date

 @param d Day of month 1..31
 @param m Month 0..13 ,  if m or d is 0 return current date.
 @param y Year in 4 digits e.g. 5731

 @attention no sanity cheak !!
 */
int
hdate_hdate_to_gdate (int d, int m, int y, int *gd, int *gm, int *gy)
{
	int jd;

	/* sanity checks */
	if (!(m >= 1 && m <= 12) ||
	    !((d >= 1)
	      && ((y >= 3000 && m == 6 && d <= 59)
		  || (d <= 31))) || !(y > 0))
		return 1;

	/* end of cheking */

	jd = hdate_hdate_to_jd (d, m, y);

	hdate_jd_to_gdate (jd, gd, gm, gy);

	return 0;
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

 @param d Day of month 1..31
 @param m Month 1..14
 @param y Year in 4 digits e.g. 5753
 */
int
hdate_hdate_to_jd (int d, int m, int y)
{
	int s;

	y -= 3744;
	s = hdate_days_from_start (y);
	d += s;
	s = hdate_days_from_start (y + 1) - s;	/* length of year */

	if (m == 13)
	{
		m = 6;
	}
	if (m == 14)
	{
		m = 6;
		d += 30;
	}

	d += (59 * (m - 1) + 1) / 2;	/* regular months */
	/* special cases */
	if (s % 10 > 4 && m > 2)	/* long Heshvan */
		d++;
	if (s % 10 < 4 && m > 3)	/* short Kislev */
		d--;

	if (s > 365 && m > 6)	/* leap year */
		d += 30;
	d -= 6002;

	y = (d + 36525) * 4 / 146097 - 1;
	d -= y / 4 * 146097 + (y % 4) * 36524;

	d += 1715119;		/* days since 1 Tishrei 3744 to julean */

	return d;
}
