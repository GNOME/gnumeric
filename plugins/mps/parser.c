/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * mps.c: MPS file parser.
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 *      Reads an MPS file and stores the data in it in data structures
 *      defined in mps.h.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <gnumeric-config.h>
#include "mps.h"
#include <gnumeric.h>
#include "cell.h"
#include "sheet.h"
#include "value.h"
#include <goffice/app/io-context.h>
#include "workbook-view.h"
#include "workbook.h"
#include <goffice/app/module-plugin-defs.h>
#include "ranges.h"
#include "style.h"
#include "value.h"
#include "solver.h"
#include "sheet-style.h"
#include "parse-util.h"
#include <goffice/app/file.h>
#include <goffice/app/error-info.h>
#include <glib/gi18n-lib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


/************************************************************************
 *
 * Parser's low level stuff.
 */

/*---------------------------------------------------------------------*/

/* Read a line from the file. */
static gboolean
mps_get_line (MpsInputContext *ctxt)
{
	do {
		ctxt->line = gsf_input_textline_ascii_gets (ctxt->input);
		if (ctxt->line == NULL)
			return FALSE;
		/* Check if a comment or empty line */
	} while (ctxt->line[0] == '*' || ctxt->line[0] == '\0');

	return TRUE;
}

static gboolean
mps_parse_data (gchar *str, gchar *type, gchar *name1, gchar *name2,
		gchar *value1, gchar *name3, gchar *value2)
{
        gint  i;
	gchar *n1 = name1;
	gchar *n2 = name2;
	gchar *n3 = name3;

	for (i = 0; i < 8; i++)
	        name1[i] = name2[i] = name3[i] = ' ';
	*value2 = *name3 = '\0';
        if (!(*str) || *str++ != ' ' || !(*str))
	        return FALSE;

	/* Type field is present */
	if (*str != ' ') {
	        *type++ = *str++;
		if (!(*str))
		        return FALSE;
		if (*str != ' ')
		        *type++ = *str++;
		else
		        str++;
		*type = '\0';
	} else
	        str += 2;

	/* Label 1 */
	if (!(*str) || *str++ != ' ')
	        return FALSE;
	for (i=5; i<=12; i++, str++) {
	        *name1++ = *str;
	        if (!(*str))
		        goto ok_out;
	}
	*name1 = '\0';

	/* Label 2 */
	if (*str == '\0')
	        goto ok_out;
	if (*str++ != ' ')
	        return FALSE;
	if (*str == '\0')
	        goto ok_out;
	if (*str++ != ' ')
	        return FALSE;
	for (i = 15; i <= 22; i++, str++) {
	        *name2++ = *str;
		if (!(*str))
		        return FALSE;
	}
	*name2 = '\0';

	/* Value 1 */
	if (!(*str) || *str++ != ' ' || !(*str) || *str++ != ' ')
	        return FALSE;
	for (i = 25; i <= 36; i++, str++) {
	        *value1++ = *str;
		if (!(*str))
		        goto ok_out;
	}
	*value1 = '\0';

	/* Label 3 */
	if (!(*str))
	        goto ok_out;
	if (*str++ != ' ')
	        return FALSE;
	if (!(*str))
	        goto ok_out;
	if (*str++ != ' ')
	        return FALSE;
	if (!(*str))
	        goto ok_out;
	if (*str++ != ' ')
	        return FALSE;
	for (i = 40; i <= 47; i++, str++) {
	        *name3++ = *str;
		if (!(*str))
		        return FALSE;
	}
	*name3 = '\0';

	/* Value 2 */
	if (!(*str) || *str++ != ' ' || !(*str) || *str++ != ' ')
	        return FALSE;
	for (i = 50; i <= 61; i++, str++) {
	        *value2++ = *str;
		if (!(*str))
		        goto ok_out;
	}
	*value2 = '\0';

 ok_out:
	for (i = 7; i >= 0; i--)
	        if (n1[i] != ' ')
		        break;
	n1[i+1] = '\0';
	for (i = 7; i >= 0; i--)
	        if (n2[i] != ' ')
		        break;
	n2[i+1] = '\0';
	for (i = 7; i >= 0; i--)
	        if (n3[i] != ' ')
		        break;
	n3[i+1] = '\0';

	return TRUE;
}

