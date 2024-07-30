/*
 * html.c
 *
 * Copyright (C) 1999, 2000 Rasca, Berlin
 * EMail: thron@gmx.de
 * Copyright (c) 2001-2013 Andreas J. Guelzow
 * EMail: aguelzow@pyrshep.ca
 * Copyright 2023 Morten Welinder <terra@gnone.org>
 *
 * Contributors :
 *   Almer. S. Tigelaar <almer1@dds.nl>
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

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <goffice/goffice.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet-style.h>
#include <style.h>
#include <style-color.h>
#include "html.h"
#include <cell.h>
#include <sheet.h>
#include <sheet-merge.h>
#include <value.h>
#include "font.h"
#include <cellspan.h>
#include <style-border.h>
#include <rendered-value.h>
#include <style.h>
#include <hlink.h>
#include <gutils.h>

#include <gsf/gsf-output.h>
#include <string.h>

/*
 * html_version_t:
 *
 * version selector
 *
 */
typedef enum {
	HTML40    = 0,
	HTML32    = 1,
	HTML40F   = 2,
	XHTML     = 3
} html_version_t;

/*
 * html_print_encoded:
 *
 * @output: the stream
 * @str: the string
 *
 * print the string to output encoded all special chars
 *
 */
static void
html_print_encoded (GsfOutput *output, char const *str)
{
	gunichar c;

	if (str == NULL)
		return;
	for (; *str != '\0' ; str = g_utf8_next_char (str)) {
		switch (*str) {
			case '<':
				gsf_output_puts (output, "&lt;");
				break;
			case '>':
				gsf_output_puts (output, "&gt;");
				break;
			case '&':
				gsf_output_puts (output, "&amp;");
				break;
			case '\"':
				gsf_output_puts (output, "&quot;");
				break;
			case '\n':
				gsf_output_puts (output, "<br>\n");
				break;
			case '\r':
				gsf_output_puts (output, "<br>\r");
				if( *(str+1) == '\n' ) {
					gsf_output_puts (output, "\n");
					str++;
				}
				break;
			default:
				c = g_utf8_get_char (str);
				if (((c >= 0x20) && (c < 0x80)) ||
				    (c == '\n') || (c == '\r') || (c == '\t'))
					gsf_output_write (output, 1, str);
				else
					gsf_output_printf (output, "&#%u;", c);
				break;
		}
	}
}

static void
html_get_text_color (GnmCell *cell, GnmStyle const *style, guint *r, guint *g, guint *b)
{
	GOColor fore = gnm_cell_get_render_color (cell);

	if (fore == 0)
		*r = *g = *b = 0;
	else {
		*r = GO_COLOR_UINT_R (fore);
		*g = GO_COLOR_UINT_G (fore);
		*b = GO_COLOR_UINT_B (fore);
	}
}
static void
html_get_back_color (GnmStyle const *style, guint *r, guint *g, guint *b)
{
	GnmColor const *color = gnm_style_get_back_color (style);
	*r = GO_COLOR_UINT_R (color->go_color);
	*g = GO_COLOR_UINT_G (color->go_color);
	*b = GO_COLOR_UINT_B (color->go_color);
}

static const char *
underline_span_pango (html_version_t version, PangoUnderline u)
{
	if (u == PANGO_UNDERLINE_NONE)
		return "";
	if (version == HTML32)
		return "<u>";

	switch (u) {
	case PANGO_UNDERLINE_SINGLE:
	case PANGO_UNDERLINE_SINGLE_LINE:
		return "<span class=\"underline\">";
	case PANGO_UNDERLINE_LOW:
		return "<span class=\"lowunderline\">";
	case PANGO_UNDERLINE_DOUBLE:
	case PANGO_UNDERLINE_DOUBLE_LINE:
		return "<span class=\"doubleunderline\">";
	case PANGO_UNDERLINE_ERROR:
	case PANGO_UNDERLINE_ERROR_LINE:
		return "<span class=\"errorunderline\">";
	default:
		return "";
	}
}


