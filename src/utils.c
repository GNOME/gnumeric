/*
 * utils.c:  Various utility routines that do not depend on the GUI of Gnumeric
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org)
 *    Jukka-Pekka Iivonen (iivonen@iki.fi)
 */
#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <string.h>
#include <string.h>
#include <ctype.h>
#include <gnome.h>
#include "numbers.h"
#include "mathfunc.h"
#include "symbol.h"
#include "str.h"
#include "expr.h"
#include "utils.h"
#include "gnumeric.h"
#include "sheet.h"

#define SMALL_BUF_SIZE 40
static char small_buffer [SMALL_BUF_SIZE];

void
float_get_from_range (const char *start, const char *end, float_t *t)
{
	char *p;
	int  size = end - start;

	if (size < SMALL_BUF_SIZE-1){
		p = small_buffer;
		strncpy (small_buffer, start, size);
		small_buffer [size] = 0;
	} else {
		p = g_malloc (size + 1);

		strcpy (p, start);
		p [size] = 0;
	}
#ifdef GNUMERIC_USE_GMP
	mpf_init_set_str (*t, p, 10);
#else
	*t = atof (p);
#endif
	if (p != small_buffer)
		g_free (p);
}

void
int_get_from_range (const char *start, const char *end, int_t *t)
{
	char *p;
	int  size = end - start;

	if (size < SMALL_BUF_SIZE-1){
		p = small_buffer;
		strncpy (small_buffer, start, size);
		small_buffer [size] = 0;
	} else {
		p = g_malloc (size + 1);

		strncpy (p, start, size);
		p [size] = 0;
	}
#ifdef GNUMERIC_USE_GMP
	mpz_init_set_str (*t, p, 10);
#else
	*t = atoi (p);
#endif
	if (p != small_buffer)
		g_free (p);
}

const char *
cell_name (int col, int row)
{
	static char buffer [2 + 4 * sizeof (long)];
	char *p = buffer;

	if (col <= 'Z'-'A'){
		*p++ = col + 'A';
	} else {
		int a = col / ('Z'-'A'+1);
		int b = col % ('Z'-'A'+1);

		*p++ = a + 'A' - 1;
		*p++ = b + 'A';
	}
	sprintf (p, "%d", row+1);

	return buffer;
}

const char *
col_name (int col)
{
	static char buffer [20];
	char *p = buffer;

	if (col <= 'Z'-'A'){
		*p++ = col + 'A';
	} else {
		int a = col / ('Z'-'A'+1);
		int b = col % ('Z'-'A'+1);

		*p++ = a + 'A' - 1;
		*p++ = b + 'A';
	}
	*p = 0;

	return buffer;
}

/**
 * Converts a column name into an integer
 **/
int
col_from_name (const char *cell_str)
{
	char c;
	int col = 0;

	c = toupper (*cell_str++);
	if (c < 'A' || c > 'Z')
		return FALSE;
	col = c - 'A';
	c = toupper (*cell_str);
	if (c >= 'A' && c <= 'Z')
		col = ((col + 1) * ('Z' - 'A' + 1)) + (c - 'A');
	if (col >= SHEET_MAX_COLS)
		return FALSE;
	else
		return col;
}

/*
 * parse_cell_name
 * @cell_name:   a string representation of a cell name.
 * @col:         result col
 * @row:         result row
 *
 * Return value: true if the cell_name could be successfully parsed
 */
int
parse_cell_name (const char *cell_str, int *col, int *row)
{
	char c;

	/* Parse column name: one or two letters.  */
	c = toupper (*cell_str++);
	if (c < 'A' || c > 'Z')
		return FALSE;
	*col = c - 'A';
	c = toupper (*cell_str);
	if (c >= 'A' && c <= 'Z') {
		*col = ((*col + 1) * ('Z' - 'A' + 1)) + (c - 'A');
		cell_str++;
	}
	if (*col >= SHEET_MAX_COLS)
		return FALSE;

	/* Parse row number: a sequence of digits.  */
	for (*row = 0; *cell_str; cell_str++) {
		if (!isdigit (*cell_str))
			return FALSE;
		*row = *row * 10 + (*cell_str - '0');
		if (*row > SHEET_MAX_ROWS) /* Note: ">" is deliberate.  */
			return FALSE;
	}
	if (*row == 0)
		return FALSE;

	/* Internal row numbers are one less than the displayed.  */
	(*row)--;
	return TRUE;
}

