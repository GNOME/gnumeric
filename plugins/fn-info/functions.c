/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-information.c:  Information built-in functions
 *
 * Authors:
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   Jody Goldberg (jody@gnome.org)
 *   Morten Welinder (terra@diku.dk)
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
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <cell.h>
#include <str.h>
#include <ranges.h>
#include <sheet.h>
#include <workbook.h>
#include <format.h>
#include <formats.h>
#include <style.h>
#include <value.h>
#include <expr.h>
#include <expr-impl.h>
#include <sheet-style.h>
#include <number-match.h>

#include <sys/utsname.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/***************************************************************************/

static const char *help_cell = {
	N_("@FUNCTION=CELL\n"
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
	   "             \t\tand it's format displays it with parentheses.\n"
	   "  row        \t\tReturns the number of the row in @ref.\n"
	   "  width      \t\tReturns the column width.\n"
	   "\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CEll(\"format\",A1) returns the code of the format of the cell A1.\n"
	   "\n"
	   "@SEEALSO=")
};

typedef struct {
	const char *format;
	const char *output;
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

static Value *
translate_cell_format (StyleFormat const *format)
{
	int i;
	char *fmt;
	const int translate_table_count = sizeof (translate_table) /
		sizeof(translate_t);

	if (format == NULL)
		return value_new_string ("G");

	fmt = style_format_as_XL (format, FALSE);

	/*
	 * TODO : What does this do in different locales ??
	 */
	for (i = 0; i < translate_table_count; i++) {
		const translate_t *t = &translate_table[i];

		if (!g_ascii_strcasecmp (fmt, t->format)) {
			g_free (fmt);
			return value_new_string (t->output);
		}
	}

	g_free (fmt);
	return value_new_string ("G");
}

static FormatCharacteristics
retrieve_format_info (Sheet *sheet, int col, int row)
{
	MStyle *mstyle = sheet_style_get (sheet, col, row);
	StyleFormat *format = mstyle_get_format (mstyle);
	FormatCharacteristics info;

	cell_format_classify (format, &info);

	return info;
}

/* TODO : turn this into a range based routine */
static Value *
gnumeric_cell (FunctionEvalInfo *ei, Value **argv)
{
	const char *info_type = value_peek_string (argv[0]);
	CellRef const *ref = &argv [1]->v_range.cell.a;

	/* from CELL - limited usefulness! */
	if (!g_ascii_strcasecmp(info_type, "address")) {
		ParsePos pp;
		char *ref_name = cellref_as_string (ref,
			parse_pos_init_evalpos (&pp, ei->pos), TRUE);
		return value_new_string_nocopy (ref_name);

	/* from later 123 versions - USEFUL! */
	} else if (!g_ascii_strcasecmp(info_type, "coord")) {
		ParsePos pp;
		CellRef tmp = *ref;
		char *ref_name;

		if (tmp.sheet == NULL)
			tmp.sheet = ei->pos->sheet;
		ref_name = cellref_as_string (&tmp,
			parse_pos_init_evalpos (&pp, ei->pos), FALSE);
		return value_new_string_nocopy (ref_name);

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
	 * Another place where Excel doesn't conform to it's documentation!
	 */
	} else if (!g_ascii_strcasecmp (info_type, "color")) {
		FormatCharacteristics info =
			retrieve_format_info (ei->pos->sheet, ref->col, ref->row);

		/* 0x01 = first bit (1) indicating negative colors */
		return (info.negative_fmt & 0x01) ? value_new_int (1) :
			value_new_int (0);

	/* absolutely pointless - compatibility only */
	} else if (!g_ascii_strcasecmp (info_type, "contents") ||
		   !g_ascii_strcasecmp (info_type, "value")) {
		Cell const *cell =
			sheet_cell_get (ei->pos->sheet, ref->col, ref->row);
		if (cell && cell->value)
			return value_duplicate (cell->value);
		return value_new_empty ();

	/* from CELL - limited usefulness!
	 * A testament to Microsoft's hypocracy! They could include this from
	 * 123R2.2 (it wasn't in 123R2.0x), modify it in Excel 4.0 to include
	 * the worksheet name, but they can't make any other changes to CELL?!
	 */
	} else if (!g_ascii_strcasecmp (info_type, "filename")) {
		char const *name = workbook_get_filename (ei->pos->sheet->workbook);

		if (name == NULL)
			return value_new_string ("");
		else
			return value_new_string (name);

	/* from CELL */
	/* Backwards compatibility w/123 - unnecessary */
	} else if (!g_ascii_strcasecmp (info_type, "format")) {
		MStyle const *mstyle =
			sheet_style_get (ei->pos->sheet, ref->col, ref->row);

		return translate_cell_format (mstyle_get_format (mstyle));

	/* from CELL */
	/* Backwards compatibility w/123 - unnecessary */
	} else if (!g_ascii_strcasecmp (info_type, "parentheses")) {
		FormatCharacteristics info =
			retrieve_format_info (ei->pos->sheet, ref->col, ref->row);

		/* 0x02 = second bit (2) indicating parentheses */
		return (info.negative_fmt & 0x02) ? value_new_int (1) :
			value_new_int (0);

	/* from CELL */
	/* Backwards compatibility w/123 - unnecessary */
	} else if (!g_ascii_strcasecmp (info_type, "prefix") ||
		   !g_ascii_strcasecmp (info_type, "prefixcharacter")) {
		MStyle const *mstyle =
			sheet_style_get (ei->pos->sheet, ref->col, ref->row);
		Cell const *cell =
			sheet_cell_get (ei->pos->sheet, ref->col, ref->row);

		if (cell && cell->value && cell->value->type == VALUE_STRING) {
			switch (mstyle_get_align_h (mstyle)) {
			case HALIGN_GENERAL: return value_new_string ("'");
			case HALIGN_LEFT:    return value_new_string ("'");
			case HALIGN_RIGHT:   return value_new_string ("\"");
			case HALIGN_CENTER:  return value_new_string ("^");
			case HALIGN_FILL:    return value_new_string ("\\");
			default : 	     return value_new_string ("");
			}
		}
		return value_new_string ("");

	/* from CELL */
	} else if (!g_ascii_strcasecmp (info_type, "locked") ||
		   !g_ascii_strcasecmp (info_type, "protect")) {
		MStyle const *mstyle =
			sheet_style_get (ei->pos->sheet, ref->col, ref->row);
		return value_new_int (mstyle_get_content_locked (mstyle) ? 1 : 0);

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
		Cell const *cell =
			sheet_cell_get (ei->pos->sheet, ref->col, ref->row);
		if (cell && cell->value) {
			if (cell->value->type == VALUE_STRING)
				return value_new_string ("l");
			else
				return value_new_string ("v");
		}
		return value_new_string ("b");

	/* from CELL */
	} else if (!g_ascii_strcasecmp (info_type, "width") ||
		   !g_ascii_strcasecmp (info_type, "columnwidth")) {
		ColRowInfo const *info =
			sheet_col_get_info (ei->pos->sheet, ref->col);
		double charwidth;
		int    cellwidth;

		charwidth = gnumeric_default_font->approx_width.pts.digit;
		cellwidth = info->size_pts;

		return value_new_int (rint (cellwidth / charwidth));
	}

	return value_new_error (ei->pos, gnumeric_err_VALUE);
}

#warning implement this
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

static const char *help_expression = {
	N_("@FUNCTION=EXPRESSION\n"
	   "@SYNTAX=EXPRESSION(cell)\n"
	   "@DESCRIPTION="
	   "EXPRESSION returns expression in @cell as a string, or "
	   "empty if the cell is not an expression.\n"
	   "@EXAMPLES=\n"
	   "in A1 EXPRESSION(A2) equals 'EXPRESSION(A3)'.\n"
	   "in A2 EXPRESSION(A3) equals empty.\n"
	   "\n"
	   "@SEEALSO=TEXT")
};

static Value *
gnumeric_expression (FunctionEvalInfo *ei, Value **args)
{
	Value const * const v = args[0];
	if (v->type == VALUE_CELLRANGE) {
		Cell *cell;
		CellRef const * a = &v->v_range.cell.a;
		CellRef const * b = &v->v_range.cell.b;

		if (a->col != b->col || a->row != b->row || a->sheet !=b->sheet)
			return value_new_error (ei->pos, gnumeric_err_REF);

		cell = sheet_cell_get (eval_sheet (a->sheet, ei->pos->sheet),
				       a->col, a->row);

		if (cell && cell_has_expr (cell)) {
			ParsePos pos;
			char * expr_string =
			    gnm_expr_as_string (cell->base.expression,
				parse_pos_init_cell (&pos, cell));
			return value_new_string_nocopy (expr_string);
		}
	}

	return value_new_empty ();
}


/***************************************************************************/

static const char *help_countblank = {
        N_("@FUNCTION=COUNTBLANK\n"
           "@SYNTAX=COUNTBLANK(range)\n"

           "@DESCRIPTION="
           "COUNTBLANK returns the number of blank cells in a @range.\n\n"
	   "* This function is Excel compatible.\n"
           "\n"
	   "@EXAMPLES=\n"
	   "COUNTBLANK(A1:A20) returns the number of blank cell in A1:A20.\n"
	   "\n"
           "@SEEALSO=COUNT")
};

static Value *
cb_countblank (Sheet *sheet, int col, int row,
	       Cell *cell, void *user_data)
{
	cell_eval (cell);
	if (!cell_is_blank (cell))
		*((int *)user_data) -= 1;
	return NULL;
}

static Value *
gnumeric_countblank (FunctionEvalInfo *ei, Value **args)
{
	Sheet *start_sheet, *end_sheet;
	Range r;
	int count;

	rangeref_normalize (&args[0]->v_range.cell, ei->pos,
		&start_sheet, &end_sheet, &r);
	count = range_width (&r) * range_height	(&r);
	if (start_sheet != end_sheet && end_sheet != NULL)
		count *= 1 + abs (end_sheet->index_in_wb - start_sheet->index_in_wb);
	workbook_foreach_cell_in_range (ei->pos, args[0],
		CELL_ITER_IGNORE_BLANK, &cb_countblank, &count);

	return value_new_int (count);
}

/***************************************************************************/

static const char *help_info = {
	N_("@FUNCTION=INFO\n"
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
};


static Value *
gnumeric_info (FunctionEvalInfo *ei, Value **argv)
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
	} else if (!g_ascii_strcasecmp (info_type, "recalc")) {
		/* Current recalculation mode; returns "Automatic" or "Manual".  */
		return value_new_string (_("Automatic"));
	} else if (!g_ascii_strcasecmp (info_type, "release")) {
		/* Version of Gnumeric (Well, Microsoft Excel), as text.  */
		return value_new_string (GNUMERIC_VERSION);
	} else if (!g_ascii_strcasecmp (info_type, "system")) {
		/* Name of the operating environment.  */
		struct utsname unamedata;

		if (uname (&unamedata) == -1)
			return value_new_error (ei->pos, _("Unknown system"));
		else
			return value_new_string (unamedata.sysname);
	} else if (!g_ascii_strcasecmp (info_type, "totmem")) {
		/* Total memory available, including memory already in use, in
		 * bytes.
		 */
		return value_new_int (16 << 20);  /* Good enough... */
	}

	return value_new_error (ei->pos, _("Unknown info_type"));
}

