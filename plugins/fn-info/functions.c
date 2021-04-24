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
#if 0
	{ "General", "G" },
	{ "0", "F0" },
	{ "#,##0", ",0" },
	{ "0.00", "F2" },
	{ "#,##0.00", ",2" },
	{ "\"$\"#,##0_);\\(\"$\"#,##0\\)", "C0" },
	{ "\"$\"#,##0_);[Red]\\(\"$\"#,##0\\)", "C0-" },
	{ "\"$\"#,##0.00_);\\(\"$\"#,##0.00\\)", "C2" },
	{ "\"$\"#,##0.00_);[Red]\\(\"$\"#,##0.00\\)", "C2-" },
	{ "0%", "P0" },
	{ "0.00%", "P2" },
	{ "0.00e+00", "S2" },
	{ "# ?/?", "G" },
	{ "# ?" "?/?" "?", "G" },   /* Don't accidentally use trigraphs here. */
#endif
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
		return value_new_int (gnm_style_get_contents_locked (mstyle) ? 1 : 0);

    /* different characteristics grouped for efficiency
     * TYPE needed for backward compatibility w/123 but otherwise useless
     * DATATYPE and FORMULATYPE are options in later 123 versions' @CELL
     * no need for them but included to make 123 conversion easier
    Case "datatype", "formulatype", "type"
        t = Left(prop, 1)

            rv = IIf( t = "f" And rng.HasFormula, "f", "" )

            If rng.formula = "" Then
                rv = rv & "b"
            ElseIf IsNumeric("0" & CStr(rng.Value)) _
              Or (t = "t" And IsError(rng.Value)) Then
                rv = rv & "v"
            ElseIf rng.Value = CVErr(xlErrNA) Then
                rv = rv & "n"
            ElseIf IsError(rng.Value) Then
                rv = rv & "e"
            Else
                rv = rv & "l"
            End If
        End If
	*/

	} else if (!g_ascii_strcasecmp (info_type, "type") ||
		   !g_ascii_strcasecmp (info_type, "datatype") ||
		   !g_ascii_strcasecmp (info_type, "formulatype")) {
		GnmCell const *cell =
			sheet_cell_get (sheet, ref->col, ref->row);
		if (cell && cell->value) {
			if (VALUE_IS_STRING (cell->value))
				return value_new_string ("l");
			else
				return value_new_string ("v");
		}
		return value_new_string ("b");

	/* from CELL */
	} else if (!g_ascii_strcasecmp (info_type, "width") ||
		   !g_ascii_strcasecmp (info_type, "columnwidth")) {
		ColRowInfo const *info =
			sheet_col_get_info (sheet, ref->col);
		double charwidth;
		int    cellwidth;

		charwidth = gnm_font_default_width;
		cellwidth = info->size_pts;

		return value_new_int (rint (cellwidth / charwidth));
	}

	return value_new_error_VALUE (ei->pos);
}

