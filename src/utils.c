/*
 * utils.c:  Various utility routines that do not depend on the GUI of Gnumeric
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <string.h>
#include <string.h>
#include <ctype.h>
#include "numbers.h"
#include "symbol.h"
#include "str.h"
#include "expr.h"
#include "utils.h"

#define SMALL_BUF_SIZE 40
static char small_buffer [SMALL_BUF_SIZE];

void
float_get_from_range (char *start, char *end, float_t *t)
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
int_get_from_range (char *start, char *end, int_t *t)
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

char *
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

char *
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

/*
 * parse_cell_name
 * @cell_name:   a string representation of a cell name.
 * @col:         result col
 * @row:         result row
 *
 * Return value: true if the cell_name could be successfully parsed
 */
int
parse_cell_name (char *cell_str, int *col, int *row)
{
	*col = 0;
	*row = 0;

	if (!isalpha (*cell_str))
		return FALSE;

	*col = toupper (*cell_str++) - 'A';

	if (isalpha (*cell_str))
		*col = ((*col+1) * ('Z' - 'A' + 1)) + (toupper (*cell_str++) - 'A');

	if (!isdigit (*cell_str))
		return FALSE;

	for (;*cell_str; cell_str++){
		if (!isdigit (*cell_str))
			return FALSE;
		*row = *row * 10 + (*cell_str - '0');
	}
	if (*row == 0)
		return FALSE;

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
	const char *s = (char*)v;
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

static guint32
january_1900()
{
	static guint32 julian = 0;

	if (!julian) {
		GDate* date = g_date_new_dmy (1, 1, 1900);
		/* Day 1 means 1st of jannuary of 1900 */
		julian = g_date_julian (date) - 1;
		g_date_free (date);
	}
	
	return julian;
}

guint32 
g_date_serial (GDate* date)
{
	return g_date_julian (date) - january_1900 ();
}

GDate*
g_date_new_serial (guint32 serial)
{
	return g_date_new_julian (serial + january_1900 ());
}
