/*
 * A font selector widget.  This is a simplified version of the
 * GnomePrint font selector widget.
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Almer S. Tigelaar (almer@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "widget-font-selector.h"

#include "../global-gnome-font.h"
#include <value.h>
#include <mstyle.h>
#include <preview-grid.h>
#include <style-color.h>

#include <gal/util/e-util.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/libgnomeui.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 * Inside this file we define a number of aliases for the
 * FontSelector object, a short hand for it is FS
 *
 * I do want to avoid typing.
 */
#define FS(x) FONT_SELECTOR(x)

typedef FontSelector Fs;
typedef FontSelectorClass FsClass;

static GtkHBoxClass *fs_parent_class;

/* Signals we emit */
enum {
	FONT_CHANGED,
	LAST_SIGNAL
};

static guint fs_signals[LAST_SIGNAL] = { 0 };

static void
reload_preview (FontSelector *fs, MStyle *style)
{
	if (style != NULL) {
		MStyle *old = fs->mstyle;
		gtk_signal_emit (GTK_OBJECT (fs),
			fs_signals[FONT_CHANGED], style);
		fs->mstyle = mstyle_copy_merge (old, style);
		mstyle_unref (old);
	}

	gnome_canvas_request_redraw (fs->font_preview_canvas, INT_MIN, INT_MIN,
				     INT_MAX / 2, INT_MAX / 2);
}

/*
 * We cannot moveto a list element until it is mapped.
 */
static void
list_mapped (GtkWidget *widget, gpointer user_data)
{
	GtkCList * clist = GTK_CLIST (widget);
	int row = 0;
	if (clist->selection)
		row = GPOINTER_TO_UINT (clist->selection->data);
	if (!gtk_clist_row_is_visible (clist, row))
		gtk_clist_moveto (clist, row, 0, 0.5, 0.0);
}

static void
font_selected (GtkCList *font_list, int col, int row, GdkEvent *event, FontSelector *fs)
{
	 gchar *text;
	 MStyle *change;

	 gtk_clist_get_text (font_list, GPOINTER_TO_INT (font_list->selection->data), 0, &text);
	 gtk_entry_set_text (GTK_ENTRY (fs->font_name_entry), text);

	 change = mstyle_new ();
	 mstyle_set_font_name (change, text);
	 reload_preview (fs, change);
	 mstyle_unref (change);
}

static void
fs_fill_font_name_list (FontSelector *fs)
{
	 GList *l;

	 for (l = gnumeric_font_family_list; l; l = l->next) {
		 char *name = l->data;
		 char *array[1];

		 array[0] = name;

		 gtk_clist_append (GTK_CLIST (fs->font_name_list), array);
	 }

	 g_signal_connect (
		 GTK_OBJECT (fs->font_name_list), "select_row",
		 GTK_SIGNAL_FUNC (font_selected), fs);
	 g_signal_connect (
		 GTK_OBJECT (fs->font_name_list), "map",
		 GTK_SIGNAL_FUNC (list_mapped), NULL);
}

static char *styles[] = {
	 N_("Normal"),
	 N_("Bold"),
	 N_("Bold italic"),
	 N_("Italic"),
	 NULL
};

static void
style_selected (GtkCList *style_list, int col, int row, GdkEvent *event, FontSelector *fs)
{
	 MStyle *change = mstyle_new ();
	 row = GPOINTER_TO_INT (style_list->selection->data);

	 switch (row) {
	 case 0:
		 mstyle_set_font_bold (change, FALSE);
		 mstyle_set_font_italic (change, FALSE);
		 break;
	 case 1:
		 mstyle_set_font_bold (change, TRUE);
		 mstyle_set_font_italic (change, FALSE);
		 break;

	 case 2:
		 mstyle_set_font_bold (change, TRUE);
		 mstyle_set_font_italic (change, TRUE);
		 break;
	 case 3:
		 mstyle_set_font_bold (change, FALSE);
		 mstyle_set_font_italic (change, TRUE);
		 break;
	 }

	 gtk_entry_set_text (GTK_ENTRY (fs->font_style_entry), _(styles[row]));
	 reload_preview (fs, change);
	 mstyle_unref (change);
}

static void
fs_fill_font_style_list (FontSelector *fs)
{
	 GtkCList *style_list = GTK_CLIST (fs->font_style_list);
	 int i;

	 for (i = 0; styles[i] != NULL; i++) {
		 char *array[1];
		 array[0] = _(styles[i]);

		 gtk_clist_append (style_list, array);
	 }
	 g_signal_connect (
		 GTK_OBJECT (fs->font_style_list), "select_row",
		 GTK_SIGNAL_FUNC (style_selected), fs);
	 g_signal_connect (
		 GTK_OBJECT (fs->font_style_list), "map",
		 GTK_SIGNAL_FUNC (list_mapped), NULL);
}