static const char *
underline_span (html_version_t version, GnmUnderline u)
{
	if (u == UNDERLINE_NONE)
		return "";
	if (version == HTML32)
		return "<u>";

	switch (u) {
	case UNDERLINE_SINGLE:
		return "<span class=\"underline\">";
	case UNDERLINE_DOUBLE:
		return "<span class=\"doubleunderline\">";
	case UNDERLINE_SINGLE_LOW:
		return "<span class=\"lowunderline\">";
	case UNDERLINE_DOUBLE_LOW:
		return "<span class=\"lowdoubleunderline\">";
	default:
		return "";
	}
}


/*****************************************************************************/

static void
cb_html_add_chars (GsfOutput *output, char const *text, size_t len)
{
	char buffer[1000];

	if (len == 0)
		return;

	if (len < sizeof (buffer)) {
		memcpy (buffer, text, len);
		buffer[len] = 0;
		html_print_encoded (output, buffer);
	} else {
		char * str = g_strndup (text, len);
		html_print_encoded (output, str);
		g_free (str);
	}
}

static char const *
cb_html_attrs_as_string (GsfOutput *output, PangoAttribute *a, html_version_t version)
{
/* 	PangoColor const *c; */
	char const *closure = NULL;

	switch (a->klass->type) {
	case PANGO_ATTR_FAMILY :
		break; /* ignored */
	case PANGO_ATTR_SIZE :
		break; /* ignored */
	case PANGO_ATTR_RISE:
		if (((PangoAttrInt *)a)->value > 5) {
			gsf_output_puts (output, "<sup>");
			closure = "</sup>";
		} else if (((PangoAttrInt *)a)->value < -5) {
			gsf_output_puts (output, "<sub>");
			closure = "</sub>";
		}
		break;
	case PANGO_ATTR_STYLE :
		if (((PangoAttrInt *)a)->value == PANGO_STYLE_ITALIC) {
			gsf_output_puts (output, "<i>");
			closure = "</i>";
		}
		break;
	case PANGO_ATTR_WEIGHT :
		if (((PangoAttrInt *)a)->value > 600){
			gsf_output_puts (output, "<b>");
			closure = "</b>";
		}
		break;
	case PANGO_ATTR_STRIKETHROUGH :
		if (((PangoAttrInt *)a)->value == 1) {
			if (version == HTML32) {
				gsf_output_puts (output, "<strike>");
				closure = "</strike>";
			} else {
				gsf_output_puts
					(output,
					 "<span style=\"text-decoration: "
					 "line-through;\">");
				closure = "</span>";
			}
		}
		break;
	case PANGO_ATTR_UNDERLINE: {
		PangoUnderline u = ((PangoAttrInt *)a)->value;
		if (u != PANGO_UNDERLINE_NONE) {
			gsf_output_puts (output, underline_span_pango (version, u));
			closure = (version == HTML32 ? "</u>" : "</span>");
		}
		break;
	}
	case PANGO_ATTR_FOREGROUND :
/* 		c = &((PangoAttrColor *)a)->color; */
/* 		g_string_append_printf (accum, "[color=%02xx%02xx%02x", */
/* 			((c->red & 0xff00) >> 8), */
/* 			((c->green & 0xff00) >> 8), */
/* 			((c->blue & 0xff00) >> 8)); */
		break;/* ignored */
	default :
		if (a->klass->type ==
		    go_pango_attr_subscript_get_attr_type ()) {
			if (((GOPangoAttrSubscript *)a)->val) {
				gsf_output_puts (output, "<sub>");
				closure = "</sub>";
			}
		} else if (a->klass->type ==
			   go_pango_attr_superscript_get_attr_type ()) {
			if (((GOPangoAttrSuperscript *)a)->val) {
				gsf_output_puts (output, "<sup>");
				closure = "</sup>";
			}
		}
		break; /* ignored */
	}

	return closure;
}

