/*
 * The gnumeric print dialog.
 *
 * Author:
 *   Michael Meeks (mmeeks@gnu.org)
 *   
 */

#include <config.h>
#include <libgnomeprint/gnome-print-i18n.h>
#include <gnome.h>
#include <libgnomeprint/gnome-printer.h>
#include <libgnomeprint/gnome-printer-profile.h>
#include <libgnomeprint/gnome-printer-dialog.h>

#include "sheet.h"
#include "dialog-printer.h"
#include "gnumeric-util.h"

static GnomePrinterDialogClass *dialog_parent_class = NULL;

static void
gnumeric_printer_dialog_class_init (GnomePrinterDialogClass *Class)
{
	dialog_parent_class = gtk_type_class (gnome_dialog_get_type ());
}

GtkType
gnumeric_printer_dialog_get_type (void)
{
	static GtkType printer_dialog_type = 0;
	
	if (!printer_dialog_type)
	{
		GtkTypeInfo printer_dialog_info =
		{
			"GnumericPrinterDialog",
			sizeof (GnumericPrinterDialog),
			sizeof (GnumericPrinterDialogClass),
			(GtkClassInitFunc) gnumeric_printer_dialog_class_init,
			(GtkObjectInitFunc) NULL,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};
		printer_dialog_type = gtk_type_unique (gnome_dialog_get_type (),
						       &printer_dialog_info);
	}	
	return printer_dialog_type;
}

static void
range_clicked (GtkButton *button, GnumericPrinterDialog *pd)
{
	gpointer ptr;

	g_return_if_fail (pd != NULL);
	g_return_if_fail (button != NULL);

	ptr = gtk_object_get_data (GTK_OBJECT (button), "print_range");
	pd->range = GPOINTER_TO_INT (ptr);
}

static GtkWidget *
make_range_sel_widget (GnumericPrinterDialog *pd)
{
	GtkTable  *tab = GTK_TABLE (gtk_table_new (2, 2, FALSE));
	GtkWidget *button;
	
	button = gtk_radio_button_new_with_label (NULL,
						  _("active sheet"));
	gtk_object_set_data (GTK_OBJECT (button), "print_range", 
			     GINT_TO_POINTER (PRINT_ACTIVE_SHEET));
	gtk_signal_connect  (GTK_OBJECT (button), "clicked",
			     GTK_SIGNAL_FUNC (range_clicked), pd);
	if (pd->range == PRINT_ACTIVE_SHEET)
		gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (button),
					     TRUE);
	gtk_table_attach_defaults (tab, button, 0, 1, 0, 1);

	button = gtk_radio_button_new_with_label (
		gtk_radio_button_group (GTK_RADIO_BUTTON (button)),
		_("all sheets"));
	gtk_object_set_data (GTK_OBJECT (button), "print_range", 
			     GINT_TO_POINTER (PRINT_ALL_SHEETS));
	gtk_signal_connect  (GTK_OBJECT (button), "clicked",
			     GTK_SIGNAL_FUNC (range_clicked), pd);
	if (pd->range == PRINT_ALL_SHEETS)
		gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (button),
					     TRUE);
	gtk_table_attach_defaults (tab, button, 1, 2, 0, 1);

	button = gtk_radio_button_new_with_label (
		gtk_radio_button_group (GTK_RADIO_BUTTON (button)),
		_("selection"));
	gtk_object_set_data (GTK_OBJECT (button), "print_range", 
			     GINT_TO_POINTER (PRINT_SHEET_SELECTION));
	gtk_signal_connect  (GTK_OBJECT (button), "clicked",
			     GTK_SIGNAL_FUNC (range_clicked), pd);
	if (pd->range == PRINT_SHEET_SELECTION)
		gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (button),
					     TRUE);
	gtk_table_attach_defaults (tab, button, 0, 1, 1, 2);

	gtk_widget_show_all (GTK_WIDGET (tab));

	return GTK_WIDGET (tab);
}

PrintRange
gnumeric_printer_dialog_get_range (GnumericPrinterDialog *pd)
{
	if (!pd)
		return PRINT_ACTIVE_SHEET;
	else
		return pd->range;
}


GtkWidget *
gnumeric_printer_dialog_new (PrintRange default_range)
{
	GnumericPrinterDialog *pd;
	GtkWidget             *printer_dialog;
	
	pd = gtk_type_new (gnumeric_printer_dialog_get_type ());
	printer_dialog = GTK_WIDGET (pd);
	pd->range      = default_range;
	
	gtk_window_set_title (GTK_WINDOW (printer_dialog), _("Select Printer"));
	
	gnome_dialog_append_button (GNOME_DIALOG (printer_dialog),
				    GNOME_STOCK_BUTTON_OK);
	
	gnome_dialog_append_button (GNOME_DIALOG (printer_dialog),
				     GNOME_STOCK_BUTTON_CANCEL);
	
	gnome_dialog_set_default (GNOME_DIALOG(printer_dialog), 0);
	
	pd->gnome_printer_widget = GNOME_PRINTER_WIDGET (gnome_printer_widget_new ());
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (printer_dialog)->vbox),
			    GTK_WIDGET (pd->gnome_printer_widget),
			    TRUE, TRUE, 0);
	
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (printer_dialog)->vbox),
			    make_range_sel_widget (pd),
			    TRUE, TRUE, 0);

	return GTK_WIDGET (pd);
}

/**
 * gnumeric_printer_dialog_run:
 * @range: pointer to the default range
 * @workbook: The workbook calling the routine.
 * 
 * A convenience function.
 * 
 * Return value: the selected range in *range.
 **/
GnomePrinter *
gnumeric_printer_dialog_run (PrintRange *range, Workbook *wb)
{
	int                    bn;
	GnumericPrinterDialog *pd;
	GnomePrinterWidget    *pw;
	GnomePrinter          *printer;
	GtkWidget             *printer_dialog;
	
	printer_dialog = gnumeric_printer_dialog_new (*range);
	pd = GNUMERIC_PRINTER_DIALOG (printer_dialog);
	pw = pd->gnome_printer_widget;

	/* The printer menu is better, but gnome_printer_dialog does
           not expose it */
	gtk_widget_grab_focus (GTK_WIDGET(pw->r1));
	gnome_dialog_editable_enters(GNOME_DIALOG(pd), 
				     GTK_EDITABLE(pw->entry_command));
	gnome_dialog_editable_enters(GNOME_DIALOG(pd), 
				     GTK_EDITABLE(pw->entry_filename));

	gtk_window_set_modal (GTK_WINDOW (printer_dialog), TRUE);
	bn = gnumeric_dialog_run (wb, GNOME_DIALOG (printer_dialog));
	
	if (bn < 0)
		return NULL;
       	
	printer = NULL;

	if (bn == 0) {
		printer = gnome_printer_widget_get_printer (
			GNOME_PRINTER_WIDGET (pd->gnome_printer_widget));
		*range = gnumeric_printer_dialog_get_range (pd);
	}
	
	gtk_widget_destroy (printer_dialog);

	return printer;
}