/************************************************************************
 *
 * Parser.
 */

/*
 * NAME section parsing.  Saves the program name into `ctxt->name'.
 *
 * Returns FALSE on error.
 */
static gboolean
mps_parse_name (MpsInputContext *ctxt)
{
        while (1) {
	        gchar *line;

		if (!mps_get_line (ctxt))
		        return FALSE;

		if (strncmp (ctxt->line, "NAME", 4) == 0
		    && g_ascii_isspace ((ctxt->line[4]))) {
		        line = ctxt->line + 5;
			while (g_ascii_isspace (*line))
			        line++;

			ctxt->name = g_strdup (ctxt->line);
			break;
		} else
		        return FALSE;
	}

	return TRUE;
}


/* Add one ROW definition. */
gboolean
mps_add_row (MpsInputContext *ctxt, MpsRowType type, gchar *txt)
{
        MpsRow *row;
	int    len;

        while (g_ascii_isspace (*txt))
	          txt++;

	row = g_new (MpsRow, 1);
	len = strlen(txt);

	if (len == 0)
	          return FALSE;

	row->name = g_strdup (txt);
	row->type = type;

	if (type == ObjectiveRow)
	          ctxt->objective_row = row;
	else {
		row->index = ctxt->n_rows;
		ctxt->n_rows += 1;

		ctxt->rows = g_slist_prepend (ctxt->rows, row);
	}

	return TRUE;
}

/*
 * ROWS section parsing.  Saves the number of rows into `ctxt->n_rows'.
 * The rows are saved into `ctxt->rows' which is a GSList containing
 * MpsRow elements.  These elements get their `name', `type', and
 * `index' fields set.
 *
 * Returns FALSE on error.
 */
static gboolean
mps_parse_rows (MpsInputContext *ctxt)
{
	gchar  type[3], n1[10], n2[10], n3[10], v1[20], v2[20];
	GSList *tmp;

	while (1) {
	        if (!mps_get_line (ctxt))
		        return FALSE;

		if (strncmp (ctxt->line, "ROWS", 4) == 0)
		        break;
		else
		        return FALSE;
	}

	while (1) {
	        if (!mps_get_line (ctxt))
		        return FALSE;

		if (!mps_parse_data (ctxt->line, type, n1, n2, v1, n3, v2)) {
		        if (ctxt->line[0] != ' ')
			        goto ok_out;
			else
			        return FALSE;
		}

		if (strcmp (type, "E") == 0) {
		        if (!mps_add_row (ctxt, EqualityRow, n1))
			        return FALSE;
		} else if (strcmp (type, "L") == 0) {
		        if (!mps_add_row (ctxt, LessOrEqualRow, n1))
			        return FALSE;
		} else if (strcmp (type, "G") == 0) {
		        if (!mps_add_row (ctxt, GreaterOrEqualRow, n1))
			        return FALSE;
		} else if (strcmp (type, "N") == 0) {
		        if (!mps_add_row (ctxt, ObjectiveRow, n1))
			        return FALSE;
		} else
		        return FALSE;
	}

 ok_out:
	for (tmp = ctxt->rows; tmp != NULL; tmp = tmp->next) {
	        MpsRow *row = (MpsRow *) tmp->data;
		g_hash_table_insert (ctxt->row_hash, row->name, (gpointer) row);
	}
	if (ctxt->objective_row) {
		g_hash_table_insert (ctxt->row_hash, ctxt->objective_row->name,
				     (gpointer) ctxt->objective_row);
		ctxt->objective_row->index = ctxt->n_rows;
	} else {
		g_warning ("Missing objective row.  File is most likely corrupted.");
	}
	ctxt->n_rows += 1;

	return TRUE;
}


