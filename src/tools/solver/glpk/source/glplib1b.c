/* glplib1b.c (platform-independent ISO C version) */

/*----------------------------------------------------------------------
-- This code is part of GNU Linear Programming Kit (GLPK).
--
-- Copyright (C) 2000, 01, 02, 03, 04, 05, 06 Andrew Makhorin,
-- Department for Applied Informatics, Moscow Aviation Institute,
-- Moscow, Russia. All rights reserved. E-mail: <mao@mai2.rcnet.ru>.
--
-- GLPK is free software; you can redistribute it and/or modify it
-- under the terms of the GNU General Public License as published by
-- the Free Software Foundation; either version 2, or (at your option)
-- any later version.
--
-- GLPK is distributed in the hope that it will be useful, but WITHOUT
-- ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
-- or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
-- License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with GLPK; see the file COPYING. If not, write to the Free
-- Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
-- 02110-1301, USA.
----------------------------------------------------------------------*/

#include <stdlib.h>
#include <time.h>
#include "glplib.h"

/*----------------------------------------------------------------------
-- jday - convert calendar date to Julian day.
--
-- This procedure converts a calendar date, Gregorian calendar, to the
-- corresponding Julian day number j. From the given day d, month m, and
-- year y, the Julian day number j is computed without using tables. The
-- procedure is valid for any valid Gregorian calendar date. */

static int jday(int d, int m, int y)
{     int c, ya, j;
      if (m > 2) m -= 3; else m += 9, y--;
      c = y / 100;
      ya = y - 100 * c;
      j = (146097 * c) / 4 + (1461 * ya) / 4 + (153 * m + 2) / 5 + d +
         1721119;
      return j;
}

/*----------------------------------------------------------------------
-- lib_get_time - determine the current universal time.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- gnm_float lib_get_time(void);
--
-- *Returns*
--
-- The routine lib_get_time returns the current universal time (UTC),
-- in seconds, elapsed since 12:00:00 GMT January 1, 2000. */

gnm_float lib_get_time(void)
{     time_t timer;
      struct tm *tm;
      int j2000, j;
      gnm_float secs;
      timer = time(NULL);
      tm = gmtime(&timer);
      j2000 = 2451545 /* = jday(1, 1, 2000) */;
      j = jday(tm->tm_mday, tm->tm_mon + 1, 1900 + tm->tm_year);
      secs = (((gnm_float)(j - j2000) * 24.0 + (gnm_float)tm->tm_hour) * 60.0
         + (gnm_float)tm->tm_min) * 60.0 + (gnm_float)tm->tm_sec - 43200.0;
      return secs;
}

/* eof */
