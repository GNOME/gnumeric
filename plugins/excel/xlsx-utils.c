/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xlsx-utils.c : Utilities shared between xlsx import and export.
 *
 * Copyright (C) 2006 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

/*****************************************************************************/

#include <gnumeric-config.h>
#include <gnumeric.h>

#include "xlsx-utils.h"

#include "parse-util.h"
#include "position.h"
#include "sheet.h"
#include "func.h"
#include <glib-object.h>

typedef struct {
	GnmExprConventions base;
	GHashTable *extern_ids;
} XLSXExprConventions;

static char const *
xlsx_extern_id (GnmExprConventions const *conv, Workbook *wb)
{
	XLSXExprConventions const *xconv = (XLSXExprConventions const *)conv;
	char const *res = g_hash_table_lookup (xconv->extern_ids, wb);
	if ( NULL == res) {
		char *id = g_strdup_printf ("[%u]",
			g_hash_table_size (xconv->extern_ids));
		g_object_ref (wb);
		g_hash_table_insert (xconv->extern_ids, wb, id);
	}
	return res;
}

static void
xlsx_cellref_as_string (GString *target, GnmExprConventions const *conv,
			GnmCellRef const *cell_ref,
			GnmParsePos const *pp, gboolean no_sheetname)
{
	Sheet const *sheet = cell_ref->sheet;

	/* If it is a non-local reference, add the path to the external sheet */
	if (sheet != NULL) {
		if (pp->wb != NULL && sheet->workbook != pp->wb)
			g_string_append (target, xlsx_extern_id (conv, sheet->workbook));
		g_string_append (target, sheet->name_quoted);
		g_string_append_c (target, '!');
	}
	cellref_as_string (target, conv, cell_ref, pp, TRUE);
}

static void
xlsx_rangeref_as_string (GString *target, GnmExprConventions const *conv,
			 GnmRangeRef const *ref, GnmParsePos const *pp)
{
	GnmRangeRef tmp = *ref;

	if (ref->a.sheet) {
		tmp.a.sheet = tmp.b.sheet = NULL;
		if (pp->wb != NULL && ref->a.sheet->workbook != pp->wb)
			g_string_append (target, xlsx_extern_id (conv, ref->a.sheet->workbook));
		g_string_append (target, ref->a.sheet->name_quoted);
		if (ref->b.sheet != NULL && ref->a.sheet != ref->b.sheet) {
			g_string_append_c (target, ':');
			g_string_append (target, ref->b.sheet->name_quoted);
		}
		g_string_append_c (target, '!');
	}
	rangeref_as_string (target, conv, &tmp, pp);
}

GnmExprConventions *
xlsx_expr_conv_new ()
{
	GnmExprConventions *conv = gnm_expr_conventions_new_full (
		sizeof (XLSXExprConventions));
	XLSXExprConventions *xconv = (XLSXExprConventions *)conv;

	conv->decimal_sep_dot		= TRUE;
	conv->input.range_ref		= rangeref_parse;
	conv->output.cell_ref		= xlsx_cellref_as_string;
	conv->output.range_ref		= xlsx_rangeref_as_string;
	conv->range_sep_colon		= TRUE;
	conv->sheet_name_sep		= '!';
	conv->arg_sep			= ',';
	conv->array_col_sep		= ',';
	conv->array_row_sep		= ';';
	conv->output.translated		= FALSE;
	xconv->extern_ids = g_hash_table_new_full (g_direct_hash, g_direct_equal,
		(GDestroyNotify) g_object_unref, g_free);

	return conv;
}

void
xlsx_expr_conv_free (GnmExprConventions *conv)
{
	XLSXExprConventions *xconv = (XLSXExprConventions *)conv;
	g_hash_table_destroy (xconv->extern_ids);
	gnm_expr_conventions_free (conv);
}
