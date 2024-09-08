/*
 * latex.c
 *
 * Copyright (C) 1999, 2000 Rasca, Berlin
 * EMail: thron@gmx.de
 *
 * Copyright (C) 2001 Adrian Custer, Berkeley
 * email: acuster@nature.berkeley.edu
 *
 * Copyright (C) 2001-2014 Andreas J. Guelzow, Edmonton
 * email: aguelzow@pyrshep.ca
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
 */


/*
 * This file contains the LaTeX2e plugin functions.
 *
 *
 * The LaTeX2e functions are named:
 *		latex_file_save()
 *              latex_table_visible_file_save()
 *              latex_table_file_save
 *
 */


#include <gnumeric-config.h>
#include <gnumeric.h>
#include <gnumeric-conf.h>
#include "latex.h"
#include <goffice/goffice.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-merge.h>
#include <style.h>
#include <style-color.h>
#include <font.h>
#include <cell.h>
#include <gnm-format.h>
#include <style-border.h>
#include <sheet-style.h>
#include <parse-util.h>
#include <rendered-value.h>
#include <cellspan.h>
#include <print-info.h>
#include <gutils.h>

#include <locale.h>
#include <gsf/gsf-output.h>
#include <string.h>

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

/* the index into the following array is GnmStyleBorderType */
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
	  {{":b:",""}, { ":b:",""}, { ":",":"}}}}
};

/**
 * latex_raw_str :
 * @p:	    a pointer to a char, start of the string to be processed
 * @output: output stream where the processed characters are written.
 * @utf8:   is this a utf8 string?
 *
 * @return:
 * If @p is in form of \L{foo}, return the char pointer pointing to '}' of \L{foo}
 * else return @p untouched;
 *
 * Check if @p is in form of \L{foo}.
 * If it is, the exact "foo" will be put into @output, without any esacaping.
 *
 */
static char const *
latex_raw_str(char const *p, GsfOutput *output, gboolean utf8)
{
	char const *p_begin, *p_orig = p;
	int depth = 1;
	if(strncasecmp(p, "\\L{", 3) == 0){
		p += 3;
		p_begin = p;
		/* find the matching close bracket */
		for(; *p; p = utf8 ? g_utf8_next_char(p) : p + 1){
			switch(*p){ /* FIXME: how to put in unmatched brackets? */
				case '{':
					depth ++;
					break;
				case '}':
					depth--;
					if(depth == 0){
						/* put the string beginning from p_begin to p to output */
						gsf_output_write(output, p - p_begin, p_begin);
						return p;
					}
			}
		}
	}
	return p_orig;
}


/**
 * latex_fputs_utf :
 *
 * @p:      a pointer to a char, start of the string to be processed.
 * @output: output stream where the processed characters are written.
 *
 * This escapes any special LaTeX characters from the LaTeX engine,
 * except the ones enclosed in "\L{" and "}".
 * Re-ordered from Rasca's code to have most common first.
 */
static void
latex_fputs_utf (char const *p, GsfOutput *output)
{
	char const *rlt;
	for (; *p; p = g_utf8_next_char (p)) {
		switch (g_utf8_get_char (p)) {

			/* These are the classic TeX symbols $ & % # _ { } (see Lamport, p.15) */
		case '$': case '&': case '%': case '#':
		case '_': case '{': case '}':
			gsf_output_printf (output, "\\%c", *p);
			break;
			/* These are the other special characters ~ ^ \ (see Lamport, p.15) */
		case '^': case '~':
			gsf_output_printf (output, "\\%c{ }", *p);
			break;
		case '\\':
			rlt = latex_raw_str(p, output, TRUE);
			if(rlt == p)
			    gsf_output_puts (output, "$\\backslash$");
			else
			    p = rlt;
			break;
			/* Are these available only in LaTeX through mathmode? */
		case '>': case '<':
			gsf_output_printf (output, "$%c$", *p);
			break;

		default:
			gsf_output_write (output,
					  (g_utf8_next_char (p)) - p, p);
			break;
		}
	}
}

/**
 * latex_math_fputs_utf :
 *
 * @p:     a pointer to a char, start of the string to be processed.
 * @output: output stream where the processed characters are written.
 *
 * This escapes any special LaTeX characters from the LaTeX engine,
 * except the ones enclosed in "\L{" and "}".
 *
 * We assume that htis will be set in Mathematics mode.
 */
static void
latex_math_fputs_utf (char const *p, GsfOutput *output)
{
	char const *rlt;
	for (; *p; p = g_utf8_next_char (p)) {
		switch (g_utf8_get_char (p)) {

			/* These are the classic TeX symbols $ & % # (see Lamport, p.15) */
			case '$': case '&': case '%': case '#':
				gsf_output_printf (output, "\\%c", *p);
				break;
			/* These are the other special characters ~ (see Lamport, p.15) */
			case '~':
				gsf_output_printf (output, "\\%c{ }", *p);
				break;
			case '\\':
				rlt = latex_raw_str(p, output, TRUE);
				if(rlt == p)
				    gsf_output_puts (output, "$\\backslash$");
				else
				    p = rlt;
				break;
			default:
				gsf_output_write (output,
						  (g_utf8_next_char (p)) - p, p);
				break;
		}
	}
}

/**
 * latex_convert_latin_to_utf
 *
 * @text: string to convert
 *
 * return value needs to be freed with g_free
 *
 * call g_convert_with_fallback and also handle utf minus.
 *
 */
static char *
latex_convert_latin_to_utf (char const *text)
{
	char * encoded_text = NULL;

	gsize bytes_read;
	gsize bytes_written;

	if (g_utf8_strchr (text,-1, 0x2212) == NULL) {
		encoded_text = g_convert_with_fallback
			(text, strlen (text),
			 "ISO-8859-1", "UTF-8", (gchar *)"?",
			 &bytes_read, &bytes_written, NULL);
	} else {
		gunichar* ucs_string = NULL;
		gunichar* this_unichar;
		char *new_text;
		glong items_read;
		glong items_written;

		ucs_string = g_utf8_to_ucs4_fast (text, -1, &items_written);
		for (this_unichar = ucs_string; *this_unichar != '\0'; this_unichar++) {
			if (*this_unichar == 0x2212)
				*this_unichar = 0x002d;
		}
		new_text = g_ucs4_to_utf8 (ucs_string, -1, &items_read, &items_written, NULL);
		g_free (ucs_string);
		encoded_text = g_convert_with_fallback
			(new_text, strlen (new_text),
			 "ISO-8859-1", "UTF-8", (gchar *)"?",
			 &bytes_read, &bytes_written, NULL);
		g_free (new_text);
	}

	return encoded_text;
}

