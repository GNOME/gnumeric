#ifndef GNUMERIC_WIDGET_PIXMAP_COMBO_H
#define GNUMERIC_WIDGET_PIXMAP_COMBO_H

#include <libgnome/gnome-defs.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkentry.h>
#include <libgnomeui/gnome-canvas.h>
#include "gtk-combo-box.h"

BEGIN_GNOME_DECLS

#define PIXMAP_COMBO_TYPE     (pixmap_combo_get_type ())
#define PIXMAP_COMBO(obj)     (GTK_CHECK_CAST((obj), PIXMAP_COMBO_TYPE, PixmapCombo))
#define PIXMAP_COMBO_CLASS(k) (GTK_CHECK_CLASS_CAST(k), PIXMAP_COMBO_TYPE)
#define IS_PIXMAP_COMBO(obj)  (GTK_CHECK_TYPE((obj), PIXMAP_COMBO_TYPE))

typedef struct {
	char const *untranslated_tooltip;
	char **xpm_data;
	int  index;
} PixmapComboElement;

typedef struct {
	GtkComboBox     combo_box;

	/* Static information */
	PixmapComboElement const *elements;
	int cols, rows;
	int num_elements;

	/* State info */
	int last_index;

	/* Interface elements */
	GtkWidget    *combo_table, *preview_button;
	GtkWidget    *preview_pixmap;
	GtkTooltips  *tool_tip;
	GnomePixmap **pixmaps;
} PixmapCombo;

GtkType    pixmap_combo_get_type      (void);
GtkWidget *pixmap_combo_new           (PixmapComboElement const *elements,
				       int ncols, int nrows);
void       pixmap_combo_select_pixmap (PixmapCombo *combo, int index);
				  
typedef struct {
	GnomeCanvasClass parent_class;

	/* Signals emited by this widget */
	void (* changed) (PixmapCombo *pixmap_combo, int index);
} PixmapComboClass;

END_GNOME_DECLS

#endif GNUMERIC_WIDGET_PIXMAP_COMBO_H
