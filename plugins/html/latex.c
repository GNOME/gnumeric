/*
 * latex.c
 *
 * Copyright (C) 1999, 2000 Rasca, Berlin
 * EMail: thron@gmx.de
 *
 * Copyright (C) 2001 Adrian Custer, Berkeley
 * email: acuster@nature.berkeley.edu
 *
 * Copyright (C) 2001 Andreas J. Guelzow, Edmonton
 * email: aguelzow@taliesin.ca
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


/*
 * This file contains the LaTeX2e plugin functions.
 *
 *
 * The LaTeX2e function is named:
 * 		latex_file_save()
 *
 */


#include <gnumeric-config.h>
#include <gnumeric.h>
#include "latex.h"
#include <plugin-util.h>
#include <io-context.h>
#include <error-info.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-merge.h>
#include <style.h>
#include <style-color.h>
#include <font.h>
#include <cell.h>
#include <formats.h>
#include <style-border.h>
#include <sheet-style.h>
#include <parse-util.h>
#include <cellspan.h>
#include <rendered-value.h>

#include <errno.h>
#include <stdio.h>
#include <locale.h>

typedef enum {
	LATEX_NO_BORDER = 0,
	LATEX_SINGLE_BORDER = 1,
	LATEX_DOUBLE_BORDER = 2,
	LATEX_MAX_BORDER
} latex_border_t;

typedef struct {
	latex_border_t latex;
	char const     *vertical;
	char const     *horizontal;
} latex_border_translator_t;

/* the index into the following array is StyleBorderType */
static latex_border_translator_t const border_styles[] = {
	{LATEX_NO_BORDER,     "",  "~"},
	{LATEX_SINGLE_BORDER, "|", "-"},
	{LATEX_SINGLE_BORDER, "|", "-"},
	{LATEX_SINGLE_BORDER, "|", "-"},
	{LATEX_SINGLE_BORDER, "|", "-"},
	{LATEX_DOUBLE_BORDER, "||","="},
	{LATEX_DOUBLE_BORDER, "||","="},
	{LATEX_SINGLE_BORDER, "|", "-"},
	{LATEX_SINGLE_BORDER, "|", "-"},
	{LATEX_SINGLE_BORDER, "|", "-"},
	{LATEX_SINGLE_BORDER, "|", "-"},
	{LATEX_SINGLE_BORDER, "|", "-"},
	{LATEX_SINGLE_BORDER, "|", "-"},
	{LATEX_NO_BORDER,     "",  ""}
};

typedef struct {
	char const     *p_1;
	char const     *p_2;
} latex_border_connectors_t;


static latex_border_connectors_t const conn_styles[LATEX_MAX_BORDER]
[LATEX_MAX_BORDER][LATEX_MAX_BORDER][LATEX_MAX_BORDER] = {
        /*  FIXME: once we are sure that none of the numbered */
        /*entries are in fact needed we should removed the digits */
	{{{{"",""}, { "",""}, { "",""}},
	  {{"",""}, { "|",""}, { "32",""}},
	  {{"",""}, { "11",""}, { "","|t:"}}},
	 {{{"",""}, { "",""}, { "",""}},
	  {{"","|"}, { "|",""}, { "33",""}},
	  {{"1",""}, { "13",""}, { "34",""}}},
	 {{{"",""}, { "",""}, { "|","|"}},
	  {{"","|b|"}, { "14",""}, { "|","|"}},
	  {{"","|b:"}, { "|b:",""}, { "|",":"}}}},
	{{{{"",""}, { "",""}, { "",""}},
	  {{"",""}, { "|",""}, { "35",""}},
	  {{"","|"}, { "17",""}, { "36",""}}},
	 {{{"","|"}, { "|",""}, { "37",""}},
	  {{"|",""}, { "",""}, { "38",""}},
	  {{"4",""}, { "19",""}, { "39",""}}},
	 {{{"","|b|"}, { "20",""}, { "|","|"}},
	  {{"5",""}, { "21",""}, { "|","|"}},
	  {{"|b:",""}, { "22",""}, { "40",""}}}},
	{{{{"",""}, { "23",""}, { ":t|",""}},
	  {{"|",""}, { "24",""}, { ":t|",""}},
	  {{"",""}, { "",""}, { ":t:",""}}},
	 {{{"7",""}, { "26",""}, { ":t|",""}},
	  {{"8",""}, { "27",""}, { "43",""}},
	  {{"",""}, { "",""}, { ":t:",""}}},
	 {{{":b|",""}, { "29",""}, { ":","|"}},
	  {{":b|",""}, { "30",""}, { ":|",""}},
	  {{":b:",""}, { "31",""}, { ":",":"}}}}
};


/**
 * latex_fputs :
 *
 * @p : a pointer to a char, start of the string to be processed.
 * @fp : a file pointer where the processed characters are written.
 *
 * This escapes any special LaTeX characters from the LaTeX engine. Re-ordered
 * from Rasca's code to have most common first.
 */
