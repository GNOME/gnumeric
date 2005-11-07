/* vim: set sw=8: */

/*
 * openoffice-read.c : import open/star calc files
 *
 * Copyright (C) 2002-2004 Jody Goldberg (jody@gnome.org)
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

#include <gnumeric-config.h>
#include <gnumeric.h>

#include <gnm-plugin.h>
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
#include <style-color.h>
#include <sheet-style.h>
#include <mstyle.h>
#include <gnm-format.h>
#include <command-context.h>
#include <goffice/app/io-context.h>
#include <goffice/utils/go-units.h>
#include <goffice/utils/datetime.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-zip.h>
#include <gsf/gsf-opendoc-utils.h>
#include <glib/gi18n.h>

#include <string.h>
#include <locale.h>

GNM_PLUGIN_MODULE_HEADER;

typedef enum {
	OO_STYLE_UNKNOWN,
	OO_STYLE_CELL,
	OO_STYLE_COL,
	OO_STYLE_ROW,
	OO_STYLE_SHEET,
	OO_STYLE_GRAPHICS,
	OO_STYLE_PARAGRAPH,
	OO_STYLE_TEXT
} OOStyleType;

typedef struct {
	IOContext 	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */

	GnmParsePos 	pos;

	int 		 col_inc;
	gboolean 	 simple_content;
	gboolean 	 error_content;
	GHashTable	*cell_styles;
	GHashTable	*col_row_styles;
	GHashTable	*formats;

	union {
		GnmStyle *cell;
		double	 *col_row;
	} cur_style;
	GnmStyle 	*default_style_cell;
	OOStyleType	 cur_style_type;
	GnmStyle	*col_default_styles[SHEET_MAX_COLS];
	GSList		*sheet_order;
	int	 	 richtext_len;
	GString		*accum_fmt;
	char		*fmt_name;

	GnmExprConventions *exprconv;
} OOParseState;

static gboolean oo_warning (GsfXMLIn *xin, char const *fmt, ...)
	G_GNUC_PRINTF (2, 3);

static gboolean
oo_warning (GsfXMLIn *xin, char const *fmt, ...)
{
	OOParseState *state = (OOParseState *)xin->user_state;
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

	gnm_io_warning (state->context, "%s", msg);
	g_free (msg);

	return FALSE; /* convenience */
}

static gboolean
oo_attr_bool (GsfXMLIn *xin, xmlChar const * const *attrs,
	      int ns_id, char const *name, gboolean *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return FALSE;
	*res = g_ascii_strcasecmp ((gchar *)attrs[1], "false") && strcmp (attrs[1], "0");

	return TRUE;
}

static gboolean
oo_attr_int (GsfXMLIn *xin, xmlChar const * const *attrs,
	     int ns_id, char const *name, int *res)
{
	char *end;
	int tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return FALSE;

	tmp = strtol ((gchar *)attrs[1], &end, 10);
	if (*end)
		return oo_warning (xin, "Invalid attribute '%s', expected integer, received '%s'",
				   name, attrs[1]);

	*res = tmp;
	return TRUE;
}

static gboolean
oo_attr_float (GsfXMLIn *xin, xmlChar const * const *attrs,
	       int ns_id, char const *name, gnm_float *res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return FALSE;

	tmp = gnm_strto ((gchar *)attrs[1], &end);
	if (*end)
		return oo_warning (xin, "Invalid attribute '%s', expected number, received '%s'",
				   name, attrs[1]);
	*res = tmp;
	return TRUE;
}


static GnmColor *
oo_parse_color (GsfXMLIn *xin, xmlChar const *str, char const *name)
{
	guint r, g, b;

	g_return_val_if_fail (str != NULL, NULL);

	if (3 == sscanf (str, "#%2x%2x%2x", &r, &g, &b))
		return style_color_new_i8 (r, g, b);

	oo_warning (xin, "Invalid attribute '%s', expected color, received '%s'",
		    name, str);
	return NULL;
}
static GnmColor *
oo_attr_color (GsfXMLIn *xin, xmlChar const * const *attrs,
	       int ns_id, char const *name)
{
	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (attrs[0] != NULL, NULL);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return NULL;
	return oo_parse_color (xin, attrs[1], name);
}

