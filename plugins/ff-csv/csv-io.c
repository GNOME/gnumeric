/*
 * csv-io.c: save/read Sheets using a CSV encoding.
 *
 * Miguel de Icaza <miguel@gnu.org>
 *
 */

#include <config.h>
#include <stdio.h>
#include <gnome.h>
#include "plugin.h"
#include "gnumeric.h"
#include "file.h"
#include "libcsv.h"


static void
load_table_into_sheet (struct csv_table *table, Sheet *sheet)
{
	int row;

	for (row = 0; row < table->height; row++){
		Cell *cell;
		int col;

		for (col = 0; col < CSV_WIDTH (table, row); col++){
			cell = sheet_cell_new (sheet, col, row);
			cell_set_text_simple (cell, CSV_ITEM (table, row, col));
		}
			
	}
}

static Workbook *
csv_read_workbook (const char* filename)
{
	struct csv_table table;
	Workbook *book;
	Sheet *sheet;
	FILE *f;
	char *name;

	book = workbook_new ();
	if (!book)
		return NULL;

	f = fopen (filename, "r");
	if (f == NULL)
		return NULL;
	
	if (csv_load_table (f, &table) == -1)
		return NULL;

	fclose (f);
	

	name = g_strdup_printf (_("Imported %s"), g_basename (filename));
	sheet = sheet_new (book, name);
	g_free (name);
	
	workbook_attach_sheet (book, sheet);

	load_table_into_sheet (&table, sheet);
	
	csv_destroy_table (&table);
	
	return book;
}


static int
csv_can_unload (PluginData *pd)
{
	/* We can always unload */
	return TRUE;
}

static void
csv_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (NULL, csv_read_workbook);
}

int
init_plugin (PluginData * pd)
{
	file_format_register_open (1, _("Comma Separated Value (CSV) import"), NULL, csv_read_workbook);
	pd->can_unload = csv_can_unload;
	pd->cleanup_plugin = csv_cleanup_plugin;
	pd->title = g_strdup (_("Comma Separated Value (CSV) module"));
	
	return 0;
}