static void
latex_fputs (char const *p, FILE *fp)
{
	for (; *p; p++) {
		switch (*p) {

			/* These are the classic TeX symbols $ & % # _ { } (see Lamport, p.15) */
			case '$': case '&': case '%': case '#':
			case '_': case '{': case '}':
				fprintf (fp, "\\%c", *p);
				break;
			/* These are the other special characters ~ ^ \ (see Lamport, p.15) */
			case '^': case '~':
				fprintf (fp, "\\%c{ }", *p);
				break;
			case '\\':
				fputs ("$\\backslash$", fp);
				break;
			/* Are these available only in LaTeX through mathmode? */
			case '>': case '<':
				fprintf (fp, "$%c$", *p);
				break;

			default:
				fputc (*p, fp);
				break;
		}
	}
}

/**
 * latex_math_fputs :
 *
 * @p : a pointer to a char, start of the string to be processed.
 * @fp : a file pointer where the processed characters are written.
 *
 * This escapes any special LaTeX characters from the LaTeX engine.
 * 
 * We assume that htis will be set in Mathematics mode.
 */
static void
latex_math_fputs (char const *p, FILE *fp)
{
	for (; *p; p++) {
		switch (*p) {

			/* These are the classic TeX symbols $ & % # (see Lamport, p.15) */
			case '$': case '&': case '%': case '#':
				fprintf (fp, "\\%c", *p);
				break;
			/* These are the other special characters ~ (see Lamport, p.15) */
			case '~':
				fprintf (fp, "\\%c{ }", *p);
				break;
			case '\\':
				fputs ("\\backslash", fp);
				break;

			default:
				fputc (*p, fp);
				break;
		}
	}
}


/**
 * latex2e_write_file_header:
 *
 * @fp : a file pointer where the cell contents will be written.
 *
 * This ouputs the LaTeX header. Kept separate for esthetics.
 */

static void
latex2e_write_file_header(FILE *fp)
{
		fputs(
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
"%%                                                                  %%\n"
"%%  This is the header of a LaTeX2e file exported from Gnumeric.    %%\n"
"%%                                                                  %%\n"
"%%  This file can be compiled as it stands or included in another   %%\n"
"%%  LaTeX document. The table is based on the longtable package so  %%\n"
"%%  the longtable options (headers, footers...) can be set in the   %%\n"
"%%  preamble section below (see PRAMBLE).                           %%\n"
"%%                                                                  %%\n"
"%%  To include the file in another, the following two lines must be %%\n"
"%%  in the including file:                                          %%\n"
"%%        \\def\\inputGnumericTable{}                                 %%\n"
"%%  at the begining of the file and:                                %%\n"
"%%        \\input{name-of-this-file.tex}                             %%\n"
"%%  where the table is to be placed. Note also that the including   %%\n"
"%%  file must use the following packages for the table to be        %%\n"
"%%  rendered correctly:                                             %%\n"
"%%    \\usepackage[latin1]{inputenc}                                 %%\n"
"%%    \\usepackage{color}                                            %%\n"
"%%    \\usepackage{array}                                            %%\n"
"%%    \\usepackage{longtable}                                        %%\n"
"%%    \\usepackage{calc}                                             %%\n"
"%%    \\usepackage{multirow}                                         %%\n"
"%%    \\usepackage{hhline}                                           %%\n"
"%%    \\usepackage{ifthen}                                           %%\n"
"%%  optionally (for landscape tables embedded in another document): %%\n"
"%%    \\usepackage{lscape}                                           %%\n"
"%%                                                                  %%\n"
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
"\n"
"\n"
"\n"
"%%  This section checks if we are begin input into another file or  %%\n"
"%%  the file will be compiled alone. First use a macro taken from   %%\n"
"%%  the TeXbook ex 7.7 (suggestion of Han-Wen Nienhuys).            %%\n"
"\\def\\ifundefined#1{\\expandafter\\ifx\\csname#1\\endcsname\\relax}\n"
"\n"
"\n"
"%%  Check for the \\def token for inputed files. If it is not        %%\n"
"%%  defined, the file will be processed as a standalone and the     %%\n"
"%%  preamble will be used.                                          %%\n"
"\\ifundefined{inputGnumericTable}\n"
"\n"
"%%  We must be able to close or not the document at the end.        %%\n"
"	\\def\\gnumericTableEnd{\\end{document}}\n"
"\n"
"\n"
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
"%%                                                                  %%\n"
"%%  This is the PREAMBLE. Change these values to get the right      %%\n"
"%%  paper size and other niceties. Uncomment the landscape option   %%\n"
"%%  to the documentclass defintion for standalone documents.        %%\n"
"%%                                                                  %%\n"
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
"\n"
"	\\documentclass[12pt%\n"
"	                  %,landscape%\n"
"                    ]{report}\n"
"	\\usepackage[latin1]{inputenc}\n"
"	\\usepackage{color}\n"
"        \\usepackage{array}\n"
"	\\usepackage{longtable}\n"
"        \\usepackage{calc}\n"
"        \\usepackage{multirow}\n"
"        \\usepackage{hhline}\n"
"        \\usepackage{ifthen}\n"
"\n"
"	\\begin{document}\n"
"\n"
"\n"
"%%  End of the preamble for the standalone. The next section is for %%\n"
"%%  documents which are included into other LaTeX2e files.          %%\n"
"\\else\n"
"\n"
"%%  We are not a stand alone document. For a regular table, we will %%\n"
"%%  have no preamble and only define the closing to mean nothing.   %%\n"
"    \\def\\gnumericTableEnd{}\n"
"\n"
"%%  If we want landscape mode in an embedded document, comment out  %%\n"
"%%  the line above and uncomment the two below. The table will      %%\n"
"%%  begin on a new page and run in landscape mode.                  %%\n"
"%       \\def\\gnumericTableEnd{\\end{landscape}}\n"
"%       \\begin{landscape}\n"
"\n"
"\n"
"%%  End of the else clause for this file being \\input.              %%\n"
"\\fi\n"
"\n"
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
"%%                                                                  %%\n"
"%%  The rest is the gnumeric table, except for the closing          %%\n"
"%%  statement. Changes below will alter the table\'s appearance.     %%\n"
"%%                                                                  %%\n"
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
"\n"
"\\providecommand{\\gnumericmathit}[1]{#1} \n" 
"%%  Uncomment the next line if you would like your numbers to be in %%\n"
"%%  italics if they are italizised in the gnumeric table.           %%\n"
"%\\renewcommand{\\gnumericmathit}[1]{\\mathit{#1}}\n"
"\\providecommand{\\gnumericPB}[1]%\n"
"{\\let\\gnumericTemp=\\\\#1\\let\\\\=\\gnumericTemp\\hspace{0pt}}\n"
" \\ifundefined{gnumericTableWidthDefined}\n"
"        \\newlength{\\gnumericTableWidth}\n"
"        \\newlength{\\gnumericTableWidthComplete}\n"
"        \\global\\def\\gnumericTableWidthDefined{}\n"
" \\fi\n"
"\n"
"%%  The default table format retains the relative column widths of  %%\n"
"%%  gnumeric. They can easily be changed to c, r or l. In that case %%\n"
"%%  you may want to comment out the next line and uncomment the one %%\n"
"%%  thereafter                                                      %%\n"
"\\providecommand\\gnumbox{\\makebox[0pt]}\n"
"%%\\providecommand\\gnumbox[1][]{\\makebox}\n"
"\n"
"%% to adjust positions in multirow situations                       %%\n"
"\\setlength{\\bigstrutjot}{\\jot}\n"
"\\setlength{\\extrarowheight}{\\doublerulesep}\n"
"\n"
"%%  The \\setlongtables command keeps column widths the same across  %%\n"
"%%  pages. Simply comment out next line for varying column widths.  %%\n"
"\\setlongtables\n"
"\n"
,fp);
}


