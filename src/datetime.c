/*
 * datetime.c: Date and time routines grabbed from elsewhere.
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Morten Welinder <terra@diku.dk>
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <config.h>
#include "datetime.h"
#include "number-match.h"
#include <math.h>

#define SECS_PER_DAY (24 * 60 * 60)
#define HALF_SEC (0.5 / SECS_PER_DAY)

/* ------------------------------------------------------------------------- */

/* One less that the Julian day number of 19000101.  */
static int date_origin = 0;

/*
 * The serial number of 19000228.  Excel allocates a serial number for
 * the non-existing date 19000229.
 */
static const int date_serial_19000228 = 58;

static void
date_init (void)
{
	/* Day 1 means 1st of January of 1900 */
	GDate* date = g_date_new_dmy (1, 1, 1900);
	date_origin = g_date_julian (date) - 1;
	g_date_free (date);
}

/* ------------------------------------------------------------------------- */

int
datetime_g_to_serial (GDate *date)
{
	int day;

	if (!date_origin)
		date_init ();

	day = g_date_julian (date) - date_origin;
	return day + (day > date_serial_19000228);
}

/* ------------------------------------------------------------------------- */

GDate* 
datetime_serial_to_g (int serial)
{
	if (!date_origin)
		date_init ();

	if (serial <= date_serial_19000228)
		return g_date_new_julian (serial + date_origin);
	else if (serial == date_serial_19000228 + 1) {
		g_warning ("Request for date 19000229.");
		return g_date_new_julian (serial + date_origin);
	} else
		return g_date_new_julian (serial + date_origin - 1);
}

/* ------------------------------------------------------------------------- */

float_t
datetime_value_to_serial_raw (const Value *v)
{
	float_t serial;

	if (VALUE_IS_NUMBER (v))
		serial = value_get_as_float (v);
	else {
		char *str, *format;
		double dserial;

		str = value_get_as_string (v);
		if (format_match (str, &dserial, &format)) {
			serial = dserial;
		} else
			serial = 0;
		g_free (str);
	}
	return serial;
}

/* ------------------------------------------------------------------------- */

float_t
datetime_timet_to_serial_raw (time_t t)
{
	struct tm *tm = localtime (&t);
	int secs;
	GDate date;

        g_date_clear (&date, 1);
	g_date_set_time (&date, t);
	secs = tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
	return datetime_g_to_serial (&date) + secs / (double)SECS_PER_DAY;
}

/* ------------------------------------------------------------------------- */

inline int
datetime_serial_raw_to_serial (float_t raw)
{
	return (int) floor (raw + HALF_SEC);
}

/* ------------------------------------------------------------------------- */

int
datetime_value_to_serial (const Value *v)
{
	return datetime_serial_raw_to_serial (datetime_value_to_serial_raw (v));
}

/* ------------------------------------------------------------------------- */

int
datetime_timet_to_serial (time_t t)
{
	return datetime_serial_raw_to_serial (datetime_timet_to_serial_raw (t));
}

/* ------------------------------------------------------------------------- */

GDate *
datetime_value_to_g (const Value *v)
{
	int serial;
	serial = datetime_value_to_serial (v);
	return serial ? datetime_serial_to_g (serial) : NULL;
}

/* ------------------------------------------------------------------------- */
/* This is time-only assuming a 24h day.  It probably loses completely on */
/* days with summer time ("daylight savings") changes.  */

inline int
datetime_serial_raw_to_seconds (float_t raw)
{
	raw += HALF_SEC;
	return (raw - floor (raw)) * SECS_PER_DAY;
}

/* ------------------------------------------------------------------------- */

int
datetime_value_to_seconds (const Value *v)
{
	return datetime_serial_raw_to_seconds (datetime_value_to_serial_raw (v));
}

/* ------------------------------------------------------------------------- */

int
datetime_timet_to_seconds (time_t t)
{
	return datetime_serial_raw_to_seconds (datetime_timet_to_serial_raw (t));
}

/* ------------------------------------------------------------------------- */
