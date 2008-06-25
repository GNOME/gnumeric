/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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
#include <str.h>
#include <ranges.h>
#include <sheet.h>
#include <workbook.h>
#include <gnm-format.h>
#include <style.h>
#include <style-font.h>
#include <value.h>
#include <expr.h>
#include <expr-impl.h>
#include <workbook.h>
#include <sheet-style.h>
#include <number-match.h>
#include <gnm-i18n.h>

#include <goffice/app/go-doc.h>
#include <goffice/app/go-plugin.h>
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=CELL\n"
	   "@SYNTAX=CELL(type,ref)\n"

	   "@DESCRIPTION="
	   "CELL returns information about the formatting, location, or "
	   "contents of a cell.\n"
	   "\n"
	   "@type specifies the type of information you want to obtain:\n\n"
	   "  address    \tReturns the given cell reference as text.\n"
	   "  col        \t\tReturns the number of the column in @ref.\n"
	   "  contents   \tReturns the contents of the cell in @ref.\n"
	   "  format     \t\tReturns the code of the format of the cell.\n"
	   "  parentheses\tReturns 1 if @ref contains a negative value\n"
	   "             \t\tand its format displays it with parentheses.\n"
	   "  row        \t\tReturns the number of the row in @ref.\n"
	   "  width      \t\tReturns the column width.\n"
	   "\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "Cell(\"format\",A1) returns the code of the format of the cell A1.\n"
	   "\n"
	   "@SEEALSO=INDIRECT")
	},
	{ GNM_FUNC_HELP_END }
};

typedef struct {
	char const *format;
	char const *output;
} translate_t;
static const translate_t translate_table[] = {
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

	if (format == NULL)
		return value_new_string ("G");

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

#warning "FIXME: CELL('format',...) isn't right"
	/*
	 * 1. The above lookup should be done with respect to just the
	 *    first of format alternatives.
	 * 2. I don't think colour should count.
	 * 3. We should add a dash if there are more alternatives.
	 */

	return value_new_string ("G");
}