static void
html_new_markup (GsfOutput *output, const PangoAttrList *markup, char const *text,
		 html_version_t version)
{
	int handled = 0;
	PangoAttrIterator * iter;
	int from, to;
	int len = strlen (text);
	GString *closure = g_string_new (NULL);

	iter = pango_attr_list_get_iterator ((PangoAttrList *) markup);

	do {
		GSList *list, *l;

		g_string_erase (closure, 0, -1);
		pango_attr_iterator_range (iter, &from, &to);
		from = (from > len) ? len : from; /* Since "from" can be really big! */
		to = (to > len) ? len : to;       /* Since "to" can be really big!   */
		cb_html_add_chars (output, text + handled, from - handled);
		list = pango_attr_iterator_get_attrs (iter);
		for (l = list; l != NULL; l = l->next) {
			char const *result = cb_html_attrs_as_string (output, l->data, version);
			if (result != NULL)
				g_string_prepend (closure, result);
		}
		g_slist_free (list);
		cb_html_add_chars (output, text + from, to - from);
		gsf_output_puts (output, closure->str);
		handled = to;
	} while (pango_attr_iterator_next (iter));

	g_string_free (closure, TRUE);
	pango_attr_iterator_destroy (iter);

	return;
}


/*****************************************************************************/

static void
html_write_cell_content (GsfOutput *output, GnmCell *cell, GnmStyle const *style, html_version_t version)
{
	guint r = 0;
	guint g = 0;
	guint b = 0;
	char *rendered_string;
	gboolean hidden = gnm_style_get_contents_hidden (style);
	GnmHLink* hlink = gnm_style_get_hlink (style);
	char* hlink_target = NULL;

	if (hlink) {
		const char *target = gnm_hlink_get_target (hlink);
		if (GNM_IS_HLINK_URL (hlink)) {
			hlink_target = go_url_encode (target, 1);
		} else if (GNM_IS_HLINK_EXTERNAL (hlink)) {
			char *et = go_url_encode (target, 1);
			hlink_target = g_strconcat ("file://", et, NULL);
			g_free (et);
		}
	}

	if (version == HTML32 && hidden)
		gsf_output_puts (output, "<!-- 'HIDDEN DATA' -->");
	else {
		if (style != NULL) {
			if (gnm_style_get_font_italic (style))
				gsf_output_puts (output, "<i>");
			if (gnm_style_get_font_bold (style))
				gsf_output_puts (output, "<b>");
			if (gnm_style_get_font_uline (style) != UNDERLINE_NONE) {
				GnmUnderline u = gnm_style_get_font_uline (style);
				gsf_output_puts (output, underline_span (version, u));
			}
			if (font_is_monospaced (style))
				gsf_output_puts (output, "<tt>");
			if (gnm_style_get_font_strike (style)) {
				if (version == HTML32)
					gsf_output_puts (output, "<strike>");
				else
					gsf_output_puts (output,
							 "<span style=\"text-decoration: line-through;\">");
			}
			switch (gnm_style_get_font_script (style)) {
			case GO_FONT_SCRIPT_SUB:
				gsf_output_puts (output, "<sub>");
				break;
			case GO_FONT_SCRIPT_SUPER:
				gsf_output_puts (output, "<sup>");
				break;
			default:
				break;
			}
		}

		if (hlink_target) {
			gsf_output_printf (output, "<a href=\"%s\">", hlink_target);
			g_free (hlink_target);
		}

		if (cell != NULL) {
			const PangoAttrList * markup = NULL;

			if (style != NULL && version != HTML40) {
				html_get_text_color (cell, style, &r, &g, &b);
				if (r > 0 || g > 0 || b > 0)
					gsf_output_printf (output, "<font color=\"#%02X%02X%02X\">", r, g, b);
			}

			if (VALUE_IS_STRING (cell->value)
			    && (VALUE_FMT (cell->value) != NULL)
			    && go_format_is_markup (VALUE_FMT (cell->value)))
				markup = go_format_get_markup (VALUE_FMT (cell->value));

			if (markup != NULL) {
				const char *str = value_peek_string (cell->value);
				html_new_markup (output, markup, str, version);
			} else {
				rendered_string = gnm_cell_get_rendered_text (cell);
				html_print_encoded (output, rendered_string);
				g_free (rendered_string);
			}
		}

		if (r > 0 || g > 0 || b > 0)
			gsf_output_puts (output, "</font>");
		if (hlink_target)
			gsf_output_puts (output, "</a>");
		if (style != NULL) {
			if (gnm_style_get_font_strike (style)) {
				if (version == HTML32)
					gsf_output_puts (output, "</strike>");
				else
					gsf_output_puts (output, "</span>");
			}
			switch (gnm_style_get_font_script (style)) {
			case GO_FONT_SCRIPT_SUB:
				gsf_output_puts (output, "</sub>");
				break;
			case GO_FONT_SCRIPT_SUPER:
				gsf_output_puts (output, "</sup>");
				break;
			default:
				break;
			}
			if (font_is_monospaced (style))
				gsf_output_puts (output, "</tt>");
			if (gnm_style_get_font_uline (style) != UNDERLINE_NONE)
				gsf_output_puts (output, version == HTML32 ? "</u>" : "</span>");
			if (gnm_style_get_font_bold (style))
				gsf_output_puts (output, "</b>");
			if (gnm_style_get_font_italic (style))
				gsf_output_puts (output, "</i>");
		}
	}
}

