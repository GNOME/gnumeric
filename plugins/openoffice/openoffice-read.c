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
	GsfXmlSAXState base;

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
	      char const *name, gboolean *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	*res = g_ascii_strcasecmp ((gchar *)attrs[1], "false") && strcmp (attrs[1], "0");

	return TRUE;
}

static gboolean
oo_attr_int (OOParseState *state, xmlChar const * const *attrs,
	     char const *name, int *res)
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
	       char const *name, gnum_float *res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
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
	       char const *name)
{
	guint r, g, b;

	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (attrs[0] != NULL, NULL);
	g_return_val_if_fail (attrs[1] != NULL, NULL);

	if (strcmp (attrs[0], name))
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
	      char const *name, OOEnum const *enums, int *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
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

#define oo_expr_parse_str(str, pp, flags, err)				\
	gnm_expr_parse_str (str, pp,					\
		GNM_EXPR_PARSE_USE_OPENCALC_CONVENTIONS |		\
		GNM_EXPR_PARSE_CREATE_PLACEHOLDER_FOR_UNKNOWN_FUNC |	\
		flags, &oo_rangeref_parse, err)

/****************************************************************************/

static void
oo_table_start (GsfXmlSAXState *gsf_state, xmlChar const **attrs)
{
	/* <table:table table:name="Result" table:style-name="ta1"> */
	OOParseState *state = (OOParseState *)gsf_state;
	int i;

	state->pos.eval.col = -1;
	state->pos.eval.row = -1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (!strcmp (attrs[0], "table:name")) {
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
oo_col_start (GsfXmlSAXState *gsf_state, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)gsf_state;
	MStyle *style = NULL;
	int repeat_count = 1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (!strcmp (attrs[0], "table:default-cell-style-name"))
			style = g_hash_table_lookup (state->styles, attrs[1]);
		else if (oo_attr_int (state, attrs, "table:number-columns-repeated", &repeat_count))
			;

	while (repeat_count-- > 0)
		state->col_default_styles[state->pos.eval.col++] = style;
}

static void
oo_row_start (GsfXmlSAXState *gsf_state, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)gsf_state;
	int      repeat_count;
	gboolean repeat_flag = FALSE;

	state->pos.eval.row++;
	state->pos.eval.col = 0;

	g_return_if_fail (state->pos.eval.row < SHEET_MAX_ROWS);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (oo_attr_int (state, attrs, "table:number-rows-repeated", &repeat_count))
			repeat_flag = TRUE;
	}
	if (repeat_flag)
		state->pos.eval.row += repeat_count -1;
}