/**
 * latex_fputs_latin :
 *
 * @p:      a pointer to a char, start of the string to be processed.
 * @output: output stream where the processed characters are written.
 *
 * This escapes any special LaTeX characters from the LaTeX engine,
 * except the ones enclosed in "\L{" and "}".
 * Re-ordered from Rasca's code to have most common first.
 */
static void
latex_fputs_latin (char const *text, GsfOutput *output)
{
	char * encoded_text = NULL;
	char const *p;
	char const *rlt;

	encoded_text = latex_convert_latin_to_utf (text);

	for (p = encoded_text; *p; p++) {
		switch (*p) {

			/* These are the classic TeX symbols $ & % # _ { } (see Lamport, p.15) */
		case '$': case '&': case '%': case '#':
		case '_': case '{': case '}':
			gsf_output_printf (output, "\\%c", *p);
			break;
			/* These are the other special characters ~ ^ \ (see Lamport, p.15) */
		case '^': case '~':
			gsf_output_printf (output, "\\%c{ }", *p);
			break;
		case '\\':
			rlt = latex_raw_str(p, output, FALSE);
			if(rlt == p)
			    gsf_output_puts (output, "$\\backslash$");
			else
			    p = rlt;
			break;
			/* Are these available only in LaTeX through mathmode? */
		case '>': case '<': case 'µ':
			gsf_output_printf (output, "$%c$", *p);
			break;

		default:
			gsf_output_write (output, 1, p);
			break;
		}
	}
	g_free (encoded_text);
}

/**
 * latex_math_fputs_latin :
 *
 * @p:     a pointer to a char, start of the string to be processed.
 * @output: output stream where the processed characters are written.
 *
 * This escapes any special LaTeX characters from the LaTeX engine,
 * except the ones enclosed in "\L{" and "}".
 *
 * We assume that htis will be set in Mathematics mode.
 */
static void
latex_math_fputs_latin (char const *text, GsfOutput *output)
{
	char * encoded_text = NULL;
	char const *p;
	char const *rlt;

	encoded_text = latex_convert_latin_to_utf (text);

	for (p = encoded_text; *p; p++) {
		switch (*p) {

			/* These are the classic TeX symbols $ & % # (see Lamport, p.15) */
			case '$': case '&': case '%': case '#':
				gsf_output_printf (output, "\\%c", *p);
				break;
			/* These are the other special characters ~ (see Lamport, p.15) */
			case '~':
				gsf_output_printf (output, "\\%c{ }", *p);
				break;
			case '\\':
				rlt = latex_raw_str(p, output, FALSE);
				if(rlt == p)
				    gsf_output_puts (output, "$\\backslash$");
				else
				    p = rlt;
				break;

			default:
				gsf_output_write (output, 1, p);
				break;
		}
	}
	g_free (encoded_text);
}

static void
latex_fputs (char const *text, GsfOutput *output)
{
	if (gnm_conf_get_plugin_latex_use_utf8 ())
		latex_fputs_utf (text, output);
	else
		latex_fputs_latin (text, output);
}

static void
latex_math_fputs (char const *text, GsfOutput *output)
{
	if (gnm_conf_get_plugin_latex_use_utf8 ())
		latex_math_fputs_utf (text, output);
	else
		latex_math_fputs_latin (text, output);
}

static GnmValue *
cb_find_font_encodings (GnmCellIter const *iter, gboolean *fonts)
{
	GnmCell *cell = iter->cell;
	if (cell) {
		char const *rs =
			gnm_rendered_value_get_text
			(gnm_cell_fetch_rendered_value (cell, TRUE));
		while (*rs) {
			gunichar ch = g_utf8_get_char (rs);
			GUnicodeScript script = g_unichar_get_script (ch);
			if (script > 0 && script <= G_UNICODE_SCRIPT_MANDAIC)
				fonts [script] = 1;
			rs = g_utf8_next_char (rs);
		}
	}
	return NULL;
}

/**
 * latex2e_write_font_encodings writes
 * \usepackage[T2A]{fontenc}
 * in the presence of cyrillic text
 */

static void
latex2e_write_font_encodings (GsfOutput *output, Sheet *sheet, GnmRange const *range)
{
	gboolean *fonts = g_new0 (gboolean, G_UNICODE_SCRIPT_MANDAIC + 1);

	sheet_foreach_cell_in_range
		(sheet, CELL_ITER_IGNORE_BLANK | CELL_ITER_IGNORE_HIDDEN, range,
		 (CellIterFunc)&cb_find_font_encodings, fonts);

	if (fonts[G_UNICODE_SCRIPT_CYRILLIC])
		gsf_output_puts (output,
"       \\usepackage[T2A]{fontenc}\n"
				 );
}

/**
 * latex2e_write_file_header:
 *
 * @output: Output stream where the cell contents will be written.
 *
 * This ouputs the LaTeX header. Kept separate for esthetics.
 */

