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
 * This file contains both the LaTeX2e and the LaTeX 2.09 plugin functions.
 *
 * The LateX 2.09 function is named:
 * 		latex_file_save()          which calls
 * 			latex_fprintf_cell()   to write each cell. This in turn calls
 * 				latex_fputs()      to escape LaTeX specific characters.
 *
 * The LaTeX2e function is named:
 * 		latex2e_file_save()       			 which calls 3 functions:
 * 			latex2e_write_multicolumn_cell() to write each cell. This calls
 * 				latex_fputs()               	just like above.
 * 			latex_write_file_header()		 to output LaTeX definitions.
 * 			latex_write_table_header()		 to start each table.
 *
 */


#include <config.h>
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

#include <errno.h>

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
latex_fputs (const char *p, FILE *fp)
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
 * latex_fprintf_cell :
 *
 * @fp : a file pointer where the cell contents will be written.
 * @cell : the cell whose contents are to be written.
 *
 * This processes each cell. Only used in the LaTeX 2.09 exporter,
 * the functionality was folded into the LaTeX2e exporter.
 */
static void
latex_fprintf_cell (FILE *fp, const Cell *cell)
{
	char *s;

	if (cell_is_blank (cell))
		return;
	s = cell_get_rendered_text (cell);
	latex_fputs (s, fp);
	g_free (s);
}


/**
 * latex_file_save : The LateX 2.09 exporter.
 *
 * @FileSaver :        structure for file plugins.
 * @IOcontext :        currently not used but reserved for the future.
 * @WorkbookView :     provides the way to access the sheet being exported.
 * @filename :         file written to.
 *
 * The function writes every sheet of the workbook to a LaTeX 2.09 table.
 */
