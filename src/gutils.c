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
#include <ctype.h>
#include "numbers.h"
#include "gutils.h"
#include "gnumeric.h"
#include "sheet.h"
#include "ranges.h"


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

gint
gnumeric_strcase_equal (gconstpointer v, gconstpointer v2)
{
	return strcasecmp ((const gchar*) v, (const gchar*)v2) == 0;
}

/* a char* hash function from ASU */
guint
gnumeric_strcase_hash (gconstpointer v)
{
	const unsigned char *s = (const unsigned char *)v;
	const unsigned char *p;
	guint h = 0, g;

	for(p = s; *p != '\0'; p += 1) {
		h = ( h << 4 ) + tolower (*p);
		if ( ( g = h & 0xf0000000 ) ) {
			h = h ^ (g >> 24);
			h = h ^ g;
		}
	}

	return h /* % M */;
}