/* returns pts */
static char const *
oo_parse_distance (GsfXMLIn *xin, xmlChar const *str,
		  char const *name, double *pts)
{
	double num;
	char *end = NULL;

	g_return_val_if_fail (str != NULL, NULL);

	num = strtod (str, &end);
	if (str != (xmlChar const *)end) {
		if (0 == strncmp (end, "mm", 2)) {
			num = GO_CM_TO_PT (num/10.);
			end += 2;
		} else if (0 == strncmp (end, "m", 1)) {
			num = GO_CM_TO_PT (num*100.);
			end ++;
		} else if (0 == strncmp (end, "km", 2)) {
			num = GO_CM_TO_PT (num*100000.);
			end += 2;
		} else if (0 == strncmp (end, "cm", 2)) {
			num = GO_CM_TO_PT (num);
			end += 2;
		} else if (0 == strncmp (end, "pt", 2)) {
			end += 2;
		} else if (0 == strncmp (end, "pc", 2)) { /* pica 12pt == 1 pica */
			num /= 12.;
			end += 2;
		} else if (0 == strncmp (end, "ft", 2)) {
			num = GO_IN_TO_PT (num*12.);
			end += 2;
		} else if (0 == strncmp (end, "mi", 2)) {
			num = GO_IN_TO_PT (num*63360.);
			end += 2;
		} else if (0 == strncmp (end, "inch", 4)) {
			num = GO_IN_TO_PT (num);
			end += 4;
		} else {
			oo_warning (xin, "Invalid attribute '%s', unknown unit '%s'",
				    name, str);
			return NULL;
		}
	} else {
		oo_warning (xin, "Invalid attribute '%s', expected distance, received '%s'",
			    name, str);
		return NULL;
	}

	*pts = num;
	return end;
}
/* returns pts */
static char const *
oo_attr_distance (GsfXMLIn *xin, xmlChar const * const *attrs,
		  int ns_id, char const *name, double *pts)
{
	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (attrs[0] != NULL, NULL);
	g_return_val_if_fail (attrs[1] != NULL, NULL);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return NULL;
	return oo_parse_distance (xin, attrs[1], name, pts);
}

typedef struct {
	char const * const name;
	int val;
} OOEnum;

static gboolean
oo_attr_enum (GsfXMLIn *xin, xmlChar const * const *attrs,
	      int ns_id, char const *name, OOEnum const *enums, int *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return FALSE;

	for (; enums->name != NULL ; enums++)
		if (!strcmp (enums->name, attrs[1])) {
			*res = enums->val;
			return TRUE;
		}
	return oo_warning (xin, "Invalid attribute '%s', unknown enum value '%s'",
			   name, attrs[1]);
}

#warning "FIXME: expand this later."
#define oo_expr_parse_str(str, pp, flags, err)	\
	gnm_expr_parse_str (str, pp,		\
		flags, state->exprconv, err)

/****************************************************************************/

static void
oo_date_convention (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* <table:null-date table:date-value="1904-01-01"/> */
	OOParseState *state = (OOParseState *)xin->user_state;
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
	OOParseState *state = (OOParseState *)xin->user_state;
	int i;

	state->pos.eval.col = 0;
	state->pos.eval.row = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "name")) {
			state->pos.sheet = workbook_sheet_by_name (state->pos.wb, attrs[1]);
			if (NULL == state->pos.sheet) {
				state->pos.sheet = sheet_new (state->pos.wb, attrs[1]);
				workbook_sheet_attach (state->pos.wb, state->pos.sheet);
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
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmStyle *style = NULL;
	double   *width_pts = NULL;
	int repeat_count = 1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "default-cell-style-name"))
			style = g_hash_table_lookup (state->cell_styles, attrs[1]);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "style-name"))
			width_pts = g_hash_table_lookup (state->col_row_styles, attrs[1]);
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-columns-repeated", &repeat_count))
			;

	while (repeat_count-- > 0) {
		if (width_pts != NULL)
			sheet_col_set_size_pts (state->pos.sheet,
				state->pos.eval.col, *width_pts, TRUE);
		state->col_default_styles[state->pos.eval.col++] = style;
	}
}

static void
oo_row_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	int	 i, repeat_count = 1;
	double  *height_pts = NULL;

	state->pos.eval.row++;
	state->pos.eval.col = 0;

	g_return_if_fail (state->pos.eval.row < SHEET_MAX_ROWS);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "style-name"))
			height_pts = g_hash_table_lookup (state->col_row_styles, attrs[1]);
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-rows-repeated", &repeat_count))
			;
	}
	if (height_pts != NULL)
		for (i = repeat_count; i-- > 0 ; )
			sheet_row_set_size_pts (state->pos.sheet,
				state->pos.eval.row + i, *height_pts, TRUE);
	state->pos.eval.row += repeat_count - 1;
}

static char const *
oo_cellref_parse (GnmCellRef *ref, char const *start, GnmParsePos const *pp)
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
			workbook_sheet_attach (pp->wb, ref->sheet);
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
oo_rangeref_parse (GnmRangeRef *ref, char const *start, GnmParsePos const *pp,
		   GnmExprConventions const *convention)
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
oo_cell_content_span_start (GsfXMLIn *xin, xmlChar const **attrs)
{
}

static void
oo_cell_content_span_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
}