static void
latex2e_write_file_header(GsfOutput *output, Sheet *sheet, GnmRange const *range)
{
	gboolean is_landscape = FALSE, use_utf8;
	GtkPageOrientation orient = print_info_get_paper_orientation (sheet->print_info);

	is_landscape = (orient == GTK_PAGE_ORIENTATION_LANDSCAPE ||
			orient == GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE);
	use_utf8 = gnm_conf_get_plugin_latex_use_utf8 ();

	gsf_output_puts (output,
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
"%%  at the beginning of the file and:                               %%\n"
"%%        \\input{name-of-this-file.tex}                             %%\n"
"%%  where the table is to be placed. Note also that the including   %%\n"
"%%  file must use the following packages for the table to be        %%\n"
"%%  rendered correctly:                                             %%\n"
);

	if (use_utf8)
		gsf_output_puts (output,
"%%    \\usepackage{ucs}                                              %%\n"
"%%    \\usepackage[utf8x]{inputenc}                                  %%\n"
"%%    \\usepackage[T2A]{fontenc}    % if cyrillic is used            %%\n"
			);
	else
		gsf_output_puts (output,
"%%    \\usepackage[latin1]{inputenc}                                 %%\n"
			);

	gsf_output_puts (output,
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
"%%  paper size and other niceties.                                  %%\n"
"%%                                                                  %%\n"
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
"\n"
		);
	if (is_landscape)
		gsf_output_puts (output,
"	\\documentclass[12pt%\n"
"			  ,landscape%\n"
"                    ]{report}\n"
				 );
	else
		gsf_output_puts (output,
"	\\documentclass[12pt%\n"
"			  %,landscape%\n"
"                    ]{report}\n"
				 );



	if (gnm_conf_get_plugin_latex_use_utf8 ()) {
		gsf_output_puts (output,
"       \\usepackage{ucs}\n"
"       \\usepackage[utf8x]{inputenc}\n"
			);
		latex2e_write_font_encodings (output, sheet, range);
	} else
		gsf_output_puts (output,
"       \\usepackage[latin1]{inputenc}\n"
			);

	gsf_output_puts (output,
"       \\usepackage{fullpage}\n"
"       \\usepackage{color}\n"
"       \\usepackage{array}\n"
"       \\usepackage{longtable}\n"
"       \\usepackage{calc}\n"
"       \\usepackage{multirow}\n"
"       \\usepackage{hhline}\n"
"       \\usepackage{ifthen}\n"
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
"        \\newlength{\\gnumericMultiRowLength}\n"
"        \\global\\def\\gnumericTableWidthDefined{}\n"
" \\fi\n"
"%% The following setting protects this code from babel shorthands.  %%\n"
" \\ifthenelse{\\isundefined{\\languageshorthands}}{}{\\languageshorthands{english}}"
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
);
}


/**
 * latex2e_write_table_header:
 *
 * @output:   Output stream where the cell contents will be written.
 * @num_cols: The number of columns in the table
 *
 * A convenience function that also helps make nicer code.
 */
static void
latex2e_write_table_header(GsfOutput *output, int num_cols)
{
	int col;


	gsf_output_puts (output,
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
"%%  The longtable options. (Caption, headers... see Goosens, p.124) %%\n"
"%\t\\caption{The Table Caption.}             \\\\	%\n"
"% \\hline	% Across the top of the table.\n"
"%%  The rest of these options are table rows which are placed on    %%\n"
"%%  the first, last or every page. Use \\multicolumn if you want.    %%\n"
"\n"
"%%  Header for the first page.                                      %%\n"
);

	gsf_output_printf (output, "%%\t\\multicolumn{%d}{c}{The First Header} \\\\ \\hline \n", num_cols);
	gsf_output_printf (output, "%%\t\\multicolumn{1}{c}{colTag}\t%%Column 1\n");
	for (col = 2 ; col < num_cols; col++)
		gsf_output_printf (output, "%%\t&\\multicolumn{1}{c}{colTag}\t%%Column %d\n",col);
	gsf_output_printf (output, "%%\t&\\multicolumn{1}{c}{colTag}\t\\\\ \\hline %%Last column\n");
	gsf_output_printf (output, "%%\t\\endfirsthead\n\n");

	gsf_output_printf (output, "%%%%  The running header definition.                                  %%%%\n");
	gsf_output_printf (output, "%%\t\\hline\n");
	gsf_output_printf (output, "%%\t\\multicolumn{%d}{l}{\\ldots\\small\\slshape continued} \\\\ \\hline\n", num_cols);
	gsf_output_printf (output, "%%\t\\multicolumn{1}{c}{colTag}\t%%Column 1\n");
	for (col = 2 ; col < num_cols; col++)
				gsf_output_printf (output, "%%\t&\\multicolumn{1}{c}{colTag}\t%%Column %d\n",col);
	gsf_output_printf (output, "%%\t&\\multicolumn{1}{c}{colTag}\t\\\\ \\hline %%Last column\n");
	gsf_output_printf (output, "%%\t\\endhead\n\n");

	gsf_output_printf (output, "%%%%  The running footer definition.                                  %%%%\n");
	gsf_output_printf (output, "%%\t\\hline\n");
	gsf_output_printf (output, "%%\t\\multicolumn{%d}{r}{\\small\\slshape continued\\ldots}", num_cols);
	gsf_output_printf (output, " \\\\\n");
	gsf_output_printf (output, "%%\t\\endfoot\n\n");

	gsf_output_printf (output, "%%%%  The ending footer definition.                                   %%%%\n");
	gsf_output_printf (output, "%%\t\\multicolumn{%d}{c}{That's all folks} \\\\ \\hline \n", num_cols);
	gsf_output_printf (output, "%%\t\\endlastfoot\n");
	gsf_output_puts (output, "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n\n");

}

/**
 * latex2e_find_vline:
 *
 * @col:
 * @row:
 * @sheet:
 * @which_border: GnmStyleElement (MSTYLE_BORDER_LEFT or MSTYLE_BORDER_RIGHT)
  *
 * Determine the border style
 *
 */
static GnmStyleBorderType
latex2e_find_this_vline (int col, int row, Sheet *sheet, GnmStyleElement which_border)
{
	GnmBorder const	*border;
	GnmStyle const	*style;

	if (col < 0 || row < 0)
		return GNM_STYLE_BORDER_NONE;

	style = sheet_style_get (sheet, col, row);
	border = gnm_style_get_border (style, which_border);

	if (!gnm_style_border_is_blank (border))
		return border->line_type;

	if (which_border == MSTYLE_BORDER_LEFT) {
		if (col <= 0)
			return GNM_STYLE_BORDER_NONE;
		style = sheet_style_get (sheet, col - 1, row);
		border = gnm_style_get_border (style, MSTYLE_BORDER_RIGHT);
		return ((border == NULL) ? GNM_STYLE_BORDER_NONE : border->line_type);
	} else {
		if ((col+1) >= colrow_max (TRUE, sheet))
		    return GNM_STYLE_BORDER_NONE;
		style = sheet_style_get (sheet, col + 1, row);
		border = gnm_style_get_border (style, MSTYLE_BORDER_LEFT);
		return ((border == NULL) ? GNM_STYLE_BORDER_NONE : border->line_type);
	}

	return GNM_STYLE_BORDER_NONE;
}