/***************************************************************************/

static const char *help_iserror = {
	N_("@FUNCTION=ISERROR\n"
	   "@SYNTAX=ISERROR(value)\n"

	   "@DESCRIPTION="
	   "ISERROR returns a TRUE value if the expression has an error.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISERROR(NA()) equals TRUE.\n"
	   "\n"
	   "@SEEALSO=ERROR")
};

static Value *
gnumeric_iserror (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_bool (argv[0]->type == VALUE_ERROR);
}

/***************************************************************************/

static const char *help_isna = {
	N_("@FUNCTION=ISNA\n"
	   "@SYNTAX=ISNA(value)\n"

	   "@DESCRIPTION="
	   "ISNA returns TRUE if the value is the #N/A error value.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISNA(NA()) equals TRUE.\n"
	   "\n"
	   "@SEEALSO=NA")
};

/*
 * We need to operator directly in the input expression in order to bypass
 * the error handling mechanism
 */
static Value *
gnumeric_isna (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_bool (argv[0]->type == VALUE_ERROR &&
			       !strcmp (gnumeric_err_NA, argv[0]->v_err.mesg->str));
}

/***************************************************************************/

static const char *help_iserr = {
	N_("@FUNCTION=ISERR\n"
	   "@SYNTAX=ISERR(value)\n"

	   "@DESCRIPTION="
	   "ISERR returns TRUE if the value is any error value except #N/A.\n\n"
	   "* This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ISERR(NA()) return FALSE.\n"
	   "\n"
	   "@SEEALSO=ISERROR")
};

