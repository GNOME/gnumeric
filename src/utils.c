#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
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
	static char buffer [10];
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
cellref_name (CellRef *cell_ref, int eval_col, int eval_row)
{
	static char buffer [20];
	char *p = buffer;
	int col, row;
	
	if (cell_ref->col_relative)
		col = eval_col + cell_ref->col;
	else {
		*p++ = '$';
		col = cell_ref->col;
	}
	
	if (col < 'Z'-'A'){
		*p++ = col + 'A';
	} else {
		int a = col / ('Z'-'A');
		int b = col % ('Z'-'A');
		
		*p++ = a + 'A';
		*p++ = b + 'A';
	}
	if (cell_ref->row_relative)
		row = eval_row + cell_ref->row;
	else {
		*p++ = '$';
		row = cell_ref->row;
	}

	sprintf (p, "%d", row+1);

	return buffer;
}

char *
col_name (int col)
{
	static char buffer [20];
	char *p = buffer;
	
	if (col < 'Z'-'A'){
		*p++ = col + 'A';
	} else {
		int a = col / ('Z'-'A');
		int b = col % ('Z'-'A');

		*p++ = a + 'A';
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