static void
oo_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmExpr const	*expr = NULL;
	GnmValue		*val = NULL;
	gboolean	 bool_val;
	gnm_float	 float_val;
	int array_cols = -1, array_rows = -1;
	int merge_cols = -1, merge_rows = -1;
	GnmStyle *style = NULL;

	state->col_inc = 1;
	state->error_content = FALSE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-columns-repeated", &state->col_inc))
			;
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "formula")) {
			char const *expr_string;

			if (attrs[1] == NULL) {
				oo_warning (xin, _("Missing expression"));
				continue;
			}
			expr_string = gnm_expr_char_start_p (attrs[1]);
			if (expr_string == NULL)
				oo_warning (xin, _("Expression '%s' does not start with a recognized character"), attrs[1]);
			else if (*expr_string == '\0')
				/* Ick.  They seem to store error cells as
				 * having value date with expr : '=' and the
				 * message in the content.
				 */
				state->error_content = TRUE;
			else {
				GnmParseError  perr;
				parse_error_init (&perr);
				expr = oo_expr_parse_str (expr_string,
					&state->pos, 0, &perr);
				if (expr == NULL) {
					oo_warning (xin, _("Unable to parse '%s' because '%s'"),
						    attrs[1], perr.err->message);
					parse_error_free (&perr);
				}
			}
		} else if (oo_attr_bool (xin, attrs, OO_NS_TABLE, "boolean-value", &bool_val))
			val = value_new_bool (bool_val);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "date-value")) {
			unsigned y, m, d, h, mi;
			float s;
			unsigned n = sscanf (attrs[1], "%u-%u-%uT%u:%u:%g",
					     &y, &m, &d, &h, &mi, &s);

			if (n >= 3) {
				GDate date;
				g_date_set_dmy (&date, d, m, y);
				if (g_date_valid (&date)) {
					unsigned d_serial = datetime_g_to_serial (&date,
						workbook_date_conv (state->pos.wb));
					if (n >= 6) {
						double time_frac = h + ((double)mi / 60.) + ((double)s / 3600.);
						val = value_new_float (d_serial + time_frac / 24.);
					} else
						val = value_new_int (d_serial);
				}
			}
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "time-value")) {
			unsigned h, m, s;
			if (3 == sscanf (attrs[1], "PT%uH%uM%uS", &h, &m, &s))
				val = value_new_float (h + ((double)m / 60.) + ((double)s / 3600.));
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "string-value"))
			val = value_new_string (attrs[1]);
		else if (oo_attr_float (xin, attrs, OO_NS_TABLE, "value", &float_val))
			val = value_new_float (float_val);
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-matrix-columns-spanned", &array_cols))
			;
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-matrix-rows-spanned", &array_rows))
			;
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-columns-spanned", &merge_cols))
			;
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-rows-spanned", &merge_rows))
			;
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "style-name")) {
			style = g_hash_table_lookup (state->cell_styles, attrs[1]);
		}
	}

	if (style == NULL)
		style = state->col_default_styles[state->pos.eval.col];
	if (style != NULL) {
		gnm_style_ref (style);
		if (state->col_inc > 1) {
			GnmRange tmp;
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
		GnmCell *cell = sheet_cell_fetch (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		if (array_cols > 0 || array_rows > 0) {
			if (array_cols < 0) {
				array_cols = 1;
				oo_warning (xin, _("Invalid array expression does not specify number of columns."));
			} else if (array_rows < 0) {
				array_rows = 1;
				oo_warning (xin, _("Invalid array expression does not specify number of rows."));
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
		GnmCell *cell = sheet_cell_fetch (state->pos.sheet,
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
		GnmRange r;
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
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->col_inc > 1) {
		GnmCell *cell = sheet_cell_get (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		if (!cell_is_empty (cell)) {
			int i = 1;
			GnmCell *next;
			for (; i < state->col_inc ; i++) {
				next = sheet_cell_fetch (state->pos.sheet,
					state->pos.eval.col + i, state->pos.eval.row);
				cell_set_value (next, value_dup (cell->value));
			}
		}
	}

	state->pos.eval.col += state->col_inc;
}

static void
oo_covered_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->col_inc = 1;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-columns-repeated", &state->col_inc))
			;
#if 0
		/* why bother it is covered ? */
		else if (!strcmp (attrs[0], OO_NS_TABLE, "style-name"))
			style = g_hash_table_lookup (state->cell_styles, attrs[1]);

	if (style == NULL)
		style = state->col_default_styles[state->pos.eval.col];
	if (style != NULL) {
		gnm_style_ref (style);
		sheet_style_set_pos (state->pos.sheet,
		     state->pos.eval.col, state->pos.eval.row,
		     style);
	}
#endif
}

static void
oo_covered_cell_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->pos.eval.col += state->col_inc;
}

static void
oo_cell_content_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->simple_content || state->error_content) {
		GnmValue *v;
		GnmCell *cell = sheet_cell_fetch (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		if (state->simple_content)
			v = value_new_string (xin->content->str);
		else
			v = value_new_error (NULL, xin->content->str);
		cell_set_value (cell, v);
	}
}

static void
oo_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const style_types [] = {
		{ "table-cell",	  OO_STYLE_CELL },
		{ "table-row",	  OO_STYLE_ROW },
		{ "table-column", OO_STYLE_COL },
		{ "table",	  OO_STYLE_SHEET },
		{ "graphics",	  OO_STYLE_GRAPHICS },
		{ "paragraph",	  OO_STYLE_PARAGRAPH },
		{ "text",	  OO_STYLE_TEXT },
		{ NULL,	0 },
	};

	OOParseState *state = (OOParseState *)xin->user_state;
	xmlChar const *name = NULL;
	xmlChar const *parent_name = NULL;
	GnmStyle *style;
	GOFormat *fmt = NULL;
	int tmp;

	g_return_if_fail (state->cur_style_type == OO_STYLE_UNKNOWN);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "family", style_types, &tmp))
			state->cur_style_type = tmp;
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "name"))
			name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "parent-style-name"))
			parent_name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "data-style-name")) {
			GOFormat *tmp = g_hash_table_lookup (state->formats, attrs[1]);
			if (tmp != NULL)
				fmt = tmp;
		}

	switch (state->cur_style_type) {
	case OO_STYLE_CELL:
		style = (parent_name != NULL)
			? g_hash_table_lookup (state->cell_styles, parent_name)
			: NULL;
		state->cur_style.cell = (style != NULL)
			? gnm_style_dup (style) : gnm_style_new_default ();

		if (fmt != NULL)
			gnm_style_set_format (state->cur_style.cell, fmt);

		if (name != NULL)
			g_hash_table_replace (state->cell_styles,
					      g_strdup (name),
					      state->cur_style.cell);
		else if (0 == strcmp (xin->node->id, "DEFAULT_STYLE")) {
			 if (state->default_style_cell)
				 gnm_style_unref (state->default_style_cell);
			 state->default_style_cell = state->cur_style.cell;
		}
		break;

	case OO_STYLE_COL:
	case OO_STYLE_ROW:
		state->cur_style.col_row = g_new0 (double, 1);
		if (name)
			g_hash_table_replace (state->col_row_styles,
					      g_strdup (name),
					      state->cur_style.col_row);
		break;

	default:
		break;
	}
}

