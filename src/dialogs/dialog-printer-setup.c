/*
 * dialog-printer-setup.c: Printer setup dialog box
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"
#include "print-info.h"

typedef struct {
	Workbook    *workbook;
	GnomeDialog *dialog;
	
	GtkWidget   *radio_orient_vertical;
	GtkWidget   *radio_orient_horizontal;
	GtkWidget   *radio_scale_percent;
	GtkWidget   *radio_scale_fit;

	GtkAdjustment *adj_scale_percent;
	GtkAdjustment *adj_scale_page_height;
	GtkAdjustment *adj_scale_page_width;
} dialog_print_info_t;

static void
add_element (GtkWidget *box, GtkWidget *element)
{
	gtk_box_pack_start (GTK_BOX (box), element, TRUE, FALSE, 0);
}

static void
add_element_nf (GtkWidget *box, GtkWidget *element)
{
	gtk_box_pack_start (GTK_BOX (box), element, FALSE, FALSE, 0);
}

static GtkWidget *
make_line (char *text)
{
	if (text){
		GtkWidget *hbox;
		
		hbox = gtk_hbox_new (FALSE, GNOME_PAD_SMALL);
		gtk_box_pack_start (GTK_BOX (hbox),
				    gtk_label_new (text),
				    FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (hbox),
				    gtk_hseparator_new (),
				    TRUE, TRUE, 0);
		gtk_widget_show_all (hbox);
		return hbox;
	} else {
		GtkWidget *line;

		line = gtk_hseparator_new ();
		gtk_widget_show (line);

		return line;
	}
}

static void
append_image_to_box (GtkWidget *box, char *name)
{
	GtkWidget *image;
	char *path;
	
	path = gnome_unconditional_pixmap_file (name);

	image = gnome_pixmap_new_from_file (path);
	if (!image)
		return;

	gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 0);
}

GtkWidget *
make_orientation (dialog_print_info_t *dpi)
{
	GtkWidget *hbox;
	
	hbox = gtk_hbox_new (FALSE, GNOME_PAD_SMALL);
	
	append_image_to_box (hbox, "gnumeric/orient-vertical.png");
	dpi->radio_orient_vertical =
		gtk_radio_button_new_with_label (NULL, _("Vertical"));
	add_element_nf (hbox, dpi->radio_orient_vertical);
	
	append_image_to_box (hbox, "gnumeric/orient-horizontal.png");
	dpi->radio_orient_horizontal =
		gtk_radio_button_new_with_label (
			GTK_RADIO_BUTTON (dpi->radio_orient_vertical)->group,
			_("Horizontal"));
	add_element_nf (hbox, dpi->radio_orient_horizontal);

	gtk_widget_show_all (hbox);
	
	return hbox;
}

static GtkWidget *
make_scale (dialog_print_info_t *dpi)
{
	GtkTable *t;
	GtkWidget *l1, *l2;
	void *group;
	
	t = (GtkTable *) gtk_table_new (0, 0, 0);

	l1 = gtk_label_new (_("Adjust to:"));
	gtk_misc_set_alignment (GTK_MISC (l1), 0.0, 0.5);
	l2 = gtk_label_new (_("Adjust to fit:"));
	gtk_misc_set_alignment (GTK_MISC (l2), 0.0, 0.5);
	
	dpi->radio_scale_percent =
		gtk_radio_button_new (NULL);
	group = GTK_RADIO_BUTTON (dpi->radio_scale_percent)->group;
	dpi->radio_scale_fit =
		gtk_radio_button_new_from_widget (dpi->radio_scale_percent);
	gtk_container_add (GTK_CONTAINER (dpi->radio_scale_percent), l1);
	gtk_container_add (GTK_CONTAINER (dpi->radio_scale_fit), l2);
	
	dpi->adj_scale_percent = (GtkAdjustment *)
		gtk_adjustment_new (100, 0, 20000, 1, 1, 1);
	dpi->adj_scale_page_height = (GtkAdjustment *)
		gtk_adjustment_new (1, 1, 256, 1, 1, 1);
	dpi->adj_scale_page_width = (GtkAdjustment *)
		gtk_adjustment_new (1, 1, 256, 1, 1, 1);

	gtk_table_attach (
		t, dpi->radio_scale_percent,
		0, 1, 0, 1, 0, 0, 0, 0);
	gtk_table_attach (
		t, dpi->radio_scale_fit,
		0, 1, 1, 2, 0, 0, 0, 0);

	gtk_table_attach (
		t, gtk_spin_button_new (dpi->adj_scale_percent, 1.0, 5),
		1, 2, 0, 1, 0, 0, 0, 0);
	gtk_table_attach (
		t, gtk_spin_button_new (dpi->adj_scale_page_width, 1.0, 5),
		1, 2, 1, 2, 0, 0, 0, 0);
	gtk_table_attach (
		t, gtk_spin_button_new (dpi->adj_scale_page_height, 1.0, 5),
		3, 4, 1, 2, 0, 0, 0, 0);

	gtk_table_attach (
		t, gtk_label_new (_("% of the normal size")),
		2, 3, 0, 1, 0, 0, GNOME_PAD_SMALL, 0);
	gtk_table_attach (
		t, gtk_label_new (_("pages of width, and")),
		2, 3, 1, 2, 0, 0, GNOME_PAD_SMALL, 0);
	gtk_table_attach (
		t, gtk_label_new (_("pages of height")),
		4, 5, 1, 2, 0, 0, GNOME_PAD_SMALL, 0);
	
	return GTK_WIDGET (t);
}

/*
 * The Page page.
 */
