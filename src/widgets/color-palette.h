#ifndef GNUMERIC_COLOR_PALETTE_H
#define GNUMERIC_COLOR_PALETTE_H

#include <libgnome/gnome-defs.h>
#include <gtk/gtkwidget.h>

BEGIN_GNOME_DECLS

#define COLOR_PALETTE_TYPE     (color_palette_get_type ())
#define COLOR_PALETTE(obj)     (GTK_CHECK_CAST((obj), COLOR_PALETTE_TYPE, ColorPalette))
#define COLOR_PALETTE_CLASS(k) (GTK_CHECK_CLASS_CAST(k), COLOR_PALETTE_TYPE)
#define IS_COLOR_PALETTE(obj)  (GTK_CHECK_TYPE((obj), COLOR_PALETTE_TYPE))

typedef struct _ColorPalette ColorPalette;

GtkType         color_palette_get_type (void);

GtkWidget       *color_palette_new (char const * const no_color_label,
				    GdkColor *default_color,
				    gchar *group_name);

GdkColor        *color_palette_get_current_color (ColorPalette *P);
GtkWidget       *color_palette_get_color_picker (ColorPalette *P);

END_GNOME_DECLS

#endif GNUMERIC_PALETTE_H