static void
size_selected (GtkCList *size_list, int col, int row, GdkEvent *event, FontSelector *fs)
{
	 MStyle *change = mstyle_new ();
	 gchar *text;

	 row = GPOINTER_TO_INT (size_list->selection->data);
	 gtk_clist_get_text (size_list, row, 0, &text);
	 gtk_entry_set_text (GTK_ENTRY (fs->font_size_entry), text);
	 mstyle_set_font_size (change, atof (text));
	 reload_preview (fs, change);
	 mstyle_unref (change);
}

static void
size_changed (GtkEntry *entry, FontSelector *fs)
{
	 char const *text = gtk_entry_get_text (entry);
	 double size = atof (text);
	 if (size >= 1. && size < 128) {
		 MStyle *change = mstyle_new ();
		 mstyle_set_font_size (change, size);
		 reload_preview (fs, change);
		 mstyle_unref (change);
	 }
}

static void
fs_fill_font_size_list (FontSelector *fs)
{
	int i;

	for (i = 0; gnumeric_point_sizes[i] != 0; i++) {
		char buffer[4 * sizeof (int)];
		char *array[1];

		sprintf (buffer, "%d", gnumeric_point_sizes[i]);
		array[0] = buffer;
		gtk_clist_append (GTK_CLIST (fs->font_size_list), array);
	}
	g_signal_connect (
		GTK_OBJECT (fs->font_size_list), "select_row",
		GTK_SIGNAL_FUNC (size_selected), fs);
	g_signal_connect (
		GTK_OBJECT (fs->font_size_list), "map",
		GTK_SIGNAL_FUNC (list_mapped), NULL);

	g_signal_connect (
		GTK_OBJECT (fs->font_size_entry), "changed",
		GTK_SIGNAL_FUNC (size_changed), fs);
}

static int
cb_get_row_height (PreviewGrid *pg, int row, FontSelector *fs)
{
	return fs->height;
}

static int
cb_get_col_width (PreviewGrid *pg, int col, FontSelector *fs)
{
	return fs->width;
}

static MStyle *
cb_get_cell_style (PreviewGrid *pg, int row, int col, FontSelector *fs)
{
	return fs->mstyle;
}

static Value *
cb_get_cell_value (PreviewGrid *pg, int row, int col, FontSelector *fs)
{
	/*
	 * FIXME: :-( Why don't Value *'s support refcounting?
	 */
	return value_duplicate (fs->value);
}

static void
canvas_size_changed (GtkWidget *widget, GtkAllocation *allocation, FontSelector *fs)
{
	fs->width  = allocation->width - 1;
	fs->height = allocation->height - 1;

	gnome_canvas_set_scroll_region (fs->font_preview_canvas, 0, 0,
					fs->width, fs->height);
	reload_preview (fs, NULL);
}

static void
fs_init (FontSelector *fs)
{
	GtkWidget *toplevel;
	GtkWidget *old_parent;

	fs->gui = glade_xml_new (GNUMERIC_GLADEDIR "/font-sel.glade", NULL, "gnumeric");
	if (!fs->gui) {
		g_warning ("Could not load font-sel.glade");
                return;
	}

	toplevel = glade_xml_get_widget (fs->gui, "toplevel-table");
	old_parent = gtk_widget_get_toplevel (toplevel);
	gtk_widget_reparent (toplevel, GTK_WIDGET (fs));
	gtk_widget_destroy (old_parent);
	gtk_widget_queue_resize (toplevel);

	fs->width = fs->height = 1;
	fs->font_name_entry  = glade_xml_get_widget (fs->gui, "font-name-entry");
	fs->font_style_entry = glade_xml_get_widget (fs->gui, "font-style-entry");
	fs->font_size_entry  = glade_xml_get_widget (fs->gui, "font-size-entry");
	fs->font_name_list  = glade_xml_get_widget (fs->gui, "font-name-list");
	fs->font_style_list = glade_xml_get_widget (fs->gui, "font-style-list");
	fs->font_size_list  = glade_xml_get_widget (fs->gui, "font-size-list");

	fs->font_preview_canvas = GNOME_CANVAS (glade_xml_get_widget (fs->gui, "font-preview-canvas"));
	fs->font_preview_grid = PREVIEW_GRID (gnome_canvas_item_new (
		gnome_canvas_root (fs->font_preview_canvas),
		preview_grid_get_type (),
		"RenderGridlines", FALSE,
		NULL));

	g_signal_connect (G_OBJECT (fs->font_preview_grid),
		"get_row_height",
		GTK_SIGNAL_FUNC (cb_get_row_height), fs);
	g_signal_connect (GTK_OBJECT (fs->font_preview_grid),
		"get_col_width",
		GTK_SIGNAL_FUNC (cb_get_col_width), fs);
	g_signal_connect (G_OBJECT (fs->font_preview_grid),
		"get_cell_style",
		GTK_SIGNAL_FUNC (cb_get_cell_style), fs);
	g_signal_connect (G_OBJECT (fs->font_preview_grid),
		"get_cell_value",
		GTK_SIGNAL_FUNC (cb_get_cell_value), fs);
	g_signal_connect (G_OBJECT (fs->font_preview_canvas),
		"size-allocate",
		GTK_SIGNAL_FUNC (canvas_size_changed), fs);

	fs->mstyle = mstyle_new_default ();
	fs->value  = value_new_string ("AaBbCcDdEe12345");
	mstyle_set_align_v   (fs->mstyle, VALIGN_CENTER);
	mstyle_set_align_h   (fs->mstyle, HALIGN_CENTER);
	mstyle_set_font_size (fs->mstyle, 10);

	fs_fill_font_style_list (fs);
	fs_fill_font_name_list (fs);
	fs_fill_font_size_list (fs);
}

