/* vim: set sw=8: */

/*
 * openoffice-read.c : import open/star calc files
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <gnumeric-config.h>
#include <gnumeric.h>

#include <module-plugin-defs.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-merge.h>
#include <ranges.h>
#include <cell.h>
#include <value.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <parse-util.h>
#include <datetime.h>
#include <style-color.h>
#include <sheet-style.h>
#include <mstyle.h>
#include <format.h>
#include <command-context.h>
#include <io-context.h>

#include <gnumeric-i18n.h>
#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-zip.h>

#include <string.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

typedef struct {
	GsfXMLIn base;

	IOContext 	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */

	ParsePos 	pos;

	int 		 col_inc;
	gboolean 	 simple_content;
	gboolean 	 error_content;
	GHashTable	*styles;
	GHashTable	*formats;
	MStyle		*style;
	MStyle		*col_default_styles[SHEET_MAX_COLS];
	GSList		*sheet_order;

	GnmExprConventions *exprconv;
} OOParseState;

static void oo_warning (OOParseState *state, char const *fmt, ...)
	G_GNUC_PRINTF (2, 3);

static void
oo_warning (OOParseState *state, char const *fmt, ...)
{
	char *msg;
	va_list args;

	va_start (args, fmt);
	msg = g_strdup_vprintf (fmt, args);
	va_end (args);

	if (IS_SHEET (state->pos.sheet)) {
		char *tmp;
		if (state->pos.eval.col >= 0 && state->pos.eval.row >= 0)
			tmp = g_strdup_printf ("%s!%s : %s",
				state->pos.sheet->name_quoted,
				cellpos_as_string (&state->pos.eval), msg);
		else
			tmp = g_strdup_printf ("%s : %s",
				state->pos.sheet->name_quoted, msg);
		g_free (msg);
		msg = tmp;
	}

	gnm_io_warning (state->context, msg);
	g_free (msg);
}

static gboolean
oo_attr_bool (OOParseState *state, xmlChar const * const *attrs,
	      int ns_id, char const *name, gboolean *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (&state->base, attrs[0], ns_id, name))
		return FALSE;
	*res = g_ascii_strcasecmp ((gchar *)attrs[1], "false") && strcmp (attrs[1], "0");

	return TRUE;
}

static gboolean
oo_attr_int (OOParseState *state, xmlChar const * const *attrs,
	     int ns_id, char const *name, int *res)
{
	char *end;
	int tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	tmp = strtol ((gchar *)attrs[1], &end, 10);
	if (*end) {
		oo_warning (state, "Invalid attribute '%s', expected integer, received '%s'",
			    name, attrs[1]);
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}

static gboolean
oo_attr_float (OOParseState *state, xmlChar const * const *attrs,
	       int ns_id, char const *name, gnm_float *res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (&state->base, attrs[0], ns_id, name))
		return FALSE;

	tmp = strtognum ((gchar *)attrs[1], &end);
	if (*end) {
		oo_warning (state, "Invalid attribute '%s', expected number, received '%s'",
			    name, attrs[1]);
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}

static StyleColor *
oo_attr_color (OOParseState *state, xmlChar const * const *attrs,
	       int ns_id, char const *name)
{
	guint r, g, b;

	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (attrs[0] != NULL, NULL);
	g_return_val_if_fail (attrs[1] != NULL, NULL);

	if (!gsf_xml_in_namecmp (&state->base, attrs[0], ns_id, name))
		return NULL;
	if (3 == sscanf (attrs[1], "#%2x%2x%2x", &r, &g, &b))
		return style_color_new_i8 (r, g, b);

	oo_warning (state, "Invalid attribute '%s', expected color, received '%s'",
		    name, attrs[1]);
	return NULL;
}

typedef struct {
	char const * const name;
	int val;
} OOEnum;

static gboolean
oo_attr_enum (OOParseState *state, xmlChar const * const *attrs,
	      int ns_id, char const *name, OOEnum const *enums, int *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (&state->base, attrs[0], ns_id, name))
		return FALSE;

	for (; enums->name != NULL ; enums++)
		if (!strcmp (enums->name, attrs[1])) {
			*res = enums->val;
			return TRUE;
		}
	oo_warning (state, "Invalid attribute '%s', unknown enum value '%s'",
		    name, attrs[1]);
	return FALSE;
}

