#include <gnome.h>
#include "gnumeric.h"

/* The locations within the main table in the workbook */
#define WB_EA_LINE   0
#define WB_EA_SHEETS 1

#define WB_COLS      1

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

static void
workbook_setup_sheets (Workbook *wb)
{
	wb->notebook = gtk_notebook_new ();
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (wb->notebook), GTK_POS_BOTTOM);

	gtk_table_attach (GTK_TABLE (wb->table), wb->notebook,
			  0, WB_COLS, WB_EA_SHEETS, WB_EA_SHEETS+1,
			  GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
			  0, 0);
}

static void
workbook_setup_edit_area (Workbook *wb)
{
	GtkWidget *ok_button, *cancel_button, *box, *box2;
	GtkWidget *pix;
	
	wb->ea_status = gtk_entry_new ();
	wb->ea_input  = gtk_entry_new ();
	ok_button     = gtk_button_new ();
	cancel_button = gtk_button_new ();
	box           = gtk_hbox_new (0, 0);
	box2          = gtk_hbox_new (0, 0);
	
	gtk_widget_set_usize (wb->ea_status, 100, 0);
	
	/* Ok */
	pix = gnome_stock_pixmap_widget_new (wb->toplevel, GNOME_STOCK_BUTTON_OK);
	gtk_container_add (GTK_CONTAINER (ok_button), pix);

	/* Cancel */
	pix = gnome_stock_pixmap_widget_new (wb->toplevel, GNOME_STOCK_BUTTON_CANCEL);
	gtk_container_add (GTK_CONTAINER (cancel_button), pix);

	gtk_box_pack_start (GTK_BOX (box2), wb->ea_status, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), ok_button, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), cancel_button, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box2), box, 0, 0, 0);
	gtk_box_pack_end   (GTK_BOX (box2), wb->ea_input, 1, 1, 0);

	gtk_table_attach (GTK_TABLE (wb->table), box2,
			  0, 1, 0, 1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);
}

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
	wb->table    = gtk_table_new (0, 0, 0);

	workbook_setup_edit_area (wb);
	workbook_setup_sheets (wb);
	gnome_app_set_contents (GNOME_APP (wb->toplevel), wb->table);
	gnome_app_create_menus (GNOME_APP (wb->toplevel), workbook_menu);
	
	gtk_widget_show_all (wb->table);
	return wb;
}

static void
zoom_in (GtkButton *b, Sheet *sheet)
{
	double pix = GNOME_CANVAS (sheet->sheet_view)->pixels_per_unit;
	
	if (pix < 10.0){
		pix += 0.5;
		sheet_set_zoom_factor (sheet, pix);
	}
}

static void
zoom_out (GtkButton *b, Sheet *sheet)
{
	double pix = GNOME_CANVAS (sheet->sheet_view)->pixels_per_unit;
	
	if (pix > 1.0){
		pix -= 0.5;
		sheet_set_zoom_factor (sheet, pix);
	}
}

static void
buttons (Sheet *sheet, GtkTable *table)
{
	GtkWidget *b;
	
	b = gtk_button_new_with_label ("Zoom out");
	gtk_table_attach (table, b,
			  0, 1, 1, 2, 0, 0, 0, 0);
	gtk_signal_connect (GTK_OBJECT (b), "clicked",
			    GTK_SIGNAL_FUNC (zoom_out), sheet);

	b = gtk_button_new_with_label ("Zoom in");
	gtk_table_attach (table, b,
			  1, 2, 1, 2, 0, 0, 0, 0);
	gtk_signal_connect (GTK_OBJECT (b), "clicked",
			    GTK_SIGNAL_FUNC (zoom_in), sheet);
	
}

void
workbook_attach_sheet (Workbook *wb, Sheet *sheet)
{
	GtkWidget *t;

	g_hash_table_insert (wb->sheets, sheet->name, sheet);

	t = gtk_table_new (0, 0, 0);
	gtk_table_attach (GTK_TABLE (t), sheet->toplevel,
			  0, 3, 0, 1,
			  GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
	buttons (sheet, GTK_TABLE (t));
	gtk_widget_show_all (t);
	gtk_notebook_append_page (GTK_NOTEBOOK (wb->notebook),
				  t, gtk_label_new (sheet->name));
}

Workbook *
workbook_new_with_sheets (int sheet_count)
{
	GtkWidget *focus;
	Workbook *wb;
	Sheet *first_sheet = 0;
	int i;

	wb = workbook_new ();

	for (i = 0; i < sheet_count; i++){
		Sheet *sheet;
		char name [80];

		snprintf (name, sizeof (name), "Sheet %d", i);
		sheet = sheet_new (wb, name);
		workbook_attach_sheet (wb, sheet);

		if (!first_sheet)
			first_sheet = sheet;
	}

	focus = first_sheet->sheet_view;
	gtk_window_set_focus (GTK_WINDOW (wb->toplevel), focus);

	return wb;
}

/*
 * This routine sets up the default styles for the
 * workbook
 */
void
workbook_realized (Workbook *workbook, GdkWindow *window)
{
}
