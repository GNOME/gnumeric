/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * sylk.c - file import of SYLK files
 * Copyright 1999 Jeff Garzik <jgarzik@mandrakesoft.com>
 *
 * With some code from:
 * csv-io.c: read sheets using a CSV encoding.
 *
 * Miguel de Icaza <miguel@gnu.org>
 * Jody Goldberg   <jody@gnome.org>
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "file.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "cell.h"
#include "sheet.h"
#include "value.h"
#include "error-info.h"
#include "plugin-util.h"
#include "plugin.h"
#include "module-plugin-defs.h"

#include <string.h>
#include <stdlib.h>
#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-input-textline.h>
#include <gsf/gsf-utils.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

gboolean sylk_file_probe (GnumFileOpener const *fo, GsfInput *input, FileProbeLevel pl);
void     sylk_file_open (GnumFileOpener const *fo, IOContext *io_context,
                         WorkbookView *wb_view, GsfInput *input);

#define arraysize(x)     (sizeof(x)/sizeof(*(x)))

typedef struct {
	long     picture_idx;
	unsigned italic : 1;
	unsigned bold : 1;
	unsigned grid_top : 1;
	unsigned grid_bottom : 1;
	unsigned grid_left : 1;
	unsigned grid_right : 1;
} sylk_format_t;


typedef struct {
	GsfInputTextline *input;
	Sheet	 	 *sheet;

	/* SYLK current X/Y pointers (begin at 1) */
	long cur_x, cur_y;

	/* SYLK maximum dimensions (begin at 1) */
	long max_x, max_y;

	/* XXX doesn't really belong here at all */
	ValueType val_type;
	char *val_s;
	long val_l;
	double val_d;

	sylk_format_t def_fmt;

	unsigned got_start : 1;
	unsigned got_end : 1;
	unsigned show_formulas : 1;		/* sheet-wide */
	unsigned show_commas : 1;		/* sheet-wide */
	unsigned hide_rowcol_hdrs : 1;		/* sheet-wide */
	unsigned hide_def_gridlines : 1;	/* sheet-wide */

	GIConv          converter;
} SylkReadState;

static size_t
sylk_next_token_len (const char *line)
{
	size_t len = 0;

	while (1) {
		if (!*line ||
		    (*line == ';' && *(line + 1) != ';'))
			break;

		len++;
		line++;

		g_assert (len < 10000);
	}

	return len;
}


static void
sylk_parse_value (SylkReadState *state, const char *str,
		  size_t *len)
{
	const char *s;

	state->val_type = VALUE_EMPTY;
	if (state->val_s) {
		g_free (state->val_s);
		state->val_s = NULL;
	}

	*len = sylk_next_token_len (str);

	/* error strings start with '#' */
	if (*str == '#') {
		/* ignore for now */
		state->val_type = VALUE_EMPTY;
		return;
	}

	/* remaining non-strings, floats and ints */
	else if (*str != '"') {
		/* floats */
		if (strchr (str, '.')) {
			state->val_type = VALUE_FLOAT;
			state->val_d = g_strtod (str, NULL);
		}

		/* ints */
		else {
			state->val_type = VALUE_INTEGER;
			state->val_l = strtol (str, NULL, 10);
		}

		return;
	}

	/* boolean values */
	else if (!strcmp (str,"\"TRUE\"") ||
		 !strcmp (str,"\"FALSE\"")) {
		state->val_type = VALUE_BOOLEAN;
		state->val_l = (strcmp(str,"\"TRUE\"") == 0 ? TRUE : FALSE);
		return;
	}

	/* the remaining case: strings */

	state->val_type = VALUE_STRING;
	*len = 1;
	str++;

	/* XXX does not handle " inside of quoted string */
	s = strchr (str, '"');
	if (!s) {
		state->val_s = g_strdup (str);
		*len += strlen (str);
	} else {
		*len += (s - str + 1);
		state->val_s = g_strndup (str, (*len) - 2);
	}
}


static gboolean
sylk_rtd_c_parse (SylkReadState *state, const char *str)
{
	size_t len;

	len = sylk_next_token_len (str);
	while (str && *str && len > 0) {
		switch (*str) {
			case 'K':
				str++;
				sylk_parse_value (state, str, &len);
				break;
			case 'X':
				state->cur_x = strtol (str + 1, NULL, 10);
				break;
			case 'Y':
				state->cur_y = strtol (str + 1, NULL, 10);
				break;
			default:
				/* do nothing */
				break;
		}

		str += (len + 1);
		len = sylk_next_token_len (str);
	}

	if (state->val_type != VALUE_EMPTY) {
		Cell *cell = sheet_cell_fetch (state->sheet, state->cur_x - 1,
					       state->cur_y - 1);
		g_assert (cell);

		if (state->val_type == VALUE_STRING)
			cell_set_text (cell, state->val_s);

		else {
			Value *v;

			if (state->val_type == VALUE_FLOAT)
				v = value_new_float (state->val_d);
			else if (state->val_type == VALUE_BOOLEAN)
				v = value_new_bool (state->val_l);
			else
				v = value_new_int (state->val_l);

			g_assert (v);
			cell_set_value (cell, v);
		}
	}

	state->val_type = VALUE_EMPTY;
	if (state->val_s) {
		g_free (state->val_s);
		state->val_s = NULL;
	}

	return TRUE;
}