#warning "FIXME: expand this later."
#define oo_expr_parse_str(str, pp, flags, err)	\
	gnm_expr_parse_str (str, pp,		\
		flags, state->exprconv, err)

/****************************************************************************/

enum {
	OO_NS_OFFICE,
	OO_NS_STYLE,
	OO_NS_TEXT,
	OO_NS_TABLE,
	OO_NS_DRAW,
	OO_NS_NUMBER,
	OO_NS_CHART,
	OO_NS_DR3D,
	OO_NS_FORM,
	OO_NS_SCRIPT,
	OO_NS_CONFIG,
	OO_NS_MATH,
	OO_NS_FO,
	OO_NS_XLINK,
	OO_NS_SVG
};

static GsfXMLInNS content_ns[] = {
	GSF_XML_IN_NS (OO_NS_OFFICE,	"http://openoffice.org/2000/office"),
	GSF_XML_IN_NS (OO_NS_STYLE,	"http://openoffice.org/2000/style"),
	GSF_XML_IN_NS (OO_NS_TEXT,	"http://openoffice.org/2000/text"),
	GSF_XML_IN_NS (OO_NS_TABLE,	"http://openoffice.org/2000/table"),
	GSF_XML_IN_NS (OO_NS_DRAW,	"http://openoffice.org/2000/drawing"),
	GSF_XML_IN_NS (OO_NS_NUMBER,	"http://openoffice.org/2000/datastyle"),
	GSF_XML_IN_NS (OO_NS_CHART,	"http://openoffice.org/2000/chart"),
	GSF_XML_IN_NS (OO_NS_DR3D,	"http://openoffice.org/2000/dr3d"),
	GSF_XML_IN_NS (OO_NS_FORM,	"http://openoffice.org/2000/form"),
	GSF_XML_IN_NS (OO_NS_SCRIPT,	"http://openoffice.org/2000/script"),
	GSF_XML_IN_NS (OO_NS_CONFIG,	"http://openoffice.org/2001/config"),
	GSF_XML_IN_NS (OO_NS_MATH,	"http://www.w3.org/1998/Math/MathML"),
	GSF_XML_IN_NS (OO_NS_FO,	"http://www.w3.org/1999/XSL/Format"),
	GSF_XML_IN_NS (OO_NS_XLINK,	"http://www.w3.org/1999/xlink"),
	GSF_XML_IN_NS (OO_NS_SVG,	"http://www.w3.org/2000/svg"),
	{ NULL }
};

static void
oo_date_convention (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* <table:null-date table:date-value="1904-01-01"/> */
	OOParseState *state = (OOParseState *)xin;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "date-value")) {
			if (!strncmp (attrs[1], "1904", 4))
				workbook_set_1904 (state->pos.wb, TRUE);
		}
}

static void
oo_table_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* <table:table table:name="Result" table:style-name="ta1"> */
	OOParseState *state = (OOParseState *)xin;
	int i;

	state->pos.eval.col = -1;
	state->pos.eval.row = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "name")) {
			state->pos.sheet = workbook_sheet_by_name (state->pos.wb, attrs[1]);
			if (NULL == state->pos.sheet) {
				state->pos.sheet = sheet_new (state->pos.wb, attrs[1]);
				workbook_sheet_attach (state->pos.wb, state->pos.sheet, NULL);
			}

			/* store a list of the sheets in the correct order */
			state->sheet_order = g_slist_prepend (state->sheet_order,
							      state->pos.sheet);
		}
	for (i = SHEET_MAX_COLS ; i-- > 0 ; )
		state->col_default_styles[i] = NULL;
}

static void
oo_col_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin;
	MStyle *style = NULL;
	int repeat_count = 1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "default-cell-style-name"))
			style = g_hash_table_lookup (state->styles, attrs[1]);
		else if (oo_attr_int (state, attrs, OO_NS_TABLE, "number-columns-repeated", &repeat_count))
			;

	while (repeat_count-- > 0)
		state->col_default_styles[state->pos.eval.col++] = style;
}