static void
oo_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	switch (state->cur_style_type) {
	case OO_STYLE_CELL : state->cur_style.cell = NULL;
		break;
	case OO_STYLE_COL : 
	case OO_STYLE_ROW : state->cur_style.col_row = NULL;
		break;

	default :
		break;
	}
	state->cur_style_type = OO_STYLE_UNKNOWN;
}

static void
oo_date_day (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->accum_fmt == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_NUMBER, "style"))
			is_short = 0 == strcmp (attrs[1], "short");

	g_string_append (state->accum_fmt, is_short ? "d" : "dd");
}
static void
oo_date_month (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean as_text = FALSE;
	gboolean is_short = TRUE;

	if (state->accum_fmt == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_NUMBER, "style"))
			is_short = 0 == strcmp (attrs[1], "short");
		else if (oo_attr_bool (xin, attrs, OO_NS_NUMBER, "textual", &as_text))
			;
	g_string_append (state->accum_fmt, as_text
			 ? (is_short ? "mmm" : "mmmm")
			 : (is_short ? "m" : "mm"));
}
static void
oo_date_year (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->accum_fmt == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_NUMBER, "style"))
			is_short = 0 == strcmp (attrs[1], "short");
	g_string_append (state->accum_fmt, is_short ? "yy" : "yyyy");
}
static void
oo_date_era (GsfXMLIn *xin, xmlChar const **attrs)
{
}
static void
oo_date_day_of_week (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->accum_fmt == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_NUMBER, "style"))
			is_short = 0 == strcmp (attrs[1], "short");

	g_string_append (state->accum_fmt, is_short ? "ddd" : "dddd");
}
static void
oo_date_week_of_year (GsfXMLIn *xin, xmlChar const **attrs)
{
}
static void
oo_date_quarter (GsfXMLIn *xin, xmlChar const **attrs)
{
}
static void
oo_date_hours (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->accum_fmt == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_NUMBER, "style"))
			is_short = 0 == strcmp (attrs[1], "short");
	g_string_append (state->accum_fmt, is_short ? "h" : "hh");
}
static void
oo_date_minutes (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->accum_fmt == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_NUMBER, "style"))
			is_short = 0 == strcmp (attrs[1], "short");
	g_string_append (state->accum_fmt, is_short ? "m" : "mm");
}
static void
oo_date_seconds (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->accum_fmt == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_NUMBER, "style"))
			is_short = 0 == strcmp (attrs[1], "short");
	g_string_append (state->accum_fmt, is_short ? "s" : "ss");
}
static void
oo_date_am_pm (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	if (state->accum_fmt != NULL)
		g_string_append (state->accum_fmt, "AM/PM");

}
static void
oo_date_text_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->accum_fmt == NULL)
		return;

	g_string_append (state->accum_fmt, xin->content->str);
}

static void
oo_date_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "name"))
			name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "family") &&
			 0 != strcmp (attrs[1], "data-style"))
			return;

	g_return_if_fail (state->accum_fmt == NULL);
	g_return_if_fail (name != NULL);

	state->accum_fmt = g_string_new (NULL);
	state->fmt_name = g_strdup (name);
}

static void
oo_date_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	g_return_if_fail (state->accum_fmt != NULL);

	g_hash_table_insert (state->formats, state->fmt_name,
		go_format_new_from_XL (state->accum_fmt->str, FALSE));
	g_string_free (state->accum_fmt, TRUE);
	state->accum_fmt = NULL;
	state->fmt_name = NULL;
}

static void
oo_parse_border (GsfXMLIn *xin, GnmStyle *style,
		 char const *str, GnmStyleElement location)
{
	double pts;
	char const *end = oo_parse_distance (xin, str, "border", &pts);
	if (end == NULL || end == str)
		return;
/* "0.035cm solid #000000" */
}

