#ifndef GNUMERIC_DATETIME_H
#define GNUMERIC_DATETIME_H

#include "gnumeric.h"
#include "value.h"
#include <time.h>

/*
 * Naming conventions:
 *
 * "g": a GDate *.
 * "timet": Unix' time_t.
 * "serial": Excel serial day number.
 * "serial_raw": serial plus time as fractional day.
 */

/* These do not round and produces fractional values, i.e., includes time.  */
float_t datetime_value_to_serial_raw (const Value *v);
float_t datetime_timet_to_serial_raw (time_t t);

/* These are date-only, no time.  */
int datetime_value_to_serial (const Value *v);
int datetime_timet_to_serial (time_t t);
GDate* datetime_value_to_g (const Value *v);
int datetime_g_to_serial (GDate *date);
GDate* datetime_serial_to_g (int serial);
int datetime_serial_raw_to_serial (float_t raw);

/* These are time-only assuming a 24h day.  It probably loses completely on */
/* days with summer time ("daylight savings") changes.  */
int datetime_value_to_seconds (const Value *v);
int datetime_timet_to_seconds (time_t t);
int datetime_serial_raw_to_seconds (float_t raw);

#endif
