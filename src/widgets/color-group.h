#ifndef GNUMERIC_COLOR_GROUP_H
#define GNUMERIC_COLOR_GROUP_H

#include "gnumeric.h"
#include <gnome.h>
#include <libgnomeui/gnome-canvas.h>
#include <libgnome/gnome-defs.h>
#include <gtk/gtkobject.h>
#include "color.h"

BEGIN_GNOME_DECLS

#define COLOR_GROUP_TYPE     (color_group_get_type ())
#define COLOR_GROUP(obj)     (GTK_CHECK_CAST((obj), COLOR_GROUP_TYPE, ColorGroup))
#define COLOR_GROUP_CLASS(k) (GTK_CHECK_CLASS_CAST(k), COLOR_GROUP_TYPE)
#define IS_COLOR_GROUP(obj)  (GTK_CHECK_TYPE((obj), COLOR_GROUP_TYPE))

typedef struct _ColorGroup ColorGroup;

GtkType         color_group_get_type  (void);

GtkObject      *color_group_new_named (gchar * name);

void            color_group_set_history_size (ColorGroup *cg, gint size);

gint            color_group_get_history_size (ColorGroup *cg);

GtkObject      *color_group_from_name (const gchar * name);

GdkColor       *color_group_most_recent_color (ColorGroup *cg);

GdkColor       *color_group_oldest_color (ColorGroup *cg);

GdkColor       *color_group_next_color (ColorGroup *cg);

GdkColor       *color_group_previous_color (ColorGroup *cg);

void            color_group_add_color (ColorGroup *cg, GdkColor *color, gboolean custom_color);

GdkColor       *color_group_get_current_color (ColorGroup *cg);

void            color_group_set_current_color (ColorGroup *cg, GdkColor *color);
END_GNOME_DECLS

#endif GNUMERIC_COLOR_GROUP_H