gint
gnumeric_strcase_equal (gconstpointer v, gconstpointer v2)
{
	return strcasecmp ((const gchar*) v, (const gchar*)v2) == 0;
}

/* a char* hash function from ASU */
guint
gnumeric_strcase_hash (gconstpointer v)
{
	const char *s = (const char*)v;
	const char *p;
	guint h=0, g;

	for(p = s; *p != '\0'; p += 1) {
		h = ( h << 4 ) + tolower (*p);
		if ( ( g = h & 0xf0000000 ) ) {
			h = h ^ (g >> 24);
			h = h ^ g;
		}
	}

	return h /* % M */;
}

/* One less that the Julian day number of 19000101.  */
static guint32 date_origin = 0;

/* The serial number of 19000228.  Excel allocates a serial number for
 * the non-existing date 19000229.
 */
static const guint32 date_serial_19000228 = 58;

static void
date_init (void)
{
	/* Day 1 means 1st of January of 1900 */
	GDate* date = g_date_new_dmy (1, 1, 1900);
	date_origin = g_date_julian (date) - 1;
	g_date_free (date);
}

guint32
g_date_serial (GDate* date)
{
	guint32 day;

	if (!date_origin)
		date_init ();

	day = g_date_julian (date) - date_origin;
	return day + (day > date_serial_19000228);
}

GDate*
g_date_new_serial (guint32 serial)
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

/*
 * Returns a list of cells in a string.  If the named cells does not
 * exist, they are created.  If the input string is not valid
 * error_flag is set.
 */
GSList *
parse_cell_name_list (Sheet *sheet,
		      const char *cell_name_str,
		      int *error_flag)
{
        char     *buf;
	GSList   *cells = NULL;
	Cell     *cell;
	int      i, n, col, row;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (cell_name_str != NULL, NULL);
	g_return_val_if_fail (error_flag != NULL, NULL);

	buf = g_malloc (strlen (cell_name_str) + 1);
	for (i = n = 0; 1; i++){

	        if ((cell_name_str [i] == ',') || (cell_name_str [i] == '\0')){
		        buf [n] = '\0';

			if (!parse_cell_name (buf, &col, &row)){
			        *error_flag = 1;
				free (buf);
				g_slist_free (cells);
				return NULL;
			}

			cell = sheet_cell_get (sheet, col, row);

			if (cell == NULL){
			        cell = sheet_cell_new (sheet, col, row);
				cell_set_text (cell, "");
			}
			cells = g_slist_append (cells, (gpointer) cell);
			n = 0;
		} else
		        buf [n++] = cell_name_str [i];

		if (cell_name_str [i] == '\0')
		        break;
	}

	*error_flag = 0;
	free (buf);
	return cells;
}


/*
 * Conservative random number generator.  The result is (supposedly) uniform
 * and between 0 and 1.  (0 possible, 1 not.)  The result should have about
 * 64 bits randomness.
 */
double
random_01 (void)
{
#ifdef HAVE_RANDOM
	int r1, r2;

	r1 = random () & 2147483647;
	r2 = random () & 2147483647;

	return (r1 + (r2 / 2147483648.0)) / 2147483648.0;
#elif defined (HAVE_DRAND48)
	return drand48 ();
#else
	/* We try to work around lack of randomness in rand's lower bits.  */
	int prime = 65537;
	int r1, r2, r3, r4;

	g_assert (RAND_MAX > ((1 << 12) - 1));

	r1 = (rand () ^ (rand () << 12)) % prime;
	r2 = (rand () ^ (rand () << 12)) % prime;
	r3 = (rand () ^ (rand () << 12)) % prime;
	r4 = (rand () ^ (rand () << 12)) % prime;

	return (r1 + (r2 + (r3 + r4 / (double)prime) / prime) / prime) / prime;
#endif
}

/*
 * Generate a N(0,1) distributed number.
 */
double
random_normal (void)
{
	return qnorm (random_01 (), 0, 1);
}
