/*
 * sylk.c - file import of SYLK files
 * Copyright 1999 Jeff Garzik <jgarzik@mandrakesoft.com>
 *
 * With some code from:
 * csv-io.c: read sheets using a CSV encoding.
 *
 * Miguel de Icaza <miguel@gnu.org>
 * Jody Goldberg   <jgoldberg@home.com>
 */

#include <config.h>
#include <gnome.h>
#include <errno.h>
#include "gnumeric.h"
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

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

gboolean sylk_file_probe (GnumFileOpener const *fo, const gchar *filename, FileProbeLevel pl);
void     sylk_file_open (GnumFileOpener const *fo, IOContext *io_context,
                         WorkbookView *wb_view, const char *filename);

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
	/* input data */
	FILE *f;

	/* gnumeric sheet */
	Sheet *sheet;

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
} sylk_file_state_t;


/* why?  because we must handle Mac text files, and because I'm too lazy to make it fast */
static char *
fgets_mac (char *s, size_t s_len, FILE *f)
{
	size_t read = 0;
	char *orig_s = s;

	s_len--;

	while (!ferror (f) && !feof (f) && (read < s_len)) {
		*s = (char) fgetc (f);
		if (*s == EOF)
			break;

		read++;

		if (*s == '\n')
			break;
		if (*s == '\r') {
			int ch = fgetc (f);
			if ((ch != EOF) && (ch != '\n'))
				ungetc (ch, f);
			else if (ch != EOF) {
				*s = '\n';
				read++;
			}
			break;
		}

		s++;
	}

	if (read > 0) {
		orig_s [read] = 0;
		return orig_s;
	}

	return NULL;
}


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
sylk_parse_value (sylk_file_state_t *src, const char *str,
		  size_t *len)
{
	const char *s;

	src->val_type = VALUE_EMPTY;
	if (src->val_s) {
		g_free (src->val_s);
		src->val_s = NULL;
	}

	*len = sylk_next_token_len (str);

	/* error strings start with '#' */
	if (*str == '#') {
		/* ignore for now */
		src->val_type = VALUE_EMPTY;
		return;
	}

	/* remaining non-strings, floats and ints */
	else if (*str != '"') {
		/* floats */
		if (strchr (str, '.')) {
			src->val_type = VALUE_FLOAT;
			src->val_d = g_strtod (str, NULL);
		}

		/* ints */
		else {
			src->val_type = VALUE_INTEGER;
			src->val_l = strtol (str, NULL, 10);
		}

		return;
	}

	/* boolean values */
	else if (!strcmp (str,"\"TRUE\"") ||
		 !strcmp (str,"\"FALSE\"")) {
		src->val_type = VALUE_BOOLEAN;
		src->val_l = (strcmp(str,"\"TRUE\"") == 0 ? TRUE : FALSE);
		return;
	}

	/* the remaining case: strings */

	src->val_type = VALUE_STRING;
	*len = 1;
	str++;

	/* XXX does not handle " inside of quoted string */
	s = strchr (str, '"');
	if (!s) {
		src->val_s = g_strdup (str);
		*len += strlen (str);
	} else {
		*len += (s - str + 1);
		src->val_s = g_strndup (str, (*len) - 2);
	}
}


static gboolean
sylk_rtd_c_parse (sylk_file_state_t *src, const char *str)
{
	size_t len;

	len = sylk_next_token_len (str);
	while (str && *str && len > 0) {
		switch (*str) {
			case 'K':
				str++;
				sylk_parse_value (src, str, &len);
				break;
			case 'X':
				src->cur_x = strtol (str + 1, NULL, 10);
				break;
			case 'Y':
				src->cur_y = strtol (str + 1, NULL, 10);
				break;
			default:
				/* do nothing */
				break;
		}

		str += (len + 1);
		len = sylk_next_token_len (str);
	}

	if (src->val_type != VALUE_EMPTY) {
		Cell *cell = sheet_cell_fetch (src->sheet, src->cur_x - 1,
					       src->cur_y - 1);
		g_assert (cell);

		if (src->val_type == VALUE_STRING)
			cell_set_text (cell, src->val_s);

		else {
			Value *v;

			if (src->val_type == VALUE_FLOAT)
				v = value_new_float (src->val_d);
			else if (src->val_type == VALUE_BOOLEAN)
				v = value_new_bool (src->val_l);
			else
				v = value_new_int (src->val_l);

			g_assert (v);
			cell_set_value (cell, v, NULL);
		}

	}

	src->val_type = VALUE_EMPTY;
	if (src->val_s) {
		g_free (src->val_s);
		src->val_s = NULL;
	}

	return TRUE;
}