static gboolean
sylk_rtd_f_parse (SylkReadState *state, const char *str)
{
	size_t len;

	len = sylk_next_token_len (str);
	while (str && *str && len > 0) {
		switch (*str) {
			case 'E':
				state->show_formulas = TRUE;
				break;
			case 'G':
				state->hide_def_gridlines = TRUE;
				break;
			case 'H':
				state->hide_rowcol_hdrs = TRUE;
				break;
			case 'K':
				state->show_commas = TRUE;
				break;
			case 'P':
				state->def_fmt.picture_idx = atol (str + 1);
				break;
			case 'S':
				str++;
				switch (*str) {
					case 'I':
						state->def_fmt.italic = TRUE;
						break;
					case 'D':
						state->def_fmt.bold = TRUE;
						break;
					case 'T':
						state->def_fmt.grid_top = TRUE;
						break;
					case 'L':
						state->def_fmt.grid_bottom = TRUE;
						break;
					case 'B':
						state->def_fmt.grid_left = TRUE;
						break;
					case 'R':
						state->def_fmt.grid_right = TRUE;
						break;
					default:
						g_warning ("unhandled style S%c.", *str);
						break;
				}
				str--;
				break;
			case 'X':
				state->cur_x = atoi (str + 1);
				break;
			case 'Y':
				state->cur_y = atoi (str + 1);
				break;
			default:
				g_warning ("unhandled F option %c.", *str);
				break;
		}

		str += (len + 1);
		len = sylk_next_token_len (str);
	}

	return TRUE;
}


static gboolean
sylk_rtd_b_parse (SylkReadState *state, const char *str)
{
	size_t len;

	len = sylk_next_token_len (str);
	while (str && *str && len > 0) {
		switch (*str) {
			case 'X':
				state->max_x = atoi (str + 1);
				break;
			case 'Y':
				state->max_y = atoi (str + 1);
				break;
			default:
				/* do nothing */
				break;
		}

		str += (len + 1);
		len = sylk_next_token_len (str);
	}

	return TRUE;
}


static gboolean
sylk_rtd_e_parse (SylkReadState *state, const char *str)
{
	state->got_end = TRUE;
	return TRUE;
}


static gboolean
sylk_rtd_id_parse (SylkReadState *state, const char *str)
{
	state->got_start = TRUE;
	return TRUE;
}


static gboolean
sylk_parse_line (SylkReadState *state, char *buf)
{
	static const struct {
		char const *name;
		gboolean (*handler) (SylkReadState *state, const char *str);
	} sylk_rtd_list[] = {
		{ "B;",  sylk_rtd_b_parse },
		{ "C;",  sylk_rtd_c_parse },
		{ "E;",  sylk_rtd_e_parse },
		{ "F;",  sylk_rtd_f_parse },
		{ "ID;", sylk_rtd_id_parse },
	};
	unsigned i;

	for (i = 0; i < G_N_ELEMENTS (sylk_rtd_list); i++)
		if (strncmp (sylk_rtd_list [i].name, buf,
			     strlen (sylk_rtd_list [i].name)) == 0) {
			sylk_rtd_list [i].handler (state,
				buf + strlen (sylk_rtd_list [i].name));
			return TRUE;
		}

	fprintf (stderr, "unhandled directive: '%s'\n", buf);

	return TRUE;
}

static void
sylk_parse_sheet (SylkReadState *state, ErrorInfo **ret_error)
{
	char *buf;

	*ret_error = NULL;

	if ((buf = gsf_input_textline_ascii_gets (state->input)) == NULL ||
	    strncmp ("ID;", buf, 3)) {
		*ret_error = error_info_new_str (_("Not SYLK file"));
		return;
	}

	while ((buf = gsf_input_textline_ascii_gets (state->input)) != NULL) {
		char *utf8buf;
		g_strchomp (buf);

		utf8buf = g_convert_with_iconv (buf, -1, state->converter, NULL, NULL, NULL);

		if (utf8buf[0] && !sylk_parse_line (state, utf8buf)) {
			g_free (utf8buf);
			*ret_error = error_info_new_str (_("error parsing line\n"));
			return;
		}

		g_free (utf8buf);
	}
}

void
sylk_file_open (GnumFileOpener const *fo,
		IOContext	*io_context,
                WorkbookView	*wb_view,
		GsfInput	*input)
{
	SylkReadState state;
	char const *input_name;
	char *name, *base;
	Workbook *book = wb_view_workbook (wb_view);
	ErrorInfo *sheet_error;

	input_name = gsf_input_name (input);
	if (input_name == NULL)
		input_name = "";
	base = g_path_get_basename (input_name);
	name = g_strdup_printf (_("Imported %s"), base);

	memset (&state, 0, sizeof (state));
	state.input = gsf_input_textline_new (input);
	state.sheet = sheet_new (book, name);
	state.cur_x = state.cur_y = 1;
	state.converter	 = g_iconv_open ("UTF-8", "ISO-8859-1");

	workbook_sheet_attach (book, state.sheet, NULL);
	g_free (name);
	g_free (base);

	sylk_parse_sheet (&state, &sheet_error);
	if (sheet_error != NULL)
		gnumeric_io_error_info_set (io_context,
		                            error_info_new_str_with_details (
		                            _("Error while reading sheet."),
		                            sheet_error));
	g_object_unref (G_OBJECT (state.input));
	gsf_iconv_close (state.converter);
}

gboolean
sylk_file_probe (GnumFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	char const *header = NULL;
	if (!gsf_input_seek (input, 0, G_SEEK_SET))
		header = gsf_input_read (input, 3, NULL);
	return (header != NULL && strncmp (header, "ID;", 3) == 0);
}
