/*
 * csv-io.c: read sheets using a CSV encoding.
 *
 * Miguel de Icaza <miguel@gnu.org>
 * Jody Goldberg   <jgoldberg@home.com>
 */

#include <config.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <gnome.h>
#include "plugin.h"
#include "gnumeric.h"
#include "file.h"

#define arraysize(x)     (sizeof(x)/sizeof(*(x)))

struct sylk_file_state
{
	/* input data */
	FILE *f;

	/* gnumeric sheet */
	Sheet *sheet;
	
	/* SYLK current X/Y pointers (begin at 1) */
	int cur_x, cur_y;
	
	ValueType val_type;
	char *val_s;
	long val_l;
	double val_d;
	
	gboolean not_first_line;
};

struct sylk_line {
	GList *lines;
};

static size_t sylk_next_token_len (const char *line)
{
	size_t len = 0;

	while (1) {
		if (!*line ||
		    (*line == ';' && *(line + 1) != ';'))
			break;
		
		len++;
		line++;
	}

	return len;
}


static void sylk_parse_value (struct sylk_file_state *src, const char *str,
			      size_t *len)
{
	const char *s;

	*len = sylk_next_token_len (str);

	if (*str != '"') {
		if (strchr (str, '.')) {
			src->val_type = VALUE_FLOAT;
			src->val_d = strtod (str, NULL);
		} else {
			src->val_type = VALUE_INTEGER;
			src->val_d = atol (str);
		}
		
		return;
	}
	
	src->val_type = VALUE_STRING;
	*len = 1;
	str++;

	s = strchr (str, '"');
	if (!s) {
		src->val_s = g_strdup (str);
		*len += strlen (str);
	} else {
		/* XXX does not handle " inside of quoted string */
		*len += (s - str + 1);
		src->val_s = g_strndup (str, (*len) - 2);
	}
}


static gboolean sylk_rtd_c_parse (struct sylk_file_state *src, const char *str)
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
				src->cur_x = atoi (str + 1);
				break;
			case 'Y':
				src->cur_y = atoi (str + 1);
				break;
			default:
				/* do nothing */
				break;
		}

		str += (len + 1);
		len = sylk_next_token_len (str);
	}

	if (src->val_type != VALUE_EMPTY) {
		/* XXX memory leak / bug, if X/Y location appears
		 * more than once */
		Cell *cell = sheet_cell_new (src->sheet, src->cur_x - 1,
					     src->cur_y - 1);
		g_assert (cell);
		
		if (src->val_type == VALUE_STRING)
			cell_set_text_simple (cell, src->val_s);
		else {
			Value v;

			v.type = src->val_type;
			if (v.type == VALUE_FLOAT)
				v.v.v_float = src->val_d;
			else
				v.v.v_int = src->val_l;

			cell_set_value_simple (cell, &v);
		}
			
	}
	
	if (src->val_s) {
		g_free (src->val_s);
		src->val_s = NULL;
	}
	
	src->val_type = VALUE_EMPTY;
	
	return TRUE;
}


static gboolean sylk_rtd_f_parse (struct sylk_file_state *src, const char *str)
{
	size_t len;

	len = sylk_next_token_len (str);
	while (str && *str && len > 0) {
		switch (*str) {
			case 'X':
				src->cur_x = atoi (str + 1);
				break;
			case 'Y':
				src->cur_y = atoi (str + 1);
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


static const struct {
	const char *name;
	gboolean (*handler) (struct sylk_file_state *src, const char *str);
} sylk_rtd_list[] = {
	{ "F;", sylk_rtd_f_parse },
	{ "C;", sylk_rtd_c_parse },
};


static gboolean
sylk_parse_line (struct sylk_file_state *src, char *buf)
{
	int i;
	gboolean result = TRUE;
	
	for (i = 0; i < arraysize (sylk_rtd_list); i++)
		if (strncmp (sylk_rtd_list[i].name, buf,
			     strlen (sylk_rtd_list[i].name)) == 0) {
			result = sylk_rtd_list[i].handler (src,
				buf + strlen (sylk_rtd_list[i].name));
			break;
		}
	
	return result;
}

static gboolean
sylk_parse_sheet (struct sylk_file_state *src)
{
	char buf [BUFSIZ];
	
	while (fgets (buf, sizeof (buf), src->f) != NULL) {
		/* if it's not an SYLK file, bail */
		if (!src->not_first_line) {
			if (strncmp ("ID;", buf, 3))
				return FALSE;
			src->not_first_line = 1;
		}

		g_strchomp (buf);
		if ( buf[0] && !sylk_parse_line (src, buf) )
			return FALSE;
	}

	if (ferror (src->f))
		return FALSE;

	return TRUE;
}

static gboolean
sylk_read_workbook (Workbook *book, char const *filename)
{
	/* TODO : When there is a reasonable error reporting
	 * mechanism use it and put all the error code back
	 */
	struct sylk_file_state src;
	char *name;
	gboolean result;
	FILE *f = fopen(filename, "r");
	if (!f)
		return FALSE;

	name = g_strdup_printf (_("Imported %s"), g_basename (filename));

	memset (&src, 0, sizeof (src));
	src.f	  = f;
	src.sheet = sheet_new (book, name);

	workbook_attach_sheet (book, src.sheet);
	g_free (name);

	result = sylk_parse_sheet (&src);

	fclose(f);
	
	return result;
}

static int
sylk_can_unload (PluginData *pd)
{
	/* We can always unload */
	return TRUE;
}

static void
sylk_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (NULL, sylk_read_workbook);
}

int
init_plugin (PluginData * pd)
{
	file_format_register_open (1, _("MultiPlan (SYLK) import"), NULL, sylk_read_workbook);
	pd->can_unload = sylk_can_unload;
	pd->cleanup_plugin = sylk_cleanup_plugin;
	pd->title = g_strdup (_("MultiPlan (SYLK) file import module"));

	return 0;
}
