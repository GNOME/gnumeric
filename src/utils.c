#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <string.h>
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

		strcpy (p, start);
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
	static char buffer [10];
	char *p = buffer;
	
	if (col < 'Z'-'A'){
		*p++ = col + 'A';
	} else {
		int a = col / ('Z'-'A');
		int b = col % ('Z'-'A');
		
		*p++ = a + 'A';
		*p++ = b + 'A';
	}
	sprintf (p, "%d", row+1);

	return buffer;
}

