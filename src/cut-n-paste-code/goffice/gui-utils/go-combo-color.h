#ifndef GNUMERIC_WIDGET_COLOR_COMBO_H
#define GNUMERIC_WIDGET_COLOR_COMBO_H

#include <libgnome/gnome-defs.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkentry.h>
#include <libgnomeui/gnome-canvas.h>
#include "gtk-combo-box.h"

BEGIN_GNOME_DECLS

#define COLOR_COMBO_TYPE     (color_combo_get_type ())
#define COLOR_COMBO(obj)     (GTK_CHECK_CAST((obj), COLOR_COMBO_TYPE, ColorCombo))
#define COLOR_COMBO_CLASS(k) (GTK_CHECK_CLASS_CAST(k), COLOR_COMBO_TYPE)
#define IS_COLOR_COMBO(obj)  (GTK_CHECK_TYPE((obj), COLOR_COMBO_TYPE))

typedef struct {
	GtkComboBox     combo_box;

	/*
	 * Canvas where we display
	 */
	GtkWidget       *preview_button;
	GnomeCanvas     *preview_canvas;
	GnomeCanvasItem *preview_color_item;
	GdkImlibImage   *preview_image;

	GtkWidget       *color_table;

	/*
	 * Array of colors
	 */
	GnomeCanvasItem **items;
	
	int cols, rows;
	int total;
	int last_index;
} ColorCombo;

GtkType    color_combo_get_type      (void);
GtkWidget *color_combo_new           (char **icon);
void       color_combo_construct     (ColorCombo *cc, char **icon, gboolean no_color,
				      int ncols, int nrows, char **color_names);
GtkWidget *color_combo_new_with_vals (char **icon,
				      int ncols, int nrows, gboolean no_color,
				      char **color_names);
void       color_combo_select_color  (ColorCombo *color_combo, int index);
				  
typedef struct {
	GnomeCanvasClass parent_class;

	/* Signals emited by this widget */
	void (* changed) (ColorCombo *color_combo, GdkColor *color, int index);
} ColorComboClass;

END_GNOME_DECLS

#endif GNUMERIC_WIDGET_COLOR_COMBO_H