static void
oo_row_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin;
	int      repeat_count;
	gboolean repeat_flag = FALSE;

	state->pos.eval.row++;
	state->pos.eval.col = 0;

	g_return_if_fail (state->pos.eval.row < SHEET_MAX_ROWS);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (oo_attr_int (state, attrs, OO_NS_TABLE, "number-rows-repeated", &repeat_count))
			repeat_flag = TRUE;
	}
	if (repeat_flag)
		state->pos.eval.row += repeat_count -1;
}

static char const *
oo_cellref_parse (CellRef *ref, char const *start, ParsePos const *pp)
{
	char const *tmp1, *tmp2, *ptr = start;
	/* sheet name cannot contain '.' */
	if (*ptr != '.') {
		char *name;
		if (*ptr == '$') /* ignore abs vs rel sheet name */
			ptr++;
		tmp1 = strchr (ptr, '.');
		if (tmp1 == NULL)
			return start;
		name = g_alloca (tmp1-ptr+1);
		strncpy (name, ptr, tmp1-ptr);
		name[tmp1-ptr] = 0;
		ptr = tmp1+1;

		/* OpenCalc does not pre-declare its sheets, but it does have a
		 * nice unambiguous format.  So if we find a name that has not
		 * been added yet add it.  Reorder below.
		 */
		ref->sheet = workbook_sheet_by_name (pp->wb, name);
		if (ref->sheet == NULL) {
			ref->sheet = sheet_new (pp->wb, name);
			workbook_sheet_attach (pp->wb, ref->sheet, NULL);
		}
	} else {
		ptr++; /* local ref */
		ref->sheet = NULL;
	}

	tmp1 = col_parse (ptr, &ref->col, &ref->col_relative);
	if (!tmp1)
		return start;
	tmp2 = row_parse (tmp1, &ref->row, &ref->row_relative);
	if (!tmp2)
		return start;

	if (ref->col_relative)
		ref->col -= pp->eval.col;
	if (ref->row_relative)
		ref->row -= pp->eval.row;
	return tmp2;
}

static char const *
oo_rangeref_parse (RangeRef *ref, char const *start, ParsePos const *pp)
{
	char const *ptr;

	g_return_val_if_fail (start != NULL, start);
	g_return_val_if_fail (pp != NULL, start);

	if (*start != '[')
		return start;
	ptr = oo_cellref_parse (&ref->a, start+1, pp);
	if (*ptr == ':')
		ptr = oo_cellref_parse (&ref->b, ptr+1, pp);
	else
		ref->b = ref->a;

	if (*ptr == ']')
		return ptr + 1;
	return start;
}