static GnmStyleBorderType
latex2e_find_vline (int col, int row, Sheet *sheet, GnmStyleElement which_border)
{
	/* We are checking for NONE boreders first since there should only be a few merged ranges */
	GnmStyleBorderType result = latex2e_find_this_vline (col, row, sheet, which_border);
	GnmCellPos pos;
	GnmRange const * range;

	if (result == GNM_STYLE_BORDER_NONE)
		return GNM_STYLE_BORDER_NONE;

	pos.col = col;
	pos.row = row;
	range = gnm_sheet_merge_contains_pos (sheet, &pos);

	if (range) {
		if ((which_border == MSTYLE_BORDER_LEFT && col == range->start.col)
		    || (which_border == MSTYLE_BORDER_RIGHT&& col == range->end.col))
			return result;
		else
			return GNM_STYLE_BORDER_NONE;
	}
	return result;
}

/**
 * latex2e_print_vert_border:
 *
 * @output: Output stream where the cell contents will be written.
 * @clines:  GnmStyleBorderType indicating the type of border
 *
 */
static void
latex2e_print_vert_border (GsfOutput *output, GnmStyleBorderType style)
{
	g_return_if_fail (/* style >= 0 && */ style < G_N_ELEMENTS (border_styles));

	gsf_output_printf (output, "%s", border_styles[style].vertical);
}

/**
 * latex2e_write_blank_multicolumn_cell:
 *
 * @output: output stream where the cell contents will be written.
 * @star_col:
 * @start_row:
 * @num_merged_cols: an integer value of the number of columns to merge.
 * @num_merged_rows: an integer value of the number of rows to merge.
 * @sheet:  the current sheet.
 *
 * This function creates all the LaTeX code for the cell of a table (i.e. all
 * the code that might fall between two ampersands (&)), assuming that
 * the cell is in fact NULL. We therefore have only to worry about a few
 * formatting issues.
 *
 */
