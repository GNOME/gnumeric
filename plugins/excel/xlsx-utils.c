/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xlsx-utils.c : Utilities shared between xlsx import and export.
 *
 * Copyright (C) 2006-2007 Jody Goldberg (jody@gnome.org)
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
#include "workbook.h"
#include "sheet.h"
#include "func.h"
#include "gnm-format.h"
#include <goffice/goffice.h>
#include <glib-object.h>
#include <string.h>

typedef struct {
	GnmConventions base;
	GHashTable *extern_id_by_wb;
	GHashTable *extern_wb_by_id;
} XLSXExprConventions;

static void
xlsx_add_extern_id (GnmConventionsOut *out, Workbook *wb)
{
	if (wb != out->pp->wb) {
		XLSXExprConventions const *xconv = (XLSXExprConventions const *)out->convs;
		char *id = g_hash_table_lookup (xconv->extern_id_by_wb, wb);
		if (NULL == id) {
			id = g_strdup_printf ("[%u]",
				g_hash_table_size (xconv->extern_id_by_wb));
			g_object_ref (wb);
			g_hash_table_insert (xconv->extern_id_by_wb, wb, id);
		}
		g_string_append (out->accum, id);
	}
}

static Workbook *
xlsx_lookup_external_wb (GnmConventions const *convs,
			 G_GNUC_UNUSED Workbook *ref_wb,
			 char const *name)
{
	XLSXExprConventions const *xconv = (XLSXExprConventions const *)convs;
	if (strcmp (name, "0") == 0)
		return ref_wb;
	if (0) g_printerr ("lookup '%s'\n", name);
	return g_hash_table_lookup (xconv->extern_wb_by_id, name);
}

static void
xlsx_cellref_as_string (GnmConventionsOut *out,
			GnmCellRef const *cell_ref,
			gboolean no_sheetname)
{
	Sheet const *sheet = cell_ref->sheet;

	/* If it is a non-local reference, add the path to the external sheet */
	if (sheet != NULL) {
		xlsx_add_extern_id (out, sheet->workbook);
		g_string_append (out->accum, sheet->name_quoted);
		g_string_append_c (out->accum, '!');
	}
	cellref_as_string (out, cell_ref, TRUE);
}

static void
xlsx_rangeref_as_string (GnmConventionsOut *out, GnmRangeRef const *ref)
{
	if (ref->a.sheet) {
		GnmRangeRef local_ref = *ref;

		xlsx_add_extern_id (out, ref->a.sheet->workbook);

		local_ref.a.sheet = local_ref.b.sheet = NULL;
		g_string_append (out->accum, ref->a.sheet->name_quoted);
		if (ref->b.sheet != NULL && ref->a.sheet != ref->b.sheet) {
			g_string_append_c (out->accum, ':');
			g_string_append (out->accum, ref->b.sheet->name_quoted);
		}
		g_string_append_c (out->accum, '!');

		rangeref_as_string (out, &local_ref);
	} else
		rangeref_as_string (out, ref);
}

Workbook *
xlsx_conventions_add_extern_ref (GnmConventions *convs, char const *path)
{
	XLSXExprConventions *xconv = (XLSXExprConventions *)convs;
	Workbook *res = g_object_new (WORKBOOK_TYPE, NULL);
	(void) go_doc_set_uri (GO_DOC (res), path);
	g_hash_table_insert (xconv->extern_wb_by_id,
		g_strdup_printf ("%d", g_hash_table_size (xconv->extern_wb_by_id) + 1),
		res);
	if (0) g_printerr ("add %d = '%s'\n", g_hash_table_size (xconv->extern_wb_by_id), path);
	return res;
}

GnmConventions *
xlsx_conventions_new (void)
{
	GnmConventions *convs = gnm_conventions_new_full (
		sizeof (XLSXExprConventions));
	XLSXExprConventions *xconv = (XLSXExprConventions *)convs;

	convs->decimal_sep_dot		= TRUE;
	convs->input.range_ref		= rangeref_parse;
	convs->input.external_wb	= xlsx_lookup_external_wb;
	convs->output.cell_ref		= xlsx_cellref_as_string;
	convs->output.range_ref		= xlsx_rangeref_as_string;
	convs->range_sep_colon		= TRUE;
	convs->sheet_name_sep		= '!';
	convs->arg_sep			= ',';
	convs->array_col_sep		= ',';
	convs->array_row_sep		= ';';
	convs->output.translated		= FALSE;
	xconv->extern_id_by_wb = g_hash_table_new_full (g_direct_hash, g_direct_equal,
		(GDestroyNotify) g_object_unref, g_free);
	xconv->extern_wb_by_id = g_hash_table_new_full (g_str_hash, g_str_equal,
		g_free, (GDestroyNotify) g_object_unref);

	return convs;
}

void
xlsx_conventions_free (GnmConventions *convs)
{
	XLSXExprConventions *xconv = (XLSXExprConventions *)convs;
	g_hash_table_destroy (xconv->extern_id_by_wb);
	g_hash_table_destroy (xconv->extern_wb_by_id);
	gnm_conventions_unref (convs);
}

/**
 * xlsx_pivot_date_fmt :
 *
 * Returns : A #GOFormat in the convention used for dates in pivot tables.
 **/
GOFormat *
xlsx_pivot_date_fmt (void)
{
	return go_format_new_from_XL ("yyyy-mm-dd\"T\"hh:mm:ss");
}