/* Add one COLUMN definition. */
static gboolean
mps_add_column (MpsInputContext *ctxt, gchar *row_name, gchar *col_name,
		gchar *value_str)
{
        MpsCol     *col;
	MpsRow     *row;
	MpsColInfo *i;

	row = (MpsRow *) g_hash_table_lookup (ctxt->row_hash, row_name);
	if (row == NULL)
	          return FALSE;

	col        = g_new (MpsCol, 1);
	col->row   = row;
	col->name  = g_strdup (col_name);
	col->value = atof (value_str);
	ctxt->cols = g_slist_prepend (ctxt->cols, col);

	i = (MpsColInfo *) g_hash_table_lookup (ctxt->col_hash, col_name);
	if (i == NULL) {
	          i = g_new (MpsColInfo, 1);
		  i->index = ctxt->n_cols;
		  i->name = strcpy (g_malloc (strlen (col_name) + 1), col_name);
		  ctxt->n_cols += 1;
		  g_hash_table_insert (ctxt->col_hash, col->name, (gpointer) i);
	}

	return TRUE;
}

/*
 * COLUMNS section parsing.  Saves the number of columns into `ctxt->n_cols'.
 * The columns are saved into `ctxt->cols' which is a GSList containing
 * MpsCol elements.  Fields `row', `name' and `value' are set of each element.
 *
 * Keeps track of the column names using `ctxt->col_hash'.
 *
 * Returns FALSE on error.
 */
static gboolean
mps_parse_columns (MpsInputContext *ctxt)
{
	gchar type[3], n1[10], n2[10], n3[10], v1[20], v2[20];

	while (1) {
	        if (strncmp (ctxt->line, "COLUMNS", 7) == 0)
		        break;
		else
		        return FALSE;

		if (!mps_get_line (ctxt))
		        return FALSE;
	}

	while (1) {
	        if (!mps_get_line (ctxt))
		        return FALSE;

		if (!mps_parse_data (ctxt->line, type, n1, n2, v1, n3, v2)) {
		        if (ctxt->line[0] != ' ')
			        return TRUE;
			else
			        return FALSE;
		}

		if (!mps_add_column (ctxt, n2, n1, v1))
		        return FALSE;

		/* Optional second column definition */
		if (*v2)
		        if (!mps_add_column (ctxt, n3, n1, v2))
			        return FALSE;
	}

	return TRUE;
}


/* Add one RHS definition. */
static gboolean
mps_add_rhs (MpsInputContext *ctxt, gchar *rhs_name, gchar *row_name,
	     gchar *value_str)
{
        MpsRhs *rhs;

	rhs       = g_new (MpsRhs, 1);
	rhs->name = g_strdup (rhs_name);
	rhs->row  = (MpsRow *) g_hash_table_lookup (ctxt->row_hash, row_name);
	if (rhs->row == NULL)
	          return FALSE;
	rhs->value = atof (value_str);
	ctxt->rhs  = g_slist_prepend (ctxt->rhs, rhs);

	return TRUE;
}

/*
 * RHS section parsing.  Saves the RHS entries into ctxt->rhs list (GSList).
 * MpsRhs is the type of the elements in the list.  Fields `name', `row',
 * and `value' are stored into each element.
 *
 * Returns FALSE on error.
 */
static gboolean
mps_parse_rhs (MpsInputContext *ctxt)
{
	gchar type[3], rhs_name[10], row_name[10], value[20], n2[10], v2[20];

	if (strncmp (ctxt->line, "RHS", 3) != 0 || ctxt->line[3] != '\0')
	        return FALSE;

	while (1) {
	        if (!mps_get_line (ctxt))
		        return FALSE;

		if (!mps_parse_data (ctxt->line, type, rhs_name, row_name,
				     value, n2, v2)) {
		        if (ctxt->line[0] != ' ')
			        return TRUE;
			else
			        return FALSE;
		}

		if (!mps_add_rhs (ctxt, rhs_name, row_name, value))
		        return FALSE;

		/* Optional second RHS definition */
		if (*v2)
			if (!mps_add_rhs (ctxt, rhs_name, n2, v2))
			        return FALSE;
	}

	return TRUE;
}


