/*
 * GNU Oleo input filter for Gnumeric
 *
 * Author:
 *    Robert Brady <rwb197@ecs.soton.ac.uk>
 *
 * partially based on the Lotus-123 code,
 * partially based on actual Oleo code.
 */
#include <config.h>
#include "oleo.h"

#include <workbook.h>
#include <sheet.h>
#include <cell.h>
#include <value.h>
#include <parse-util.h>
#include <plugin-util.h>

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#define OLEO_DEBUG 0

static Sheet *
attach_sheet (Workbook *wb, int idx)
{
	Sheet *sheet;
	char  *sheet_name;

	sheet_name = g_strdup_printf (_("Sheet%d"), idx);
	sheet = sheet_new (wb, sheet_name);
	g_free (sheet_name);
	workbook_sheet_attach (wb, sheet, NULL);

	return sheet;
}

#define OLEO_TO_GNUMERIC(a) ((a) - 1)
#define GNUMERIC_TO_OLEO(a) ((a) + 1)

static Cell *
oleo_insert_value (Sheet *sheet, guint32 col, guint32 row, Value *val)
{
	Cell *cell;

	g_return_val_if_fail (val != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);

	cell = sheet_cell_fetch (sheet, OLEO_TO_GNUMERIC (col),
				        OLEO_TO_GNUMERIC (row));

	g_return_val_if_fail (cell != NULL, NULL);
	cell_set_value (cell, val, NULL);
	return cell;
}

/* adapted from Oleo */
static long
astol (char **ptr)
{
	long i = 0;
	int sign = 1;
	unsigned char *s, c;

	s = (unsigned char *)*ptr;

	/* Skip whitespace */
	while (isspace (*s))
		if (*s++ == '\0') {
			*ptr = (char *)s;
			return (0);
		}
	/* Check for - or + */
	if (*s == '-') {
		s++;
		sign = -1;
	}
	else if (*s == '+')
		s++;

	/* FIXME -- this is silly and assumed 32 bit ints.  */
	/* Read in the digits */
	for (; (c = *s); s++) {
		if (!isdigit (c) || i > 214748364 ||
		    (i == 214748364 && c > (sign > 0 ? '7' : '8')))
			break;
		i = i * 10 + c - '0';
	}
	*ptr = (char *)s;
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
			double double_result = g_strtod (cval, &error);
		        if (!*error)
		     		oleo_insert_value (sheet, *ccol, *crow,
						   value_new_float (double_result));
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

void
oleo_read (IOContext *io_context, Workbook *wb, const gchar *filename)
{
	FILE *f;
	int sheetidx  = 0;
	int ccol = 0, crow = 0;
	Sheet *sheet = NULL;
	char str[2048];
	ErrorInfo *error;

	f = gnumeric_fopen_error_info (filename, "rb", &error);
	if (f == NULL) {
		gnumeric_io_error_info_set (io_context, error);
		return;
	}

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
}