/**
 * latex2e_write_table_header:
 *
 * @fp : a file pointer where the cell contents will be written.
 * @num_cols: The number of columns in the table
 *
 * A convenience function that also helps make nicer code.
 */
static void
latex2e_write_table_header(FILE *fp, int num_cols)
{
	int col;


	fputs (
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
"%%  The longtable options. (Caption, headers... see Goosens, p.124) %%\n"
"%\t\\caption{The Table Caption.}             \\\\	%\n"
"% \\hline	% Across the top of the table.\n"
"%%  The rest of these options are table rows which are placed on    %%\n"
"%%  the first, last or every page. Use \\multicolumn if you want.    %%\n"
"\n"
"%%  Header for the first page.                                      %%\n"
,fp);

	fprintf (fp, "%%\t\\multicolumn{%d}{c}{The First Header} \\\\ \\hline \n", num_cols);
	fprintf (fp, "%%\t\\multicolumn{1}{c}{colTag}\t%%Column 1\n");
	for (col = 1 ; col < num_cols; col++)
		fprintf (fp, "%%\t&\\multicolumn{1}{c}{colTag}\t%%Column %d\n",col);
	fprintf (fp, "%%\t&\\multicolumn{1}{c}{colTag}\t\\\\ \\hline %%Last column\n");
	fprintf (fp, "%%\t\\endfirsthead\n\n");

	fprintf (fp, "%%%%  The running header definition.                                  %%%%\n");
	fprintf (fp, "%%\t\\hline\n");
	fprintf (fp, "%%\t\\multicolumn{%d}{l}{\\ldots\\small\\slshape continued} \\\\ \\hline\n", num_cols);
	fprintf (fp, "%%\t\\multicolumn{1}{c}{colTag}\t%%Column 1\n");
	for (col = 1 ; col < num_cols; col++)
				fprintf (fp, "%%\t&\\multicolumn{1}{c}{colTag}\t%%Column %d\n",col);
	fprintf (fp, "%%\t&\\multicolumn{1}{c}{colTag}\t\\\\ \\hline %%Last column\n");
	fprintf (fp, "%%\t\\endhead\n\n");

	fprintf (fp, "%%%%  The running footer definition.                                  %%%%\n");
	fprintf (fp, "%%\t\\hline\n");
	fprintf (fp, "%%\t\\multicolumn{%d}{r}{\\small\\slshape continued\\ldots}", num_cols);
	fprintf (fp, " \\\\\n");
	fprintf (fp, "%%\t\\endfoot\n\n");

	fprintf (fp, "%%%%  The ending footer definition.                                   %%%%\n");
	fprintf (fp, "%%\t\\multicolumn{%d}{c}{That's all folks} \\\\ \\hline \n", num_cols);
	fprintf (fp, "%%\t\\endlastfoot\n");
	fputs ("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n\n",fp);

}

