#ifndef GNUMERIC_DIALOG_PRINT_H
#define GNUMERIC_DIALOG_PRINT_H

#include "print.h"

#define GNUMERIC_TYPE_PRINTER_DIALOG	     (gnumeric_printer_dialog_get_type ())
#define GNUMERIC_PRINTER_DIALOG(obj)	     (GTK_CHECK_CAST ((obj), GNUMERIC_TYPE_PRINTER_DIALOG, GnumericPrinterDialog))
#define GNUMERIC_PRINTER_DIALOG_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), GNUMERIC_TYPE_PRINTER_DIALOG, GnumericPrinterDialogClass))
#define GNUMERIC_IS_PRINTER_DIALOG(obj)	     (GTK_CHECK_TYPE ((obj), GNUMERIC_TYPE_PRINTER_DIALOG))
#define GNUMERIC_IS_PRINTER_DIALOG_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), GNUMERIC_TYPE_PRINTER_DIALOG))

typedef struct {
	GnomeDialog         dialog;
	GnomePrinterWidget *gnome_printer_widget;
	PrintRange          range;
} GnumericPrinterDialog;

typedef struct {
	GnomeDialogClass parent_class;
} GnumericPrinterDialogClass;

GtkType       gnumeric_printer_dialog_get_type    (void);
GtkWidget    *gnumeric_printer_dialog_new         (PrintRange default_range);
PrintRange    gnumeric_printer_dialog_get_range   (GnumericPrinterDialog *pd);
GnomePrinter *gnumeric_printer_dialog_run         (PrintRange *range);

#endif /*  GNUMERIC_DIALOG_PRINT_H */