static void
oo_style_prop_cell (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const h_alignments [] = {
		{ "start",	HALIGN_LEFT },
		{ "center",	HALIGN_CENTER },
		{ "end", 	HALIGN_RIGHT },
		{ "justify",	HALIGN_JUSTIFY },
		{ "automatic",	HALIGN_GENERAL },
		{ NULL,	0 },
	};
	static OOEnum const v_alignments [] = {
		{ "bottom", 	VALIGN_BOTTOM },
		{ "top",	VALIGN_TOP },
		{ "middle",	VALIGN_CENTER },
		{ "automatic",	VALIGN_TOP },
		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmColor *color;
	GnmStyle *style = state->cur_style.cell;
	GnmHAlign h_align = HALIGN_GENERAL;
	gboolean h_align_is_valid = FALSE;
	int tmp;

	g_return_if_fail (style != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if ((color = oo_attr_color (xin, attrs, OO_NS_FO, "background-color"))) {
			gnm_style_set_back_color (style, color);
			gnm_style_set_pattern (style, 1);
		} else if ((color = oo_attr_color (xin, attrs, OO_NS_FO, "color")))
			gnm_style_set_font_color (style, color);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "cell-protect"))
			gnm_style_set_content_locked (style, !strcmp (attrs[1], "protected"));
		else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "text-align", h_alignments, &tmp))
			h_align = tmp;
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "text-align-source"))
			h_align_is_valid = !strcmp (attrs[1], "fixed");
		else if (oo_attr_enum (xin, attrs, OO_NS_FO, "vertical-align", v_alignments, &tmp))
			gnm_style_set_align_v (style, tmp);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "wrap-option"))
			gnm_style_set_wrap_text (style, !strcmp (attrs[1], "wrap"));
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "border-bottom"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_BOTTOM);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "border-left"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_LEFT);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "border-right"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_RIGHT);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "border-top"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_TOP);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "font-name"))
			gnm_style_set_font_name (style, attrs[1]);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "font-size")) {
			float size;
			if (1 == sscanf (attrs[1], "%fpt", &size))
				gnm_style_set_font_size (style, size);
		}
#if 0
		else if (!strcmp (attrs[0], OO_NS_FO, "font-weight")) {
				gnm_style_set_font_bold (style, TRUE);
				gnm_style_set_font_uline (style, TRUE);
			="normal"
		} else if (!strcmp (attrs[0], OO_NS_FO, "font-style" )) {
			="italic"
				gnm_style_set_font_italic (style, TRUE);
		} else if (!strcmp (attrs[0], OO_NS_STYLE, "text-underline" )) {
			="italic"
				gnm_style_set_font_italic (style, TRUE);
		}
#endif

	gnm_style_set_align_h (style, h_align_is_valid ? h_align : HALIGN_GENERAL);
}
		       