/**
 * latex2e_find_vline:
 *
 * @col:
 * @row:
 * @sheet:
 * @which_border: MStyleElementType (MSTYLE_BORDER_LEFT or MSTYLE_BORDER_RIGHT)
  *
 * Determine the border style
 *
 */
static StyleBorderType
latex2e_find_vline (int col, int row, Sheet *sheet, MStyleElementType which_border)
{
	StyleBorder	   *border;
	MStyle             *mstyle;

	if (col < 0 || row < 0)
		return STYLE_BORDER_NONE;

	mstyle = sheet_style_get (sheet, col, row);
	border = mstyle_get_border (mstyle, which_border);

	if (!(style_border_is_blank (border) ||
	      border->line_type == STYLE_BORDER_NONE))
		return border->line_type;

	if (which_border == MSTYLE_BORDER_LEFT) {
		if (col < 1)
			return STYLE_BORDER_NONE;
		mstyle = sheet_style_get (sheet, col - 1, row);
		border = mstyle_get_border (mstyle, MSTYLE_BORDER_RIGHT);
		return ((style_border_is_blank (border)) ? STYLE_BORDER_NONE :
			border->line_type);
	} else {
		mstyle = sheet_style_get (sheet, col + 1, row);
		border = mstyle_get_border (mstyle, MSTYLE_BORDER_LEFT);
		return ((style_border_is_blank (border)) ? STYLE_BORDER_NONE :
			border->line_type);
	}

	return STYLE_BORDER_NONE;
}

/**
 * latex2e_print_vert_border:
 *
 * @fp : a file pointer where the cell contents will be written.
 * @clines: StyleBorderType indicating the type of border
 *
 */
static void
latex2e_print_vert_border (FILE *fp, StyleBorderType style)
{
	fprintf (fp, border_styles[style].vertical);
}

/**
 * latex2e_write_blank_cell:
 *
 * @fp : a file pointer where the cell contents will be written.
 * @col :
 * @row :
 * @first_column :
 * @sheet : the current sheet.
 *
 * This function creates all the LaTeX code for the cell of a table (i.e. all
 * the code that might fall between two ampersands (&)), assuming that
 * the cell is in fact NULL. We therefore have only to worry about a few
 * formatting issues.
 *
 */
static void
latex2e_write_blank_cell (FILE *fp, gint col, gint row, gint index,
			  StyleBorderType *borders, Sheet *sheet)
{
	CellPos pos;
	StyleBorderType left_border = STYLE_BORDER_NONE;
	StyleBorderType right_border = STYLE_BORDER_NONE;

	pos.col = col;
	pos.row = row;

	if (index == 0) {
		left_border = *borders;
	}
	right_border = borders[index + 1];

	if (left_border == STYLE_BORDER_NONE &&  right_border == STYLE_BORDER_NONE)
		fprintf (fp, "\n");
	else {
		/* Open the multicolumn statement. */
		fprintf (fp, "\\multicolumn{1}{");

		if (left_border != STYLE_BORDER_NONE)
			latex2e_print_vert_border (fp, left_border);

		/* Drop in the left hand format delimiter. */
		fprintf (fp, "c");

		if (right_border != STYLE_BORDER_NONE)
			latex2e_print_vert_border (fp, right_border);

		/*Close the right delimiter, as above. Also add the empty text delimiters.*/
		fprintf (fp,"}{}%%\n");
	}
}


/**
 * latex2e_write_multicolumn_cell:
 *
 * @fp : a file pointer where the cell contents will be written.
 * @cell : the cell whose contents are to be written.
 * @num_merged_cols : an integer value of the number of columns to merge.
 * @num_merged_rows : an integer value of the number of rows to merge.
 * @sheet : the current sheet.
 *
 * This function creates all the LaTeX code for the cell of a table (i.e. all
 * the code that might fall between two ampersands (&)).
 *
 * Note: we are _not_ putting single cell into \multicolumns since this
 * makes it much more difficult to change column widths later on.
 */