/* Add one BOUND definition. */
static gboolean
mps_add_bound (MpsInputContext *ctxt, MpsBoundType type, gchar *bound_name,
	       gchar *col_name, gchar *value_str)
{
        MpsBound   *bound;
	MpsColInfo *info;

	info = (MpsColInfo *) g_hash_table_lookup (ctxt->col_hash, col_name);
	if (info == NULL)
	        return FALSE;  /* Column is not defined */

	bound       = g_new (MpsBound, 1);
	bound->name = g_new (gchar,
			     4 * sizeof (gint) + strlen (bound_name) + 11);
	sprintf(bound->name, "Bound #%d: %s", ctxt->n_bounds + 1, bound_name);
	bound->col_index = info->index;
	bound->value     = atof (value_str);
	bound->type      = type;
	ctxt->bounds     = g_slist_prepend (ctxt->bounds, bound);
	(ctxt->n_bounds)++;

	return TRUE;
}

/*
 * RANGES section parsing.  Ranges are currently not supported.
 *
 * Returns FALSE on error.
 */
static gboolean
mps_parse_ranges (MpsInputContext *ctxt)
{
	/* gchar        type[3], n1[10], n2[10], v1[20], n3[10], v2[20]; */

	if (strncmp (ctxt->line, "ENDATA", 6) == 0)
	        return TRUE;

	if (strncmp (ctxt->line, "RANGES", 6) != 0 || ctxt->line[6] != '\0')
	        return TRUE;

	return FALSE;
}

/*
 * BOUNDS section parsing.  Saves the bounds into `ctxt->bounds' GSList.
 * Each list element is MpsBound, and their `name', `col_index', and `value'
 * fields are stored.
 *
 * Returns FALSE on error.
 */
static gboolean
mps_parse_bounds (MpsInputContext *ctxt)
{
	gchar        type[3], n1[10], n2[10], v1[20], n3[10], v2[20];
	MpsBoundType t;

	if (strncmp (ctxt->line, "ENDATA", 6) == 0)
	        return TRUE;

	if (strncmp (ctxt->line, "BOUNDS", 6) != 0 || ctxt->line[6] != '\0')
	        return FALSE;

	while (1) {
	        if (!mps_get_line (ctxt))
		        return FALSE;

		if (!mps_parse_data (ctxt->line, type, n1, n2, v1, n3, v2)) {
		        if (ctxt->line[0] != ' ')
			        return TRUE;
			else
			        return FALSE;
		}

		if (strncmp (type, "UP", 2) == 0)
			t = UpperBound;
		else if (strncmp (type, "LO", 2) == 0)
			t = LowerBound;
		else if (strncmp (type, "FX", 2) == 0)
			t = FixedVariable;
		else
		        return FALSE; /* Not all bound types are implemented */

		if (!mps_add_bound (ctxt, t, n1, n2, v1))
			return FALSE;
	}
}

/*
 * MPS Parser.
 */
void
mps_parse_file (MpsInputContext *ctxt)
{
        if (!mps_parse_name (ctxt)) {
	        gnumeric_io_error_info_set
			(ctxt->io_context,
			 error_info_new_printf (_("Problem name was not "
						  "defined in the file.")));
	} else if (!mps_parse_rows (ctxt)) {
	        gnumeric_io_error_info_set
			(ctxt->io_context,
			 error_info_new_printf (_("Invalid ROWS section in "
						  "the file.")));
	} else if (!mps_parse_columns (ctxt)) {
	        gnumeric_io_error_info_set
			(ctxt->io_context,
			 error_info_new_printf (_("Invalid COLUMNS section "
						  "in the file.")));
	} else if (!mps_parse_rhs (ctxt)) {
	        gnumeric_io_error_info_set
			(ctxt->io_context,
			 error_info_new_printf (_("Invalid RHS section in the "
						  "file.")));
	} else if (!mps_parse_ranges (ctxt)) {
	        gnumeric_io_error_info_set
			(ctxt->io_context,
			 error_info_new_printf (_("Invalid RANGES section in "
						  "the file.")));
	} else if (!mps_parse_bounds (ctxt)) {
	        gnumeric_io_error_info_set
			(ctxt->io_context,
			 error_info_new_printf (_("Invalid BOUNDS section in "
						  "the file.")));
	}
}