/* TODO : turn this into a range based routine */
static GnmValue *
gnumeric_cell (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *info_type = value_peek_string (argv[0]);
	GnmCellRef const *ref = &argv [1]->v_range.cell.a;
	const Sheet *sheet = eval_sheet (ref->sheet, ei->pos->sheet);

	/*
	 * CELL translates it's keywords (ick)
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
			case HALIGN_GENERAL:
			case HALIGN_LEFT:
			case HALIGN_JUSTIFY:
			case HALIGN_DISTRIBUTED:
						return value_new_string ("'");
			case HALIGN_RIGHT:	return value_new_string ("\"");
			case HALIGN_CENTER_ACROSS_SELECTION:
			case HALIGN_CENTER:	return value_new_string ("^");
			case HALIGN_FILL:	return value_new_string ("\\");
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=EXPRESSION\n"
	   "@SYNTAX=EXPRESSION(cell)\n"
	   "@DESCRIPTION="
	   "EXPRESSION returns expression in @cell as a string, or "
	   "empty if the cell is not an expression.\n"
	   "@EXAMPLES=\n"
	   "entering '=EXPRESSION(A3)' in A2 = empty (assuming there is nothing in A3).\n"
	   "entering '=EXPRESSION(A2)' in A1 = 'EXPRESSION(A3)'.\n"
	   "\n"
	   "@SEEALSO=TEXT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_expression (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue const * const v = argv[0];
	if (v->type == VALUE_CELLRANGE) {
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=GET.FORMULA\n"
	   "@SYNTAX=GET.FORMULA(cell)\n"
	   "@DESCRIPTION="
	   "EXPRESSION returns expression in @cell as a string, or "
	   "empty if the cell is not an expression.\n"
	   "@EXAMPLES=\n"
	   "entering '=GET.FORMULA(A3)' in A2 = empty (assuming there is nothing in A3).\n"
	   "entering '=GET.FORMULA(A2)' in A1 = '=GET.FORMULA(A3)'.\n"
	   "\n"
	   "@SEEALSO=EXPRESSION")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_get_formula (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue const * const v = argv[0];
	if (v->type == VALUE_CELLRANGE) {
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

static GnmFuncHelp const help_countblank[] = {
	{ GNM_FUNC_HELP_OLD,
        F_("@FUNCTION=COUNTBLANK\n"
           "@SYNTAX=COUNTBLANK(range)\n"

           "@DESCRIPTION="
           "COUNTBLANK returns the number of blank cells in a @range.\n\n"
	   "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "COUNTBLANK(A1:A20) returns the number of blank cell in A1:A20.\n"
	   "\n"
           "@SEEALSO=COUNT")
	},
	{ GNM_FUNC_HELP_END }
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

	if (v->type == VALUE_CELLRANGE) {
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=INFO\n"
	   "@SYNTAX=INFO(type)\n"

	   "@DESCRIPTION="
	   "INFO returns information about the current operating environment. "
	   "\n\n"
	   "@type is the type of information you want to obtain:\n\n"
	   "  memavail \tReturns the amount of memory available, bytes.\n"
	   "  memused  \tReturns the amount of memory used (bytes).\n"
	   "  numfile  \t\tReturns the number of active worksheets.\n"
	   "  osversion\t\tReturns the operating system version.\n"
	   "  recalc   \t\tReturns the recalculation mode (automatic).\n"
	   "  release  \t\tReturns the version of Gnumeric as text.\n"
	   "  system   \t\tReturns the name of the environment.\n"
	   "  totmem   \t\tReturns the amount of total memory available.\n"
	   "\n"
	   "* This function is Excel compatible, except that types directory "
	   "and origin are not implemented.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "INFO(\"system\") returns \"Linux\" on a Linux system.\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
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
#ifdef HAVE_UNAME
	} else if (!g_ascii_strcasecmp (info_type, "osversion")) {
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
#else
#ifdef G_OS_WIN32
	} else if (!g_ascii_strcasecmp (info_type, "osversion")) {
		return value_new_string ("Windows (32-bit) NT 5.01");	/* fake XP */
#endif
#endif
	} else if (!g_ascii_strcasecmp (info_type, "recalc")) {
		/* Current recalculation mode; returns "Automatic" or "Manual".  */
		Workbook const *wb = ei->pos->sheet->workbook;
		return value_new_string (
			workbook_get_recalcmode (wb) ? _("Automatic") : _("Manual"));
	} else if (!g_ascii_strcasecmp (info_type, "release")) {
		/* Version of Gnumeric (Well, Microsoft Excel), as text.  */
		return value_new_string (GNM_VERSION_FULL);
#ifdef HAVE_UNAME
	} else if (!g_ascii_strcasecmp (info_type, "system")) {
		/* Name of the operating environment.  */
		struct utsname unamedata;

		if (uname (&unamedata) == -1)
			return value_new_error (ei->pos, _("Unknown system"));
		else
			return value_new_string (unamedata.sysname);
#else
#ifdef G_OS_WIN32
	} else if (!g_ascii_strcasecmp (info_type, "system")) {
		return value_new_string ("pcdos");	/* seems constant */
#endif
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISERROR\n"
	   "@SYNTAX=ISERROR(value)\n"

	   "@DESCRIPTION="
	   "ISERROR returns a TRUE value if the expression has an error.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISERROR(NA()) equals TRUE.\n"
	   "\n"
	   "@SEEALSO=ERROR")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_iserror (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (VALUE_IS_ERROR (argv[0]));
}

/***************************************************************************/

static GnmFuncHelp const help_isna[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISNA\n"
	   "@SYNTAX=ISNA(value)\n"

	   "@DESCRIPTION="
	   "ISNA returns TRUE if the value is the #N/A error value.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISNA(NA()) equals TRUE.\n"
	   "\n"
	   "@SEEALSO=NA")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISERR\n"
	   "@SYNTAX=ISERR(value)\n"

	   "@DESCRIPTION="
	   "ISERR returns TRUE if the value is any error value except #N/A.\n\n"
	   "* This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ISERR(NA()) return FALSE.\n"
	   "\n"
	   "@SEEALSO=ISERROR")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_iserr (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (VALUE_IS_ERROR (argv[0]) &&
			       value_error_classify (argv[0]) != GNM_ERROR_NA);
}