static gboolean
sylk_rtd_f_parse (sylk_file_state_t *src, const char *str)
{
	size_t len;

	len = sylk_next_token_len (str);
	while (str && *str && len > 0) {
		switch (*str) {
			case 'E':
				src->show_formulas = TRUE;
				break;
			case 'G':
				src->hide_def_gridlines = TRUE;
				break;
			case 'H':
				src->hide_rowcol_hdrs = TRUE;
				break;
			case 'K':
				src->show_commas = TRUE;
				break;
			case 'P':
				src->def_fmt.picture_idx = atol (str + 1);
				break;
			case 'S':
				str++;
				switch (*str) {
					case 'I':
						src->def_fmt.italic = TRUE;
						break;
					case 'D':
						src->def_fmt.bold = TRUE;
						break;
					case 'T':
						src->def_fmt.grid_top = TRUE;
						break;
					case 'L':
						src->def_fmt.grid_bottom = TRUE;
						break;
					case 'B':
						src->def_fmt.grid_left = TRUE;
						break;
					case 'R':
						src->def_fmt.grid_right = TRUE;
						break;
					default:
						g_warning ("unhandled style S%c\n", *str);
						break;
				}
				str--;
				break;
			case 'X':
				src->cur_x = atoi (str + 1);
				break;
			case 'Y':
				src->cur_y = atoi (str + 1);
				break;
			default:
				g_warning ("unhandled F option %c\n", *str);
				break;
		}

		str += (len + 1);
		len = sylk_next_token_len (str);
	}

	return TRUE;
}


static gboolean
sylk_rtd_b_parse (sylk_file_state_t *src, const char *str)
{
	size_t len;

	len = sylk_next_token_len (str);
	while (str && *str && len > 0) {
		switch (*str) {
			case 'X':
				src->max_x = atoi (str + 1);
				break;
			case 'Y':
				src->max_y = atoi (str + 1);
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
sylk_rtd_e_parse (sylk_file_state_t *src, const char *str)
{
	src->got_end = TRUE;
	return TRUE;
}


static gboolean
sylk_rtd_id_parse (sylk_file_state_t *src, const char *str)
{
	src->got_start = TRUE;
	return TRUE;
}


static const struct {
	const char *name;
	gboolean (*handler) (sylk_file_state_t *src, const char *str);
} sylk_rtd_list[] = {
	{ "B;",  sylk_rtd_b_parse },
	{ "C;",  sylk_rtd_c_parse },
	{ "E;",  sylk_rtd_e_parse },
	{ "F;",  sylk_rtd_f_parse },
	{ "ID;", sylk_rtd_id_parse },
};


static gboolean
sylk_parse_line (sylk_file_state_t *src, char *buf)
{
	int i;

	for (i = 0; i < (int) arraysize (sylk_rtd_list); i++)
		if (strncmp (sylk_rtd_list [i].name, buf,
			     strlen (sylk_rtd_list [i].name)) == 0) {
			sylk_rtd_list [i].handler (src,
				buf + strlen (sylk_rtd_list [i].name));
			return TRUE;
		}

	fprintf (stderr, "unhandled directive: '%s'\n", buf);

	return TRUE;
}

static void
sylk_parse_sheet (sylk_file_state_t *src, ErrorInfo **ret_error)
{
	char buf [BUFSIZ];

	*ret_error = NULL;
	if (fgets_mac (buf, sizeof (buf), src->f) == NULL) {
		*ret_error = error_info_new_from_errno ();
		return;
	}

	if (strncmp ("ID;", buf, 3)) {
		*ret_error = error_info_new_str (_("Not SYLK file"));
		return;
	}

	while (fgets_mac (buf, sizeof (buf), src->f) != NULL) {
		g_strchomp (buf);
		if ( buf [0] && !sylk_parse_line (src, buf) ) {
			*ret_error = error_info_new_str (_("error parsing line\n"));
			return;
		}
	}

	if (ferror (src->f)) {
		*ret_error = error_info_new_from_errno ();
		return ;
	}
}

void
sylk_file_open (GnumFileOpener const *fo, IOContext *io_context,
                WorkbookView *wb_view, const char *filename)
{
	/*
	 * TODO : When there is a reasonable error reporting
	 * mechanism use it and put all the error code back
	 */
	sylk_file_state_t src;
	char *name;
	FILE *f;
	Workbook *book = wb_view_workbook (wb_view);
	ErrorInfo *sheet_error;

	f = fopen (filename, "r");
	if (f == NULL) {
		gnumeric_io_error_info_set (io_context,
		                            error_info_new_str_with_details (
		                            _("Error while opening sylk file."),
		                            error_info_new_from_errno ()));
		return;
	}

	name = g_strdup_printf (_("Imported %s"), g_basename (filename));

	memset (&src, 0, sizeof (src));
	src.f	  = f;
	src.sheet = sheet_new (book, name);
	src.cur_x = src.cur_y = 1;

	workbook_sheet_attach (book, src.sheet, NULL);
	g_free (name);

	sylk_parse_sheet (&src, &sheet_error);
	if (sheet_error != NULL) {
		gnumeric_io_error_info_set (io_context,
		                            error_info_new_str_with_details (
		                            _("Error while reading sheet."),
		                            sheet_error));
	}

	fclose(f);
}

gboolean
sylk_file_probe (GnumFileOpener const *fo, const char *filename, FileProbeLevel pl)
{
	char buf [32] = "";
	FILE *f;
	int error;

	f = fopen (filename, "r");
	if (f == NULL) {
		return FALSE;
	}

	fgets (buf, sizeof (buf), f);
	error = ferror (f);
	fclose (f);

	return (!error && strncmp (buf, "ID;", 3) == 0);
}
