/*
 * fn-information.c:  Information built-in functions
 *
 * Authors:
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   Jody Goldberg (jody@gnome.org)
 *   Morten Welinder (terra@gnome.org)
 *   Almer S. Tigelaar (almer@gnome.org)
 *   Harlan Grove
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 *
 * Many thanks to Harlan Grove for his excellent characterization and writeup
 * of the multitude of different potential arguments across the various
 * different spreadsheets.  Although neither the code is not his, the set of
 * attributes, and the comments on their behviour are.  Hence he holds partial
 * copyright on the CELL implementation.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <func.h>
#include <parse-util.h>
#include <cell.h>
#include <ranges.h>
#include <sheet.h>
#include <workbook.h>
#include <gnm-format.h>
#include <style.h>
#include <style-font.h>
#include <value.h>
#include <expr.h>
#include <workbook.h>
#include <sheet-style.h>
#include <number-match.h>
#include <gnm-i18n.h>
#include <hlink.h>

#include <goffice/goffice.h>
#include <gnm-plugin.h>

#ifdef HAVE_UNAME
#include <sys/utsname.h>
#endif
#include <math.h>
#include <stdlib.h>
#include <string.h>

GNM_PLUGIN_MODULE_HEADER;

/***************************************************************************/

static GnmFuncHelp const help_cell[] = {
        { GNM_FUNC_HELP_NAME, F_("CELL:information of @{type} about @{cell}")},
        { GNM_FUNC_HELP_ARG, F_("type:string specifying the type of information requested")},
        { GNM_FUNC_HELP_ARG, F_("cell:cell reference")},
        { GNM_FUNC_HELP_DESCRIPTION, F_("@{type} specifies the type of information you want to obtain:\n"
					"  address        \t\tReturns the given cell reference as text.\n"
					"  col            \t\tReturns the number of the column in @{cell}.\n"
					"  color          \t\tReturns 0.\n"
					"  contents       \t\tReturns the contents of the cell in @{cell}.\n"
					"  column         \t\tReturns the number of the column in @{cell}.\n"
					"  columnwidth    \tReturns the column width.\n"
					"  coord          \t\tReturns the absolute address of @{cell}.\n"
					"  datatype       \tsame as type\n"
					"  filename       \t\tReturns the name of the file of @{cell}.\n"
					"  format         \t\tReturns the code of the format of the cell.\n"
					"  formulatype    \tsame as type\n"
					"  locked         \t\tReturns 1 if @{cell} is locked.\n"
					"  parentheses    \tReturns 1 if @{cell} contains a negative value\n"
					"                 \t\tand its format displays it with parentheses.\n"
					"  prefix         \t\tReturns a character indicating the horizontal\n"
					"                 \t\talignment of @{cell}.\n"
					"  prefixcharacter  \tsame as prefix\n"
					"  protect        \t\tReturns 1 if @{cell} is locked.\n"
					"  row            \t\tReturns the number of the row in @{cell}.\n"
					"  sheetname      \tReturns the name of the sheet of @{cell}.\n"
					"  type           \t\tReturns \"l\" if @{cell} contains a string, \n"
					"                 \t\t\"v\" if it contains some other value, and \n"
					"                 \t\t\"b\" if @{cell} is blank.\n"
					"  value          \t\tReturns the contents of the cell in @{cell}.\n"
					"  width          \t\tReturns the column width.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=CELL(\"col\",A1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=CELL(\"width\",A1)" },
        { GNM_FUNC_HELP_SEEALSO, "INDIRECT"},
        { GNM_FUNC_HELP_END}
};

typedef struct {
	char const *format;
	char const *output;
} translate_t;
static const translate_t translate_table[] = {
	{ "m/d/yy", "D4" },
	{ "m/d/yy h:mm", "D4" },
	{ "mm/dd/yy", "D4" },
	{ "d-mmm-yy", "D1" },
	{ "dd-mmm-yy", "D1" },
	{ "d-mmm", "D2" },
	{ "dd-mmm", "D2" },
	{ "mmm-yy", "D3" },
	{ "mm/dd", "D5" },
	{ "h:mm am/pm", "D7" },
	{ "h:mm:ss am/pm", "D6" },
	{ "h:mm", "D9" },
	{ "h:mm:ss", "D8" }
};

static GnmValue *
translate_cell_format (GOFormat const *format)
{
	int i;
	const char *fmt;
	const int translate_table_count = G_N_ELEMENTS (translate_table);
	gboolean exact;
	GOFormatDetails details;

	if (format == NULL)
		goto fallback;

	fmt = go_format_as_XL (format);

	/*
	 * TODO : What does this do in different locales ??
	 */
	for (i = 0; i < translate_table_count; i++) {
		const translate_t *t = &translate_table[i];

		if (!g_ascii_strcasecmp (fmt, t->format)) {
			return value_new_string (t->output);
		}
	}

	go_format_get_details (format, &details, &exact);
	if (0 && !exact) {
		g_printerr ("Inexact for %s\n", fmt);
		goto fallback;
	}

	switch (details.family) {
	case GO_FORMAT_NUMBER:
		return value_new_string_nocopy
			(g_strdup_printf
			 ("%c%d",
			  details.thousands_sep ? ',' : 'F',
			  details.num_decimals));
	case GO_FORMAT_CURRENCY:
	case GO_FORMAT_ACCOUNTING:
		return value_new_string_nocopy
			(g_strdup_printf
			 ("C%d%s",
			  details.num_decimals,
			  details.negative_red ? "-" : ""));
	case GO_FORMAT_PERCENTAGE:
		return value_new_string_nocopy
			(g_strdup_printf
			 ("P%d",
			  details.num_decimals));
	case GO_FORMAT_SCIENTIFIC:
		return value_new_string_nocopy
			(g_strdup_printf
			 ("S%d",
			  details.num_decimals));
	default:
		goto fallback;
	}

fallback:
	return value_new_string ("G");
}

/* TODO : turn this into a range based routine */
static GnmValue *
gnumeric_cell (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *info_type = value_peek_string (argv[0]);
	GnmCellRef const *ref;
	const Sheet *sheet;

	if (!VALUE_IS_CELLRANGE (argv[1]))
		return value_new_error_VALUE (ei->pos);

	ref = &argv[1]->v_range.cell.a;
	sheet = eval_sheet (ref->sheet, ei->pos->sheet);

	/*
	 * CELL translates its keywords (ick)
	  adresse	- address
	  colonne	- col
	  contenu	- contents
	  couleur	- color
	  format	- format
	  largeur	- width
	  ligne		- row
	  nomfichier	- filename
	  parentheses	- parentheses
	  prefixe	- prefix
	  protege	- protect
	  type		- type
	  */

	/* from CELL - limited usefulness! */
	if (!g_ascii_strcasecmp(info_type, "address")) {
		GnmParsePos pp;
		GnmConventionsOut out;
		out.accum = g_string_new (NULL);
		out.pp    = parse_pos_init_evalpos (&pp, ei->pos);
		out.convs = gnm_conventions_default;
		cellref_as_string (&out, ref, TRUE);
		return value_new_string_nocopy (g_string_free (out.accum, FALSE));

	} else if (!g_ascii_strcasecmp(info_type, "sheetname")) {
		return value_new_string (sheet->name_unquoted);

	/* from later 123 versions - USEFUL! */
	} else if (!g_ascii_strcasecmp(info_type, "coord")) {
		GnmParsePos pp;
		GnmConventionsOut out;
		out.accum = g_string_new (NULL);
		out.pp    = parse_pos_init_evalpos (&pp, ei->pos);
		out.convs = gnm_conventions_default;
		cellref_as_string (&out, ref, TRUE);
		return value_new_string_nocopy (g_string_free (out.accum, FALSE));

	/* from CELL - pointless - use COLUMN instead! */
	} else if (!g_ascii_strcasecmp (info_type, "col") ||
		   !g_ascii_strcasecmp (info_type, "column")) {
		return value_new_int (ref->col + 1);

	/* from CELL - pointless - use ROW instead! */
	} else if (!g_ascii_strcasecmp (info_type, "row")) {
		return value_new_int (ref->row + 1);

	/* from CELL - limited usefulness
	 * NOTE: differences between Excel & 123 - Excel's returns 1 whenever
	 * there's a color specified for EITHER positive OR negative values
	 * in the number format, e.g., 1 for format "[Black]0;-0;0" but not
	 * for format "0;-0;[Green]0"
	 * Another place where Excel doesn't conform to its documentation!
	 *
	 * 20180503: and even the above isn't right.  What appears to be test
	 * is this:
	 * (a) The format must be conditional; "[Red]0" won't do
	 * (b) One of the first two conditional formats must have a color
	 *     specified.
	 */
	} else if (!g_ascii_strcasecmp (info_type, "color")) {
		/* See 1.7.6 for old version.  */
		return value_new_int (0);

	/* absolutely pointless - compatibility only */
	} else if (!g_ascii_strcasecmp (info_type, "contents") ||
		   !g_ascii_strcasecmp (info_type, "value")) {
		GnmCell const *cell =
			sheet_cell_get (sheet, ref->col, ref->row);
		if (cell && cell->value)
			return value_dup (cell->value);
		return value_new_empty ();

	/* from CELL - limited usefulness!
	 * A testament to Microsoft's hypocracy! They could include this from
	 * 123R2.2 (it wasn't in 123R2.0x), modify it in Excel 4.0 to include
	 * the worksheet name, but they can't make any other changes to CELL?!
	 */
	} else if (!g_ascii_strcasecmp (info_type, "filename")) {
		char const *name = go_doc_get_uri (GO_DOC (sheet->workbook));

		if (name == NULL)
			return value_new_string ("");
		else
			return value_new_string (name);

	/* from CELL */
	/* Backwards compatibility w/123 - unnecessary */
	} else if (!g_ascii_strcasecmp (info_type, "format")) {
		GnmStyle const *mstyle =
			sheet_style_get (sheet, ref->col, ref->row);

		return translate_cell_format (gnm_style_get_format (mstyle));

	/* from CELL */
	/* Backwards compatibility w/123 - unnecessary */
	} else if (!g_ascii_strcasecmp (info_type, "parentheses")) {
		/* See 1.7.6 for old version.  */
		return value_new_int (0);

	/* from CELL */
	/* Backwards compatibility w/123 - unnecessary */
	} else if (!g_ascii_strcasecmp (info_type, "prefix") ||
		   !g_ascii_strcasecmp (info_type, "prefixcharacter")) {
		GnmStyle const *mstyle =
			sheet_style_get (sheet, ref->col, ref->row);
		GnmCell const *cell =
			sheet_cell_get (sheet, ref->col, ref->row);

		if (cell && cell->value && VALUE_IS_STRING (cell->value)) {
			switch (gnm_style_get_align_h (mstyle)) {
			case GNM_HALIGN_GENERAL:
			case GNM_HALIGN_LEFT:
			case GNM_HALIGN_JUSTIFY:
			case GNM_HALIGN_DISTRIBUTED:
						return value_new_string ("'");
			case GNM_HALIGN_RIGHT:	return value_new_string ("\"");
			case GNM_HALIGN_CENTER_ACROSS_SELECTION:
			case GNM_HALIGN_CENTER:	return value_new_string ("^");
			case GNM_HALIGN_FILL:	return value_new_string ("\\");
			default :		return value_new_string ("");
			}
		}
		return value_new_string ("");

	/* from CELL */
	} else if (!g_ascii_strcasecmp (info_type, "locked") ||
		   !g_ascii_strcasecmp (info_type, "protect")) {
		GnmStyle const *mstyle =
			sheet_style_get (sheet, ref->col, ref->row);
		// Not boolean for some reason
		return value_new_int (gnm_style_get_contents_locked (mstyle) ? 1 : 0);
	} else if (!g_ascii_strcasecmp (info_type, "type") ||
		   !g_ascii_strcasecmp (info_type, "datatype") ||
		   !g_ascii_strcasecmp (info_type, "formulatype")) {
		GnmCell const *cell =
			sheet_cell_get (sheet, ref->col, ref->row);
		if (!cell || VALUE_IS_EMPTY(cell->value))
			return value_new_string ("b");
		if (VALUE_IS_STRING (cell->value))
			// Q: should we pick this case if cell has expression?
			return value_new_string ("l");
		return value_new_string ("v");

	/* from CELL */
	} else if (!g_ascii_strcasecmp (info_type, "width") ||
		   !g_ascii_strcasecmp (info_type, "columnwidth")) {
		ColRowInfo const *info =
			sheet_col_get_info (sheet, ref->col);
		double charwidth;
		int    cellwidth;

		charwidth = gnm_font_default_width;
		cellwidth = info->size_pts;

		// FIXME: Docs say to return two-element array
		return value_new_int (rint (cellwidth / charwidth));
	}

	return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_expression[] = {
        { GNM_FUNC_HELP_NAME, F_("EXPRESSION:expression in @{cell} as a string")},
        { GNM_FUNC_HELP_ARG, F_("cell:a cell reference")},
        { GNM_FUNC_HELP_NOTE, F_("If @{cell} contains no expression, EXPRESSION returns empty.")},
        { GNM_FUNC_HELP_SEEALSO, "TEXT"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_expression (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue const * const v = argv[0];
	if (VALUE_IS_CELLRANGE (v)) {
		GnmCell *cell;
		GnmCellRef const * a = &v->v_range.cell.a;
		GnmCellRef const * b = &v->v_range.cell.b;

		if (a->col != b->col || a->row != b->row || a->sheet !=b->sheet)
			return value_new_error_REF (ei->pos);

		cell = sheet_cell_get (eval_sheet (a->sheet, ei->pos->sheet),
				       a->col, a->row);

		if (cell && gnm_cell_has_expr (cell)) {
			GnmParsePos pos;
			char *expr_string = gnm_expr_top_as_string
				(cell->base.texpr,
				 parse_pos_init_cell (&pos, cell),
				 gnm_conventions_default);
			return value_new_string_nocopy (expr_string);
		}
	}

	return value_new_empty ();
}
/***************************************************************************/

static GnmFuncHelp const help_get_formula[] = {
	{ GNM_FUNC_HELP_NAME, F_("GET.FORMULA:the formula in @{cell} as a string")},
	{ GNM_FUNC_HELP_ARG, F_("cell:the referenced cell")},
	{ GNM_FUNC_HELP_ODF, F_("GET.FORMULA is the OpenFormula function FORMULA.") },
	{ GNM_FUNC_HELP_EXAMPLES, F_("If A1 is empty and A2 contains =B1+B2, then\n"
				     "GET.FORMULA(A2) yields '=B1+B2' and\n"
				     "GET.FORMULA(A1) yields ''.") },
	{ GNM_FUNC_HELP_SEEALSO, "EXPRESSION,ISFORMULA"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_get_formula (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue const * const v = argv[0];
	if (VALUE_IS_CELLRANGE (v)) {
		GnmCell *cell;
		GnmCellRef const * a = &v->v_range.cell.a;
		GnmCellRef const * b = &v->v_range.cell.b;

		if (a->col != b->col || a->row != b->row || a->sheet !=b->sheet)
			return value_new_error_REF (ei->pos);

		cell = sheet_cell_get (eval_sheet (a->sheet, ei->pos->sheet),
				       a->col, a->row);

		if (cell && gnm_cell_has_expr (cell)) {
			GnmConventionsOut out;
			GnmParsePos	  pp;
			out.accum = g_string_new ("=");
			out.pp    = parse_pos_init_cell (&pp, cell);
			out.convs = gnm_conventions_default;
			gnm_expr_top_as_gstring (cell->base.texpr, &out);
			return value_new_string_nocopy (g_string_free (out.accum, FALSE));
		}
	}

	return value_new_empty ();
}

/***************************************************************************/

static GnmFuncHelp const help_isformula[] = {
	{ GNM_FUNC_HELP_NAME, F_("ISFORMULA:TRUE if @{cell} contains a formula")},
	{ GNM_FUNC_HELP_ARG, F_("cell:the referenced cell")},
	{ GNM_FUNC_HELP_ODF, F_("ISFORMULA is OpenFormula compatible.") },
	{ GNM_FUNC_HELP_SEEALSO, "GET.FORMULA"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_isformula (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue const * const v = argv[0];
	if (VALUE_IS_CELLRANGE (v)) {
		GnmCell *cell;
		GnmCellRef const * a = &v->v_range.cell.a;
		GnmCellRef const * b = &v->v_range.cell.b;

		if (a->col != b->col || a->row != b->row || a->sheet !=b->sheet)
			return value_new_error_REF (ei->pos);

		cell = sheet_cell_get (eval_sheet (a->sheet, ei->pos->sheet),
				       a->col, a->row);
		return value_new_bool (cell && gnm_cell_has_expr (cell));
	}

	return value_new_error_REF (ei->pos);
}


/***************************************************************************/

static GnmFuncHelp const help_countblank[] = {
        { GNM_FUNC_HELP_NAME, F_("COUNTBLANK:the number of blank cells in @{range}")},
        { GNM_FUNC_HELP_ARG, F_("range:a cell range")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, F_("COUNTBLANK(A1:A20) returns the number of blank cell in A1:A20.") },
        { GNM_FUNC_HELP_SEEALSO, "COUNT"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
cb_countblank (GnmValueIter const *iter, gpointer user)
{
	GnmValue const *v = iter->v;

	if (VALUE_IS_STRING (v) && value_peek_string (v)[0] == 0)
		; /* Nothing -- the empty string is blank.  */
	else if (VALUE_IS_EMPTY (v))
		; /* Nothing  */
	else
		*((int *)user) -= 1;

	return NULL;
}

static GnmValue *
gnumeric_countblank (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue const *v = argv[0];
	int count =
		value_area_get_width (v, ei->pos) *
		value_area_get_height (v, ei->pos);
	int nsheets = 1;

	if (VALUE_IS_CELLRANGE (v)) {
		GnmRange r;
		Sheet *start_sheet, *end_sheet;

		gnm_rangeref_normalize (&v->v_range.cell, ei->pos,
					&start_sheet, &end_sheet, &r);

		if (start_sheet != end_sheet && end_sheet != NULL)
			nsheets = 1 + abs (end_sheet->index_in_wb -
					   start_sheet->index_in_wb);
	}

	count *= nsheets;

	value_area_foreach (v, ei->pos, CELL_ITER_IGNORE_BLANK,
			    &cb_countblank, &count);

	return value_new_int (count);
}

/***************************************************************************/

static GnmFuncHelp const help_info[] = {
        { GNM_FUNC_HELP_NAME, F_("INFO:information about the current operating environment "
				 "according to @{type}")},
        { GNM_FUNC_HELP_ARG, F_("type:string giving the type of information requested")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("INFO returns information about the current operating "
					"environment according to @{type}:\n"
					"  memavail     \t\tReturns the amount of memory available, bytes.\n"
					"  memused      \tReturns the amount of memory used (bytes).\n"
					"  numfile      \t\tReturns the number of active worksheets.\n"
					"  osversion    \t\tReturns the operating system version.\n"
					"  recalc       \t\tReturns the recalculation mode (automatic).\n"
					"  release      \t\tReturns the version of Gnumeric as text.\n"
					"  system       \t\tReturns the name of the environment.\n"
					"  totmem       \t\tReturns the amount of total memory available.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=INFO(\"system\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=INFO(\"release\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=INFO(\"numfile\")" },
        { GNM_FUNC_HELP_SEEALSO, "CELL"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_info (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const * const info_type = value_peek_string (argv[0]);
	if (!g_ascii_strcasecmp (info_type, "directory")) {
		/* Path of the current directory or folder.  */
		return value_new_error (ei->pos, _("Unimplemented"));
	} else if (!g_ascii_strcasecmp (info_type, "memavail")) {
		/* Amount of memory available, in bytes.  */
		return value_new_int (15 << 20);  /* Good enough... */
	} else if (!g_ascii_strcasecmp (info_type, "memused")) {
		/* Amount of memory being used for data.  */
		return value_new_int (1 << 20);  /* Good enough... */
	} else if (!g_ascii_strcasecmp (info_type, "numfile")) {
		/* Number of active worksheets.  */
		return value_new_int (1);  /* Good enough... */
	} else if (!g_ascii_strcasecmp (info_type, "origin")) {
		/* Absolute A1-style reference, as text, prepended with "$A:"
		 * for Lotus 1-2-3 release 3.x compatibility. Returns the cell
		 * reference of the top and leftmost cell visible in the
		 * window, based on the current scrolling position.
		 */
		return value_new_error (ei->pos, _("Unimplemented"));
	} else if (!g_ascii_strcasecmp (info_type, "osversion")) {
#ifdef HAVE_UNAME
		/* Current operating system version, as text.  */
		struct utsname unamedata;

		if (uname (&unamedata) == -1)
			return value_new_error (ei->pos,
						_("Unknown version"));
		else {
			char *tmp = g_strdup_printf (_("%s version %s"),
						     unamedata.sysname,
						     unamedata.release);
			return value_new_string_nocopy (tmp);
		}
#elif defined(G_OS_WIN32)
		/* fake XP */
		return value_new_string ("Windows (32-bit) NT 5.01");
#else
		// Nothing -- go to catch-all
#endif
	} else if (!g_ascii_strcasecmp (info_type, "recalc")) {
		/* Current recalculation mode; returns "Automatic" or "Manual".  */
		Workbook const *wb = ei->pos->sheet->workbook;
		return value_new_string (
			workbook_get_recalcmode (wb) ? _("Automatic") : _("Manual"));
	} else if (!g_ascii_strcasecmp (info_type, "release")) {
		/* Version of Gnumeric (Well, Microsoft Excel), as text.  */
		return value_new_string (GNM_VERSION_FULL);
	} else if (!g_ascii_strcasecmp (info_type, "system")) {
#ifdef HAVE_UNAME
		/* Name of the operating environment.  */
		struct utsname unamedata;

		if (uname (&unamedata) == -1)
			return value_new_error (ei->pos, _("Unknown system"));
		else
			return value_new_string (unamedata.sysname);
#elif defined(G_OS_WIN32)
		return value_new_string ("pcdos");	/* seems constant */
#else
		// Nothing -- go to catch-all
#endif
	} else if (!g_ascii_strcasecmp (info_type, "totmem")) {
		/* Total memory available, including memory already in use, in
		 * bytes.
		 */
		return value_new_int (16 << 20);  /* Good enough... */
	}

	return value_new_error (ei->pos, _("Unknown info_type"));
}

/***************************************************************************/

static GnmFuncHelp const help_iserror[] = {
        { GNM_FUNC_HELP_NAME, F_("ISERROR:TRUE if @{value} is any error value")},
        { GNM_FUNC_HELP_ARG, F_("value:a value")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ISERROR(NA())" },
        { GNM_FUNC_HELP_EXAMPLES, "=ISERROR(5/0)" },
        { GNM_FUNC_HELP_SEEALSO, "ISERR,ISNA"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_iserror (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (VALUE_IS_ERROR (argv[0]));
}

/***************************************************************************/

static GnmFuncHelp const help_isna[] = {
        { GNM_FUNC_HELP_NAME, F_("ISNA:TRUE if @{value} is the #N/A error value")},
        { GNM_FUNC_HELP_ARG, F_("value:a value")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ISNA(NA())" },
        { GNM_FUNC_HELP_EXAMPLES, "=ISNA(5/0)" },
        { GNM_FUNC_HELP_SEEALSO, "NA"},
        { GNM_FUNC_HELP_END}
};

/*
 * We need to operator directly in the input expression in order to bypass
 * the error handling mechanism
 */
static GnmValue *
gnumeric_isna (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (value_error_classify (argv[0]) == GNM_ERROR_NA);
}

/***************************************************************************/

static GnmFuncHelp const help_iserr[] = {
        { GNM_FUNC_HELP_NAME, F_("ISERR:TRUE if @{value} is any error value except #N/A")},
        { GNM_FUNC_HELP_ARG, F_("value:a value")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ISERR(NA())" },
        { GNM_FUNC_HELP_EXAMPLES, "=ISERR(5/0)" },
        { GNM_FUNC_HELP_SEEALSO, "ISERROR"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_iserr (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (VALUE_IS_ERROR (argv[0]) &&
			       value_error_classify (argv[0]) != GNM_ERROR_NA);
}

/***************************************************************************/

static GnmFuncHelp const help_error_type[] = {
        { GNM_FUNC_HELP_NAME, F_("ERROR.TYPE:the type of @{error}")},
        { GNM_FUNC_HELP_ARG, F_("error:an error")},
        { GNM_FUNC_HELP_DESCRIPTION, F_("ERROR.TYPE returns an error number corresponding to the given "
					"error value.  The error numbers for error values are:\n\n"
					"\t#DIV/0!  \t\t2\n"
					"\t#VALUE!  \t3\n"
					"\t#REF!    \t\t4\n"
					"\t#NAME?   \t5\n"
					"\t#NUM!    \t6\n"
					"\t#N/A     \t\t7")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ERROR.TYPE(NA())" },
        { GNM_FUNC_HELP_EXAMPLES, "=ERROR.TYPE(ERROR(\"#X\"))" },
        { GNM_FUNC_HELP_SEEALSO, "ISERROR"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_error_type (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	switch (value_error_classify (argv[0])) {
	case GNM_ERROR_NULL: return value_new_int (1);
	case GNM_ERROR_DIV0: return value_new_int (2);
	case GNM_ERROR_VALUE: return value_new_int (3);
	case GNM_ERROR_REF: return value_new_int (4);
	case GNM_ERROR_NAME: return value_new_int (5);
	case GNM_ERROR_NUM: return value_new_int (6);
	case GNM_ERROR_NA: return value_new_int (7);
	default:
		return value_new_error_NA (ei->pos);
	}
}

/***************************************************************************/

static GnmFuncHelp const help_na[] = {
        { GNM_FUNC_HELP_NAME, F_("NA:the error value #N/A")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=NA()" },
        { GNM_FUNC_HELP_EXAMPLES, "=ISNA(NA())" },
        { GNM_FUNC_HELP_SEEALSO, "ISNA"},
        { GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_na (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_error_NA (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_error[] = {
        { GNM_FUNC_HELP_NAME, F_("ERROR:the error with the given @{name}")},
        { GNM_FUNC_HELP_ARG, F_("name:string")},
        { GNM_FUNC_HELP_EXAMPLES, "=ERROR(\"#N/A\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=ISNA(ERROR(\"#N/A\"))" },
        { GNM_FUNC_HELP_SEEALSO, "ISERROR"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_error (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_error (ei->pos, value_peek_string (argv[0]));
}

/***************************************************************************/

static GnmFuncHelp const help_isblank[] = {
        { GNM_FUNC_HELP_NAME, F_("ISBLANK:TRUE if @{value} is blank")},
        { GNM_FUNC_HELP_ARG, F_("value:a value")},
        { GNM_FUNC_HELP_DESCRIPTION, F_("This function checks if a value is blank.  Empty cells are blank, but empty strings are not.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ISBLANK(\"\")" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_isblank (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (VALUE_IS_EMPTY (argv[0]));
}

/***************************************************************************/

static GnmFuncHelp const help_iseven[] = {
        { GNM_FUNC_HELP_NAME, F_("ISEVEN:TRUE if @{n} is even")},
        { GNM_FUNC_HELP_ARG, F_("n:number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ISEVEN(4)" },
        { GNM_FUNC_HELP_SEEALSO, "ISODD"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_iseven (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float r = gnm_fmod (gnm_abs (x), 2);

	/* If x is too big, this will always be true.  */
	return value_new_bool (r < 1);
}

/***************************************************************************/

static GnmFuncHelp const help_islogical[] = {
        { GNM_FUNC_HELP_NAME, F_("ISLOGICAL:TRUE if @{value} is a logical value")},
        { GNM_FUNC_HELP_ARG, F_("value:a value")},
        { GNM_FUNC_HELP_DESCRIPTION, F_("This function checks if a value is either TRUE or FALSE.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ISLOGICAL(1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=ISLOGICAL(\"Gnumeric\")" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_islogical (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (VALUE_IS_BOOLEAN (argv[0]));
}

/***************************************************************************/

static GnmFuncHelp const help_isnontext[] = {
        { GNM_FUNC_HELP_NAME, F_("ISNONTEXT:TRUE if @{value} is not text")},
        { GNM_FUNC_HELP_ARG, F_("value:a value")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ISNONTEXT(\"Gnumeric\")" },
        { GNM_FUNC_HELP_SEEALSO, "ISTEXT"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_isnontext (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (!VALUE_IS_STRING (argv[0]));
}

/***************************************************************************/

static GnmFuncHelp const help_isnumber[] = {
        { GNM_FUNC_HELP_NAME, F_("ISNUMBER:TRUE if @{value} is a number")},
        { GNM_FUNC_HELP_ARG, F_("value:a value")},
        { GNM_FUNC_HELP_DESCRIPTION, F_("This function checks if a value is a number.  Neither TRUE nor FALSE are numbers for this purpose.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ISNUMBER(\"Gnumeric\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=ISNUMBER(PI())" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_isnumber (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (argv[0] && VALUE_IS_FLOAT (argv[0]));
}

/***************************************************************************/

static GnmFuncHelp const help_isodd[] = {
        { GNM_FUNC_HELP_NAME, F_("ISODD:TRUE if @{n} is odd")},
        { GNM_FUNC_HELP_ARG, F_("n:number")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ISODD(3)" },
        { GNM_FUNC_HELP_SEEALSO, "ISEVEN"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_isodd (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float x = value_get_as_float (argv[0]);
	gnm_float r = gnm_fmod (gnm_abs (x), 2);

	/* If x is too big, this will always be false.  */
	return value_new_bool (r >= 1);
}

/***************************************************************************/

static GnmFuncHelp const help_isref[] = {
        { GNM_FUNC_HELP_NAME, F_("ISREF:TRUE if @{value} is a reference")},
        { GNM_FUNC_HELP_ARG, F_("value:a value")},
        { GNM_FUNC_HELP_DESCRIPTION, F_("This function checks if a value is a cell reference.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ISREF(A1)" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_isref (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	GnmValue *v;
	gboolean res;

	if (argc != 1)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	v = gnm_expr_eval (argv[0], ei->pos,
			   GNM_EXPR_EVAL_PERMIT_NON_SCALAR |
			   GNM_EXPR_EVAL_WANT_REF);
	res = VALUE_IS_CELLRANGE (v);
	value_release (v);

	return value_new_bool (res);
}

/***************************************************************************/

static GnmFuncHelp const help_istext[] = {
        { GNM_FUNC_HELP_NAME, F_("ISTEXT:TRUE if @{value} is text")},
        { GNM_FUNC_HELP_ARG, F_("value:a value")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=ISTEXT(\"Gnumeric\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=ISTEXT(34)" },
        { GNM_FUNC_HELP_SEEALSO, "ISNONTEXT"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_istext (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (VALUE_IS_STRING (argv[0]));
}

/***************************************************************************/

static GnmFuncHelp const help_n[] = {
        { GNM_FUNC_HELP_NAME, F_("N:@{text} converted to a number")},
        { GNM_FUNC_HELP_ARG, F_("text:string")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{text} contains non-numerical text, 0 is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=N(\"42\")" },
        { GNM_FUNC_HELP_EXAMPLES, F_("=N(\"eleven\")") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_n (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue *v;

	if (VALUE_IS_NUMBER (argv[0]))
		return value_new_float (value_get_as_float (argv[0]));

	if (!VALUE_IS_STRING (argv[0]))
		return value_new_error_NUM (ei->pos);

	v = format_match_number (value_peek_string (argv[0]),
				 NULL,
				 sheet_date_conv (ei->pos->sheet));
	if (v != NULL)
		return v;

	return value_new_float (0);
}

/***************************************************************************/

static GnmFuncHelp const help_type[] = {
        { GNM_FUNC_HELP_NAME, F_("TYPE:a number indicating the data type of @{value}")},
        { GNM_FUNC_HELP_ARG, F_("value:a value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("TYPE returns a number indicating the data type of @{value}:\n"
					"1  \t= number\n"
					"2  \t= text\n"
					"4  \t= boolean\n"
					"16 \t= error\n"
					"64 \t= array")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=TYPE(3)" },
        { GNM_FUNC_HELP_EXAMPLES, "=TYPE(\"Gnumeric\")" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_type (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue const *v = argv[0];
	switch (v ? v->v_any.type : VALUE_EMPTY) {
	case VALUE_BOOLEAN:
		return value_new_int (4);
	case VALUE_EMPTY:
	case VALUE_FLOAT:
		return value_new_int (1);
	case VALUE_CELLRANGE:
	case VALUE_ERROR:
		return value_new_int (16);
	case VALUE_STRING:
		return value_new_int (2);
	case VALUE_ARRAY:
		return value_new_int (64);
	default:
		break;
	}
	/* not reached */
	return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_getenv[] = {
        { GNM_FUNC_HELP_NAME, F_("GETENV:the value of execution environment variable @{name}")},
        { GNM_FUNC_HELP_ARG, F_("name:the name of the environment variable")},
	{ GNM_FUNC_HELP_NOTE, F_("If a variable called @{name} does not exist, #N/A will be returned.") },
	{ GNM_FUNC_HELP_NOTE, F_("Variable names are case sensitive.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=GETENV(\"HOME\")" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_getenv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *var = value_peek_string (argv[0]);
	char const *val = g_getenv (var);

	if (val && g_utf8_validate (val, -1, NULL))
		return value_new_string (val);
	else
		return value_new_error_NA (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_get_link[] = {
	{ GNM_FUNC_HELP_NAME, F_("GET.LINK:the target of the hyperlink attached to @{cell} as a string")},
	{ GNM_FUNC_HELP_ARG,  F_("cell:the referenced cell")},
	{ GNM_FUNC_HELP_NOTE, F_("The value return is not updated automatically when "
				 "the link attached to @{cell} changes but requires a"
				 " recalculation.")},
	{ GNM_FUNC_HELP_SEEALSO, "HYPERLINK"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_get_link (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue const * const v = argv[0];

	if (VALUE_IS_CELLRANGE (v)) {
		GnmCellRef const * a = &v->v_range.cell.a;
		GnmCellRef const * b = &v->v_range.cell.b;
		Sheet *sheet;
		GnmHLink *lnk;
		GnmCellPos pos;

		if (a->col != b->col || a->row != b->row || a->sheet !=b->sheet)
			return value_new_error_REF (ei->pos);

		sheet = (a->sheet == NULL) ? ei->pos->sheet : a->sheet;
		gnm_cellpos_init_cellref (&pos, a, &(ei->pos->eval), sheet);
		lnk = gnm_sheet_hlink_find (sheet, &pos);

		if (lnk)
			return value_new_string (gnm_hlink_get_target (lnk));
	}

	return value_new_empty ();
}

/***************************************************************************/

GnmFuncDescriptor const info_functions[] = {
	{ "cell",	"sr",  help_cell,
	  gnumeric_cell, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_SUBSET_WITH_EXTENSIONS, GNM_FUNC_TEST_STATUS_BASIC },
	{ "error.type",	"E",  help_error_type,
	  gnumeric_error_type, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "info",	"s",  help_info,
	  gnumeric_info, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isblank",	"E",  help_isblank,
	  gnumeric_isblank, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "iserr",	"E",    help_iserr,
	  gnumeric_iserr, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "iserror",	"E",    help_iserror,
	  gnumeric_iserror, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "iseven",	"f",  help_iseven,
	  gnumeric_iseven, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "islogical",	"E",  help_islogical,
	  gnumeric_islogical, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isna",	"E",    help_isna,
	  gnumeric_isna, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isnontext",	"E",  help_isnontext,
	  gnumeric_isnontext, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isnumber",	"E",  help_isnumber,
	  gnumeric_isnumber, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isodd",	"S",  help_isodd,
	  gnumeric_isodd, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isref",	NULL,  help_isref,
	  NULL, gnumeric_isref,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "istext",	"E",  help_istext,
	  gnumeric_istext, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "n",		"S",  help_n,
	  gnumeric_n, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "na",		"", help_na,
	  gnumeric_na, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "type",	"?",  help_type,
	  gnumeric_type, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

/* XL stores this in statistical ? */
        { "countblank",	"r",   help_countblank,
	  gnumeric_countblank, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "error",	"s",   help_error,
	  gnumeric_error, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

	{ "expression",	"r",    help_expression,
	  gnumeric_expression, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
/* XLM : looks common in charts */
	{ "get.formula", "r",    help_get_formula,
	  gnumeric_get_formula, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "get.link", "r",    help_get_link,
	  gnumeric_get_link, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "isformula", "r",    help_isformula,
	  gnumeric_isformula, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{ "getenv",	"s",  help_getenv,
	  gnumeric_getenv, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

        {NULL}
};