/***************************************************************************/

static GnmFuncHelp const help_error_type[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ERROR.TYPE\n"
	   "@SYNTAX=ERROR.TYPE(value)\n"

	   "@DESCRIPTION="
	   "ERROR.TYPE returns an error number corresponding to the given "
	   "error value.  The error numbers for error values are:\n\n"
	   "\t#DIV/0!  \t\t2\n"
	   "\t#VALUE!  \t3\n"
	   "\t#REF!    \t\t4\n"
	   "\t#NAME?   \t5\n"
	   "\t#NUM!    \t\t6\n"
	   "\t#N/A     \t\t7\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ERROR.TYPE(NA()) equals 7.\n"
	   "\n"
	   "@SEEALSO=ISERROR")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=NA\n"
	   "@SYNTAX=NA()\n"

	   "@DESCRIPTION="
	   "NA returns the error value #N/A.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "NA() equals #N/A error.\n"
	   "\n"
	   "@SEEALSO=ISNA")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_na (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_error_NA (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_error[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ERROR\n"
	   "@SYNTAX=ERROR(text)\n"

	   "@DESCRIPTION="
	   "ERROR return the specified error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ERROR(\"#OWN ERROR\").\n"
	   "\n"
	   "@SEEALSO=ISERROR")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_error (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_error (ei->pos, value_peek_string (argv[0]));
}

/***************************************************************************/

static GnmFuncHelp const help_isblank[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISBLANK\n"
	   "@SYNTAX=ISBLANK(value)\n"

	   "@DESCRIPTION="
	   "ISBLANK returns TRUE if the value is blank.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISBLANK(A1).\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_isblank (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (VALUE_IS_EMPTY (argv[0]));
}

/***************************************************************************/

static GnmFuncHelp const help_iseven[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISEVEN\n"
	   "@SYNTAX=ISEVEN(value)\n"

	   "@DESCRIPTION="
	   "ISEVEN returns TRUE if the number is even.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISEVEN(4) equals TRUE.\n"
	   "\n"
	   "@SEEALSO=ISODD")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISLOGICAL\n"
	   "@SYNTAX=ISLOGICAL(value)\n"

	   "@DESCRIPTION="
	   "ISLOGICAL returns TRUE if the value is a logical value.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISLOGICAL(A1).\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_islogical (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (VALUE_IS_BOOLEAN (argv[0]));
}

/***************************************************************************/

static GnmFuncHelp const help_isnontext[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISNONTEXT\n"
	   "@SYNTAX=ISNONTEXT(value)\n"

	   "@DESCRIPTION="
	   "ISNONTEXT Returns TRUE if the value is not text.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISNONTEXT(\"text\") equals FALSE.\n"
	   "\n"
	   "@SEEALSO=ISTEXT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_isnontext (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (!VALUE_IS_STRING (argv[0]));
}

/***************************************************************************/

static GnmFuncHelp const help_isnumber[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISNUMBER\n"
	   "@SYNTAX=ISNUMBER(value)\n"

	   "@DESCRIPTION="
	   "ISNUMBER returns TRUE if the value is a number.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISNUMBER(\"text\") equals FALSE.\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_isnumber (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (argv[0] && VALUE_IS_FLOAT (argv[0]));
}

/***************************************************************************/

static GnmFuncHelp const help_isodd[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISODD\n"
	   "@SYNTAX=ISODD(value)\n"

	   "@DESCRIPTION="
	   "ISODD returns TRUE if the number is odd.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISODD(3) equals TRUE.\n"
	   "\n"
	   "@SEEALSO=ISEVEN")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISREF\n"
	   "@SYNTAX=ISREF(value)\n"

	   "@DESCRIPTION="
	   "ISREF returns TRUE if the value is a reference.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISREF(A1) equals TRUE.\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_isref (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	if (argc != 1)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	return value_new_bool (GNM_EXPR_GET_OPER (argv[0]) == GNM_EXPR_OP_CELLREF);
}

/***************************************************************************/