#if 0
/*
*extension to CELL providing 123 @CELL/@CELLPOINTER functionality as
*well as access to most Range properties
*1st arg determines the property of characteristic being sought
*2nd arg [OPTIONAL] specifies cell reference - AcitveCell if missing
*3rd arg [OPTIONAL] specifies whether to return an array or not
*    True = return array result for .Areas(1)
*    False/missing = return scalar result for .Areas(1).Cells(1, 1)
*/
Function ExtCell( _
  prop As String, _
  Optional rng As Variant, _
  Optional rar As Boolean = False _
) As Variant
    Dim ws As Worksheet, wb As Workbook, rv As Variant
    Dim i As Long, j As Long, m As Long, n As Long, t As String

    Application.Volatile True

    If TypeOf rng Is Range Then
        If rar Then
            Set rng = rng.Areas(1)
        Else
            Set rng = rng.Areas(1).Cells(1, 1)
        End If
    ElseIf IsMissing(rng) Then
        Set rng = ActiveCell
    Else
        ExtCell = CVErr(xlErrRef)
        Exit Function
    End If

    prop = LCase(prop)

    m = rng.rows.Count
    n = rng.Columns.Count
    rv = rng.Value

    Set ws = rng.Worksheet
    Set wb = ws.Parent

    Select Case prop

    Case "across"  /* from later 123 versions - limited usefulness! */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = CLng( _
                      rng.Cells(i, j).HorizontalAlignment = _
                      xlHAlignCenterAcrossSelection _
                    )
                Next j
            Next i
        Else
            rv = CLng( _
              rng.HorizontalAlignment = _
              xlHAlignCenterAcrossSelection _
            )
        End If

    Case "backgroundcolor"  /* from later 123 versions - USEFUL! */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).Interior.ColorIndex
                Next j
            Next i
        Else
            rv = rng.Interior.ColorIndex
        End If

    Case "bold"  /* from later 123 versions - USEFUL! */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = CLng(rng.Cells(i, j).Font.Bold)
                Next j
            Next i
        Else
            rv = CLng(rng.Font.Bold)
        End If


    Case "bottomborder"  /* from later 123 versions - USEFUL! */
    /* Note: many possible return values! wrap inside SIGN to test T/F */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = _
                      rng.Cells(i, j).Borders(xlEdgeBottom).LineStyle - _
                      xlLineStyleNone
                Next j
            Next i
        Else
            rv = rng.Borders(xlEdgeBottom).LineStyle - xlLineStyleNone
        End If

    Case "bottombordercolor"  /* from later 123 versions - USEFUL! */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = _
                      rng.Cells(i, j).Borders(xlEdgeBottom).ColorIndex
                Next j
            Next i
        Else
            rv = rng.Borders(xlEdgeBottom).ColorIndex
        End If

    Case "columnhidden"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).EntireColumn.Hidden
                Next j
            Next i
        Else
            rv = rng.EntireColumn.Hidden
        End If

    Case "comment"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    If Not rng.Cells(i, j).Comment Is Nothing Then
                        rv(i, j) = rng.Cells(i, j).Comment.text
                    Else
                        rv(i, j) = ""
                    End If
                Next j
            Next i
        Else
            If Not rng.Comment Is Nothing Then
                rv = rng.Comment.text
            Else
                rv = ""
            End If
        End If

    Case "currentarray"  /* NOTE: returns Range addresses! */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).CurrentArray.Address
                Next j
            Next i
        Else
            rv = rng.CurrentArray.Address
        End If

    Case "currentregion"  /* NOTE: returns Range addresses! */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).CurrentRegion.Address
                Next j
            Next i
        Else
            rv = rng.CurrentRegion.Address
        End If

    Case "filedate"  /* from later 123 versions - limited usefulness! */
        t = wb.BuiltinDocumentProperties("Last Save Time")  /* invariant! */

        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = t
                Next j
            Next i
        Else
            rv = t
        End If

    Case "fontface", "fontname", "typeface"  /* from later 123 versions */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).Font.Name
                Next j
            Next i
        Else
            rv = rng.Font.Name
        End If

    Case "fontsize", "pitch", "typesize"  /* from later 123 versions */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).Font.Size
                Next j
            Next i
        Else
            rv = rng.Font.Size
        End If

    Case "formula"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).formula
                Next j
            Next i
        Else
            rv = rng.formula
        End If

    Case "formulaarray"  /* questionable usefulness */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).FormulaArray
                Next j
            Next i
        Else
            rv = rng.FormulaArray
        End If

    Case "formulahidden"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = CLng(rng.Cells(i, j).FormulaHidden)
                Next j
            Next i
        Else
            rv = CLng(rng.FormulaHidden)
        End If

    Case "formulalocal"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).FormulaLocal
                Next j
            Next i
        Else
            rv = rng.FormulaLocal
        End If

    Case "formular1c1"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).FormulaR1C1
                Next j
            Next i
        Else
            rv = rng.FormulaR1C1
        End If

    Case "formular1c1local"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).FormulaR1C1Local
                Next j
            Next i
        Else
            rv = rng.FormulaR1C1Local
        End If

    Case "halign", "horizontalalignment"  /* from later 123 versions */
    /* Note: different return values than 123. 0 = general alignment */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = _
                      rng.Cells(i, j).HorizontalAlignment - _
                      xlHAlignGeneral
                Next j
            Next i
        Else
            rv = rng.HorizontalAlignment - xlHAlignGeneral
        End If

    Case "hasarray"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = CLng(rng.Cells(i, j).HasArray)
                Next j
            Next i
        Else
            rv = CLng(rng.HasArray)
        End If

    Case "hasformula"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = CLng(rng.Cells(i, j).HasFormula)
                Next j
            Next i
        Else
            rv = CLng(rng.HasFormula)
        End If

    Case "hashyperlink", "hashyperlinks"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = CLng(rng.Cells(i, j).Hyperlinks.Count > 0)
                Next j
            Next i
        Else
            rv = CLng(rng.Hyperlinks.Count > 0)
        End If

    Case "height", "rowheight"  /* from later 123 versions - USEFUL! */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).Height
                Next j
            Next i
        Else
            rv = rng.Height
        End If

    Case "hidden"  /* see ColumnHidden and RowHidden - this is less useful */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = CLng(rng.Cells(i, j).Hidden)
                Next j
            Next i
        Else
            rv = CLng(rng.Hidden)
        End If

    Case "hyperlinkaddress"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).Hyperlinks(1).Address
                Next j
            Next i
        Else
            rv = rng.Hyperlinks(1).Address
        End If

    Case "indentlevel"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).rng.IndentLevel
                Next j
            Next i
        Else
            rv = rng.rng.IndentLevel
        End If

    Case "italic"  /* from later 123 versions - USEFUL! */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = CLng(rng.Cells(i, j).Font.Italic)
                Next j
            Next i
        Else
            rv = CLng(rng.Font.Italic)
        End If

    Case "left"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).Left
                Next j
            Next i
        Else
            rv = rng.Left
        End If

    Case "leftborder"  /* from later 123 versions */
    /* Note: many possible return values! wrap inside SIGN to test T/F */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = _
                      rng.Cells(i, j).Borders(xlEdgeLeft).LineStyle - _
                      xlLineStyleNone
                Next j
            Next i
        Else
            rv = rng.Borders(xlEdgeLeft).LineStyle - xlLineStyleNone
        End If

    Case "leftbordercolor"  /* from later 123 versions */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = _
                      rng.Cells(i, j).Borders(xlEdgeLeft).ColorIndex
                Next j
            Next i
        Else
            rv = rng.Borders(xlEdgeLeft).ColorIndex
        End If

    Case "mergearea"  /* NOTE: returns Range addresses! */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).MergeArea.Address
                Next j
            Next i
        Else
            rv = rng.MergeArea.Address
        End If

    Case "mergecells"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = CLng(rng.Cells(i, j).MergeCells)
                Next j
            Next i
        Else
            rv = CLng(rng.MergeCells)
        End If

    Case "name"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).Name
                Next j
            Next i
        Else
            rv = rng.Name
        End If

    Case "numberformat"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).NumberFormat
                Next j
            Next i
        Else
            rv = rng.NumberFormat
        End If

    Case "numberformatlocal"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).NumberFormatLocal
                Next j
            Next i
        Else
            rv = rng.NumberFormatLocal
        End If

    Case "orientation", "rotation"  /* from later 123 versions */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).Orientation
                Next j
            Next i
        Else
            rv = rng.Orientation
        End If

    Case "pattern"  /* from later 123 versions */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = _
                      rng.Cells(i, j).Interior.Pattern - _
                      xlPatternNone
                Next j
            Next i
        Else
            rv = rng.Interior.Pattern - xlPatternNone
        End If

    Case "patterncolor"  /* from later 123 versions */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = _
                      rng.Cells(i, j).Interior.PatternColorIndex
                Next j
            Next i
        Else
            rv = rng.Interior.PatternColorIndex
        End If

    Case "rightborder"  /* from later 123 versions */
    /* Note: many possible return values! wrap inside SIGN to test T/F */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = _
                      rng.Cells(i, j).Borders(xlEdgeRight).LineStyle - _
                      xlLineStyleNone
                Next j
            Next i
        Else
            rv = rng.Borders(xlEdgeRight).LineStyle - xlLineStyleNone
        End If

    Case "rightbordercolor"  /* from later 123 versions */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = _
                      rng.Cells(i, j).Borders(xlEdgeRight).ColorIndex
                Next j
            Next i
        Else
            rv = rng.Borders(xlEdgeRight).ColorIndex
        End If

    Case "rowhidden"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = CLng(rng.Cells(i, j).EntireRow.Hidden)
                Next j
            Next i
        Else
            rv = CLng(rng.EntireRow.Hidden)
        End If

    Case "scrollarea"
    /* Who needs consistency?! Why doesn't this return a Range object? */
        t = ws.ScrollArea  /* invariant! */

        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = t
                Next j
            Next i
        Else
            rv = t
        End If

    Case "sheet", "worksheet"  /* from later 123 versions - USEFUL! */
        t = ws.Index  /* invariant! */

        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = t
                Next j
            Next i
        Else
            rv = t
        End If

    Case "sheetname", "worksheetname"  /* from later 123 versions - USEFUL! */
        t = ws.Name  /* invariant */

        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = t
                Next j
            Next i
        Else
            rv = t
        End If

    Case "sheetcount", "sheetscount", "worksheetcount", "worksheetscount"
        t = wb.Worksheets.Count  /* invariant */

        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = t
                Next j
            Next i
        Else
            rv = t
        End If

    Case "shrinktofit"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = CLng(rng.Cells(i, j).ShrinkToFit)
                Next j
            Next i
        Else
            rv = CLng(rng.ShrinkToFit)
        End If

    Case "stylename"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).Style.Name
                Next j
            Next i
        Else
            rv = rng.Style.Name
        End If

    Case "text"  /* USEFUL! */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).text
                Next j
            Next i
        Else
            rv = rng.text
        End If

    Case "textcolor"  /* from later 123 versions - USEFUL! */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).Font.ColorIndex
                Next j
            Next i
        Else
            rv = rng.Font.ColorIndex
        End If

    Case "top"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = rng.Cells(i, j).Top
                Next j
            Next i
        Else
            rv = rng.Top
        End If

    Case "topborder"  /* from later 123 versions */
    /* Note: many possible return values! wrap inside SIGN to test T/F */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = _
                      rng.Cells(i, j).Borders(xlEdgeTop).LineStyle - _
                      xlLineStyleNone
                Next j
            Next i
        Else
            rv = rng.Borders(xlEdgeTop).LineStyle - xlLineStyleNone
        End If

    Case "topbordercolor"  /* from later 123 versions */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = _
                      rng.Cells(i, j).Borders(xlEdgeTop).ColorIndex
                Next j
            Next i
        Else
            rv = rng.Borders(xlEdgeTop).ColorIndex
        End If

    Case "underline"  /* from later 123 versions - USEFUL! */
    /* Note: many possible return values! wrap inside SIGN to test T/F */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = _
                      rng.Cells(i, j).Font.Underline - _
                      xlUnderlineStyleNone
                Next j
            Next i
        Else
            rv = rng.Font.Underline - xlUnderlineStyleNone
        End If

    Case "usedrange"  /* NOTE: returns Range addresses! */
        t = ws.UsedRange.Address  /* invariant */

        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = t
                Next j
            Next i
        Else
            rv = t
        End If

    Case "usestandardheight"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = CLng(rng.Cells(i, j).UseStandardHeight)
                Next j
            Next i
        Else
            rv = CLng(rng.UseStandardHeight)
        End If

    Case "usestandardwidth"
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = CLng(rng.Cells(i, j).UseStandardWidth)
                Next j
            Next i
        Else
            rv = CLng(rng.UseStandardWidth)
        End If

    Case "valign", "verticalalignment"  /* from later 123 versions */
    /* Note: different return values than 123. 0 = Bottom-aligned */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = _
                      rng.Cells(i, j).VerticalAlignment - _
                      xlVAlignBottom
                Next j
            Next i
        Else
            rv = rng.VerticalAlignment - xlVAlignBottom
        End If

    Case "visible", "sheetvisible", "worksheetvisible"
        t = CLng(ws.Visible)  /* invariant */

        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = t
                Next j
            Next i
        Else
            rv = t
        End If

    Case "workbookfullname"  /* same as FileName in later 123 versions */
        t = wb.FullName  /* invariant */

        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = t
                Next j
            Next i
        Else
            rv = t
        End If

    Case "workbookname"
        t = wb.Name  /* invariant */

        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = t
                Next j
            Next i
        Else
            rv = t
        End If

    Case "workbookpath"
        t = wb.path  /* invariant */

        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = t
                Next j
            Next i
        Else
            rv = t
        End If

    Case "wrap", "wraptext"  /* from later 123 versions */
        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = CLng(rng.Cells(i, j).WrapText)
                Next j
            Next i
        Else
            rv = CLng(rng.WrapText)
        End If

    Case Else  /* invalid property/characteristic */
        t = CVErr(xlErrValue)  /* invariant */

        If rar Then
            For i = 1 To m
                For j = 1 To n
                    rv(i, j) = t
                Next j
            Next i
        Else
            rv = t
        End If

    End Select

    ExtCell = rv
End Function
#endif

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