static Value *
gnumeric_iserr (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_bool (argv[0]->type == VALUE_ERROR &&
			       strcmp (gnumeric_err_NA, argv[0]->v_err.mesg->str));
}

/***************************************************************************/

static const char *help_error_type = {
	N_("@FUNCTION=ERROR.TYPE\n"
	   "@SYNTAX=ERROR(value)\n"

	   "@DESCRIPTION="
	   "ERROR.TYPE returns an error number corresponding to the given "
	   "error value.  The error numbers for error values are:\n\n"
	   "\t#DIV/0!  \t\t2\n"
	   "\t#VALUE!  \t\t3\n"
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
};

static Value *
gnumeric_error_type (FunctionEvalInfo *ei, Value **argv)
{
	int retval = -1;
	char const * mesg;
	if (argv[0]->type != VALUE_ERROR)
		return value_new_error (ei->pos, gnumeric_err_NA);

	mesg = argv[0]->v_err.mesg->str;
	if (!strcmp (gnumeric_err_NULL, mesg))
		retval = 1;
	else if (!strcmp (gnumeric_err_DIV0, mesg))
		retval = 2;
	else if (!strcmp (gnumeric_err_VALUE, mesg))
		retval = 3;
	else if (!strcmp (gnumeric_err_REF, mesg))
		retval = 4;
	else if (!strcmp (gnumeric_err_NAME, mesg))
		retval = 5;
	else if (!strcmp (gnumeric_err_NUM, mesg))
		retval = 6;
	else if (!strcmp (gnumeric_err_NA, mesg))
		retval = 7;
	else
		return value_new_error (ei->pos, gnumeric_err_NA);

	return value_new_int (retval);
}

/***************************************************************************/

static const char *help_na = {
	N_("@FUNCTION=NA\n"
	   "@SYNTAX=NA()\n"

	   "@DESCRIPTION="
	   "NA returns the error value #N/A.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "NA() equals #N/A error.\n"
	   "\n"
	   "@SEEALSO=ISNA")
};

static Value *
gnumeric_na (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_error (ei->pos, gnumeric_err_NA);
}

/***************************************************************************/

static const char *help_error = {
	N_("@FUNCTION=ERROR\n"
	   "@SYNTAX=ERROR(text)\n"

	   "@DESCRIPTION="
	   "ERROR return the specified error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ERROR(\"#OWN ERROR\").\n"
	   "\n"
	   "@SEEALSO=ISERROR")
};

static Value *
gnumeric_error (FunctionEvalInfo *ei, Value *argv[])
{
	return value_new_error (ei->pos, value_peek_string (argv[0]));
}

/***************************************************************************/

static const char *help_isblank = {
	N_("@FUNCTION=ISBLANK\n"
	   "@SYNTAX=ISBLANK(value)\n"

	   "@DESCRIPTION="
	   "ISBLANK returns TRUE if the value is blank.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISBLANK(A1).\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_isblank (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	gboolean result = FALSE;
	GnmExpr const *expr;
	if (gnm_expr_list_length (expr_node_list) != 1)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	expr = expr_node_list->data;

	/* How can this happen ? */
	if (expr == NULL)
		return value_new_bool (FALSE);

	/* Handle pointless arrays */
	if (expr->any.oper == GNM_EXPR_OP_ARRAY) {
		if (expr->array.rows != 1 || expr->array.cols != 1)
			return value_new_bool (FALSE);
		expr = expr->array.corner.expr;
	}

	if (expr->any.oper == GNM_EXPR_OP_CELLREF) {
		CellRef const *ref = &expr->cellref.ref;
		Sheet const *sheet = eval_sheet (ref->sheet, ei->pos->sheet);
		CellPos pos;

		cellref_get_abs_pos (ref, &ei->pos->eval, &pos);
		result = cell_is_blank (sheet_cell_get (sheet, pos.col,
							pos.row));
	}
	return value_new_bool (result);
}

/***************************************************************************/

static const char *help_iseven = {
	N_("@FUNCTION=ISEVEN\n"
	   "@SYNTAX=ISEVEN(value)\n"

	   "@DESCRIPTION="
	   "ISEVEN returns TRUE if the number is even.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISEVEN(4) equals TRUE.\n"
	   "\n"
	   "@SEEALSO=ISODD")
};

static Value *
gnumeric_iseven (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_bool (!(value_get_as_int (argv[0]) & 1));
}

/***************************************************************************/

static const char *help_islogical = {
	N_("@FUNCTION=ISLOGICAL\n"
	   "@SYNTAX=ISLOGICAL(value)\n"

	   "@DESCRIPTION="
	   "ISLOGICAL returns TRUE if the value is a logical value.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISLOGICAL(A1).\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_islogical (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_bool (argv[0]->type == VALUE_BOOLEAN);
}

/***************************************************************************/

static const char *help_isnontext = {
	N_("@FUNCTION=ISNONTEXT\n"
	   "@SYNTAX=ISNONTEXT(value)\n"

	   "@DESCRIPTION="
	   "ISNONTEXT Returns TRUE if the value is not text.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISNONTEXT(\"text\") equals FALSE.\n"
	   "\n"
	   "@SEEALSO=ISTEXT")
};

static Value *
gnumeric_isnontext (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_bool (argv[0]->type != VALUE_STRING);
}

/***************************************************************************/

static const char *help_isnumber = {
	N_("@FUNCTION=ISNUMBER\n"
	   "@SYNTAX=ISNUMBER(value)\n"

	   "@DESCRIPTION="
	   "ISNUMBER returns TRUE if the value is a number.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISNUMBER(\"text\") equals FALSE.\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_isnumber (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_bool (argv[0]->type == VALUE_INTEGER ||
			       argv[0]->type == VALUE_FLOAT);
}

/***************************************************************************/

static const char *help_isodd = {
	N_("@FUNCTION=ISODD\n"
	   "@SYNTAX=ISODD(value)\n"

	   "@DESCRIPTION="
	   "ISODD returns TRUE if the number is odd.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISODD(3) equals TRUE.\n"
	   "\n"
	   "@SEEALSO=ISEVEN")
};

static Value *
gnumeric_isodd (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_bool (value_get_as_int (argv[0]) & 1);
}

/***************************************************************************/

static const char *help_isref = {
	N_("@FUNCTION=ISREF\n"
	   "@SYNTAX=ISREF(value)\n"

	   "@DESCRIPTION="
	   "ISREF returns TRUE if the value is a reference.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISREF(A1) equals TRUE.\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_isref (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	GnmExpr *t;

	if (gnm_expr_list_length (expr_node_list) != 1)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	t = expr_node_list->data;
	if (!t)
		return NULL;

	return value_new_bool (t->any.oper == GNM_EXPR_OP_CELLREF);
}

/***************************************************************************/

static const char *help_istext = {
	N_("@FUNCTION=ISTEXT\n"
	   "@SYNTAX=ISTEXT(value)\n"

	   "@DESCRIPTION="
	   "ISTEXT returns TRUE if the value is text.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "ISTEXT(\"text\") equals TRUE.\n"
	   "\n"
	   "@SEEALSO=ISNONTEXT")
};

static Value *
gnumeric_istext (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_bool (argv[0]->type == VALUE_STRING);
}

/***************************************************************************/

static const char *help_n = {
	N_("@FUNCTION=N\n"
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
};

static Value *
gnumeric_n (FunctionEvalInfo *ei, Value **argv)
{
	const char *str;
	Value *v;

	if (argv[0]->type == VALUE_BOOLEAN)
		return value_new_int (value_get_as_int(argv[0]));

	if (VALUE_IS_NUMBER (argv[0]))
		return value_duplicate (argv[0]);

	if (argv[0]->type != VALUE_STRING)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	str = value_peek_string (argv[0]);
	v = format_match_number (str, NULL);
	if (v != NULL)
		return v;
	return value_new_float (0);
}

/***************************************************************************/

static const char *help_type = {
	N_("@FUNCTION=TYPE\n"
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
};

static Value *
gnumeric_type (FunctionEvalInfo *ei, Value **argv)
{
	switch (argv[0]->type) {
	/* case VALUE_EMPTY : not possible, S arguments convert this to int(0)
	 * This is XL compatible, although I don't really agree with it
	 */
	case VALUE_BOOLEAN:
		return value_new_int (4);
	case VALUE_INTEGER:
	case VALUE_FLOAT:
		return value_new_int (1);
	case VALUE_ERROR:
		return value_new_int (16);
	case VALUE_STRING:
		return value_new_int (2);
	/* case VALUE_CELLRANGE: S argument handles this */
#warning FIXME : S arguments will filter arrays
	case VALUE_ARRAY:
		return value_new_int (64);
	default:
		break;
	}
	/* not reached */
	return value_new_error (ei->pos, gnumeric_err_VALUE);
}

/***************************************************************************/

static const char *help_getenv = {
	N_("@FUNCTION=GETENV\n"
	   "@SYNTAX=GETENV(string)\n"

	   "@DESCRIPTION="
	   "GETENV retrieves a value from the execution environment.\n"
	   "\n"
	   "* If the variable specified by @STRING does not exist, #N/A! will "
	   "be returned.  Note, that variable names are case sensitive.\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_getenv (FunctionEvalInfo *ei, Value **argv)
{
	const char *var = value_peek_string (argv[0]);
	const char *val = getenv (var);

	if (val)
		return value_new_string (val);
	else
		return value_new_error (ei->pos, gnumeric_err_NA);
}

/***************************************************************************/

const GnmFuncDescriptor info_functions[] = {
	{ "cell",	"sr", N_("info_type, cell"), &help_cell,
	  gnumeric_cell, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_SUBSET_WITH_EXTENSIONS, GNM_FUNC_TEST_STATUS_BASIC },
	{ "error.type",	"E", N_("value"), &help_error_type,
	  gnumeric_error_type, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "info",	"s", N_("info_type"), &help_info,
	  gnumeric_info, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isblank",	NULL, N_("value"), &help_isblank,
	  NULL, gnumeric_isblank, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "iserr",	"E",   N_("value"), &help_iserr,
	  gnumeric_iserr, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "iserror",	"E",   N_("value"), &help_iserror,
	  gnumeric_iserror, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "iseven",	"f", N_("value"), &help_iseven,
	  gnumeric_iseven, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "islogical",	"E", N_("value"), &help_islogical,
	  gnumeric_islogical, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isna",	"E",   N_("value"), &help_isna,
	  gnumeric_isna, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isnontext",	"E", N_("value"), &help_isnontext,
	  gnumeric_isnontext, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isnumber",	"E", N_("value"), &help_isnumber,
	  gnumeric_isnumber, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isodd",	"S", N_("value"), &help_isodd,
	  gnumeric_isodd, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "isref",	NULL, N_("value"), &help_isref,
	  NULL, gnumeric_isref, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "istext",	"E", N_("value"), &help_istext,
	  gnumeric_istext, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "n",		"S", N_("value"), &help_n,
	  gnumeric_n, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "na",		"",  "", &help_na,
	  gnumeric_na, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "type",	"E", N_("value"), &help_type,
	  gnumeric_type, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

/* XL stores this in statistical ? */
        { "countblank",	"r",  N_("range"), &help_countblank,
	  gnumeric_countblank, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "error",	"s",  N_("text"), &help_error,
	  gnumeric_error, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "expression",	"r",   N_("cell"), &help_expression,
	  gnumeric_expression, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "getenv",	"s", N_("string"), &help_getenv,
	  gnumeric_getenv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

        {NULL}
};