static char *
html_get_border_style (GnmBorder *border)
{
	GString *text = g_string_new (NULL);

	switch (border->line_type) {
	case GNM_STYLE_BORDER_THIN:
		g_string_append (text, "thin solid");
		break;
	case GNM_STYLE_BORDER_MEDIUM:
		g_string_append (text, "medium solid");
		break;
	case GNM_STYLE_BORDER_DASHED:
		g_string_append (text, "thin dashed");
		break;
	case GNM_STYLE_BORDER_DOTTED:
		g_string_append (text, "thin dotted");
		break;
	case GNM_STYLE_BORDER_THICK:
		g_string_append (text, "thick solid");
		break;
	case GNM_STYLE_BORDER_DOUBLE:
		g_string_append (text, "thick double");
		break;
	case GNM_STYLE_BORDER_HAIR:
		g_string_append (text, "0.5pt solid");
		break;
	case GNM_STYLE_BORDER_MEDIUM_DASH:
		g_string_append (text, "medium dashed");
		break;
	case GNM_STYLE_BORDER_DASH_DOT:
		g_string_append (text, "thin dashed");
		break;
	case GNM_STYLE_BORDER_MEDIUM_DASH_DOT:
		g_string_append (text, "medium dashed");
		break;
	case GNM_STYLE_BORDER_DASH_DOT_DOT:
		g_string_append (text, "thin dotted");
		break;
	case GNM_STYLE_BORDER_MEDIUM_DASH_DOT_DOT:
		g_string_append (text, "medium dotted");
		break;
	case GNM_STYLE_BORDER_SLANTED_DASH_DOT:
		g_string_append (text, "thin dashed");
		break;
	default:
		break;
	}

	if (border->color) {
		guint r, g, b;
		r = GO_COLOR_UINT_R (border->color->go_color);
		g = GO_COLOR_UINT_G (border->color->go_color);
		b = GO_COLOR_UINT_B (border->color->go_color);
		g_string_append_printf (text, " #%02X%02X%02X", r, g, b);
	}

	return g_string_free (text, FALSE);
}

static void
html_write_one_border_style_40 (GsfOutput *output, GnmBorder *border, char const *border_name)
{
	char *text;
	text = html_get_border_style (border);
	if (text == NULL || *text == 0)
		return;
	gsf_output_printf (output, " %s:%s;", border_name, text);
	g_free (text);
}

static void
html_write_border_style_40 (GsfOutput *output, GnmStyle const *style)
{
	GnmBorder *border;

	border = gnm_style_get_border (style, MSTYLE_BORDER_TOP);
	if (!gnm_style_border_is_blank (border))
		html_write_one_border_style_40 (output, border, "border-top");
	border = gnm_style_get_border (style, MSTYLE_BORDER_BOTTOM);
	if (!gnm_style_border_is_blank (border))
		html_write_one_border_style_40 (output, border, "border-bottom");
	border = gnm_style_get_border (style, MSTYLE_BORDER_LEFT);
	if (!gnm_style_border_is_blank (border))
		html_write_one_border_style_40 (output, border, "border-left");
	border = gnm_style_get_border (style, MSTYLE_BORDER_RIGHT);
	if (!gnm_style_border_is_blank (border))
		html_write_one_border_style_40 (output, border, "border-right");
}