static void
oo_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin;
	GnmExpr const	*expr = NULL;
	Value		*val = NULL;
	gboolean	 bool_val;
	gnm_float	 float_val;
	int array_cols = -1, array_rows = -1;
	int merge_cols = -1, merge_rows = -1;
	MStyle *style = NULL;

	state->col_inc = 1;
	state->error_content = FALSE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (oo_attr_int (state, attrs, OO_NS_TABLE, "number-columns-repeated", &state->col_inc))
			;
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "formula")) {
			char const *expr_string;

			if (attrs[1] == NULL) {
				oo_warning (state, _("Missing expression"));
				continue;
			}
			expr_string = gnm_expr_char_start_p (attrs[1]);
			if (expr_string == NULL)
				oo_warning (state, _("Expression '%s' does not start with a recognized character"), attrs[1]);
			else if (*expr_string == '\0')
				/* Ick.  They seem to store error cells as
				 * having value date with expr : '=' and the
				 * message in the content.
				 */
				state->error_content = TRUE;
			else {
				ParseError  perr;
				parse_error_init (&perr);
				expr = oo_expr_parse_str (expr_string,
					&state->pos, 0, &perr);
				if (expr == NULL) {
					oo_warning (state, _("Unable to parse '%s' because '%s'"),
						    attrs[1], perr.err->message);
					parse_error_free (&perr);
				}
			}
		} else if (oo_attr_bool (state, attrs, OO_NS_TABLE, "boolean-value", &bool_val))
			val = value_new_bool (bool_val);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "date-value")) {
			unsigned y, m, d;
			if (3 == sscanf (attrs[1], "%u-%u-%u", &y, &m, &d)) {
				GDate date;
				g_date_set_dmy (&date, d, m, y);
				if (g_date_valid (&date))
					val = value_new_int (datetime_g_to_serial (&date,
										   workbook_date_conv (state->pos.wb)));
			}
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "string-value"))
			val = value_new_string (attrs[1]);
		else if (oo_attr_float (state, attrs, OO_NS_TABLE, "value", &float_val))
			val = value_new_float (float_val);
		else if (oo_attr_int (state, attrs, OO_NS_TABLE, "number-matrix-columns-spanned", &array_cols))
			;
		else if (oo_attr_int (state, attrs, OO_NS_TABLE, "number-matrix-rows-spanned", &array_rows))
			;
		else if (oo_attr_int (state, attrs, OO_NS_TABLE, "number-columns-spanned", &merge_cols))
			;
		else if (oo_attr_int (state, attrs, OO_NS_TABLE, "number-rows-spanned", &merge_rows))
			;
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "style-name")) {
			style = g_hash_table_lookup (state->styles, attrs[1]);
		}
	}

	if (style == NULL)
		style = state->col_default_styles[state->pos.eval.col];
	if (style != NULL) {
		mstyle_ref (style);
		if (state->col_inc > 1) {
			Range tmp;
			range_init (&tmp,
				state->pos.eval.col, state->pos.eval.row,
				state->pos.eval.col + state->col_inc - 1,
				state->pos.eval.row);
			sheet_style_set_range (state->pos.sheet, &tmp, style);
		} else
			sheet_style_set_pos (state->pos.sheet,
				state->pos.eval.col, state->pos.eval.row,
				style);
	}
	state->simple_content = FALSE;
	if (expr != NULL) {
		Cell *cell = sheet_cell_fetch (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		if (array_cols > 0 || array_rows > 0) {
			if (array_cols < 0) {
				array_cols = 1;
				oo_warning (state, _("Invalid array expression does no specify number of columns"));
			} else if (array_rows < 0) {
				array_rows = 1;
				oo_warning (state, _("Invalid array expression does no specify number of rows"));
			}
			cell_set_array_formula (state->pos.sheet,
				state->pos.eval.col, state->pos.eval.row,
				state->pos.eval.col + array_cols-1,
				state->pos.eval.row + array_rows-1,
				expr);
			if (val != NULL)
				cell_assign_value (cell, val);
		} else {
			if (val != NULL)
				cell_set_expr_and_value (cell, expr, val, TRUE);
			else
				cell_set_expr (cell, expr);
			gnm_expr_unref (expr);
		}
	} else if (val != NULL) {
		Cell *cell = sheet_cell_fetch (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		/* has cell previously been initialized as part of an array */
		if (cell_is_partial_array (cell))
			cell_assign_value (cell, val);
		else
			cell_set_value (cell, val);
	} else if (!state->error_content)
		/* store the content as a string */
		state->simple_content = TRUE;

	if (merge_cols > 0 && merge_rows > 0) {
		Range r;
		range_init (&r,
			state->pos.eval.col, state->pos.eval.row,
			state->pos.eval.col + merge_cols - 1,
			state->pos.eval.row + merge_rows - 1);
		sheet_merge_add (state->pos.sheet, &r, FALSE,
				 NULL);

	}
}

static void
oo_cell_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin;

	if (state->col_inc > 1) {
		Cell *cell = sheet_cell_get (state->pos.sheet, state->pos.eval.col, state->pos.eval.row);

		if (!cell_is_blank (cell)) {
			int i = 1;
			Cell *next;
			for (; i < state->col_inc ; i++) {
				next = sheet_cell_fetch (state->pos.sheet,
					state->pos.eval.col + i, state->pos.eval.row);
				cell_set_value (next, value_duplicate (cell->value));
			}
		}
	}

	state->pos.eval.col += state->col_inc;
}

static void
oo_covered_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin;

	state->col_inc = 1;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int (state, attrs, OO_NS_TABLE, "number-columns-repeated", &state->col_inc))
			;
#if 0
		/* why bother it is covered ? */
		else if (!strcmp (attrs[0], OO_NS_TABLE, "style-name"))
			style = g_hash_table_lookup (state->styles, attrs[1]);

	if (style == NULL)
		style = state->col_default_styles[state->pos.eval.col];
	if (style != NULL) {
		mstyle_ref (style);
		sheet_style_set_pos (state->pos.sheet,
		     state->pos.eval.col, state->pos.eval.row,
		     style);
	}