static void
latex2e_write_multicolumn_cell (FILE *fp, Cell const *cell, int num_merged_cols,
				int num_merged_rows,  gint index,
				StyleBorderType *borders, Sheet *sheet)
{
	char * rendered_string;
	StyleColor *textColor;
	gushort r,g,b;
	gboolean wrap = FALSE;
	FormatCharacteristics cell_format_characteristic;
	FormatFamily cell_format_family;
	int merge_width = 0;
	StyleBorderType left_border = STYLE_BORDER_NONE;
	StyleBorderType right_border = STYLE_BORDER_NONE;

	/* Print the cell according to its style. */
	MStyle *mstyle = cell_get_mstyle (cell);
	gboolean hidden = mstyle_get_content_hidden (mstyle);

	g_return_if_fail (mstyle != NULL);

	if (num_merged_cols > 1 || num_merged_rows > 1) {
		ColRowInfo const * ci;
		int i;

		for (i = 0; i < num_merged_cols; i++) {
			ci = sheet_col_get_info (sheet, cell->pos.col + i);
			merge_width += ci->size_pixels;
		}
	}

	if (index == 0) {
		left_border = *borders;
	}
	right_border = borders[index + num_merged_cols];

	/* We only set up a multicolumn command if necessary */
	if (num_merged_cols > 1) {
		int i;

		/* Open the multicolumn statement. */
		fprintf (fp, "\\multicolumn{%d}{", num_merged_cols);

		if (left_border != STYLE_BORDER_NONE)
			latex2e_print_vert_border (fp, left_border);

		if (num_merged_rows > 1) {
			fprintf (fp, "c");
		} else {
			fprintf (fp, "p{");
			for (i = 0; i < num_merged_cols; i++) {
				fprintf (fp, "\t\\gnumericCol%s+%%\n",
					 col_name (cell->pos.col + i));
			}
			fprintf (fp, "\t\\tabcolsep*2*%i}", num_merged_cols - 1);
		}

		if (right_border != STYLE_BORDER_NONE)
			latex2e_print_vert_border (fp, right_border);

		/*Close the right delimiter, as above. Also open the text delimiter.*/
		fprintf (fp,"}%%\n\t{");
	} else if (left_border != STYLE_BORDER_NONE || right_border != STYLE_BORDER_NONE) {

		/* Open the multicolumn statement. */
		fprintf (fp, "\\multicolumn{1}{");

		if (left_border != STYLE_BORDER_NONE)
			latex2e_print_vert_border (fp, left_border);

		/* Drop in the left hand format delimiter. */
		fprintf (fp, "p{\\gnumericCol%s}", col_name(cell->pos.col));

		if (right_border != STYLE_BORDER_NONE)
			latex2e_print_vert_border (fp, right_border);

		/*Close the right delimiter, as above. Also open the text delimiter.*/
		fprintf (fp,"}%%\n\t{");

	}



	if (num_merged_rows > 1) {
		int i;
		/* Open the multirow statement. */
		fprintf (fp, "\\multirow{%d}[%i]*{\\begin{tabular}{p{",
			 num_merged_rows, num_merged_rows/2);
		for (i = 0; i < num_merged_cols; i++) {
			fprintf (fp, "\t\\gnumericCol%s+%%\n", col_name (cell->pos.col + i));
		}
		if (num_merged_cols > 2)
			fprintf (fp, "\t\\tabcolsep*2*%i}}", num_merged_cols - 2);
		else
			fprintf (fp, "\t0pt}}");
	}


	/* Send the alignment of the cell through a routine to deal with
	 * HALIGN_GENERAL and then deal with the three cases. */
	switch ( style_default_halign(mstyle, cell) ) {
	case HALIGN_RIGHT:
		fprintf (fp, "\\gnumericPB{\\raggedleft}");
		break;
	case HALIGN_CENTER:
	case HALIGN_CENTER_ACROSS_SELECTION:
		fprintf (fp, "\\gnumericPB{\\centering}");
		break;
	case HALIGN_LEFT:
		fprintf (fp, "\\gnumericPB{\\raggedright}");
		break;
	case HALIGN_JUSTIFY:
		break;
	default:
		break;
	}

        /* Check whether we should do word wrapping */
	wrap = mstyle_get_wrap_text (mstyle);

	/* if we don't wrap put it into an mbox, adjusted to width 0 to avoid moving */
	/* it to the second line of the parbox */
	if (!wrap)
		switch ( style_default_halign(mstyle, cell) ) {
		case HALIGN_RIGHT:
			fprintf (fp, "\\gnumbox[r]{");
			break;
		case HALIGN_CENTER:
		case HALIGN_CENTER_ACROSS_SELECTION:
			fprintf (fp, "\\gnumbox{");
			break;
		case HALIGN_LEFT:
			fprintf (fp, "\\gnumbox[l]{");
			break;
		case HALIGN_JUSTIFY:
			fprintf (fp, "\\gnumbox[s]{");
			break;
		default:
			fprintf (fp, "\\makebox{");
			break;
		}

	if (!cell_is_blank (cell)) {
                /* Check the foreground (text) colour. */
		textColor = cell_get_render_color (cell);
		if (textColor == NULL && mstyle_is_element_set (mstyle, MSTYLE_COLOR_FORE))
			textColor = mstyle_get_color (mstyle, MSTYLE_COLOR_FORE);
		if (textColor == NULL)
			r = g = b = 0;
		else {
			r = textColor->red;
			g = textColor->green;
			b = textColor->blue;
		}
		if (r != 0 || g != 0 || b != 0) {
			char *locale;
			locale = setlocale (LC_NUMERIC, "C");
			fprintf (fp, "{\\color[rgb]{%.2f,%.2f,%.2f} ",
				 (double)r/65535, (double)g/65535, (double)b/65535);
			locale = setlocale (LC_NUMERIC, locale);
		}

		/* Establish the font's style for the styles that can be addressed by LaTeX.
		 * More complicated efforts (like changing fonts) are left to the user.
      		 */

		if (hidden)
		  fprintf (fp, "\\phantom{");

		if (font_is_monospaced (mstyle))
			fprintf (fp, "\\texttt{");
		else if (font_is_sansserif (mstyle))
			fprintf (fp, "\\textsf{");
		if (mstyle_get_font_bold (mstyle))
			fprintf (fp, "\\textbf{");
		if (mstyle_get_font_italic (mstyle))
			fprintf (fp, "\\textit{");


		cell_format_family = cell_format_classify (cell_get_format (cell),
							   &cell_format_characteristic);
		if (cell_format_family == FMT_NUMBER || cell_format_family == FMT_CURRENCY ||
		    cell_format_family == FMT_PERCENT || cell_format_family == FMT_FRACTION ||
		    cell_format_family == FMT_SCIENCE){
			fprintf (fp, "$");
		        if (mstyle_get_font_italic(mstyle))
			    fprintf (fp, "\\gnumericmathit{");

			/* Print the cell contents. */
			rendered_string = cell_get_rendered_text (cell);
			latex_math_fputs (rendered_string, fp);
			g_free (rendered_string);

		        if (mstyle_get_font_italic(mstyle))
			    fprintf (fp, "}");
			fprintf (fp, "$");
		} else {
			/* Print the cell contents. */
			rendered_string = cell_get_rendered_text (cell);
			latex_fputs (rendered_string, fp);
			g_free (rendered_string);
		}
		
		/* Close the styles for the cell. */
		if (mstyle_get_font_italic (mstyle))
			fprintf (fp, "}");
		if (mstyle_get_font_bold (mstyle))
			fprintf (fp, "}");
		if (font_is_monospaced (mstyle))
			fprintf (fp, "}");
		else if (font_is_sansserif (mstyle))
			fprintf (fp, "}");
		if (hidden)
		  fprintf (fp, "}");
		if (r != 0 || g != 0 || b != 0)
			fprintf (fp, "}");
	}

	/* if we don't wrap close the mbox */
	if (!wrap)
		fprintf (fp, "}");

	/* Close the multirowtext. */
	if (num_merged_rows > 1)
		fprintf(fp, "\\end{tabular}}");

	/* Close the multicolumn text bracket. */
	if (num_merged_cols > 1 || left_border != STYLE_BORDER_NONE
	    || right_border != STYLE_BORDER_NONE)
		fprintf(fp, "}");

	/* And we are done. */
	fprintf (fp, "\n");

}

