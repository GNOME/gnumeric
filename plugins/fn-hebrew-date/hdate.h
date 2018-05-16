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


#ifndef __HDATE_H__
#define __HDATE_H__

#ifdef __cplusplus
extern "C"
{
#endif

/**
 @brief Return the days from the start

 @param y The years
 @warning internal function.
*/
int
hdate_days_from_start (int y);

/**
 @brief compute hebrew date from gregorian date

 @param d Day of month 1..31
 @param m Month 1..12 ,  if m or d is 0 return current date.
 @param y Year in 4 digits e.g. 2001
 */
int
hdate_gdate_to_hdate (int d, int m, int y, int *hd, int *hm, int *hy);

/**
 @brief compute general date structure from hebrew date

 @param d Day of month 1..31
 @param m Month 0..13 ,  if m or d is 0 return current date.
 @param y Year in 4 digits e.g. 5731

 @attention no sanity cheak !!
 */
int
hdate_hdate_to_gdate (int d, int m, int y, int *gd, int *gm, int *gy);

/**
 @brief Compute Julian day (jd from Gregorian day, month and year (d, m, y)
 Algorithm from 'Julian and Gregorian Day Numbers' by Peter Meyer
 @author Yaacov Zamir ( algorithm from Henry F. Fliegel and Thomas C. Van Flandern ,1968)

 @param d Day of month 1..31
 @param m Month 1..12
 @param y Year in 4 digits e.g. 2001
 */
int
hdate_gdate_to_jd (int d, int m, int y);

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
hdate_jd_to_gdate (int jd, int *d, int *m, int *y);

/**
 @brief Converting from the Julian day to the Hebrew day
 @author Amos Shapir 1984 (rev. 1985, 1992) Yaacov Zamir 2003-2005

 @param jd Julian day
 @param d Return Day of month 1..31
 @param m Return Month 1..14
 @param y Return Year in 4 digits e.g. 2001
 */
void
hdate_jd_to_hdate (int jd, int *d, int *m, int *y);

/**
 @brief Compute Julian day (jd from Hebrew day, month and year (d, m, y)
 @author Amos Shapir 1984 (rev. 1985, 1992) Yaacov Zamir 2003-2005

 @param d Day of month 1..31
 @param m Month 1..14
 @param y Year in 4 digits e.g. 5753
 */
int
hdate_hdate_to_jd (int d, int m, int y);

/* string functions */

/**
 @brief convert an integer to hebrew string UTF-8 (logical)

 @param n The int to convert
 @attention ( 0 < n < 10000)
 @warning uses a static string, so output should be copied away.
*/
void hdate_int_to_hebrew (GString *res, int n);

/**
 @brief Return a static string, with name of hebrew month.

 @param month The number of the month 0..13 (0 - tishre, 12 - adar 1, 13 - adar 2).
 @warning uses a static string, so output should be copied away.
*/
const char *hdate_get_hebrew_month_name (int month);

/**
 @brief Return a static string, with name of hebrew month in hebrew.

 @param month The number of the month 0..13 (0 - tishre, 12 - adar 1, 13 - adar 2).
 @warning uses a static string, so output should be copied away.
*/
const char *hdate_get_hebrew_month_name_heb (int month);

#ifdef __cplusplus
}
#endif

#endif