#endif
}

static void
oo_covered_cell_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin;
	state->pos.eval.col += state->col_inc;
}

static void
oo_cell_content_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin;

	if (state->simple_content || state->error_content) {
		Cell *cell = sheet_cell_fetch (state->pos.sheet, state->pos.eval.col, state->pos.eval.row);
		Value *v;

		if (state->simple_content)
			v = value_new_string (state->base.content->str);
		else
			v = value_new_error (NULL, state->base.content->str);
		cell_set_value (cell, v);
	}
}
static void
oo_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin;
	xmlChar const *name = NULL;
	MStyle *parent = NULL;
	StyleFormat *fmt = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		/* ignore  style:family the names seem unique enough */
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "name"))
			name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "parent-style-name")) {
			MStyle *tmp = g_hash_table_lookup (state->styles, attrs[1]);
			if (tmp != NULL)
				parent = tmp;
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "data-style-name")) {
			StyleFormat *tmp = g_hash_table_lookup (state->formats, attrs[1]);
			if (tmp != NULL)
				fmt = tmp;
		}

	if (name != NULL) {
		state->style = (parent != NULL)
			? mstyle_copy (parent) : mstyle_new_default ();

		if (fmt != NULL)
			mstyle_set_format (state->style, fmt);

		g_hash_table_replace (state->styles,
			g_strdup (name), state->style);
	}
}

static void
oo_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin;
	state->style = NULL;
}

static void
oo_style_prop (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const h_alignments [] = {
		{ "start",	HALIGN_LEFT },
		{ "center",	HALIGN_CENTER },
		{ "end", 	HALIGN_RIGHT },
		{ "justify",	HALIGN_JUSTIFY },
		{ NULL,	0 },
	};
	static OOEnum const v_alignments [] = {
		{ "bottom", 	VALIGN_BOTTOM },
		{ "top",	VALIGN_TOP },
		{ "middle",	VALIGN_CENTER },
		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin;
	StyleColor *color;
	MStyle *style = state->style;
	StyleHAlignFlags h_align = HALIGN_GENERAL;
	gboolean h_align_is_valid = FALSE;
	int tmp;

	g_return_if_fail (style != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if ((color = oo_attr_color (state, attrs, OO_NS_FO, "background-color"))) {
			mstyle_set_color (style, MSTYLE_COLOR_BACK, color);
			mstyle_set_pattern (style, 1);
		} else if ((color = oo_attr_color (state, attrs, OO_NS_FO, "color")))
			mstyle_set_color (style, MSTYLE_COLOR_FORE, color);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "cell-protect"))
			mstyle_set_content_locked (style, !strcmp (attrs[1], "protected"));
		else if (oo_attr_enum (state, attrs, OO_NS_STYLE, "text-align", h_alignments, &tmp))
			h_align = tmp;
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "text-align-source"))
			h_align_is_valid = !strcmp (attrs[1], "fixed");
		else if (oo_attr_enum (state, attrs, OO_NS_FO, "vertical-align", v_alignments, &tmp))
			mstyle_set_align_v (style, tmp);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "wrap-option"))
			mstyle_set_wrap_text (style, !strcmp (attrs[1], "wrap"));
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "font-name"))
			mstyle_set_font_name (style, attrs[1]);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "font-size")) {
			float size;
			if (1 == sscanf (attrs[1], "%fpt", &size))
				mstyle_set_font_size (style, size);
		}
#if 0
		else if (!strcmp (attrs[0], OO_NS_FO, "font-weight")) {
				mstyle_set_font_bold (style, TRUE);
				mstyle_set_font_uline (style, TRUE);
			="normal"
		} else if (!strcmp (attrs[0], OO_NS_FO, "font-style" )) {
			="italic"
				mstyle_set_font_italic (style, TRUE);
		} else if (!strcmp (attrs[0], OO_NS_STYLE, "text-underline" )) {
			="italic"
				mstyle_set_font_italic (style, TRUE);
		}
#endif

	mstyle_set_align_h (style, h_align_is_valid ? h_align : HALIGN_GENERAL);
}
		       
