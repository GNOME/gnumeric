#ifndef GNUMERIC_WIDGET_COLOR_COMBO_H
#define GNUMERIC_WIDGET_COLOR_COMBO_H

#include <libgnome/gnome-defs.h>
#include <gtk/gtkwidget.h>

BEGIN_GNOME_DECLS

#define COLOR_COMBO_TYPE     (color_combo_get_type ())
#define COLOR_COMBO(obj)     (GTK_CHECK_CAST((obj), COLOR_COMBO_TYPE, ColorCombo))
#define COLOR_COMBO_CLASS(k) (GTK_CHECK_CLASS_CAST(k), COLOR_COMBO_TYPE)
#define IS_COLOR_COMBO(obj)  (GTK_CHECK_TYPE((obj), COLOR_COMBO_TYPE))

typedef struct _ColorCombo ColorCombo;

GtkType    color_combo_get_type (void);

GtkWidget *color_combo_new      (char **icon, char const * const no_color_label,
				 GdkColor *default_color, gchar *group_name);

END_GNOME_DECLS

#endif GNUMERIC_WIDGET_COLOR_COMBO_H
