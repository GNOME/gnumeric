#include <gnome.h>
#include "gnumeric.h"

static void
quit_cmd (void)
{
	gtk_main_quit ();
}

static GnomeUIInfo workbook_menu_file [] = {
	{ GNOME_APP_UI_ITEM, N_("Exit"), NULL, quit_cmd, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_EXIT },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu [] = {
	{ GNOME_APP_UI_SUBTREE, N_("File"), NULL, &workbook_menu_file },
	GNOMEUIINFO_END
};

/*
 * Sets up the workbook.
 * Right now it is adding some decorations to the window,
 * this is for testing purposes.
 */
Workbook *
workbook_new (void)
{
	Workbook *wb;

	wb = g_new0 (Workbook, 1);
	wb->toplevel = gnome_app_new ("Gnumeric", "Gnumeric");
	wb->sheets   = g_hash_table_new (g_str_hash, g_str_equal);
	wb->notebook = gtk_notebook_new ();
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (wb->notebook), GTK_POS_BOTTOM);
	gnome_app_set_contents (GNOME_APP (wb->toplevel), wb->notebook);
	gnome_app_create_menus (GNOME_APP (wb->toplevel), workbook_menu);
			     
	gtk_widget_show (wb->notebook);
	return wb;
}

void
workbook_attach_sheet (Workbook *wb, Sheet *sheet)
{
	g_hash_table_insert (wb->sheets, sheet->name, sheet);
	gtk_notebook_append_page (GTK_NOTEBOOK (wb->notebook),
				  sheet->sheet_view,
				  gtk_label_new (sheet->name));
}

Workbook *
workbook_new_with_sheets (int sheet_count)
{
	Workbook *wb;
	int i;

	wb = workbook_new ();

	for (i = 0; i < sheet_count; i++){
		Sheet *sheet;
		char name [80];

		snprintf (name, sizeof (name), "Sheet %d", i);
		sheet = sheet_new (wb, name);
		workbook_attach_sheet (wb, sheet);
	}
	return wb;
}