static void
oo_named_expr (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin;
	xmlChar const *name     = NULL;
	xmlChar const *base_str  = NULL;
	xmlChar const *expr_str = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "name"))
			name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "base-cell-address"))
			base_str = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "expression"))
			expr_str = attrs[1];

	if (name != NULL && base_str != NULL && expr_str != NULL) {
		ParseError perr;
		ParsePos   pp;
		GnmExpr const *expr;
		char *tmp = g_strconcat ("[", base_str, "]", NULL);

		parse_error_init (&perr);
		parse_pos_init (&pp, state->pos.wb, NULL, 0, 0);

		expr = oo_expr_parse_str (tmp, &pp,
			GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
			&perr);
		g_free (tmp);

		if (expr == NULL || expr->any.oper != GNM_EXPR_OP_CELLREF) {
			oo_warning (state, _("Unable to parse position for expression '%s' @ '%s' because '%s'"),
				    name, base_str, perr.err->message);
			parse_error_free (&perr);
			if (expr != NULL)
				gnm_expr_unref (expr);
		} else {
			CellRef const *ref = &expr->cellref.ref;
			parse_pos_init (&pp, state->pos.wb, ref->sheet,
					ref->col, ref->row);

			gnm_expr_unref (expr);
			expr = oo_expr_parse_str (expr_str, &pp, 0, &perr);
			if (expr == NULL) {
				oo_warning (state, _("Unable to parse position for expression '%s' with value '%s' because '%s'"),
					    name, expr_str, perr.err->message);
				parse_error_free (&perr);
			} else {
				pp.sheet = NULL;
				expr_name_add (&pp, name, expr, NULL, TRUE);
			}
		}
	}
}

static GsfXMLInNode opencalc_content_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, FALSE, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE (START, OFFICE, OO_NS_OFFICE, "document-content", FALSE, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, SCRIPT, OO_NS_OFFICE, "script", FALSE, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, OFFICE_FONTS, OO_NS_OFFICE, "font-decls", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_FONTS, FONT_DECL, OO_NS_STYLE, "font-decl", FALSE, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, OFFICE_STYLES, OO_NS_OFFICE, "automatic-styles", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE, OO_NS_STYLE, "style", FALSE, &oo_style, &oo_style_end),
      GSF_XML_IN_NODE (STYLE, STYLE_PROP, OO_NS_STYLE, "properties", FALSE, &oo_style_prop, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_PROP, OO_NS_NUMBER, "number", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, OO_NS_NUMBER, "fraction", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, OO_NS_NUMBER, "scientific-number", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, DATE_STYLE, OO_NS_NUMBER, "date-style", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_QUARTER, OO_NS_NUMBER, "quarter", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_YEAR, OO_NS_NUMBER, "year", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MONTH, OO_NS_NUMBER, "month", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY, OO_NS_NUMBER, "day", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY_OF_WEEK, OO_NS_NUMBER, "day-of-week", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_WEEK_OF_YEAR, OO_NS_NUMBER, "week-of-year", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_HOURS, OO_NS_NUMBER, "hours", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MINUTES, OO_NS_NUMBER, "minutes", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_SECONDS, OO_NS_NUMBER, "seconds", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER, "text", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_BOOL, OO_NS_NUMBER, "boolean-style", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_BOOL, BOOL_PROP, OO_NS_NUMBER, "boolean", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_CURRENCY, OO_NS_NUMBER, "currency-style", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE, OO_NS_NUMBER, "number", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE_PROP, OO_NS_STYLE, "properties", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_MAP, OO_NS_STYLE, "map", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_SYMBOL, OO_NS_NUMBER, "currency-symbol", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT, OO_NS_NUMBER, "text", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER, "percentage-style", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_STYLE_PROP, OO_NS_NUMBER, "number", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT, OO_NS_NUMBER, "text", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_TEXT, OO_NS_NUMBER, "text-style", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_PROP, OO_NS_NUMBER, "text-content", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_TIME, OO_NS_NUMBER, "time-style", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TIME, TIME_HOURS, OO_NS_NUMBER, "hours", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TIME, TIME_MINUTES, OO_NS_NUMBER, "minutes", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TIME, TIME_SECONDS, OO_NS_NUMBER, "seconds", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TIME, TIME_AM_PM, OO_NS_NUMBER, "am-pm", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TIME, TIME_TEXT, OO_NS_NUMBER, "text", FALSE, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE, OFFICE_BODY, OO_NS_OFFICE, "body", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_BODY, TABLE_CALC_SETTINGS, OO_NS_TABLE, "calculation-settings", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (TABLE_CALC_SETTINGS, DATE_CONVENTION, OO_NS_TABLE, "null-date", FALSE, oo_date_convention, NULL),

    GSF_XML_IN_NODE (OFFICE_BODY, TABLE, OO_NS_TABLE, "table", FALSE, &oo_table_start, NULL),
      GSF_XML_IN_NODE (TABLE, TABLE_COL, OO_NS_TABLE, "table-column", FALSE, &oo_col_start, NULL),
      GSF_XML_IN_NODE (TABLE, TABLE_ROW, OO_NS_TABLE, "table-row", FALSE, &oo_row_start, NULL),
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_CELL, OO_NS_TABLE, "table-cell", FALSE, &oo_cell_start, &oo_cell_end),
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_COVERED_CELL, OO_NS_TABLE, "covered-table-cell", FALSE, &oo_covered_cell_start, &oo_covered_cell_end),
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_TEXT, OO_NS_TEXT, "p", TRUE, NULL, &oo_cell_content_end),
	    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_S, OO_NS_TEXT, "s", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (TABLE, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL, OO_NS_TABLE, "table-column", FALSE, NULL, NULL), /* 2nd def */
      GSF_XML_IN_NODE (TABLE, TABLE_ROW_GROUP,	      OO_NS_TABLE, "table-row-group", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW_GROUP, OO_NS_TABLE, "table-row-group", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW,	    OO_NS_TABLE, "table-row", FALSE, NULL, NULL), /* 2nd def */
    GSF_XML_IN_NODE (OFFICE_BODY, NAMED_EXPRS, OO_NS_TABLE, "named-expressions", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_EXPR, OO_NS_TABLE, "named-expression", FALSE, &oo_named_expr, NULL),
  { NULL }
};