/**
 * latex2e_find_hhlines :
 *
 * @clines:  array of StyleBorderType* indicating the type of border
 * @length:  (remaining) positions in clines
 * @col :
 * @row :
 * @sheet :
 *
 * Determine the border style
 *
 */

static gboolean
latex2e_find_hhlines (StyleBorderType *clines, int length, int col, int row,
		      Sheet *sheet, MStyleElementType type)
{
	MStyle *mstyle;
	StyleBorder	   *border;
 	Range const *merge_range;
	CellPos pos;

	mstyle = sheet_style_get (sheet, col, row);
	border = mstyle_get_border (mstyle, type);
	if (style_border_is_blank (border))
		return FALSE;
	clines[0] = border->line_type;
	pos.col = col;
	pos.row = row;
	merge_range = sheet_merge_is_corner (sheet, &pos);
	if (merge_range != NULL) {
		int i;

		for (i = 1; i < MIN (merge_range->end.col - merge_range->start.col + 1,
				    length); i++)
			     clines[i] = border->line_type;
	}
	return TRUE;
}


/**
 * latex2e_print_hhline :
 *
 * @fp : a file pointer where the cell contents will be written.
 * @clines: an array of StyleBorderType* indicating the type of border
 * @n : the number of elements in clines
 *
 * This procedure prints an hhline command according to the content
 * of clines.
 *
 */
static void
latex2e_print_hhline (FILE *fp, StyleBorderType *clines, int n, StyleBorderType *prev_vert,
		      StyleBorderType *next_vert)
{
	int col;
	fprintf (fp, "\\hhline{");
	fprintf (fp, conn_styles[LATEX_NO_BORDER]
		 [prev_vert ? border_styles[prev_vert[0]].latex : LATEX_NO_BORDER]
		 [border_styles[clines[0]].latex]
		 [next_vert ? border_styles[next_vert[0]].latex : LATEX_NO_BORDER].p_1);
	fprintf (fp, conn_styles[LATEX_NO_BORDER]
		 [prev_vert ? border_styles[prev_vert[0]].latex : LATEX_NO_BORDER]
		 [border_styles[clines[0]].latex]
		 [next_vert ? border_styles[next_vert[0]].latex : LATEX_NO_BORDER].p_2);
	for (col = 0; col < n - 1; col++) {
		fprintf (fp, border_styles[clines[col]].horizontal);
		fprintf (fp, conn_styles[border_styles[clines[col]].latex]
			 [prev_vert ? border_styles[prev_vert[col + 1]].latex :
			  LATEX_NO_BORDER]
			 [border_styles[clines[col+1]].latex]
			 [next_vert ? border_styles[next_vert[col + 1]].latex :
			  LATEX_NO_BORDER].p_1);
		fprintf (fp, conn_styles[border_styles[clines[col]].latex]
			 [prev_vert ? border_styles[prev_vert[col + 1]].latex :
			  LATEX_NO_BORDER]
			 [border_styles[clines[col+1]].latex]
			 [next_vert ? border_styles[next_vert[col + 1]].latex :
			  LATEX_NO_BORDER].p_2);
	}
	fprintf (fp, border_styles[clines[n - 1]].horizontal);
	fprintf (fp, conn_styles[border_styles[clines[n - 1]].latex]
		 [prev_vert ? border_styles[prev_vert[n]].latex : LATEX_NO_BORDER]
		 [LATEX_NO_BORDER]
		 [next_vert ? border_styles[next_vert[n]].latex :
		  LATEX_NO_BORDER].p_1);
	fprintf (fp, conn_styles[border_styles[clines[n - 1]].latex]
		 [prev_vert ? border_styles[prev_vert[n]].latex : LATEX_NO_BORDER]
		 [LATEX_NO_BORDER]
		 [next_vert ? border_styles[next_vert[n]].latex :
		  LATEX_NO_BORDER].p_2);

	fprintf (fp, "}\n");
}