static void
oo_style_prop_row (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	double pts;

	g_return_if_fail (state->cur_style.col_row != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (NULL != oo_attr_distance (xin, attrs, OO_NS_STYLE, "row-height", &pts))
			*(state->cur_style.col_row) = pts;
}
static void
oo_style_prop_col (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	double pts;

	g_return_if_fail (state->cur_style.col_row != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (NULL != oo_attr_distance (xin, attrs, OO_NS_STYLE, "column-width", &pts))
			*(state->cur_style.col_row) = pts;
}

static void
oo_style_prop (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	switch (state->cur_style_type) {
	case OO_STYLE_CELL : oo_style_prop_cell (xin, attrs); break;
	case OO_STYLE_COL :  oo_style_prop_col (xin, attrs); break;
	case OO_STYLE_ROW :  oo_style_prop_row (xin, attrs); break;

	default :
		break;
	}
}
static void
oo_named_expr (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
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
		GnmParseError perr;
		GnmParsePos   pp;
		GnmExpr const *expr;
		char *tmp = g_strconcat ("[", base_str, "]", NULL);

		parse_error_init (&perr);
		parse_pos_init (&pp, state->pos.wb, NULL, 0, 0);

		expr = oo_expr_parse_str (tmp, &pp,
			GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
			&perr);
		g_free (tmp);

		if (expr == NULL || expr->any.oper != GNM_EXPR_OP_CELLREF) {
			oo_warning (xin, _("Unable to parse position for expression '%s' @ '%s' because '%s'"),
				    name, base_str, perr.err->message);
			parse_error_free (&perr);
			if (expr != NULL)
				gnm_expr_unref (expr);
		} else {
			GnmCellRef const *ref = &expr->cellref.ref;
			parse_pos_init (&pp, state->pos.wb, ref->sheet,
					ref->col, ref->row);

			gnm_expr_unref (expr);
			expr = oo_expr_parse_str (expr_str, &pp, 0, &perr);
			if (expr == NULL) {
				oo_warning (xin, _("Unable to parse position for expression '%s' with value '%s' because '%s'"),
					    name, expr_str, perr.err->message);
				parse_error_free (&perr);
			} else {
				pp.sheet = NULL;
				expr_name_add (&pp, name, expr, NULL, TRUE, NULL);
			}
		}
	}
}

static GsfXMLInNode opencalc_styles_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, FALSE, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE (START, OFFICE_FONTS, OO_NS_OFFICE, "font-decls", FALSE, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE_FONTS, FONT_DECL, OO_NS_STYLE, "font-decl", FALSE, NULL, NULL),
GSF_XML_IN_NODE (START, OFFICE_STYLES, OO_NS_OFFICE, "styles", FALSE, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE, OO_NS_STYLE, "style", FALSE, &oo_style, &oo_style_end),
    GSF_XML_IN_NODE (STYLE, STYLE_PROP, OO_NS_STYLE, "properties", FALSE, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (STYLE_PROP, STYLE_TAB_STOPS, OO_NS_STYLE, "tab-stops", FALSE, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, DEFAULT_STYLE, OO_NS_STYLE, "default-style", FALSE, &oo_style, &oo_style_end),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_STYLE_PROP, OO_NS_STYLE, "properties", FALSE, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (DEFAULT_STYLE_PROP, STYLE_TAB_STOPS, OO_NS_STYLE, "tab-stops", FALSE, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBER, OO_NS_NUMBER,	"number", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_TEXT, OO_NS_NUMBER,	"text", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, OO_NS_NUMBER, "fraction", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, OO_NS_NUMBER, "scientific-number", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_PROP, OO_NS_STYLE,	"properties", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_MAP, OO_NS_STYLE,		"map", FALSE, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, DATE_STYLE, OO_NS_NUMBER, "date-style", FALSE, &oo_date_style, &oo_date_style_end),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY, OO_NS_NUMBER,		"day", FALSE,	&oo_date_day, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_MONTH, OO_NS_NUMBER,		"month", FALSE,	&oo_date_month, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_YEAR, OO_NS_NUMBER,		"year", FALSE,	&oo_date_year, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_ERA, OO_NS_NUMBER,		"era", FALSE,	&oo_date_era, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY_OF_WEEK, OO_NS_NUMBER,	"day-of-week", FALSE, &oo_date_day_of_week, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_WEEK_OF_YEAR, OO_NS_NUMBER,	"week-of-year", FALSE, &oo_date_week_of_year, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_QUARTER, OO_NS_NUMBER,		"quarter", FALSE, &oo_date_quarter, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_HOURS, OO_NS_NUMBER,		"hours", FALSE,	&oo_date_hours, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_MINUTES, OO_NS_NUMBER,		"minutes", FALSE, &oo_date_minutes, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_SECONDS, OO_NS_NUMBER,		"seconds", FALSE, &oo_date_seconds, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_AM_PM, OO_NS_NUMBER,		"am-pm", FALSE,	&oo_date_am_pm, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER,		"text", TRUE,	NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT_PROP, OO_NS_STYLE,		"text-properties", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_MAP, OO_NS_STYLE,			"map", FALSE, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, TIME_STYLE, OO_NS_NUMBER, "time-style", FALSE, &oo_date_style, &oo_date_style_end),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_HOURS, OO_NS_NUMBER,		"hours", FALSE,	&oo_date_hours, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_MINUTES, OO_NS_NUMBER,		"minutes", FALSE, &oo_date_minutes, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_SECONDS, OO_NS_NUMBER,		"seconds", FALSE, &oo_date_seconds, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_AM_PM, OO_NS_NUMBER,		"am-pm", FALSE,	&oo_date_am_pm, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT, OO_NS_NUMBER, 		"text", TRUE,	NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT_PROP, OO_NS_STYLE,		"text-properties", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_MAP, OO_NS_STYLE,			"map", FALSE, NULL, NULL),

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
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_CONTENT, OO_NS_NUMBER,	"text-content", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_PROP, OO_NS_NUMBER,		"text", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_MAP, OO_NS_STYLE,		"map", FALSE, NULL, NULL),

  { NULL }
};

