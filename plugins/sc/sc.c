/*
 * sc.c - file import of SC/xspread files
 * Copyright 1999 Jeff Garzik <jgarzik@mandrakesoft.com>
 *
 * With some code from sylk.c
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


typedef struct {
	/* input data */
	FILE *f;

	/* gnumeric sheet */
	Sheet *sheet;
} sc_file_state_t;



static void
sc_cellname_to_coords (const char *cellname, int *col, int *row)
{
	int mult;

	if (!cellname || !*cellname || !isalpha(*cellname))
		goto err_out;
	
	mult = *cellname - 'A';
	if (mult < 0 || mult > 25)
		goto err_out;
	
	cellname++;

	if (isalpha(*cellname)) {
		int ofs = *cellname - 'A';
		if (ofs < 0 || ofs > 25)
			goto err_out;
		*col = (mult * 26) + ofs;
		cellname++;
	}
	
	else {
		*col = mult;
	}
	
	if (!isdigit(*cellname))
		goto err_out;

	*row = atoi (cellname);

	g_assert (*col > -1);
	g_assert (*row > -1);
	return;

err_out:
	*col = *row = -1;
}


static void
sc_parse_coord (const char **strdata, int *col, int *row)
{
	const char *s = *strdata, *space, *eq;
	size_t len = strlen (s);
	char tmpstr [16];
	
	space = strchr (s, ' ');
	eq = strstr (s, " = ");
	if (!space || !eq || space == eq)
		return;

	memcpy (tmpstr, space + 1, eq - space - 1);
	tmpstr [eq - space - 1] = 0;
	
	sc_cellname_to_coords (tmpstr, col, row);
	if (*col == -1)
		return;

	if ((eq - s + 1 + 3) > len)
		return;

	*strdata = eq + 3;
}


static gboolean
sc_string_parse (sc_file_state_t *src, const char *str, int col, int row)
{
	Cell *cell;
	char *s = NULL, *tmpout;
	const char *tmpstr;
	gboolean result = FALSE;

	if (!src || !str || *str != '"' || col == -1 || row == -1)
		goto out;
	
	s = tmpout = g_strdup (str);
	if (!s)
		goto out;
	
	tmpstr = str;
	while (*tmpstr) {
		if (*tmpstr != '\\') {
			*tmpout = *tmpstr;
			tmpout++;
		}
		tmpstr++;
	}
	if (*(tmpstr - 1) != '"')
		goto out;
	tmpout--;
	*tmpout = 0;
	
	cell = sheet_cell_fetch (src->sheet, col, row);
	if (!cell)
		goto out;
	
	cell_set_text_simple (cell, s);
	
	result = TRUE;

out:
	if (s) g_free (s);
	return result;
}


static gboolean
sc_float_parse (sc_file_state_t *src, const char *str, int col, int row)
{
	Cell *cell;
	Value *v;

	if (!src || !str || !*str || col == -1 || row == -1)
		return FALSE;
	
	cell = sheet_cell_fetch (src->sheet, col, row);
	if (!cell)
		return FALSE;
	
	v = value_new_float (atof(str));
	if (!v)
		return FALSE;

	cell_set_value_simple (cell, v);

	return TRUE;
}


typedef struct {
	const char *name;
	gboolean (*handler) (sc_file_state_t *src, const char *str, int col, int row);
	unsigned have_coord : 1;
} sc_cmd_t;


static const sc_cmd_t sc_cmd_list[] = {
	{ "leftstring ",  sc_string_parse, 1 },
	{ "rightstring ",  sc_string_parse, 1 },
	{ "label ",  sc_string_parse, 1 },
	{ "let ",  sc_float_parse, 1 },
};


static gboolean
sc_parse_line (sc_file_state_t *src, char *buf)
{
	int i;
	
	for (i = 0; i < arraysize (sc_cmd_list); i++)
		if (strncmp (sc_cmd_list [i].name, buf,
			     strlen (sc_cmd_list [i].name)) == 0) {
			const sc_cmd_t *cmd = &sc_cmd_list [i];
			int col = -1, row = -1;
			const char *strdata;
			
			strdata = buf + strlen (cmd->name);

			if (cmd->have_coord)
				sc_parse_coord (&strdata, &col, &row);

			cmd->handler (src, strdata, col, row);
			return TRUE;
		}
	
#if 0
	fprintf (stderr, "unhandled directive: '%s'\n", buf);
#endif

	return TRUE;
}


static gboolean
sc_parse_sheet (sc_file_state_t *src)
{
	char buf [BUFSIZ];
	
	while (fgets (buf, sizeof (buf), src->f) != NULL) {
		g_strchomp (buf);
		if ( buf [0] &&
		     buf [0] != '#' &&
		     !sc_parse_line (src, buf) ) {
			fprintf (stderr, "error parsing line\n");
			return FALSE;
		}
	}

	if (ferror (src->f))
		return FALSE;

	return TRUE;
}


static gboolean
sc_read_workbook (Workbook *book, const char *filename)
{
	/*
	 * TODO : When there is a reasonable error reporting
	 * mechanism use it and put all the error code back
	 */
	sc_file_state_t src;
	char *name;
	gboolean result;
	FILE *f;
	
	f = fopen (filename, "r");
	if (!f)
		return FALSE;
	
	name = g_strdup_printf (_("Imported %s"), g_basename (filename));

	memset (&src, 0, sizeof (src));
	src.f	  = f;
	src.sheet = sheet_new (book, name);

	workbook_attach_sheet (book, src.sheet);
	g_free (name);

	result = sc_parse_sheet (&src);

	fclose(f);
	
	return result;
}


static int
sc_can_unload (PluginData *pd)
{
	/* We can always unload */
	return TRUE;
}


static void
sc_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (NULL, sc_read_workbook);
}


int
init_plugin (PluginData * pd)
{
	const char *desc = _("SC/xspread file import");

	file_format_register_open (1, desc, NULL, sc_read_workbook);

	pd->can_unload     = sc_can_unload;
	pd->cleanup_plugin = sc_cleanup_plugin;
	pd->title = g_strdup (desc);
	return 0;
}