static void
fs_destroy (GtkObject *object)
{
	FontSelector *fs = FONT_SELECTOR (object);

	if (fs->value) {
		value_release (fs->value);
		fs->value = NULL;
	}

	if (fs->mstyle) {
		mstyle_unref (fs->mstyle);
		fs->mstyle = NULL;
	}

	if (fs->gui) {
		g_object_unref (G_OBJECT (fs->gui));
		fs->gui = NULL;
	}

	((GtkObjectClass *)fs_parent_class)->destroy (object);
}

static void
fs_class_init (GtkObjectClass *klass)
{
	klass->destroy = fs_destroy;

	fs_parent_class = gtk_type_class (gtk_hbox_get_type ());

	fs_signals[FONT_CHANGED] =
		gtk_signal_new (
			"font_changed",
			GTK_RUN_LAST,
			GTK_CLASS_TYPE (klass),
			GTK_SIGNAL_OFFSET (FontSelectorClass, font_changed),
			gtk_marshal_NONE__POINTER,
			GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);
}

E_MAKE_TYPE (font_selector, "FontSelector", FontSelector,
	     fs_class_init, fs_init, GTK_TYPE_HBOX)

GtkWidget *
font_selector_new (void)
{
	GtkWidget *w;

	w = gtk_type_new (FONT_SELECTOR_TYPE);
	return w;
}

static void
select_row (GtkWidget *list, int row)
{
	GtkCList *cl = GTK_CLIST (list);

	gtk_clist_select_row (cl, row, 0);
}

void
font_selector_set_value (FontSelector *fs, const Value *v)
{
	g_return_if_fail (IS_FONT_SELECTOR (fs));

	value_release (fs->value);
	if (v)
		fs->value = value_duplicate (v);
	else
		fs->value = value_new_string ("AaBbCcDdEe12345");

	reload_preview (fs, NULL);
}

void
font_selector_set_name (FontSelector *fs,
			const char *font_name)
{
	GList *l;
	int row;

	g_return_if_fail (IS_FONT_SELECTOR (fs));
	g_return_if_fail (font_name != NULL);

	for (row = 0, l = gnumeric_font_family_list; l; l = l->next, row++) {
		if (g_strcasecmp (font_name, l->data) == 0)
			break;
	}

	if (l != NULL)
		select_row (fs->font_name_list, row);

}

void
font_selector_set_style (FontSelector *fs,
			 gboolean is_bold,
			 gboolean is_italic)
{
	int n;
	MStyle *change;

	g_return_if_fail (IS_FONT_SELECTOR (fs));

	if (is_bold) {
		if (is_italic)
			n = 2;
		else
			n = 1;
	} else {
		if (is_italic)
			n = 3;
		else
			n = 0;
	}
	select_row (fs->font_style_list, n);

	change = mstyle_new ();
	mstyle_set_font_bold   (change, is_bold);
	mstyle_set_font_italic (change, is_italic);
	reload_preview (fs, change);
	mstyle_unref (change);
}

void
font_selector_set_strike (FontSelector *fs, gboolean strikethrough)
{
	MStyle *change;

	g_return_if_fail (IS_FONT_SELECTOR (fs));

	change = mstyle_new ();
	mstyle_set_font_strike (change, strikethrough);
	reload_preview (fs, change);
	mstyle_unref (change);
}

void
font_selector_set_underline (FontSelector *fs, StyleUnderlineType underline)
{
	MStyle *change;

	g_return_if_fail (IS_FONT_SELECTOR (fs));

	change = mstyle_new ();
	mstyle_set_font_uline (change, underline);
	reload_preview (fs, change);
	mstyle_unref (change);
}

void
font_selector_set_color (FontSelector *fs, StyleColor *color)
{
	MStyle *change;

	g_return_if_fail (IS_FONT_SELECTOR (fs));

	change = mstyle_new ();
	mstyle_set_color (change, MSTYLE_COLOR_FORE, color);
	reload_preview (fs, change);
	mstyle_unref (change);
}

void
font_selector_set_points (FontSelector *fs,
			  double point_size)
{
	int i;

	g_return_if_fail (IS_FONT_SELECTOR (fs));

	for (i = 0; gnumeric_point_sizes[i] != 0; i++) {
		if (gnumeric_point_sizes[i] == point_size) {
			select_row (fs->font_size_list, i);
			break;
		}
	}

	if (gnumeric_point_sizes[i] == 0) {
		char *buffer;
		buffer = g_strdup_printf ("%g", point_size);
		gtk_entry_set_text (GTK_ENTRY (fs->font_size_entry), buffer);
		g_free (buffer);
	}
}