static void
latex2e_write_blank_multicolumn_cell (GsfOutput *output, int start_col,
				      G_GNUC_UNUSED int start_row,
				      int num_merged_cols, int num_merged_rows,
				      gint index,
				      GnmStyleBorderType *borders, Sheet *sheet)
{
	int merge_width = 0;
	GnmStyleBorderType left_border = GNM_STYLE_BORDER_NONE;
	GnmStyleBorderType right_border = GNM_STYLE_BORDER_NONE;

	if (num_merged_cols > 1 || num_merged_rows > 1) {
		ColRowInfo const * ci;
		int i;

		for (i = 0; i < num_merged_cols; i++) {
			ci = sheet_col_get_info (sheet, start_col + i);
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
		gsf_output_printf (output, "\\multicolumn{%d}{", num_merged_cols);

		if (left_border != GNM_STYLE_BORDER_NONE)
			latex2e_print_vert_border (output, left_border);

		if (num_merged_rows > 1) {
			gsf_output_printf (output, "c");
		} else {
			gsf_output_printf (output, "p{");
			for (i = 0; i < num_merged_cols; i++) {
				gsf_output_printf (output, "\t\\gnumericCol%s+%%\n",
					 col_name (start_col + i));
			}
			gsf_output_printf (output, "\t\\tabcolsep*2*%i}", num_merged_cols - 1);
		}

		if (right_border != GNM_STYLE_BORDER_NONE)
			latex2e_print_vert_border (output, right_border);

		/*Close the right delimiter, as above. Also open the text delimiter.*/
		gsf_output_printf (output,"}%%\n\t{");
	} else if (left_border != GNM_STYLE_BORDER_NONE || right_border != GNM_STYLE_BORDER_NONE) {

		/* Open the multicolumn statement. */
		gsf_output_printf (output, "\\multicolumn{1}{");

		if (left_border != GNM_STYLE_BORDER_NONE)
			latex2e_print_vert_border (output, left_border);

		/* Drop in the left hand format delimiter. */
		gsf_output_printf (output, "p{\\gnumericCol%s}", col_name(start_col));

		if (right_border != GNM_STYLE_BORDER_NONE)
			latex2e_print_vert_border (output, right_border);

		/*Close the right delimiter, as above. Also open the text delimiter.*/
		gsf_output_printf (output,"}%%\n\t{");

	}

	if (num_merged_rows > 1) {
		int i;
		/* Open the multirow statement. */
		gsf_output_printf (output, "\\setlength{\\gnumericMultiRowLength}{0pt}%%\n");
		for (i = 0; i < num_merged_cols; i++) {
			gsf_output_printf (output, "\t \\addtolength{\\gnumericMultiRowLength}{\\gnumericCol%s}%%\n", col_name (start_col + i));
			if (i>0)
				gsf_output_printf (output, "\t \\addtolength{\\gnumericMultiRowLength}{\\tabcolsep}%%\n");
		}
		gsf_output_printf (output, "\t \\multirow{%i}[%i]{\\gnumericMultiRowLength}{%%\n\t ", num_merged_rows, num_merged_rows/2);

		/* Close the multirowtext. */
		gsf_output_printf (output, "}");
	}

	/* Close the multicolumn text bracket. */
	if (num_merged_cols > 1 || left_border != GNM_STYLE_BORDER_NONE
	    || right_border != GNM_STYLE_BORDER_NONE)
		gsf_output_printf (output, "}");

	/* And we are done. */
	gsf_output_printf (output, "\n");

}


/**
 * latex2e_write_multicolumn_cell:
 *
 * @output: output stream where the cell contents will be written.
 * @cell:   the cell whose contents are to be written.
 * @star_col:
 * @num_merged_cols: an integer value of the number of columns to merge.
 * @num_merged_rows: an integer value of the number of rows to merge.
 * @sheet:  the current sheet.
 *
 * This function creates all the LaTeX code for the cell of a table (i.e. all
 * the code that might fall between two ampersands (&)).
 *
 * Note: we are _not_ putting single cell into \multicolumns since this
 * makes it much more difficult to change column widths later on.
 */
static void
latex2e_write_multicolumn_cell (GsfOutput *output, GnmCell *cell, int start_col,
				int num_merged_cols, int num_merged_rows,
				gint index,
				GnmStyleBorderType *borders, Sheet *sheet)
{
	char * rendered_string;
	gushort r,g,b;
	gboolean wrap = FALSE;
	GOFormatFamily cell_format_family;
	int merge_width = 0;
	GnmStyleBorderType left_border = GNM_STYLE_BORDER_NONE;
	GnmStyleBorderType right_border = GNM_STYLE_BORDER_NONE;

	/* Print the cell according to its style. */
	GnmStyle const *style = gnm_cell_get_effective_style (cell);
	gboolean hidden = gnm_style_get_contents_hidden (style);

	g_return_if_fail (style != NULL);

	if (num_merged_cols > 1 || num_merged_rows > 1) {
		ColRowInfo const * ci;
		int i;

		for (i = 0; i < num_merged_cols; i++) {
			ci = sheet_col_get_info (sheet, start_col + i);
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
		gsf_output_printf (output, "\\multicolumn{%d}{", num_merged_cols);

		if (left_border != GNM_STYLE_BORDER_NONE)
			latex2e_print_vert_border (output, left_border);

		if (num_merged_rows > 1) {
			gsf_output_printf (output, "c");
		} else {
			gsf_output_printf (output, "p{");
			for (i = 0; i < num_merged_cols; i++) {
				gsf_output_printf (output, "\t\\gnumericCol%s+%%\n",
					 col_name (start_col + i));
			}
			gsf_output_printf (output, "\t\\tabcolsep*2*%i}", num_merged_cols - 1);
		}

		if (right_border != GNM_STYLE_BORDER_NONE)
			latex2e_print_vert_border (output, right_border);

		/*Close the right delimiter, as above. Also open the text delimiter.*/
		gsf_output_printf (output,"}%%\n\t{");
	} else if (left_border != GNM_STYLE_BORDER_NONE || right_border != GNM_STYLE_BORDER_NONE) {

		/* Open the multicolumn statement. */
		gsf_output_printf (output, "\\multicolumn{1}{");

		if (left_border != GNM_STYLE_BORDER_NONE)
			latex2e_print_vert_border (output, left_border);

		/* Drop in the left hand format delimiter. */
		gsf_output_printf (output, "p{\\gnumericCol%s}", col_name(start_col));

		if (right_border != GNM_STYLE_BORDER_NONE)
			latex2e_print_vert_border (output, right_border);

		/*Close the right delimiter, as above. Also open the text delimiter.*/
		gsf_output_printf (output,"}%%\n\t{");

	}

	if (num_merged_rows > 1) {
		int i;
		/* Open the multirow statement. */
		gsf_output_printf (output, "\\setlength{\\gnumericMultiRowLength}{0pt}%%\n");
		for (i = 0; i < num_merged_cols; i++) {
			gsf_output_printf (output, "\t \\addtolength{\\gnumericMultiRowLength}{\\gnumericCol%s}%%\n", col_name (start_col + i));
			if (i>0)
				gsf_output_printf (output, "\t \\addtolength{\\gnumericMultiRowLength}{\\tabcolsep}%%\n");
		}
		gsf_output_printf (output,
				   "\t \\multirow{%i}[%i]{\\gnumericMultiRowLength}"
				   "{\\parbox{\\gnumericMultiRowLength}{%%\n\t ",
				   num_merged_rows, num_merged_rows/2);
	}


	/* Send the alignment of the cell through a routine to deal with
	 * GNM_HALIGN_GENERAL and then deal with the three cases. */
	switch (gnm_style_default_halign (style, cell)) {
	case GNM_HALIGN_RIGHT:
		gsf_output_printf (output, "\\gnumericPB{\\raggedleft}");
		break;
	case GNM_HALIGN_DISTRIBUTED:
	case GNM_HALIGN_CENTER:
	case GNM_HALIGN_CENTER_ACROSS_SELECTION:
		gsf_output_printf (output, "\\gnumericPB{\\centering}");
		break;
	case GNM_HALIGN_LEFT:
		gsf_output_printf (output, "\\gnumericPB{\\raggedright}");
		break;
	case GNM_HALIGN_JUSTIFY:
		break;
	default:
		break;
	}

        /* Check whether we should do word wrapping */
	wrap = gnm_style_get_wrap_text (style);

	/* if we don't wrap put it into an mbox, adjusted to width 0 to avoid moving */
	/* it to the second line of the parbox */
	if (!wrap)
		switch (gnm_style_default_halign (style, cell)) {
		case GNM_HALIGN_RIGHT:
			gsf_output_printf (output, "\\gnumbox[r]{");
			break;
		case GNM_HALIGN_DISTRIBUTED:
		case GNM_HALIGN_CENTER:
		case GNM_HALIGN_CENTER_ACROSS_SELECTION:
			gsf_output_printf (output, "\\gnumbox{");
			break;
		case GNM_HALIGN_LEFT:
			gsf_output_printf (output, "\\gnumbox[l]{");
			break;
		case GNM_HALIGN_JUSTIFY:
			gsf_output_printf (output, "\\gnumbox[s]{");
			break;
		default:
			gsf_output_printf (output, "\\makebox{");
			break;
		}

	if (!gnm_cell_is_empty (cell)) {
                /* Check the foreground (text) colour. */
		GOColor fore = gnm_cell_get_render_color (cell);
		if (fore == 0)
			r = g = b = 0;
		else {
			r = GO_COLOR_UINT_R (fore);
			g = GO_COLOR_UINT_G (fore);
			b = GO_COLOR_UINT_B (fore);
		}
		if (r != 0 || g != 0 || b != 0) {
			gchar buffer[7] = {0};
			gsf_output_printf (output, "{\\color[rgb]{");
			g_ascii_formatd (buffer, 7, "%.2f",r/255.0);
			gsf_output_printf (output, "%s,", buffer);
			g_ascii_formatd (buffer, 7, "%.2f",g/255.0);
			gsf_output_printf (output, "%s,", buffer);
			g_ascii_formatd (buffer, 7, "%.2f",b/255.0);
			gsf_output_printf (output, "%s", buffer);
			gsf_output_printf (output, "} ");
		}

		/* Establish the font's style for the styles that can be addressed by LaTeX.
		 * More complicated efforts (like changing fonts) are left to the user.
		 */

		if (hidden)
			gsf_output_printf (output, "\\phantom{");

		if (font_is_monospaced (style))
			gsf_output_printf (output, "\\texttt{");
		else if (font_is_sansserif (style))
			gsf_output_printf (output, "\\textsf{");
		if (gnm_style_get_font_bold (style))
			gsf_output_printf (output, "\\textbf{");
		if (gnm_style_get_font_italic (style))
			gsf_output_printf (output, "\\textit{");


		cell_format_family = go_format_get_family (gnm_cell_get_format (cell));
		if (cell_format_family == GO_FORMAT_NUMBER ||
		    cell_format_family == GO_FORMAT_CURRENCY ||
		    cell_format_family == GO_FORMAT_PERCENTAGE ||
		    cell_format_family == GO_FORMAT_FRACTION ||
		    cell_format_family == GO_FORMAT_SCIENTIFIC){
			gsf_output_printf (output, "$");
			if (gnm_style_get_font_italic(style))
			    gsf_output_printf (output, "\\gnumericmathit{");

			/* Print the cell contents. */
			rendered_string = gnm_cell_get_rendered_text (cell);
			latex_math_fputs (rendered_string, output);
			g_free (rendered_string);

			if (gnm_style_get_font_italic(style))
			    gsf_output_printf (output, "}");
			gsf_output_printf (output, "$");
		} else {
			/* Print the cell contents. */
			rendered_string = gnm_cell_get_rendered_text (cell);
			latex_fputs (rendered_string, output);
			g_free (rendered_string);
		}

		/* Close the styles for the cell. */
		if (gnm_style_get_font_italic (style))
			gsf_output_printf (output, "}");
		if (gnm_style_get_font_bold (style))
			gsf_output_printf (output, "}");
		if (font_is_monospaced (style))
			gsf_output_printf (output, "}");
		else if (font_is_sansserif (style))
			gsf_output_printf (output, "}");
		if (hidden)
			gsf_output_printf (output, "}");
		if (r != 0 || g != 0 || b != 0)
			gsf_output_printf (output, "}");
	}

	/* if we don't wrap close the mbox */
	if (!wrap)
		gsf_output_printf (output, "}");

	/* Close the multirowtext. */
	if (num_merged_rows > 1)
		gsf_output_printf (output, "}}");

	/* Close the multicolumn text bracket. */
	if (num_merged_cols > 1 || left_border != GNM_STYLE_BORDER_NONE
	    || right_border != GNM_STYLE_BORDER_NONE)
		gsf_output_printf (output, "}");

	/* And we are done. */
	gsf_output_printf (output, "\n");

}

/**
 * latex2e_find_hhlines :
 *
 * @clines:  array of GnmStyleBorderType* indicating the type of border
 * @length:  (remaining) positions in clines
 * @col:
 * @row:
 * @sheet:
 *
 * Determine the border style
 *
 */

static gboolean
latex2e_find_hhlines (GnmStyleBorderType *clines, G_GNUC_UNUSED int length, int col, int row,
		      Sheet *sheet, GnmStyleElement type)
{
	GnmStyle const	*style;
	GnmBorder const	*border;
	GnmRange const	*range;
	GnmCellPos pos;

	style = sheet_style_get (sheet, col, row);
	border = gnm_style_get_border (style, type);
	if (gnm_style_border_is_blank (border))
		return FALSE;
	clines[0] = border->line_type;

	pos.col = col;
	pos.row = row;
	range = gnm_sheet_merge_contains_pos (sheet, &pos);
	if (range) {
		if ((type == MSTYLE_BORDER_TOP && row > range->start.row)
		    || (type == MSTYLE_BORDER_BOTTOM&& row < range->end.row)) {
			clines[0] = GNM_STYLE_BORDER_NONE;
			return FALSE;
		}
	}

	return TRUE;
}


/**
 * latex2e_print_hhline :
 *
 * @output: output stream where the cell contents will be written.
 * @clines:  an array of GnmStyleBorderType* indicating the type of border
 * @n: the number of elements in clines
 *
 * This procedure prints an hhline command according to the content
 * of clines.
 *
 */
static void
latex2e_print_hhline (GsfOutput *output, GnmStyleBorderType *clines, int n, GnmStyleBorderType *prev_vert,
		      GnmStyleBorderType *next_vert)
{
	int col;
	gsf_output_printf (output, "\\hhline{");
	gsf_output_printf (output, "%s", conn_styles[LATEX_NO_BORDER]
		 [prev_vert ? border_styles[prev_vert[0]].latex : LATEX_NO_BORDER]
		 [border_styles[clines[0]].latex]
		 [next_vert ? border_styles[next_vert[0]].latex : LATEX_NO_BORDER].p_1);
	gsf_output_printf (output, "%s", conn_styles[LATEX_NO_BORDER]
		 [prev_vert ? border_styles[prev_vert[0]].latex : LATEX_NO_BORDER]
		 [border_styles[clines[0]].latex]
		 [next_vert ? border_styles[next_vert[0]].latex : LATEX_NO_BORDER].p_2);
	for (col = 0; col < n - 1; col++) {
		gsf_output_printf (output, "%s", border_styles[clines[col]].horizontal);
		gsf_output_printf (output, "%s", conn_styles[border_styles[clines[col]].latex]
			 [prev_vert ? border_styles[prev_vert[col + 1]].latex :
			  LATEX_NO_BORDER]
			 [border_styles[clines[col+1]].latex]
			 [next_vert ? border_styles[next_vert[col + 1]].latex :
			  LATEX_NO_BORDER].p_1);
		gsf_output_printf (output, "%s", conn_styles[border_styles[clines[col]].latex]
			 [prev_vert ? border_styles[prev_vert[col + 1]].latex :
			  LATEX_NO_BORDER]
			 [border_styles[clines[col+1]].latex]
			 [next_vert ? border_styles[next_vert[col + 1]].latex :
			  LATEX_NO_BORDER].p_2);
	}
	gsf_output_printf (output, "%s", border_styles[clines[n - 1]].horizontal);
	gsf_output_printf (output, "%s", conn_styles[border_styles[clines[n - 1]].latex]
		 [prev_vert ? border_styles[prev_vert[n]].latex : LATEX_NO_BORDER]
		 [LATEX_NO_BORDER]
		 [next_vert ? border_styles[next_vert[n]].latex :
		  LATEX_NO_BORDER].p_1);
	gsf_output_printf (output, "%s", conn_styles[border_styles[clines[n - 1]].latex]
		 [prev_vert ? border_styles[prev_vert[n]].latex : LATEX_NO_BORDER]
		 [LATEX_NO_BORDER]
		 [next_vert ? border_styles[next_vert[n]].latex :
		  LATEX_NO_BORDER].p_2);

	gsf_output_printf (output, "}\n");
}

static GnmRange
file_saver_sheet_get_extent (Sheet *sheet)
{
	GnmRange r;

	if (gnm_export_range_for_sheet (sheet, &r) >= 0)
		return r;
	else
		return sheet_get_extent (sheet, TRUE, TRUE);
}

/**
 * latex_file_save :  The LaTeX2e exporter plugin function.
 *
 * @FileSaver:        New structure for file plugins. I don't understand.
 * @IOcontext:        currently not used but reserved for the future.
 * @WorkbookView:     this provides the way to access the sheet being exported.
 * @filename:         where we'll write.
 *
 * This writes the top sheet of a Gnumeric workbook to a LaTeX2e longtable. We
 * check for merges here, then call the function latex2e_write_multicolumn_cell()
 * to render the format and contents of the cell.
 */
void
latex_file_save (GOFileSaver const *fs, G_GNUC_UNUSED GOIOContext *io_context,
		 WorkbookView const *wb_view, GsfOutput *output)
{
	GnmCell *cell;
	Sheet *current_sheet;
	GnmRange total_range;
	GnmRange const *merge_range;
	int row, col, num_cols, length;
	int num_merged_cols, num_merged_rows;
	GnmStyleBorderType *clines, *this_clines;
	GnmStyleBorderType *prev_vert = NULL, *next_vert = NULL, *this_vert;
	gboolean needs_hline;

	/* Get the sheet and its range from the plugin function argument. */
	current_sheet = gnm_file_saver_get_sheet (fs, wb_view);
	total_range = file_saver_sheet_get_extent (current_sheet);

	/* This is the preamble of the LaTeX2e file. */
	latex2e_write_file_header(output, current_sheet, &total_range);

	num_cols = total_range.end.col - total_range.start.col + 1;

	gsf_output_printf (output, "\\setlength\\gnumericTableWidth{%%\n");
	for (col = total_range.start.col; col <=  total_range.end.col; col++) {
		ColRowInfo const * ci;
		ci = sheet_col_get_info (current_sheet, col);
		gsf_output_printf (output, "\t%ipt+%%\n", ci->size_pixels * 10 / 12);
	}
	gsf_output_printf (output, "0pt}\n\\def\\gumericNumCols{%i}\n", num_cols);

	gsf_output_puts (output, ""
"\\setlength\\gnumericTableWidthComplete{\\gnumericTableWidth+%\n"
"         \\tabcolsep*\\gumericNumCols*2+\\arrayrulewidth*\\gumericNumCols}\n"
"\\ifthenelse{\\lengthtest{\\gnumericTableWidthComplete > \\linewidth}}%\n"
"         {\\def\\gnumericScale{1*\\ratio{\\linewidth-%\n"
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
);

	for (col = total_range.start.col; col <=  total_range.end.col; col++) {
		ColRowInfo const * ci;
		char const *colname = col_name (col);

		ci = sheet_col_get_info (current_sheet, col);
		gsf_output_printf (output, "\\ifthenelse{\\isundefined{\\gnumericCol%s}}"
				   "{\\newlength{\\gnumericCol%s}}{}\\settowidth{\\gnumericCol%s}"
				   "{\\begin{tabular}{@{}p{%ipt*\\gnumericScale}@{}}x\\end{tabular}}\n",
				   colname, colname, colname, ci->size_pixels * 10 / 12);
	}

	/* Start outputting the table. */
	gsf_output_printf (output, "\n\\begin{longtable}[c]{%%\n");
	for (col = total_range.start.col; col <=  total_range.end.col; col++) {
		gsf_output_printf (output, "\tb{\\gnumericCol%s}%%\n", col_name (col));
	}
	gsf_output_printf (output, "\t}\n\n");

	/* Output the table header. */
	latex2e_write_table_header (output, num_cols);


	/* Step through the sheet, writing cells as appropriate. */
	for (row = total_range.start.row; row <= total_range.end.row; row++) {
		ColRowInfo const * ri;
		ri = sheet_row_get_info (current_sheet, row);
		if (ri->needs_respan)
			row_calc_spans ((ColRowInfo *) ri, row, current_sheet);

		/* We need to check for horizontal borders at the top of this row */
		length = num_cols;
		clines = g_new0 (GnmStyleBorderType, length);
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
		next_vert = g_new0 (GnmStyleBorderType, num_cols + 1);
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
			latex2e_print_hhline (output, clines, num_cols, prev_vert, next_vert);
		g_free (clines);

		for (col = total_range.start.col; col <= total_range.end.col; col++) {
			GnmCellPos pos;

			pos.col = col;
			pos.row = row;

			/* Get the cell. */
			cell = sheet_cell_get (current_sheet, col, row);

			/* Check if we are not the first cell in the row.*/
			if (col != total_range.start.col)
				gsf_output_printf (output, "\t&");
			else
				gsf_output_printf (output, "\t ");

			/* Check a merge. */
			merge_range = gnm_sheet_merge_is_corner (current_sheet, &pos);
			if (merge_range == NULL) {
				if (gnm_cell_is_empty(cell))
					latex2e_write_blank_multicolumn_cell(output, col, row,
									     1, 1,
							       col - total_range.start.col,
							       next_vert, current_sheet);
				else
					latex2e_write_multicolumn_cell(output, cell, col,
								       1, 1,
							       col - total_range.start.col,
							       next_vert, current_sheet);
				continue;
			}

			/* Get the extent of the merge. */
			num_merged_cols = merge_range->end.col - merge_range->start.col + 1;
			num_merged_rows = merge_range->end.row - merge_range->start.row + 1;

			if (gnm_cell_is_empty(cell))
				latex2e_write_blank_multicolumn_cell(output, col, row,
								     num_merged_cols,
								     num_merged_rows,
								     col - total_range.start.col,
								     next_vert, current_sheet);
			else
				latex2e_write_multicolumn_cell(output, cell, col, num_merged_cols,
							       num_merged_rows,
							       col - total_range.start.col,
							       next_vert, current_sheet);
			col += (num_merged_cols - 1);
			continue;
		}
		gsf_output_printf (output, "\\\\\n");
		g_free (prev_vert);
	}

	/* We need to check for horizontal borders at the bottom  of  the last  row */
	clines = g_new0 (GnmStyleBorderType, total_range.end.col - total_range.start.col + 1);
	needs_hline = FALSE;
	/* In case that we are at the very bottom of the sheet we cannot */
	/* check on the next line! */
	if (row < colrow_max (FALSE, current_sheet)) {
		length = num_cols;
		this_clines = clines;
		for (col = total_range.start.col; col <= total_range.end.col; col++) {
			needs_hline = latex2e_find_hhlines (this_clines, length,  col, row,
							    current_sheet, MSTYLE_BORDER_TOP)
				|| needs_hline;
			this_clines ++;
			length--;
		}
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
		latex2e_print_hhline (output, clines, num_cols, next_vert, NULL);
	g_free (clines);

	g_free (next_vert);

	gsf_output_puts (output, "\\end{longtable}\n\n"
			 "\\ifthenelse{\\isundefined{\\languageshorthands}}"
			 "{}{\\languageshorthands{\\languagename}}\n"
			 "\\gnumericTableEnd\n");
}


/**
 * latex2e_table_cell:
 *
 * @output: output stream where the cell contents will be written.
 * @cell:   the cell whose contents are to be written.
 *
 * This function creates all the LaTeX code for the cell of a table (i.e. all
 * the code that might fall between two ampersands (&)).
 *
 */
static void
latex2e_table_write_cell (GsfOutput *output, GnmCell *cell)
{
	GnmStyle const *style = gnm_cell_get_effective_style (cell);

	if (gnm_style_get_contents_hidden (style))
		return;

	if (!gnm_cell_is_empty (cell)) {
		char * rendered_string;

		rendered_string = gnm_cell_get_rendered_text (cell);
		latex_fputs (rendered_string, output);
		g_free (rendered_string);
	}
}


/**
 * latex2e_table_write_file_header:
 *
 * @output: Output stream where the cell contents will be written.
 *
 * This ouputs the LaTeX header. Kept separate for esthetics.
 */

static void
latex2e_table_write_file_header(GsfOutput *output)
{
	gsf_output_puts (output,
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
"%%                                                                  %%\n"
"%%  This is a LaTeX2e table fragment exported from Gnumeric.        %%\n"
"%%                                                                  %%\n"
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
        );
}

/**
 * latex_table_file_save_impl :  The LaTeX2e exporter plugin function.
 *
 * @WorkbookView:     this provides the way to access the sheet being exported.
 * @outpu:            where we'll write.
 * @all:              Whether to write all rows or just the visible ones.
 *
 * This writes the top sheet of a Gnumeric workbook as the content of a latex table environment.
 * We try to avoid all formatting.
 */
static void
latex_table_file_save_impl (GOFileSaver const *fs, WorkbookView const *wb_view,
			    GsfOutput *output, gboolean all)
{
	GnmCell *cell;
	Sheet *current_sheet;
	GnmRange total_range;
	int row, col;

	/* This is the preamble of the LaTeX2e file. */
	latex2e_table_write_file_header(output);

	/* Get the sheet and its range from the plugin function argument. */
	current_sheet = gnm_file_saver_get_sheet (fs, wb_view);
	total_range = file_saver_sheet_get_extent (current_sheet);

	/* Step through the sheet, writing cells as appropriate. */
	for (row = total_range.start.row; row <= total_range.end.row; row++) {
		ColRowInfo const * ri;
		ri = sheet_row_get_info (current_sheet, row);
		if (all || ri->visible) {
			if (ri->needs_respan)
				row_calc_spans ((ColRowInfo *) ri, row, current_sheet);

			for (col = total_range.start.col; col <= total_range.end.col; col++) {
				/* Get the cell. */
				cell = sheet_cell_get (current_sheet, col, row);

				/* Check if we are not the first cell in the row.*/
				if (col != total_range.start.col)
					gsf_output_printf (output, "\t&");

				if (gnm_cell_is_empty (cell))
					continue;

				latex2e_table_write_cell(output, cell);
			}
			gsf_output_printf (output, "\\\\\n");
		}
	}
}

/**
 * latex_table_file_save :  The LaTeX2e exporter plugin function.
 *
 * @FileSaver:        New structure for file plugins. I don't understand.
 * @IOcontext:        currently not used but reserved for the future.
 * @WorkbookView:     this provides the way to access the sheet being exported.
 * @output:           where we'll write.
 *
 * This writes the selected sheet of a Gnumeric workbook as the content of a
 * latex table environment.  We try to avoid all formatting.
 */
void
latex_table_file_save (GOFileSaver const *fs,
		       G_GNUC_UNUSED GOIOContext *io_context,
		       WorkbookView const *wb_view, GsfOutput *output)
{
	latex_table_file_save_impl (fs, wb_view, output, TRUE);
}

/**
 * latex_table_visible_file_save :  The LaTeX2e exporter plugin function.
 *
 * @FileSaver:        New structure for file plugins. I don't understand.
 * @IOcontext:        currently not used but reserved for the future.
 * @WorkbookView:     this provides the way to access the sheet being exported.
 * @output:           where we'll write.
 *
 * This writes the selected sheet of a Gnumeric workbook as the content of a
 * latex table environment.  We try to avoid all formatting.
 */
void
latex_table_visible_file_save (GOFileSaver const *fs,
			       G_GNUC_UNUSED GOIOContext *io_context,
			       WorkbookView const *wb_view, GsfOutput *output)
{
	latex_table_file_save_impl (fs, wb_view, output, FALSE);
}