/****************************************************************************/

typedef GValue		OOConfigItem;
typedef GHashTable	OOConfigItemSet;
typedef GHashTable	OOConfigItemMapNamed;
typedef GPtrArray	OOConfigItemMapIndexed;

#if 0
static GHashTable *
oo_config_item_set ()
{
	return NULL;
}
#endif

static GsfXMLInNode opencalc_settings_dtd [] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, FALSE, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE (START, OFFICE, OO_NS_OFFICE, "document-settings", FALSE, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, SETTINGS, OO_NS_OFFICE, "settings", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (SETTINGS, CONFIG_ITEM_SET, OO_NS_CONFIG, "config-item-set", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (CONFIG_ITEM_SET, CONFIG_ITEM, OO_NS_CONFIG, "config-item", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (CONFIG_ITEM_SET, CONFIG_ITEM_MAP_NAMED, OO_NS_CONFIG, "config-item-map-named", FALSE, NULL, NULL),
	GSF_XML_IN_NODE (CONFIG_ITEM_MAP_NAMED, CONFIG_ITEM_MAP_ENTRY, OO_NS_CONFIG, "config-item-map-entry", FALSE, NULL, NULL),
	  GSF_XML_IN_NODE (CONFIG_ITEM_MAP_ENTRY, CONFIG_ITEM, OO_NS_CONFIG, "config-item", FALSE, NULL, NULL),				/* 2nd def */
	  GSF_XML_IN_NODE (CONFIG_ITEM_MAP_ENTRY, CONFIG_ITEM_MAP_NAMED, OO_NS_CONFIG, "config-item-map-named", FALSE, NULL, NULL),	/* 2nd def */
	  GSF_XML_IN_NODE (CONFIG_ITEM_MAP_ENTRY, CONFIG_ITEM_MAP_INDEXED, OO_NS_CONFIG, "config-item-map-indexed", FALSE, NULL, NULL),	/* 2nd def */
      GSF_XML_IN_NODE (CONFIG_ITEM_SET, CONFIG_ITEM_MAP_INDEXED, OO_NS_CONFIG, "config-item-map-indexed", FALSE, NULL, NULL),
	GSF_XML_IN_NODE (CONFIG_ITEM_MAP_INDEXED, CONFIG_ITEM_MAP_ENTRY, OO_NS_CONFIG, "config-item-map-entry", FALSE, NULL, NULL),	/* 2nd def */
  { NULL }
};

/****************************************************************************/

static GnmExpr const *
function_renamer (char const *name,
		  GnmExprList *args,
		  GnmExprConventions *convs)
{
	GnmFunc *f = gnm_func_lookup ("ERROR.TYPE", NULL);
	if (f != NULL)
		return gnm_expr_new_funcall (f, args);
	return gnm_func_placeholder_factory (name, args, convs);
}

static GnmExprConventions *
oo_conventions (void)
{
	GnmExprConventions *res = gnm_expr_conventions_new ();

	res->decode_ampersands = TRUE;
	res->decimal_sep_dot = TRUE;
	res->argument_sep_semicolon = TRUE;
	res->array_col_sep_comma = TRUE;
	res->output_argument_sep = ";";
	res->output_array_col_sep = ",";
	res->unknown_function_handler = gnm_func_placeholder_factory;
	res->ref_parser = oo_rangeref_parse;

	res->function_rewriter_hash =
		g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (res->function_rewriter_hash,
			     (gchar *)"ERRORTYPE",
			     function_renamer);
	return res;
}

static GsfXMLInDoc *content_doc, *settings_doc;

void
openoffice_file_open (GnmFileOpener const *fo, IOContext *io_context,
		      WorkbookView *wb_view, GsfInput *input);
void
openoffice_file_open (GnmFileOpener const *fo, IOContext *io_context,
		      WorkbookView *wb_view, GsfInput *input)
{
	OOParseState state;
	GsfInput *content = NULL;
	GError   *err = NULL;
	GsfInfileZip *zip;
	int i;

	g_return_if_fail (IS_WORKBOOK_VIEW (wb_view));
	g_return_if_fail (GSF_IS_INPUT (input));

	zip = gsf_infile_zip_new (input, &err);
	if (zip == NULL) {
		g_return_if_fail (err != NULL);
		gnumeric_error_read (COMMAND_CONTEXT (io_context),
			err->message);
		g_error_free (err);
		return;
	}

	content = gsf_infile_child_by_name (GSF_INFILE (zip),
					    "content.xml");
	if (content == NULL) {
		gnumeric_error_read (COMMAND_CONTEXT (io_context),
			 _("No stream named content.xml found."));
		g_object_unref (G_OBJECT (zip));
		return;
	}

	/* init */
	state.context = io_context;
	state.wb_view = wb_view;
	state.pos.wb	= wb_view_workbook (wb_view);
	state.pos.sheet = NULL;
	state.pos.eval.col	= -1;
	state.pos.eval.row	= -1;
	state.styles = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) mstyle_unref);
	state.formats = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) style_format_unref);
	state.style 	  = NULL;
	state.sheet_order = NULL;
	state.exprconv = oo_conventions ();

	state.base.doc = content_doc;
	if (gsf_xml_in_parse (&state.base, content)) {
		GsfInput *settings;

		/* get the sheet in the right order (in case something was
		 * created out of order implictly */
		state.sheet_order = g_slist_reverse (state.sheet_order);
		workbook_sheet_reorder (state.pos.wb, state.sheet_order, NULL);
		g_slist_free (state.sheet_order);

		/* look for the view settings */
		settings = gsf_infile_child_by_name (GSF_INFILE (zip), "settings.xml");
		if (settings != NULL) {
			state.base.doc = settings_doc;
			gsf_xml_in_parse (&state.base, settings);
			g_object_unref (G_OBJECT (settings));
		}
	} else
		gnumeric_io_error_string (io_context, _("XML document not well formed!"));
	g_hash_table_destroy (state.styles);
	g_object_unref (G_OBJECT (content));

	g_object_unref (G_OBJECT (zip));

	i = workbook_sheet_count (state.pos.wb);
	while (i-- > 0)
		sheet_flag_recompute_spans (workbook_sheet_by_index (state.pos.wb, i));

	gnm_expr_conventions_free (state.exprconv);
}

void
plugin_init (void)
{
	content_doc  = gsf_xml_in_doc_new (opencalc_content_dtd, content_ns);
	settings_doc = gsf_xml_in_doc_new (opencalc_settings_dtd, content_ns);
}
void
plugin_cleanup (void)
{
	gsf_xml_in_doc_free (content_doc);
	gsf_xml_in_doc_free (settings_doc);
}
