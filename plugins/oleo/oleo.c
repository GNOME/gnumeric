/*
 * GNU Oleo input filter for Gnumeric
 *
 * Robert Brady <rwb197@ecs.soton.ac.uk>
 * 
 * partially based on the Lotus-123 code,
 * partially based on actual Oleo code.
 * 
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <config.h>
#include <stdio.h>
#include <ctype.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "main.h"
#include "sheet.h"
#include "file.h"
#include "utils.h"

#include "oleo.h"

#define OLEO_DEBUG 0

static Sheet *
attach_sheet (Workbook *wb, int idx)
{
	Sheet *sheet;
	char  *sheet_name;

	sheet_name = g_strdup_printf ("Sheet%d\n", idx);
	sheet = sheet_new (wb, sheet_name);
	g_free (sheet_name);
	workbook_attach_sheet (wb, sheet);

	return sheet;
}

#define OLEO_TO_GNUMERIC(a) ((a)-1)
#define GNUMERIC_TO_OLEO(a) ((a)+1)

static Cell *
oleo_insert_value (Sheet *sheet, guint32 col, guint32 row, Value *val)
{
	Cell *cell;

	g_return_val_if_fail (val != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);

	cell = sheet_cell_fetch (sheet, OLEO_TO_GNUMERIC (col), 
				        OLEO_TO_GNUMERIC (row));

	g_return_val_if_fail (cell != NULL, NULL);
	cell_set_value (cell, val);
	return cell;
}

/* adapted from Oleo */
static long
astol (char **ptr)
{
	long i = 0;
	int c;
	int sign = 1;
	char *s;

	s = *ptr;

	/* Skip whitespace */
	while (isspace (*s))
		if (*s++ == '\0') {
			*ptr = s;
			return (0);
		}
	/* Check for - or + */
	if (*s == '-') {
		s++;
		sign = -1;
	}
	else if (*s == '+')
		s++;

  /* Read in the digits */
	for (; (c = *s); s++) {
		if (!isdigit (c) || i > 214748364 ||
		    (i == 214748364 && c > (sign > 0 ? '7' : '8')))
			break;
		i = i * 10 + c - '0';
	}
	*ptr = s;
	return i * sign;
}

static void
oleo_deal_with_cell (char *str, Sheet *sheet, int *ccol, int *crow)
{
	char *ptr = str + 1, *cval = NULL;
	while (*ptr) {
		int quotes = 0;
	        if (*ptr != ';') {
#if OLEO_DEBUG > 0
		   	fprintf (stderr, "ptr : %s\n", ptr);
#endif
		   	break;
		}
		*ptr++ = '\0';
		switch (*ptr++) {
		case 'c' : 
			*ccol = astol (&ptr); 
		   	break;
		case 'r' : 
			*crow = astol (&ptr); 
		   	break;
		case 'K' : 
		   cval = ptr;
		   quotes = 0;
		   while (*ptr && (*ptr != ';' || quotes > 0))
		   	if (*ptr++ == '"')
		   		quotes = !quotes;
		   	break;
		default:
#if OLEO_DEBUG > 0
			fprintf (stderr, "oleo: Don't know how to deal with C; '%c'\n",
				 *ptr);
#endif
		        ptr = ""; /* I wish C had multilevel break */
		        break;
		}
	   
	   if (!*ptr)
	      break;
	}
	
	if (cval) {
		char *error = NULL;
		long result;
		result = strtol(cval, &error, 10);
	   
		if (!*error)
			oleo_insert_value (sheet, *ccol, *crow,
					   value_new_int (result));
		else {
			double double_result = strtod (cval, &error);
		        if (!*error)
		     		oleo_insert_value (sheet, *ccol, *crow,
						   value_new_float ((float)double_result));
	        	else {
				char *last = cval + strlen(cval) - 1;
				if (*cval == '"' && *last == '"') {
		   			*last = 0;
		   			oleo_insert_value (sheet, *ccol, *crow,
							   value_new_string (cval + 1));
				}
			else
		  		oleo_insert_value (sheet, *ccol, *crow,
						   value_new_string (cval));
			}
		}
	} else {
#if OLEO_DEBUG > 0
		fprintf (stderr, "oleo: cval is NULL.\n");
#endif
	}
}

gboolean
oleo_read (Workbook *wb, const char *filename)
{
	FILE *f = fopen (filename, "rb");
	int sheetidx  = 0;

	int ccol = 0, crow = 0;

	Sheet *sheet = NULL;
	char str[2048];

	if (!f) return FALSE;
	cell_deep_freeze_redraws ();
	sheet = attach_sheet (wb, sheetidx++);

	while (1) {
		char *n;
		fgets(str, 2000, f);
		str[2000] = 0;
		if (feof(f)) break;
		n = strchr(str, '\n');
		if (n) 
			*n = 0; 
		else 
			break;
     
		switch (str[0]) {

		case '#': /* Comment */
			continue;

		case 'C': /* Cell */
			oleo_deal_with_cell (str, sheet, &ccol, &crow);
			break;

		default: /* unknown */
#if OLEO_DEBUG > 0
			fprintf (stderr, "oleo: Don't know how to deal with %c.\n",
				 str[0]);
#endif
			break;
		}
	}
	
	fclose (f);
	return TRUE;
}