static void
html_write_border_style_40_for_merged_cell (GsfOutput *output, GnmStyle const *style,
					    Sheet *sheet, gint row, gint col)
{
	GnmBorder *border;
	GnmRange const *merge_range;
	GnmCellPos pos;
	pos.col = col;
	pos.row = row;


	border = gnm_style_get_border (style, MSTYLE_BORDER_TOP);
	if (!gnm_style_border_is_blank (border))
		html_write_one_border_style_40 (output, border, "border-top");
	border = gnm_style_get_border (style, MSTYLE_BORDER_LEFT);
	if (!gnm_style_border_is_blank (border))
		html_write_one_border_style_40 (output, border, "border-left");

	merge_range = gnm_sheet_merge_contains_pos (sheet, &pos);
	if (merge_range != NULL) {
		style = sheet_style_get (sheet, merge_range->end.col, merge_range->end.row);
		if (style == NULL)
			return;
	}

	border = gnm_style_get_border (style, MSTYLE_BORDER_BOTTOM);
	if (!gnm_style_border_is_blank (border))
		html_write_one_border_style_40 (output, border, "border-bottom");
	border = gnm_style_get_border (style, MSTYLE_BORDER_RIGHT);
	if (!gnm_style_border_is_blank (border))
		html_write_one_border_style_40 (output, border, "border-right");
}

static void
write_cell (GsfOutput *output, Sheet *sheet, gint row, gint col, html_version_t version, gboolean is_merge)
{
	GnmCell *cell;
	GnmStyle const *style;
	guint r, g, b;

	style = sheet_style_get (sheet, col, row);
	if (style != NULL && version != HTML32 && version != HTML40 &&
	    gnm_style_get_pattern (style) != 0 &&
	    gnm_style_is_element_set (style, MSTYLE_COLOR_BACK)) {
		html_get_back_color (style, &r, &g, &b);
		gsf_output_printf (output, " bgcolor=\"#%02X%02X%02X\"", r, g, b);
	}

	cell = sheet_cell_get (sheet, col, row);
	if (cell != NULL) {

		switch (gnm_style_get_align_v (style)) {
		case GNM_VALIGN_TOP:
			gsf_output_puts (output, " valign=\"top\" ");
			break;
		case GNM_VALIGN_BOTTOM:
			gsf_output_puts (output, " valign=\"bottom\" ");
			break;
		case GNM_VALIGN_DISTRIBUTED:
		case GNM_VALIGN_CENTER:
			gsf_output_puts (output, " valign=\"center\" ");
			break;
		case GNM_VALIGN_JUSTIFY:
			gsf_output_puts (output, " valign=\"baseline\" ");
			break;
		default:
			break;
		}
		switch (gnm_style_default_halign (style, cell)) {
		case GNM_HALIGN_RIGHT:
			gsf_output_puts (output, " align=\"right\" ");
			break;
		case GNM_HALIGN_DISTRIBUTED:
		case GNM_HALIGN_CENTER:
		case GNM_HALIGN_CENTER_ACROSS_SELECTION:
			gsf_output_puts (output, " align=\"center\" ");
			break;
		case GNM_HALIGN_LEFT:
			gsf_output_puts (output, " align=\"left\" ");
			break;
		case GNM_HALIGN_JUSTIFY:
			gsf_output_puts (output, " align=\"justify\" ");
			break;
		default:
			break;
		}

	}
	if (version == HTML40 || version == HTML40F  || version == XHTML) {
		if (style != NULL) {
			gsf_output_printf (output, " style=\"");
			if (gnm_style_get_pattern (style) != 0 &&
			    gnm_style_is_element_set (style, MSTYLE_COLOR_BACK)) {
				html_get_back_color (style, &r, &g, &b);
				gsf_output_printf (output, "background:#%02X%02X%02X;", r, g, b);
			}
			if (cell != NULL) {
				gint size = (int) (gnm_style_get_font_size (style) + 0.5);
				gsf_output_printf (output, " font-size:%ipt;", size);
				html_get_text_color (cell, style, &r, &g, &b);
				if (r > 0 || g > 0 || b > 0)
					gsf_output_printf (output, " color:#%02X%02X%02X;", r, g, b);
				if (gnm_style_get_contents_hidden (style))
					gsf_output_puts (output, " visibility:hidden;");
			}
			if (is_merge)
				html_write_border_style_40_for_merged_cell (output, style, sheet, row, col);
			else
				html_write_border_style_40 (output, style);
			gsf_output_printf (output, "\"");
		}
	}
	gsf_output_printf (output, ">");
	html_write_cell_content (output, cell, style, version);
	gsf_output_puts (output, "</td>\n");
}