static GnmFuncHelp const help_istext[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ISTEXT\n"
	   "@SYNTAX=ISTEXT(value)\n"

	   "@DESCRIPTION="
	   "ISTEXT returns TRUE if the value is text.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISTEXT(\"text\") equals TRUE.\n"
	   "\n"
	   "@SEEALSO=ISNONTEXT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_istext (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (VALUE_IS_STRING (argv[0]));
}

/***************************************************************************/

static GnmFuncHelp const help_n[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=N\n"
	   "@SYNTAX=N(value)\n"

	   "@DESCRIPTION="
	   "N returns a value converted to a number.  Strings containing "
	   "text are converted to the zero value.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "N(\"42\") equals 42.\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
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
				 workbook_date_conv (ei->pos->sheet->workbook));
	if (v != NULL)
		return v;

	return value_new_float (0);
}

/***************************************************************************/

static GnmFuncHelp const help_type[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=TYPE\n"
	   "@SYNTAX=TYPE(value)\n"

	   "@DESCRIPTION="
	   "TYPE returns a number indicating the data type of a value.\n\n"
	   "1  == number\n"
	   "2  == text\n"
	   "4  == boolean\n"
	   "16 == error\n"
	   "64 == array\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "TYPE(3) equals 1.\n"
	   "TYPE(\"text\") equals 2.\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_type (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue const *v = argv[0];
	switch (v ? v->type : VALUE_EMPTY) {
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=GETENV\n"
	   "@SYNTAX=GETENV(string)\n"

	   "@DESCRIPTION="
	   "GETENV retrieves a value from the execution environment.\n"
	   "\n"
	   "* If the variable specified by @string does not exist, #N/A! will "
	   "be returned.  Note, that variable names are case sensitive.\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_getenv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *var = value_peek_string (argv[0]);
	char const *val = getenv (var);

	if (val)
		return value_new_string (val);
	else
		return value_new_error_NA (ei->pos);
}

/***************************************************************************/

GnmFuncDescriptor const info_functions[] = {
	{ "cell",	"sr", N_("info_type, cell"), help_cell,
	  gnumeric_cell, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_SUBSET_WITH_EXTENSIONS, GNM_FUNC_TEST_STATUS_BASIC },
	{ "error.type",	"E", N_("value"), help_error_type,
	  gnumeric_error_type, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "info",	"s", N_("info_type"), help_info,
	  gnumeric_info, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isblank",	"E", N_("value"), help_isblank,
	  gnumeric_isblank, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "iserr",	"E",   N_("value"), help_iserr,
	  gnumeric_iserr, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "iserror",	"E",   N_("value"), help_iserror,
	  gnumeric_iserror, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "iseven",	"f", N_("value"), help_iseven,
	  gnumeric_iseven, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "islogical",	"E", N_("value"), help_islogical,
	  gnumeric_islogical, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isna",	"E",   N_("value"), help_isna,
	  gnumeric_isna, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isnontext",	"E", N_("value"), help_isnontext,
	  gnumeric_isnontext, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isnumber",	"E", N_("value"), help_isnumber,
	  gnumeric_isnumber, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isodd",	"S", N_("value"), help_isodd,
	  gnumeric_isodd, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isref",	NULL, N_("value"), help_isref,
	  NULL, gnumeric_isref, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "istext",	"E", N_("value"), help_istext,
	  gnumeric_istext, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "n",		"S", N_("value"), help_n,
	  gnumeric_n, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "na",		"",  "", help_na,
	  gnumeric_na, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "type",	"?", N_("value"), help_type,
	  gnumeric_type, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

/* XL stores this in statistical ? */
        { "countblank",	"r",  N_("range"), help_countblank,
	  gnumeric_countblank, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "error",	"s",  N_("text"), help_error,
	  gnumeric_error, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

	{ "expression",	"r",   N_("cell"), help_expression,
	  gnumeric_expression, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
/* XLM : looks common in charts */
	{ "get.formula", "r",   N_("cell"), help_get_formula,
	  gnumeric_get_formula, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

	{ "getenv",	"s", N_("string"), help_getenv,
	  gnumeric_getenv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },


        {NULL}
};
