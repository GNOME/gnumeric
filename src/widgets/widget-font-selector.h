#ifndef GNUMERIC_WIDGET_FONT_SELECTOR_H
#define GNUMERIC_WIDGET_FONT_SELECTOR_H

#include <gtk/gtk.h>
#include <libgnomeprint/gnome-font.h>
#include <glade/glade.h>

#define FONT_SELECTOR_TYPE        (font_selector_get_type ())
#define FONT_SELECTOR(obj)        (GTK_CHECK_CAST((obj), FONT_SELECTOR_TYPE, FontSelector))
#define IS_FONT_SELECTOR(obj)     (GTK_CHECK_TYPE((obj), FONT_SELECTOR_TYPE))

typedef struct {
	GtkHBox box;
	GladeXML *gui;

	GtkWidget *font_name_entry;
	GtkWidget *font_style_entry;
	GtkWidget *font_size_entry;
	GtkWidget *font_name_list;
	GtkWidget *font_style_list;
	GtkWidget *font_size_list;
	GtkWidget *font_preview;
	
	gboolean  is_bold;
	gboolean  is_italic;
	double    size;

	/* The current gnome_font */
	GnomeFont        *gnome_font;
	GnomeDisplayFont *display_font;
} FontSelector;

typedef struct {
	GtkHBoxClass parent_class;
} FontSelectorClass;

GtkType    font_selector_get_type (void);
GtkWidget *font_selector_new      (void);

void       font_selector_set_name   (FontSelector *fs,
				     const char *font_name);
void       font_selector_set_style  (FontSelector *fs,
				     gboolean is_bold,
				     gboolean is_italic);
void       font_selector_set_points (FontSelector *fs,
				     double point_size);
				   
#endif /* GNUMERIC_WIDGET_FONT_SELECTOR_H */