static GsfXMLInNode opencalc_content_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, FALSE, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE (START, OFFICE, OO_NS_OFFICE, "document-content", FALSE, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, SCRIPT, OO_NS_OFFICE, "script", FALSE, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, OFFICE_FONTS, OO_NS_OFFICE, "font-decls", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_FONTS, FONT_DECL, OO_NS_STYLE, "font-decl", FALSE, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, OFFICE_STYLES, OO_NS_OFFICE, "automatic-styles", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE, OO_NS_STYLE, "style", FALSE, &oo_style, &oo_style_end),
      GSF_XML_IN_NODE (STYLE, STYLE_PROP, OO_NS_STYLE, "properties", FALSE, &oo_style_prop, NULL),
        GSF_XML_IN_NODE (STYLE_PROP, STYLE_TAB_STOPS, OO_NS_STYLE, "tab-stops", FALSE, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBER, OO_NS_NUMBER,	  "number", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_TEXT, OO_NS_NUMBER,	  "text", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, OO_NS_NUMBER, "fraction", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, OO_NS_NUMBER, "scientific-number", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_PROP, OO_NS_STYLE,	  "properties", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_MAP, OO_NS_STYLE,		  "map", FALSE, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, DATE_STYLE, OO_NS_NUMBER, "date-style", FALSE, &oo_date_style, &oo_date_style_end),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY, OO_NS_NUMBER,		"day", FALSE,	&oo_date_day, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MONTH, OO_NS_NUMBER,		"month", FALSE,	&oo_date_month, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_YEAR, OO_NS_NUMBER,		"year", FALSE,	&oo_date_year, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_ERA, OO_NS_NUMBER,		"era", FALSE,	&oo_date_era, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY_OF_WEEK, OO_NS_NUMBER,	"day-of-week", FALSE, &oo_date_day_of_week, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_WEEK_OF_YEAR, OO_NS_NUMBER,	"week-of-year", FALSE, &oo_date_week_of_year, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_QUARTER, OO_NS_NUMBER,		"quarter", FALSE, &oo_date_quarter, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_HOURS, OO_NS_NUMBER,		"hours", FALSE,	&oo_date_hours, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MINUTES, OO_NS_NUMBER,		"minutes", FALSE, &oo_date_minutes, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_SECONDS, OO_NS_NUMBER,		"seconds", FALSE, &oo_date_seconds, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_AM_PM, OO_NS_NUMBER,		"am-pm", FALSE,	&oo_date_am_pm, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER,		"text", TRUE,	NULL, &oo_date_text_end),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT_PROP, OO_NS_STYLE,		"text-properties", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MAP, OO_NS_STYLE,		"map", FALSE, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, TIME_STYLE, OO_NS_NUMBER, "time-style", FALSE, &oo_date_style, &oo_date_style_end),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_HOURS, OO_NS_NUMBER,		"hours", FALSE,	&oo_date_hours, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_MINUTES, OO_NS_NUMBER,		"minutes", FALSE, &oo_date_minutes, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_SECONDS, OO_NS_NUMBER,		"seconds", FALSE, &oo_date_seconds, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_AM_PM, OO_NS_NUMBER,		"am-pm", FALSE,	&oo_date_am_pm, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT, OO_NS_NUMBER, 		"text", TRUE,	NULL, &oo_date_text_end),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT_PROP, OO_NS_STYLE,		"text-properties", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_MAP, OO_NS_STYLE,		"map", FALSE, NULL, NULL),

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
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_CONTENT, OO_NS_NUMBER,	"text-content", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_PROP, OO_NS_NUMBER,		"text", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_MAP, OO_NS_STYLE,		"map", FALSE, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE, OFFICE_BODY, OO_NS_OFFICE, "body", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_BODY, TABLE_CALC_SETTINGS, OO_NS_TABLE, "calculation-settings", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (TABLE_CALC_SETTINGS, DATE_CONVENTION, OO_NS_TABLE, "null-date", FALSE, oo_date_convention, NULL),

    GSF_XML_IN_NODE (OFFICE_BODY, TABLE, OO_NS_TABLE, "table", FALSE, &oo_table_start, NULL),
      GSF_XML_IN_NODE (TABLE, TABLE_COL, OO_NS_TABLE, "table-column", FALSE, &oo_col_start, NULL),
      GSF_XML_IN_NODE (TABLE, TABLE_ROW, OO_NS_TABLE, "table-row", FALSE, &oo_row_start, NULL),
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_CELL, OO_NS_TABLE, "table-cell", FALSE, &oo_cell_start, &oo_cell_end),
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_COVERED_CELL, OO_NS_TABLE, "covered-table-cell", FALSE, &oo_covered_cell_start, &oo_covered_cell_end),
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_TEXT, OO_NS_TEXT, "p", TRUE, NULL, &oo_cell_content_end),
	    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_S,    OO_NS_TEXT, "s", FALSE, NULL, NULL),
	    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_SHARED_CONTENT, &oo_cell_content_span_start, &oo_cell_content_span_end),
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_OBJECT, OO_NS_DRAW, "object", FALSE, NULL, NULL),		/* ignore for now */
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_GRAPHIC, OO_NS_DRAW, "g", FALSE, NULL, NULL),		/* ignore for now */
	    GSF_XML_IN_NODE (CELL_GRAPHIC, CELL_GRAPHIC, OO_NS_DRAW, "g", FALSE, NULL, NULL),		/* 2nd def */
	    GSF_XML_IN_NODE (CELL_GRAPHIC, DRAW_POLYLINE, OO_NS_DRAW, "polyline", FALSE, NULL, NULL),	/* 2nd def */
      GSF_XML_IN_NODE (TABLE, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL, OO_NS_TABLE, "table-column", FALSE, NULL, NULL), /* 2nd def */
      GSF_XML_IN_NODE (TABLE, TABLE_ROW_GROUP,	      OO_NS_TABLE, "table-row-group", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW_GROUP, OO_NS_TABLE, "table-row-group", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW,	    OO_NS_TABLE, "table-row", FALSE, NULL, NULL), /* 2nd def */
    GSF_XML_IN_NODE (OFFICE_BODY, NAMED_EXPRS, OO_NS_TABLE, "named-expressions", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_EXPR, OO_NS_TABLE, "named-expression", FALSE, &oo_named_expr, NULL),
    GSF_XML_IN_NODE (OFFICE_BODY, DB_RANGES, OO_NS_TABLE, "database-ranges", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (DB_RANGES, DB_RANGE, OO_NS_TABLE, "database-range", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (DB_RANGE, TABLE_SORT, OO_NS_TABLE, "sort", FALSE, NULL, NULL),
          GSF_XML_IN_NODE (TABLE_SORT, SORT_BY, OO_NS_TABLE, "sort-by", FALSE, NULL, NULL),
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
errortype_renamer (char const *name, GnmExprList *args, GnmExprConventions *convs)
{
	GnmFunc *f = gnm_func_lookup ("ERROR.TYPE", NULL);
	if (f != NULL)
		return gnm_expr_new_funcall (f, args);
	return gnm_func_placeholder_factory (name, args, convs);
}

static GnmExpr const *
oo_unknown_hander (char const *name,
		   GnmExprList *args,
		   GnmExprConventions const *convs)
{
	if (0 == strncmp ("com.sun.star.sheet.addin.Analysis.get", name, 37)) {
		GnmFunc *f = gnm_func_lookup (name + 37, NULL);
		if (f != NULL)
			return gnm_expr_new_funcall (f, args);
		g_warning ("unknown function");
	}
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
	res->dots_in_names = TRUE; /* things like com.sun.star.sheet.addin.Analysis.getErf */
	res->output_argument_sep = ";";
	res->output_array_col_sep = ",";
	res->unknown_function_handler = oo_unknown_hander;
	res->ref_parser = oo_rangeref_parse;

#warning "TODO : OO adds a 'mode' parm to floor/ceiling"
#warning "TODO : OO missing 'A1' parm for address"
	res->function_rewriter_hash =
		g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (res->function_rewriter_hash,
		(gchar *)"ERRORTYPE", errortype_renamer);
	return res;
}

void
openoffice_file_open (GOFileOpener const *fo, IOContext *io_context,
		      WorkbookView *wb_view, GsfInput *input);
void
openoffice_file_open (GOFileOpener const *fo, IOContext *io_context,
		      WorkbookView *wb_view, GsfInput *input)
{
	GsfXMLInDoc	*doc;
	OOParseState	 state;
	GsfInput	*content = NULL;
	GsfInput	*styles = NULL;
	GError		*err = NULL;
	GsfInfile	*zip;
	char *old_num_locale, *old_monetary_locale;
	int i;

	g_return_if_fail (IS_WORKBOOK_VIEW (wb_view));
	g_return_if_fail (GSF_IS_INPUT (input));

	zip = gsf_infile_zip_new (input, &err);
	if (zip == NULL) {
		g_return_if_fail (err != NULL);
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
			err->message);
		g_error_free (err);
		return;
	}

	content = gsf_infile_child_by_name (zip, "content.xml");
	if (content == NULL) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
			 _("No stream named content.xml found."));
		g_object_unref (G_OBJECT (zip));
		return;
	}
	styles = gsf_infile_child_by_name (zip, "styles.xml");
	if (styles == NULL) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
			 _("No stream named styles.xml found."));
		g_object_unref (G_OBJECT (zip));
		return;
	}

	old_num_locale = g_strdup (go_setlocale (LC_NUMERIC, NULL));
	go_setlocale (LC_NUMERIC, "C");
	old_monetary_locale = g_strdup (go_setlocale (LC_MONETARY, NULL));
	go_setlocale (LC_MONETARY, "C");
	go_set_untranslated_bools ();

	/* init */
	state.context = io_context;
	state.wb_view = wb_view;
	state.pos.wb	= wb_view_workbook (wb_view);
	state.pos.sheet = NULL;
	state.pos.eval.col	= -1;
	state.pos.eval.row	= -1;
	state.col_row_styles = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	state.cell_styles = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) gnm_style_unref);
	state.formats = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) go_format_unref);
	state.cur_style.cell   = NULL;
	state.default_style_cell = NULL;
	state.cur_style_type   = OO_STYLE_UNKNOWN;
	state.sheet_order = NULL;
	state.exprconv = oo_conventions ();
	state.accum_fmt = NULL;

	if (styles != NULL) {
		GsfXMLInDoc *doc = gsf_xml_in_doc_new (opencalc_styles_dtd, gsf_ooo_ns);
		gsf_xml_in_doc_parse (doc, styles, &state);
		gsf_xml_in_doc_free (doc);
		g_object_unref (styles);
	}

	doc  = gsf_xml_in_doc_new (opencalc_content_dtd, gsf_ooo_ns);
	if (gsf_xml_in_doc_parse (doc, content, &state)) {
		GsfInput *settings;

		/* get the sheet in the right order (in case something was
		 * created out of order implictly) */
		state.sheet_order = g_slist_reverse (state.sheet_order);
		workbook_sheet_reorder (state.pos.wb, state.sheet_order);
		g_slist_free (state.sheet_order);

		/* look for the view settings */
		settings = gsf_infile_child_by_name (zip, "settings.xml");
		if (settings != NULL) {
			GsfXMLInDoc *sdoc = gsf_xml_in_doc_new (opencalc_settings_dtd, gsf_ooo_ns);
			gsf_xml_in_doc_parse (sdoc, settings, &state);
			gsf_xml_in_doc_free (sdoc);
			g_object_unref (G_OBJECT (settings));

		}
	} else
		gnumeric_io_error_string (io_context, _("XML document not well formed!"));
	gsf_xml_in_doc_free (doc);

	if (state.default_style_cell)
		gnm_style_unref (state.default_style_cell);
	g_hash_table_destroy (state.col_row_styles);
	g_hash_table_destroy (state.cell_styles);
	g_hash_table_destroy (state.formats);
	g_object_unref (G_OBJECT (content));

	g_object_unref (G_OBJECT (zip));

	i = workbook_sheet_count (state.pos.wb);
	while (i-- > 0)
		sheet_flag_recompute_spans (workbook_sheet_by_index (state.pos.wb, i));

	gnm_expr_conventions_free (state.exprconv);

	/* go_setlocale restores bools to locale translation */
	go_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	go_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);
}