/*
 * write_row:
 *
 * @output: the stream
 * @sheet: the gnumeric sheet
 * @row: the row number
 *
 * Set up a TD node for each cell in the given row, witht eh  appropriate
 * colspan and rowspan.
 * Call write_cell for each cell.
 */
static void
write_row (GsfOutput *output, Sheet *sheet, gint row, GnmRange *range, html_version_t version)
{
	gint col;
	ColRowInfo const *ri = sheet_row_get_info (sheet, row);
	if (ri->needs_respan)
		row_calc_spans ((ColRowInfo *) ri, row, sheet);

	for (col = range->start.col; col <= range->end.col; col++) {
		CellSpanInfo const *the_span;
		GnmRange const *merge_range;
		GnmCellPos pos;
		pos.col = col;
		pos.row = row;

		/* Is this a span */
		the_span = row_span_get (ri, col);
		if (the_span != NULL) {
			gsf_output_printf (output, "<td colspan=\"%i\" ", the_span->right - col + 1);
			write_cell (output, sheet, row, the_span->cell->pos.col, version, FALSE);
			col = the_span->right;
			continue;
		}

                /* is this covered by a merge */
		merge_range = gnm_sheet_merge_contains_pos	(sheet, &pos);
		if (merge_range != NULL) {
			if (merge_range->start.col != col ||
			    merge_range->start.row != row)
				continue;
			gsf_output_printf (output, "<td colspan=\"%i\" rowspan=\"%i\" ",
				 merge_range->end.col - merge_range->start.col + 1,
				 merge_range->end.row - merge_range->start.row + 1);
			write_cell (output, sheet, row, col, version, TRUE);
			col = merge_range->end.col;
			continue;
		}
		gsf_output_puts (output, "<td ");
		write_cell (output, sheet, row, col, version, FALSE);
	}
}

/*
 * write_sheet:
 *
 * @output: the stream
 * @sheet: the gnumeric sheet
 *
 * set up a table and call write_row for each row
 */
static void
write_sheet (GsfOutput *output, Sheet *sheet,
	     html_version_t version, GOFileSaveScope save_scope)
{
	GnmRange total_range;
	gint row;

	switch (version) {
	case HTML40:
	case HTML40F:
	case XHTML:
		gsf_output_puts (output, "<p></p><table cellspacing=\"0\" cellpadding=\"3\">\n");
		break;
	default:
		gsf_output_puts (output, "<p><table border=\"1\">\n");
		break;
	}

	if (save_scope != GO_FILE_SAVE_RANGE) {
		gsf_output_puts (output, "<caption>");
		html_print_encoded (output, sheet->name_unquoted);
		gsf_output_puts (output, "</caption>\n");
	}
	total_range = sheet_get_extent (sheet, TRUE, TRUE);
	for (row = total_range.start.row; row <=  total_range.end.row; row++) {
		gsf_output_puts (output, "<tr>\n");
		write_row (output, sheet, row, &total_range, version);
		gsf_output_puts (output, "</tr>\n");
	}
	gsf_output_puts (output, "</table>\n");
}

/*
 * html_file_save:
 *
 * write the html file (version of html according to version argument)
 */
