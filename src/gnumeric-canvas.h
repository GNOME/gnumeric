#ifndef GNUMERIC_SHEET_H
#define GNUMERIC_SHEET_H

#define GNUMERIC_TYPE_SHEET     (gnumeric_sheet_get_type ())
#define GNUMERIC_SHEET(obj)     (GTK_CHECK_CAST((obj), GNUMERIC_TYPE_SHEET, GnumericSheet))
#define GNUMERIC_SHEET_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), GNUMERIC_TYPE_SHEET))
#define GNUMERIC_IS_SHEET(o)    (GTK_CHECK_TYPE((o), GNUMERIC_TYPE_SHEET))

typedef struct {
	GnomeCanvas canvas;

	Sheet       *sheet;
	ColType     top_col;
	RowType     top_row;

	/* Font used for the labels in the columns and rows */
	GdkFont     *label_font;
} GnumericSheet;

GtkType gnumeric_sheet_get_type (void);

GtkWidget *gnumeric_sheet_new (Sheet *sheet);

typedef struct {
	GnomeCanvasClass parent_class;
} GnumericSheetClass;
#endif