/**
 * latex_file_save :  The LaTeX2e exporter plugin function.
 *
 * @FileSaver :        New structure for file plugins. I don't understand.
 * @IOcontext :        currently not used but reserved for the future.
 * @WorkbookView :     this provides the way to access the sheet being exported.
 * @filename :         where we'll write.
 *
 * This writes the top sheet of a Gnumeric workbook to a LaTeX2e longtable. We
 * check for merges here, then call the function latex2e_write_multicolum_cell()
 * to render the format and contents of the cell.
 */
void
latex_file_save (GnumFileSaver const *fs, IOContext *io_context,
                   WorkbookView *wb_view, gchar const *file_name)
{
	FILE *fp;
	Cell *cell;
	Sheet *current_sheet;
	Range total_range;
 	Range const *merge_range;
	int row, col, num_cols, length;
	int num_merged_cols, num_merged_rows;
	Workbook *wb = wb_view_workbook (wb_view);
	ErrorInfo *open_error;
	StyleBorderType *clines, *this_clines;
	StyleBorderType *prev_vert = NULL, *next_vert = NULL, *this_vert;
	gboolean needs_hline;


	/* Sanity check */
	g_return_if_fail (wb != NULL);
	g_return_if_fail (file_name != NULL);

	/* Get a file pointer from the os. */
	fp = gnumeric_fopen_error_info (file_name, "w", &open_error);
	if (fp == NULL) {
		gnumeric_io_error_info_set (io_context, open_error);
		return;
	}


	/* This is the preamble of the LaTeX2e file. */
	latex2e_write_file_header(fp);

	/* Get the topmost sheet and its range from the plugin function argument. */
	current_sheet = wb_view_cur_sheet(wb_view);
	total_range = sheet_get_extent (current_sheet, TRUE);

	num_cols = total_range.end.col - total_range.start.col + 1;

	fprintf (fp, "\\setlength\\gnumericTableWidth{%%\n");
	for (col = total_range.start.col; col <=  total_range.end.col; col++) {
		ColRowInfo const * ci;
		ci = sheet_col_get_info (current_sheet, col);
		fprintf (fp, "\t%ipt+%%\n", ci->size_pixels * 10 / 12);
	}
	fprintf (fp, "0pt}\n\\def\\gumericNumCols{%i}\n", num_cols);

	fputs (""
"\\setlength\\gnumericTableWidthComplete{\\gnumericTableWidth+%\n"
"         \\tabcolsep*\\gumericNumCols*2+\\arrayrulewidth*\\gumericNumCols}\n"
"\\ifthenelse{\\lengthtest{\\gnumericTableWidthComplete > \\textwidth}}%\n"
"         {\\def\\gnumericScale{\\ratio{\\textwidth-%\n"
"                        \\tabcolsep*\\gumericNumCols*2-%\n"
"                        \\arrayrulewidth*\\gumericNumCols}%\n"
"{\\gnumericTableWidth}}}%\n"
"{\\def\\gnumericScale{1}}\n"
"\n"
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
"%%                                                                  %%\n"
"%% The following are the widths of the various columns. We are      %%\n"
"%% defining them here because then they are easier to change.       %%\n"
"%% Depending on the cell formats we may use them more than once.    %%\n"
"%%                                                                  %%\n"
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
"\n"
, fp);

	for (col = total_range.start.col; col <=  total_range.end.col; col++) {
		ColRowInfo const * ci;
		ci = sheet_col_get_info (current_sheet, col);
		fprintf (fp, "\\def\\gnumericCol%s{%ipt*\\gnumericScale}\n", col_name (col),
			 ci->size_pixels * 10 / 12);
	}

	/* Start outputting the table. */
	fprintf (fp, "\n\\begin{longtable}[c]{%%\n");
	for (col = total_range.start.col; col <=  total_range.end.col; col++) {
		fprintf (fp, "\tb{\\gnumericCol%s}%%\n", col_name (col));
	}
	fprintf (fp, "\t}\n\n");

	/* Output the table header. */
	latex2e_write_table_header (fp, num_cols);


	/* Step through the sheet, writing cells as appropriate. */
	for (row = total_range.start.row; row <= total_range.end.row; row++) {
		ColRowInfo const * ri;
		ri = sheet_row_get_info (current_sheet, row);
		if (ri->needs_respan)
			row_calc_spans ((ColRowInfo *) ri, current_sheet);
		
		/* We need to check for horizontal borders at the top of this row */
		length = num_cols;
		clines = g_new0 (StyleBorderType, length);
		needs_hline = FALSE;
		this_clines = clines;
		for (col = total_range.start.col; col <= total_range.end.col; col++) {
			needs_hline = latex2e_find_hhlines (this_clines, length,  col, row,
							    current_sheet, MSTYLE_BORDER_TOP)
				|| needs_hline;
			this_clines ++;
			length--;
		}
		/* or at the bottom of the previous */
		if (row > total_range.start.row) {
			length = num_cols;
			this_clines = clines;
			for (col = total_range.start.col; col <= total_range.end.col; col++) {
				needs_hline = latex2e_find_hhlines (this_clines, length,  col,
								    row - 1, current_sheet,
								    MSTYLE_BORDER_BOTTOM)
					|| needs_hline;
				this_clines ++;
				length--;
			}
		}
		/* We also need to know vertical borders */
		/* We do this here rather than as we output the cells since */
		/* we need to know the right connectors! */
		prev_vert = next_vert;
		next_vert = g_new0 (StyleBorderType, num_cols + 1);
		this_vert = next_vert;
		*this_vert = latex2e_find_vline (total_range.start.col, row,
						current_sheet, MSTYLE_BORDER_LEFT);
		this_vert++;
		for (col = total_range.start.col; col <= total_range.end.col; col++) {
			*this_vert = latex2e_find_vline (col, row, current_sheet,
							MSTYLE_BORDER_RIGHT);
			this_vert ++;
		}

		if (needs_hline)
			latex2e_print_hhline (fp, clines, num_cols, prev_vert, next_vert);
		g_free (clines);

		for (col = total_range.start.col; col <= total_range.end.col; col++) {
			CellSpanInfo const *the_span;

			/* Get the cell. */
			cell = sheet_cell_get (current_sheet, col, row);

			/* Check if we are not the first cell in the row.*/
			if (col != total_range.start.col)
				fprintf (fp, "\t&");
			else
				fprintf (fp, "\t ");

			/* Even an empty cell (especially an empty cell!) can be */
			/* covered by a span!                                    */
			the_span = row_span_get (ri, col);
			if (the_span != NULL) {
				latex2e_write_multicolumn_cell(fp, the_span->cell,
							       the_span->right -
							       col + 1, 1,
							       col - total_range.start.col,
							       next_vert, current_sheet);
				col += the_span->right - col;
				continue;
			}


			/* A blank cell has only a few options*/
			if (cell_is_blank(cell)) {
				latex2e_write_blank_cell(fp, col, row,
							col - total_range.start.col,
							next_vert, current_sheet);
				continue;
			}

			/* Check a merge. */
			merge_range = sheet_merge_is_corner (current_sheet, &cell->pos);
			if (merge_range == NULL) {
				latex2e_write_multicolumn_cell(fp, cell, 1, 1,
							       col - total_range.start.col,
							       next_vert, current_sheet);
				continue;
			}

			/* Get the extent of the merge. */
			num_merged_cols = merge_range->end.col - merge_range->start.col + 1;
			num_merged_rows = merge_range->end.row - merge_range->start.row + 1;

			latex2e_write_multicolumn_cell(fp, cell, num_merged_cols,
						       num_merged_rows,
						       col - total_range.start.col,
						       next_vert, current_sheet);
			col += (num_merged_cols - 1);
			continue;
		}
		fprintf (fp, "\\\\\n");
		if (prev_vert != NULL)
			g_free (prev_vert);
	}

	/* We need to check for horizontal borders at the bottom  of  the last  row */
	clines = g_new0 (StyleBorderType, total_range.end.col - total_range.start.col + 1);
	needs_hline = FALSE;
	length = num_cols;
	this_clines = clines;
	for (col = total_range.start.col; col <= total_range.end.col; col++) {
		needs_hline = latex2e_find_hhlines (this_clines, length,  col, row,
						    current_sheet, MSTYLE_BORDER_TOP)
			|| needs_hline;
		this_clines ++;
		length--;
	}
	length = num_cols;
	this_clines = clines;
	for (col = total_range.start.col; col <= total_range.end.col; col++) {
		needs_hline = latex2e_find_hhlines (this_clines, length,  col,
						    row - 1, current_sheet,
						    MSTYLE_BORDER_BOTTOM)
			|| needs_hline;
		this_clines ++;
		length--;
	}
	if (needs_hline)
		latex2e_print_hhline (fp, clines, num_cols, next_vert, NULL);
	g_free (clines);

	g_free (next_vert);

	fprintf (fp, "\\end{longtable}\n\n");
	fprintf (fp, "\\gnumericTableEnd\n");
	fclose (fp);
}