void
latex_file_save (GnumFileSaver const *fs, IOContext *io_context,
                 WorkbookView *wb_view, const gchar *file_name)
{
	FILE *fp;
	GList *sheets, *ptr;
	Cell *cell;
	int row, col;
	Workbook *wb = wb_view_workbook (wb_view);
	ErrorInfo *open_error;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (file_name != NULL);

	fp = gnumeric_fopen_error_info (file_name, "w", &open_error);
	if (fp == NULL) {
		gnumeric_io_error_info_set (io_context, open_error);
		return;
	}

	fprintf (fp, "\\documentstyle[umlaut,a4]{article}\n");
	fprintf (fp, "\\oddsidemargin -0.54cm\n\\textwidth 17cm\n");
	fprintf (fp, "\\parskip 1em\n");
	fprintf (fp, "\\begin{document}\n\n");
	sheets = workbook_sheets (wb);
	for (ptr = sheets ; ptr != NULL ; ptr = ptr->next) {
		Sheet *sheet = ptr->data;
		Range range = sheet_get_extent (sheet, FALSE);

		latex_fputs (sheet->name_unquoted, fp);
		fprintf (fp, "\n\n");
		fprintf (fp, "\\begin{tabular}{|");
		for (col = 0; col <= sheet->cols.max_used; col++) {
			fprintf (fp, "l|");
		}
		fprintf (fp, "}\\hline\n");

		for (row = range.start.row; row <= range.end.row; row++) {
			for (col = range.start.col; col <= range.end.col; col++) {
				cell = sheet_cell_get (sheet, col, row);
				if (cell_is_blank(cell)) {
					if (col != range.start.col)
						fprintf (fp, "\t&\n");
					else
						fprintf (fp, "\t\n");
				} else {
					MStyle *mstyle = cell_get_mstyle (cell);

					if (!mstyle)
						break;
					if (col != range.start.col)
						fprintf (fp, "\t&");
					else
						fprintf (fp, "\t ");
					if (mstyle_get_align_h (mstyle) == HALIGN_RIGHT)
						fprintf (fp, "\\hfill ");
					else if (mstyle_get_align_h (mstyle) == HALIGN_CENTER ||
						 /* FIXME : center across selection is wrong */
						 mstyle_get_align_h (mstyle) == HALIGN_CENTER_ACROSS_SELECTION)
						fprintf (fp, "{\\centering ");	/* doesn't work */
					if (mstyle_get_align_v (mstyle) & VALIGN_TOP)
						;
					if (font_is_monospaced (mstyle))
						fprintf (fp, "{\\tt ");
					else if (font_is_sansserif (mstyle))
						fprintf (fp, "{\\sf ");
					if (mstyle_get_font_bold (mstyle))
						fprintf (fp, "{\\bf ");
					if (mstyle_get_font_italic (mstyle))
						fprintf (fp, "{\\em ");
					latex_fprintf_cell (fp, cell);
					if (mstyle_get_font_italic (mstyle))
						fprintf (fp, "}");
					if (mstyle_get_font_bold (mstyle))
						fprintf (fp, "}");
					if (font_is_monospaced (mstyle))
						fprintf (fp, "}");
					else if (font_is_sansserif (mstyle))
						fprintf (fp, "}");
					if (mstyle_get_align_h (mstyle) & HALIGN_CENTER)
						fprintf (fp, "}");
					fprintf (fp, "\n");
				}
			}
			fprintf (fp, "\\\\\\hline\n");
		}
		fprintf (fp, "\\end{tabular}\n\n");
	}
	g_list_free (sheets);
	fprintf (fp, "\\end{document}");
	fclose (fp);
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
		fputs("
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%                                                                  %%
%%  This is the header of a LaTeX2e file exported from Gnumeric.    %%
%%                                                                  %%
%%  This file can be compiled as it stands or included in another   %%
%%  LaTeX document. The table is based on the longtable package so  %%
%%  the longtable options (headers, footers...) can be set in the   %%
%%  preamble section below (see PRAMBLE).                           %%
%%                                                                  %%
%%  To include the file in another, the following two lines must be %%
%%  in the including file:                                          %%
%%        \\def\\inputGnumericTable{}                                 %%
%%  at the begining of the file and:                                %%
%%        \\input{name-of-this-file.tex}                             %%
%%  where the table is to be placed. Note also that the including   %%
%%  file must use the following packages for the table to be        %%
%%  rendered correctly:                                             %%
%%    \\usepackage[latin1]{inputenc}                                 %%
%%    \\usepackage{color}                                            %%
%%    \\usepackage{longtable}                                        %%
%%  optionally (for landscape tables embedded in another document): %%
%%    \\usepackage{lscape}                                           %%
%%                                                                  %%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%



%%  This section checks if we are begin input into another file or  %%
%%  the file will be compiled alone. First use a macro taken from   %%
%%  the TeXbook ex 7.7 (suggestion of Han-Wen Nienhuys).            %%
\\def\\ifundefined#1{\\expandafter\\ifx\\csname#1\\endcsname\\relax}


%%  Check for the \\def token for inputed files. If it is not        %%
%%  defined, the file will be processed as a standalone and the     %%
%%  preamble will be used.                                          %%
\\ifundefined{inputGnumericTable}

%%  We must be able to close or not the document at the end.        %%
	\\def\\gnumericTableEnd{\\end{document}}


%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%                                                                  %%
%%  This is the PREAMBLE. Change these values to get the right      %%
%%  paper size and other niceties. Uncomment the landscape option   %%
%%  to the documentclass defintion for standalone documents.        %%
%%                                                                  %%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

	\\documentclass[12pt%
	                  %,landscape%
                    ]{report}
	\\usepackage[latin1]{inputenc}
	\\usepackage{color}
        \\usepackage{array}
	\\usepackage{longtable}
        \\usepackage{calc}

	\\begin{document}

%%  End of the preamble for the standalone. The next section is for %%
%%  documents which are included into other LaTeX2e files.          %%
\\else

%%  We are not a stand alone document. For a regular table, we will %%
%%  have no preamble and only define the closing to mean nothing.   %%
    \\def\\gnumericTableEnd{}

%%  If we want landscape mode in an embedded document, comment out  %%
%%  the line above and uncomment the two below. The table will      %%
%%  begin on a new page and run in landscape mode.                  %%
%       \\def\\gnumericTableEnd{\\end{landscape}}
%       \\begin{landscape}


%%  End of the else clause for this file being \\input.              %%
\\fi

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%                                                                  %%
%%  The rest is the gnumeric table, except for the closing          %%
%%  statement. Changes below will alter the table\'s appearance.     %%
%%                                                                  %%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

\\providecommand{\\gnumericPB}[1]%
{\\let\\gnumericTemp=\\\\#1\\let\\\\=\\gnumericTemp\\hspace{0pt}}

%%  The default table format retains the relative column widths of  %%
%%  gnumeric. They can easily be changed to c, r or l. In that case %%
%%  you may want to comment out the next line and uncomment the one %%
%%  thereafter                                                      %%
\\providecommand\\gnumbox{\\makebox[0pt]}
%%\\providecommand\\gnumbox[1][]{\\makebox}

%%  The \\setlongtables command keeps column widths the same across %%
%%  pages. Simply comment out next line for varying column widths.  %%
\\setlongtables

",fp);
}


/**
 * latex2e_write_table_header:
 *
 * @fp : a file pointer where the cell contents will be written.
 * @num_cols: The number of columns in the table
 *
 * A convinience function that also helps make nicer code.
 */
static void
latex2e_write_table_header(FILE *fp, int num_cols)
{
	int col;


	fputs ("
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%  The longtable options. (Caption, headers... see Goosens, p.124) %%
%\t\\caption{The Table Caption.}             \\\\	%
 \\hline	% Across the top of the table.
%%  The rest of these options are table rows which are placed on    %%
%%  the first, last or every page. Use \\multicolumn if you want.    %%

%%  Header for the first page.                                      %%
",fp);

	fprintf (fp, "%%\t\\multicolumn{%d}{|c|}{The First Header} \\\\ \\hline \n", num_cols);
	fprintf (fp, "%%\t\\multicolumn{1}{|c|}{colTag}\t%%Column 1\n");
	for (col = 1 ; col < num_cols; col++)
		fprintf (fp, "%%\t&\\multicolumn{1}{|c|}{colTag}\t%%Column %d\n",col);
	fprintf (fp, "%%\t&\\multicolumn{1}{|c|}{colTag}\t\\\\ \\hline %%Last column\n");
	fprintf (fp, "%%\t\\endfirsthead\n\n");

	fprintf (fp, "%%%%  The running header definition.                                  %%%%\n");
	fprintf (fp, "%%\t\\hline\n");
	fprintf (fp, "%%\t\\multicolumn{%d}{|l|}{\\ldots\\small\\slshape continued} \\\\ \\hline\n", num_cols);
	fprintf (fp, "%%\t\\multicolumn{1}{|c|}{colTag}\t%%Column 1\n");
	for (col = 1 ; col < num_cols; col++)
				fprintf (fp, "%%\t&\\multicolumn{1}{|c|}{colTag}\t%%Column %d\n",col);
	fprintf (fp, "%%\t&\\multicolumn{1}{|c|}{colTag}\t\\\\ \\hline %%Last column\n");
	fprintf (fp, "%%\t\\endhead\n\n");

	fprintf (fp, "%%%%  The running footer definition.                                  %%%%\n");
	fprintf (fp, "%%\t\\hline\n");
	fprintf (fp, "%%\t\\multicolumn{%d}{|r|}{\\small\\slshape continued\\ldots}", num_cols);
	fprintf (fp, " \\\\\t\\hline \n");
	fprintf (fp, "%%\t\\endfoot\n\n");

	fprintf (fp, "%%%%  The ending footer definition.                                   %%%%\n");
	fprintf (fp, "%%\t\\multicolumn{%d}{|c|}{That's all folks} \\\\ \\hline \n", num_cols);
	fprintf (fp, "%%\t\\endlastfoot\n");
	fputs ("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n\n",fp);

}


/**
 * latex2e_write_multicolumn_cell:
 *
 * @fp : a file pointer where the cell contents will be written.
 * @cell : the cell whose contents are to be written.
 * @num_merged_cols : an integer value of the number of columns to merge.
 *
 * This function creates all the LaTeX code for the cell of a table (i.e. all
 * the code that might fall between two ampersands (&)). 
 *
 * Note: we are _not_ putting single cell into \multicolumns since this
 * makes it much more difficult to change column widths later on.
 */
static void
latex2e_write_multicolumn_cell (FILE *fp, const Cell *cell, const int num_merged_cols, 
				gboolean first_col, Sheet *sheet)
{
	char * rendered_string;
	StyleColor *textColor;
	gushort r,g,b;
	gboolean wrap = FALSE;
	char *cell_format_str;
	FormatCharacteristics cell_format_characteristic;
	FormatFamily cell_format_family;

	/* Print the cell according to its style. */
	MStyle *mstyle = cell_get_mstyle (cell);

	g_return_if_fail (mstyle != NULL);

	/* We only set up a multicolumn command if necessary */
	if (num_merged_cols > 1) {
		ColRowInfo const * ci;
		int merge_width = 0;
		int i;

		for (i = 0; i < num_merged_cols; i++) {
			ci = sheet_col_get (sheet, cell->col_info->pos + i);
			merge_width += ci->size_pixels;
		}
		
		ci = cell->col_info;
		
		/* Open the multicolumn statement. */
		fprintf (fp, "\\multicolumn{%d}{", num_merged_cols);
		
		if (first_col)
			fprintf (fp, "|");
/* FIXME: merge_width is a touch too small since it doesn't include the space betweeen the cols */
		/* Drop in the left hand format delimiter. */
		fprintf (fp, "p{%ipt+\\tabcolsep*%i}", 
			 merge_width * 10 / 12, 2 * (num_merged_cols - 1));
		
		/*Close the right delimiter, as above. Also open the text delimiter.*/
		fprintf (fp,"|}%%\n\t{");
	}

	/* Send the alignment of the cell through a routine to deal with
	 * HALIGN_GENERAL and then deal with the three cases. */
	switch ( style_default_halign(mstyle, cell) ) {
	case HALIGN_RIGHT:
		fprintf (fp, "\\gnumericPB{\\raggedleft}");
		break;
	case  HALIGN_CENTER:
		/*case HALIGN_CENTER_ACROSS_SELECTION: No not here. */
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
		case  HALIGN_CENTER:
			/*case HALIGN_CENTER_ACROSS_SELECTION: No not here. */
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

	/* Check the foreground (text) colour. */
	textColor = mstyle_get_color (mstyle, MSTYLE_COLOR_FORE);
	r = textColor->red;
	g = textColor->green;
	b = textColor->blue;
	if (r != 0 || g != 0 || b != 0)
		fprintf (fp, "{\\color[rgb]{%.2f,%.2f,%.2f} ",
		(double)r/65535, (double)g/65535, (double)b/65535);       

	/* Establish the font's style for the styles that can be addressed by LaTeX.
	 * More complicated efforts (like changing fonts) are left to the user.
	 */
	if (font_is_monospaced (mstyle))
		fprintf (fp, "\\texttt{");
	else if (font_is_sansserif (mstyle))
		fprintf (fp, "\\textsf{");
	if (mstyle_get_font_bold (mstyle))
		fprintf (fp, "\\textbf{");
	if (mstyle_get_font_italic (mstyle))
		fprintf (fp, "\\textit{");


	cell_format_str = cell_get_format (cell);
	cell_format_family = cell_format_classify (cell_format_str, &cell_format_characteristic);
	g_free (cell_format_str);
	if (cell_format_family == FMT_NUMBER || cell_format_family == FMT_CURRENCY ||
	    cell_format_family == FMT_PERCENT || cell_format_family == FMT_FRACTION ||
	    cell_format_family == FMT_SCIENCE) 
		fprintf (fp, "$");

	/* Print the cell contents. */
	if (!cell_is_blank (cell)) {
		rendered_string = cell_get_rendered_text (cell);
		latex_fputs (rendered_string, fp);
		g_free (rendered_string);
	}

	if (cell_format_family == FMT_NUMBER || cell_format_family == FMT_CURRENCY ||
	    cell_format_family == FMT_PERCENT || cell_format_family == FMT_FRACTION ||
	    cell_format_family == FMT_SCIENCE) 
		fprintf (fp, "$");

	/* Close the styles for the cell. */
	if (mstyle_get_font_italic (mstyle))
		fprintf (fp, "}");
	if (mstyle_get_font_bold (mstyle))
		fprintf (fp, "}");
	if (font_is_monospaced (mstyle))
		fprintf (fp, "}");
	else if (font_is_sansserif (mstyle))
		fprintf (fp, "}");
	if (r != 0 || g != 0 || b != 0)
		fprintf (fp, "}");

	/* if we don't wrap close the mbox */
	if (!wrap)
		fprintf (fp, "}");

	/* Close the multicolumn text bracket. */
	if (num_merged_cols > 1)	
		fprintf(fp, "}");

	/* And we are done. */
	fprintf (fp, "\n");

}


/**
 * latex2e_file_save :  The LaTeX2e exporter plugin function.
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
latex2e_file_save (GnumFileSaver const *fs, IOContext *io_context,
                   WorkbookView *wb_view, const gchar *file_name)
{
	FILE *fp;
	Cell *cell;
	Sheet *current_sheet;
	Range total_range;
 	Range const *merge_range;
	int row, col, num_cols;
	int num_merged_cols, num_merged_rows;
	Workbook *wb = wb_view_workbook (wb_view);
	ErrorInfo *open_error;


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
	total_range = sheet_get_extent (current_sheet, FALSE);

	num_cols = total_range.end.col - total_range.start.col + 1;

	/* Start outputting the table. */
	fprintf (fp, "\\begin{longtable}[c]{|");
	for (col = total_range.start.col; col <=  total_range.end.col; col++) {
		ColRowInfo const * ci;
		ci = sheet_col_get_info (current_sheet, col);
		/* The alignment is over-ridden later. */
		fprintf (fp, "p{%ipt}|", ci->size_pixels * 10 / 12);
	}
	fprintf (fp, "}\n\n");

	/* Output the table header. */
	latex2e_write_table_header(fp,num_cols);


	/* Step through the sheet, writing cells as appropriate. */
	for (row = total_range.start.row; row <= total_range.end.row; row++) {
		for (col = total_range.start.col; col <= total_range.end.col; col++) {

			/* Get the cell. */
			cell = sheet_cell_get (current_sheet, col, row);

			/* Check if we are not the first cell in the row.*/
			if (col != total_range.start.col)
				fprintf (fp, "\t&");
			else
				fprintf (fp, "\t ");

			/* A blank cell gets a newline. */
			if (cell_is_blank(cell)) {
				fprintf (fp, "\n");
				continue;
			}

			/* Check a merge. */
			merge_range = sheet_merge_is_corner (current_sheet, &cell->pos);
			if (merge_range == NULL) {
				latex2e_write_multicolumn_cell(fp, cell, 1, 
							       col == total_range.start.col,
							       current_sheet);
				continue;
			}

			/* Get the extent of the merge. */
			num_merged_cols = merge_range->end.col - merge_range->start.col + 1;
			num_merged_rows = merge_range->end.row - merge_range->start.row + 1;

			/* Check for a 2-D merge (longtable can't handle).*/
			if ( (num_merged_cols > 1) && (num_merged_rows > 1) ){
				fprintf(fp,"\n\\typeout{ERROR: LaTeX's longtable can not handle 2-D merged regions!}\n");
				fclose(fp);
				/*FIXME: add an error message.*/
				return;
			}

			/* Check for a single row, we can handle with \multicolumn. */
			if (num_merged_cols > 1) {
				latex2e_write_multicolumn_cell(fp, cell, num_merged_cols,
							       col == total_range.start.col,
							       current_sheet);
				/* increment the columns by num_of_columns of merge region. */
				col += (num_merged_cols - 1);
				continue;
			}

			/* Else it's a single column which we can't handle. */
			fprintf(fp,"\n\\tyepout{ERROR: We can not yet handle merged columns!}\n");
			fclose(fp);
			/*FIXME: add an error message.*/
			return;
		}
		fprintf (fp, "\\\\ \\hline\n");
	}
	fprintf (fp, "\\end{longtable}\n\n");
	fprintf (fp, "\\gnumericTableEnd\n");
	fclose (fp);
}
