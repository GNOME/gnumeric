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
	static char buffer [3];
	char *p = buffer;
	
	g_assert (col < SHEET_MAX_COLS);
	g_assert (col >= 0);

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

	c = toupper ((unsigned char)*cell_str++);
	if (c < 'A' || c > 'Z')
		return FALSE;
	col = c - 'A';
	c = toupper ((unsigned char)*cell_str);
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
gboolean
parse_cell_name (const char *cell_str, int *col, int *row)
{
	char c;

	/* Parse column name: one or two letters.  */
	c = toupper ((unsigned char)*cell_str++);
	if (c < 'A' || c > 'Z')
		return FALSE;

	*col = c - 'A';
	c = toupper ((unsigned char)*cell_str);
	if (c >= 'A' && c <= 'Z') {
		*col = ((*col + 1) * ('Z' - 'A' + 1)) + (c - 'A');
		cell_str++;
	}
	if (*col >= SHEET_MAX_COLS)
		return FALSE;

	/* Parse row number: a sequence of digits.  */
	for (*row = 0; *cell_str; cell_str++) {
		if (*cell_str < '0' || *cell_str > '9')
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
        char     *buf, *tmp = NULL;
	GSList   *cells = NULL;
	Cell     *cell;
	int      i, n, j, k, col, row;
	gboolean range_flag = 0;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (cell_name_str != NULL, NULL);
	g_return_val_if_fail (error_flag != NULL, NULL);

	buf = g_malloc (strlen (cell_name_str) + 1);
	for (i = n = 0; ; i++){

	        if ((cell_name_str [i] == ',') || 
		    (cell_name_str [i] == ':') || 
		    (cell_name_str [i] == '\0')){
		        buf [n] = '\0';

			if (!parse_cell_name (buf, &col, &row)){
			error:
			        *error_flag = 1;
				free (buf);
				g_slist_free (cells);
				return NULL;
			}

			if (cell_name_str [i] == ':')
			        if (range_flag) {
				        g_free (tmp);
				        goto error;
				} else {
				        tmp = g_new(char, strlen(buf)+1);
					strcpy(tmp, buf);
				        range_flag = 1;
				}
			else if (range_flag) {
			        int x1, x2, y1, y2;

				parse_cell_name (tmp, &x1, &y1);
				parse_cell_name (buf, &x2, &y2);
			        for (j=x1; j<=x2; j++)
				        for (k=y1; k<=y2; k++) {
					        cell = sheet_cell_fetch
						  (sheet, j, k);
						cells = g_slist_append
						  (cells, (gpointer) cell);
					}        
			} else {
			        cell = sheet_cell_fetch (sheet, col, row);
			        cells = g_slist_append(cells, (gpointer) cell);
			}
			n = 0;
		} else
		        buf [n++] = cell_name_str [i];
		if (! cell_name_str [i])
		        break;
	}

	*error_flag = 0;
	free (buf);
	return cells;
}