static char const *
oo_cellref_parse (CellRef *ref, char const *start, ParsePos const *pp)
{
	char const *tmp1, *tmp2, *ptr = start;
	/* sheet name can not contain '.' */
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
	if (tmp1 == ptr)
		return start;
	tmp2 = row_parse (tmp1, &ref->row, &ref->row_relative);
	if (tmp2 == tmp1)
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
oo_cell_start (GsfXmlSAXState *gsf_state, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)gsf_state;
	GnmExpr const	*expr = NULL;
	Value		*val = NULL;
	gboolean	 bool_val;
	gnum_float	 float_val;
	int array_cols = -1, array_rows = -1;
	int merge_cols = -1, merge_rows = -1;
	MStyle *style = NULL;

	state->col_inc = 1;
	state->error_content = FALSE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (oo_attr_int (state, attrs, "table:number-columns-repeated", &state->col_inc))
			;
		else if (!strcmp (attrs[0], "table:formula")) {
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
		}
		else if (oo_attr_bool (state, attrs, "table:boolean-value", &bool_val))
			val = value_new_bool (bool_val);
		else if (!strcmp (attrs[0], "table:date-value")) {
			unsigned y, m, d;
			if (3 == sscanf (attrs[1], "%u-%u-%u", &y, &m, &d)) {
				GDate date;
				g_date_set_dmy (&date, d, m, y);
				if (g_date_valid (&date))
					val = value_new_int (datetime_g_to_serial (&date));
			}
		} else if (!strcmp (attrs[0], "table:string-value"))
			val = value_new_string (attrs[1]);
		else if (oo_attr_float (state, attrs, "table:value", &float_val))
			val = value_new_float (float_val);
		else if (oo_attr_int (state, attrs, "table:number-matrix-columns-spanned", &array_cols))
			;
		else if (oo_attr_int (state, attrs, "table:number-matrix-rows-spanned", &array_rows))
			;
		else if (oo_attr_int (state, attrs, "table:number-columns-spanned", &merge_cols))
			;
		else if (oo_attr_int (state, attrs, "table:number-rows-spanned", &merge_rows))
			;
		else if (!strcmp (attrs[0], "table:style-name")) {
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
oo_cell_end (GsfXmlSAXState *gsf_state)
{
	OOParseState *state = (OOParseState *)gsf_state;

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
oo_covered_cell_start (GsfXmlSAXState *gsf_state, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)gsf_state;

	state->col_inc = 1;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int (state, attrs, "table:number-columns-repeated", &state->col_inc))
			;
#if 0
		/* why bother it is covered ? */
		else if (!strcmp (attrs[0], "table:style-name"))
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
oo_covered_cell_end (GsfXmlSAXState *gsf_state)
{
	OOParseState *state = (OOParseState *)gsf_state;
	state->pos.eval.col += state->col_inc;
}

static void
oo_cell_content_end (GsfXmlSAXState *gsf_state)
{
	OOParseState *state = (OOParseState *)gsf_state;

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
oo_style (GsfXmlSAXState *gsf_state, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)gsf_state;
	xmlChar const *name = NULL;
	MStyle *parent = NULL;
	StyleFormat *fmt = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		/* ignore  style:family the names seem unique enough */
		if (!strcmp (attrs[0], "style:name"))
			name = attrs[1];
		else if (!strcmp (attrs[0], "style:parent-style-name")) {
			MStyle *tmp = g_hash_table_lookup (state->styles, attrs[1]);
			if (tmp != NULL)
				parent = tmp;
		} else if (!strcmp (attrs[0], "style:data-style-name")) {
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
oo_style_end (GsfXmlSAXState *gsf_state)
{
	OOParseState *state = (OOParseState *)gsf_state;
	state->style = NULL;
}

static void
oo_style_prop (GsfXmlSAXState *gsf_state, xmlChar const **attrs)
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
	OOParseState *state = (OOParseState *)gsf_state;
	StyleColor *color;
	MStyle *style = state->style;
	StyleHAlignFlags h_align = HALIGN_GENERAL;
	gboolean h_align_is_valid = FALSE;
	int tmp;

	g_return_if_fail (style != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if ((color = oo_attr_color (state, attrs, "fo:background-color"))) {
			mstyle_set_color (style, MSTYLE_COLOR_BACK, color);
			mstyle_set_pattern (style, 1);
		} else if ((color = oo_attr_color (state, attrs, "fo:color")))
			mstyle_set_color (style, MSTYLE_COLOR_FORE, color);
		else if (!strcmp (attrs[0], "style:cell-protect"))
			mstyle_set_content_locked (style, !strcmp (attrs[1], "protected"));
		else if (oo_attr_enum (state, attrs, "style:text-align", h_alignments, &tmp))
			h_align = tmp;
		else if (!strcmp (attrs[0], "style:text-align-source"))
			h_align_is_valid = !strcmp (attrs[1], "fixed");
		else if (oo_attr_enum (state, attrs, "fo:vertical-align", v_alignments, &tmp))
			;
		else if (!strcmp (attrs[0], "fo:vertical-align"))
			mstyle_set_align_v (style, tmp);
		else if (!strcmp (attrs[0], "fo:wrap-option"))
			mstyle_set_wrap_text (style, !strcmp (attrs[1], "wrap"));
		else if (!strcmp (attrs[0], "style:font-name"))
			mstyle_set_font_name (style, attrs[1]);
		else if (!strcmp (attrs[0], "fo:font-size")) {
			float size;
			if (1 == sscanf (attrs[1], "%fpt", &size))
				mstyle_set_font_size (style, size);
		}
#if 0
		else if (!strcmp (attrs[0], "fo:font-weight")) {
				mstyle_set_font_bold (style, TRUE);
				mstyle_set_font_uline (style, TRUE);
			="normal"
		} else if (!strcmp (attrs[0], "fo:font-style" )) {
			="italic"
				mstyle_set_font_italic (style, TRUE);
		} else if (!strcmp (attrs[0], "style:text-underline" )) {
			="italic"
				mstyle_set_font_italic (style, TRUE);
		}
#endif

	mstyle_set_align_h (style, h_align_is_valid ? h_align : HALIGN_GENERAL);
}
		       
static void
oo_named_expr (GsfXmlSAXState *gsf_state, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)gsf_state;
	xmlChar const *name     = NULL;
	xmlChar const *base_str  = NULL;
	xmlChar const *expr_str = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (!strcmp (attrs[0], "table:name"))
			name = attrs[1];
		else if (!strcmp (attrs[0], "table:base-cell-address"))
			base_str = attrs[1];
		else if (!strcmp (attrs[0], "table:expression"))
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
				expr_name_add (&pp, name, expr, NULL);
			}
		}
	}
}

