#ifndef GNUMERIC_WIDGET_FONT_SELECTOR_H
#define GNUMERIC_WIDGET_FONT_SELECTOR_H

#include <gui-gnumeric.h>
#include <libgnomeprint/gnome-font.h>
#include <glade/glade.h>

#include <mstyle.h>
#include <preview-grid.h>

#define FONT_SELECTOR_TYPE        (font_selector_get_type ())
#define FONT_SELECTOR(obj)        (G_TYPE_CHECK_INSTANCE_CAST((obj), FONT_SELECTOR_TYPE, FontSelector))
#define IS_FONT_SELECTOR(obj)     (G_TYPE_CHECK_INSTANCE_TYPE((obj), FONT_SELECTOR_TYPE))

typedef struct {
	GtkHBox box;
	GladeXML *gui;

	GtkWidget *font_name_entry;
	GtkWidget *font_style_entry;
	GtkWidget *font_size_entry;
	GtkWidget *font_name_list;
	GtkWidget *font_style_list;
	GtkWidget *font_size_list;

	GnomeCanvas *font_preview_canvas;
	PreviewGrid *font_preview_grid;
	int          width, height;

	MStyle     *mstyle;
	Value      *value;
} FontSelector;

typedef struct {
	GtkHBoxClass parent_class;

	gboolean (* font_changed) (FontSelector *fs, MStyle *mstyle);
} FontSelectorClass;

GType    font_selector_get_type (void);
GtkWidget *font_selector_new      (void);

void       font_selector_set_value     (FontSelector *fs,
					const Value *v);
void       font_selector_set_name      (FontSelector *fs,
					const char *font_name);
void       font_selector_set_style     (FontSelector *fs,
					gboolean is_bold,
					gboolean is_italic);
void       font_selector_set_underline (FontSelector *fs,
					StyleUnderlineType sut);
void       font_selector_set_strike    (FontSelector *fs,
					gboolean strikethrough);
void       font_selector_set_color     (FontSelector *fs,
					StyleColor *color);
void       font_selector_set_points    (FontSelector *fs,
					double point_size);
#endif /* GNUMERIC_WIDGET_FONT_SELECTOR_H */