static void
html_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		WorkbookView const *wb_view, GsfOutput *output, html_version_t version)
{
	Workbook *wb = wb_view_get_workbook (wb_view);
	GOFileSaveScope save_scope;
	GPtrArray *sel;
	unsigned ui;

	g_return_if_fail (fs != NULL);
	g_return_if_fail (wb != NULL);
	g_return_if_fail (output != NULL);

	switch (version) {
	case HTML32:
	gsf_output_puts (output,
"<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n"
"<html>\n"
"<head>\n\t<title>Tables</title>\n"
"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n"
"<meta name=\"generator\" content=\"Gnumeric " GNM_VERSION_FULL  " via " G_PLUGIN_FOR_HTML "\">\n"
"<style><!--\n"
"tt {\n"
"\tfont-family: courier;\n"
"}\n"
"td {\n"
"\tfont-family: helvetica, sans-serif;\n"
"}\n"
"caption {\n"
"\tfont-family: helvetica, sans-serif;\n"
"\tfont-size: 14pt;\n"
"\ttext-align: left;\n"
"}\n"
"--></style>\n"
"</head>\n<body>\n");
		break;
	case HTML40:
		gsf_output_puts (output,
"<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\"\n"
"\t\t\"http://www.w3.org/TR/html4/strict.dtd\">\n"
"<html>\n"
"<head>\n\t<title>Tables</title>\n"
"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n"
"<meta name=\"generator\" content=\"Gnumeric " GNM_VERSION_FULL  " via " G_PLUGIN_FOR_HTML "\">\n"
"<style type=\"text/css\">\n"
"tt {\n"
"\tfont-family: courier;\n"
"}\n"
"td {\n"
"\tfont-family: helvetica, sans-serif;\n"
"}\n"
"caption {\n"
"\tfont-family: helvetica, sans-serif;\n"
"\tfont-size: 14pt;\n"
"\ttext-align: left;\n"
"}\n"
".underline { text-decoration: underline; }\n"
".lowunderline { text-decoration: underline; text-underline-offset: 0.4em; }\n"
".doubleunderline { text-decoration: underline double; }\n"
".lowdoubleunderline { text-decoration: underline double; text-underline-offset: 0.4em; }\n"
".errorunderline { text-decoration: underline wavy; }\n"
"</style>\n"
"</head>\n<body>\n");
		break;
	case XHTML  :
		gsf_output_puts (output,
"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\n"
"\t\t\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n"
"<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n"
"<head>\n\t<title>Tables</title>\n"
"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />\n"
"<meta name=\"generator\" content=\"Gnumeric " GNM_VERSION_FULL  " via " G_PLUGIN_FOR_HTML "\" />\n"
"<style type=\"text/css\">\n"
"tt {\n"
"\tfont-family: courier;\n"
"}\n"
"td {\n"
"\tfont-family: helvetica, sans-serif;\n"
"}\n"
"caption {\n"
"\tfont-family: helvetica, sans-serif;\n"
"\tfont-size: 14pt;\n"
"\ttext-align: left;\n"
"}\n"
".underline { text-decoration: underline; }\n"
".lowunderline { text-decoration: underline; text-underline-offset: 0.4em; }\n"
".doubleunderline { text-decoration: underline double; }\n"
".lowdoubleunderline { text-decoration: underline double; text-underline-offset: 0.4em; }\n"
".errorunderline { text-decoration: underline wavy; }\n"
"</style>\n"
"</head>\n<body>\n");
		break;
	default:
		break;
	}

	save_scope = go_file_saver_get_save_scope (fs);

	sel = gnm_file_saver_get_sheets (fs, wb_view, TRUE);
	for (ui = 0; ui < sel->len; ui++) {
		Sheet *sheet = g_ptr_array_index (sel, ui);
		write_sheet (output, sheet, version, save_scope);
	}
	g_ptr_array_unref (sel);

	if (version == HTML32 || version == HTML40 || version == XHTML)
		gsf_output_puts (output, "</body>\n</html>\n");
}

void
html40_file_save (GOFileSaver const *fs, GOIOContext *io_context,
                  WorkbookView const *wb_view, GsfOutput *output)
{
	html_file_save (fs, io_context, wb_view, output, HTML40);
}

void
html32_file_save (GOFileSaver const *fs, GOIOContext *io_context,
                  WorkbookView const *wb_view, GsfOutput *output)
{
	html_file_save (fs, io_context, wb_view, output, HTML32);
}

void
html40frag_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		      WorkbookView const *wb_view, GsfOutput *output)
{
	html_file_save (fs, io_context, wb_view, output, HTML40F);
}

void
xhtml_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		 WorkbookView const *wb_view, GsfOutput *output)
{
	html_file_save (fs, io_context, wb_view, output, XHTML);
}

void
xhtml_range_file_save (GOFileSaver const *fs, GOIOContext *io_context,
		      WorkbookView const *wb_view, GsfOutput *output)
{
	/* Identical, but fs->save_scope is different */
	xhtml_file_save (fs, io_context, wb_view, output);
}