static GsfXmlSAXNode opencalc_dtd[] = {
GSF_XML_SAX_NODE (START, START, NULL, FALSE, NULL, NULL, 0),
GSF_XML_SAX_NODE (START, OFFICE, "office:document-content", FALSE, NULL, NULL, 0),
  GSF_XML_SAX_NODE (OFFICE, SCRIPT, "office:script", FALSE, NULL, NULL, 0),
  GSF_XML_SAX_NODE (OFFICE, OFFICE_FONTS, "office:font-decls", FALSE, NULL, NULL, 0),
    GSF_XML_SAX_NODE (OFFICE_FONTS, FONT_DECL, "style:font-decl", FALSE, NULL, NULL, 0),
  GSF_XML_SAX_NODE (OFFICE, OFFICE_STYLES, "office:automatic-styles", FALSE, NULL, NULL, 0),
    GSF_XML_SAX_NODE (OFFICE_STYLES, STYLE, "style:style", FALSE, &oo_style, &oo_style_end, 0),
      GSF_XML_SAX_NODE (STYLE, STYLE_PROP, "style:properties", FALSE, &oo_style_prop, NULL, 0),

    GSF_XML_SAX_NODE (OFFICE_STYLES, NUMBER_STYLE, "number:number-style", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (NUMBER_STYLE, NUMBER_STYLE_PROP, "number:number", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, "number:fraction", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, "number:scientific-number", FALSE, NULL, NULL, 0),
    GSF_XML_SAX_NODE (OFFICE_STYLES, DATE_STYLE, "number:date-style", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (DATE_STYLE, DATE_QUARTER, "number:quarter", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (DATE_STYLE, DATE_YEAR, "number:year", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (DATE_STYLE, DATE_MONTH, "number:month", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (DATE_STYLE, DATE_DAY, "number:day", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (DATE_STYLE, DATE_DAY_OF_WEEK, "number:day-of-week", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (DATE_STYLE, DATE_WEEK_OF_YEAR, "number:week-of-year", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (DATE_STYLE, DATE_HOURS, "number:hours", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (DATE_STYLE, DATE_MINUTES, "number:minutes", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (DATE_STYLE, DATE_SECONDS, "number:seconds", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (DATE_STYLE, DATE_TEXT, "number:text", FALSE, NULL, NULL, 0),
    GSF_XML_SAX_NODE (OFFICE_STYLES, STYLE_BOOL, "number:boolean-style", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (STYLE_BOOL, BOOL_PROP, "number:boolean", FALSE, NULL, NULL, 0),
    GSF_XML_SAX_NODE (OFFICE_STYLES, STYLE_CURRENCY, "number:currency-style", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (STYLE_CURRENCY, CURRENCY_STYLE, "number:number", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (STYLE_CURRENCY, CURRENCY_STYLE_PROP, "style:properties", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (STYLE_CURRENCY, CURRENCY_MAP, "style:map", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (STYLE_CURRENCY, CURRENCY_SYMBOL, "number:currency-symbol", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (STYLE_CURRENCY, CURRENCY_TEXT, "number:text", FALSE, NULL, NULL, 0),
    GSF_XML_SAX_NODE (OFFICE_STYLES, STYLE_PERCENTAGE, "number:percentage-style", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (STYLE_PERCENTAGE, PERCENTAGE_STYLE_PROP, "number:number", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT, "number:text", FALSE, NULL, NULL, 0),
    GSF_XML_SAX_NODE (OFFICE_STYLES, STYLE_TEXT, "number:text-style", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (STYLE_TEXT, STYLE_TEXT_PROP, "number:text-content", FALSE, NULL, NULL, 0),
    GSF_XML_SAX_NODE (OFFICE_STYLES, STYLE_TIME, "number:time-style", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (STYLE_TIME, TIME_HOURS, "number:hours", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (STYLE_TIME, TIME_MINUTES, "number:minutes", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (STYLE_TIME, TIME_SECONDS, "number:seconds", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (STYLE_TIME, TIME_AM_PM, "number:am-pm", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (STYLE_TIME, TIME_TEXT, "number:text", FALSE, NULL, NULL, 0),

  GSF_XML_SAX_NODE (OFFICE, OFFICE_BODY, "office:body", FALSE, NULL, NULL, 0),
    GSF_XML_SAX_NODE (OFFICE_BODY, TABLE_CALC_SETTINGS, "table:calculation-settings", FALSE, NULL, NULL, 0),
    GSF_XML_SAX_NODE (OFFICE_BODY, TABLE, "table:table", FALSE, &oo_table_start, NULL, 0),
      GSF_XML_SAX_NODE (TABLE, TABLE_COL, "table:table-column", FALSE, &oo_col_start, NULL, 0),
      GSF_XML_SAX_NODE (TABLE, TABLE_ROW, "table:table-row", FALSE, &oo_row_start, NULL, 0),
	GSF_XML_SAX_NODE (TABLE_ROW, TABLE_CELL, "table:table-cell", FALSE, &oo_cell_start, &oo_cell_end, 0),
	GSF_XML_SAX_NODE (TABLE_ROW, TABLE_COVERED_CELL, "table:covered-table-cell", FALSE, &oo_covered_cell_start, &oo_covered_cell_end, 0),
	  GSF_XML_SAX_NODE (TABLE_CELL, CELL_TEXT, "text:p", TRUE, NULL, &oo_cell_content_end, 0),
	    GSF_XML_SAX_NODE (CELL_TEXT, CELL_TEXT_S, "text:s", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (TABLE, TABLE_COL_GROUP, "table:table-column-group", FALSE, NULL, NULL, 0),
        GSF_XML_SAX_NODE (TABLE_COL_GROUP, TABLE_COL_GROUP, "table:table-column-group", FALSE, NULL, NULL, 0),
        GSF_XML_SAX_NODE (TABLE_COL_GROUP, TABLE_COL, "table:table-column", FALSE, NULL, NULL, 0), /* 2nd def */
      GSF_XML_SAX_NODE (TABLE, TABLE_ROW_GROUP,	      "table:table-row-group", FALSE, NULL, NULL, 0),
        GSF_XML_SAX_NODE (TABLE_ROW_GROUP, TABLE_ROW_GROUP, "table:table-row-group", FALSE, NULL, NULL, 0),
        GSF_XML_SAX_NODE (TABLE_ROW_GROUP, TABLE_ROW,	    "table:table-row", FALSE, NULL, NULL, 0), /* 2nd def */
    GSF_XML_SAX_NODE (OFFICE_BODY, NAMED_EXPRS, "table:named-expressions", FALSE, NULL, NULL, 0),
      GSF_XML_SAX_NODE (NAMED_EXPRS, NAMED_EXPR, "table:named-expression", FALSE, &oo_named_expr, NULL, 0),
  { NULL }
};


void
openoffice_file_open (GnumFileOpener const *fo, IOContext *io_context,
		      WorkbookView *wb_view, GsfInput *input)
{
	OOParseState state;
	GsfInput *content = NULL;
	GError   *err = NULL;
	GsfInfile *zip;
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

	content = gsf_infile_child_by_name (zip, "content.xml");
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

	state.base.root = opencalc_dtd;
	if (!gsf_xmlSAX_parse (content, &state.base))
		gnumeric_io_error_string (io_context, _("XML document not well formed!"));
	else
		workbook_queue_all_recalc (state.pos.wb);

	state.sheet_order = g_slist_reverse (state.sheet_order);
	workbook_sheet_reorder (state.pos.wb, state.sheet_order,  NULL);
	g_slist_free (state.sheet_order);

	g_hash_table_destroy (state.styles);
	g_object_unref (G_OBJECT (content));
	g_object_unref (G_OBJECT (zip));

	i = workbook_sheet_count (state.pos.wb);
	while (i-- > 0)
		sheet_flag_recompute_spans (workbook_sheet_by_index (state.pos.wb, i));
}