static GtkWidget *
make_page (dialog_print_info_t *dpi)
{
	GtkWidget *box;
	
	box = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);

	/*
	 * Orientation
	 */
	add_element (box, make_line (_("Orientation")));
	add_element (box, make_orientation (dpi));
	
	add_element (box, make_line (_("Scale")));
	add_element (box, make_scale (dpi));
	
	add_element (box, make_line (NULL));
	add_element (box, gtk_label_new ("..............................."));
	add_element (box, gtk_label_new ("..............................."));
	add_element (box, gtk_label_new ("..............................."));
	add_element (box, gtk_label_new ("..............................."));

	gtk_widget_show_all (box);
	return box;
}

static void
dialog_print_destroy (dialog_print_info_t *dpi)
{
	dpi->workbook->print_info->print_config_dialog_data = NULL;
	gnome_dialog_close (dpi->dialog);
	g_free (dpi);
}

static void
dialog_print_button_ok (GtkWidget *button, dialog_print_info_t *dpi)
{
	dialog_print_destroy (dpi);
}

static void
dialog_print_button_print (GtkWidget *button, dialog_print_info_t *dpi)
{
	Workbook *wb = dpi->workbook;
	
	dialog_print_button_ok (button, dpi);

	workbook_print (wb);
}

static void
dialog_print_button_cancel (GtkWidget *button, dialog_print_info_t *dpi)
{
	dialog_print_destroy (dpi);
}

static struct {
	char      *name;
	GtkWidget *(*create_page)(dialog_print_info_t *dpi);
} pages [] = {
	{ N_("Page"), make_page },
	{ NULL, NULL }
};

void
dialog_printer_setup (Workbook *wb)
{
	GnomeDialog *dialog;
	GtkWidget *notebook;
	dialog_print_info_t *dpi;
	int i;

	/*
	 * Keep track of open print configuration windows for this workbook
	 */
	if (wb->print_info->print_config_dialog_data)
		return;

	dpi = g_new (dialog_print_info_t, 1);
	dpi->workbook = wb;
	wb->print_info->print_config_dialog_data = dpi;

	/*
	 * Prepare dialog box
	 */
	dialog = (GnomeDialog *) gnome_dialog_new (
		_("Print setup"),
		GNOME_STOCK_BUTTON_OK,
		_("Print..."),
		GNOME_STOCK_BUTTON_CANCEL,
		NULL);

	gnome_dialog_button_connect (dialog, 0, dialog_print_button_ok, dpi);
	gnome_dialog_button_connect (dialog, 1, dialog_print_button_print, dpi);
	gnome_dialog_button_connect (dialog, 2, dialog_print_button_cancel, dpi);
	
	gnome_dialog_set_parent (dialog, GTK_WINDOW (wb->toplevel));
	dpi->dialog = dialog;
	
	notebook = gtk_notebook_new ();
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook),
				  GTK_POS_TOP);
	
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox),
			    notebook, TRUE, TRUE, 0);

	for (i = 0; pages [i].name; i++){
		GtkWidget *label;
		GtkWidget *content;
		
		label = gtk_label_new (_(pages [i].name));
		gtk_widget_show (label);
		content = (*pages [i].create_page)(dpi);

		gtk_notebook_append_page (
			GTK_NOTEBOOK (notebook),
			label, content);
	}
	gtk_widget_show (notebook);
	
	gtk_widget_show (GTK_WIDGET (dialog));
}


