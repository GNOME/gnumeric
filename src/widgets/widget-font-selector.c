/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * A font selector widget.  This is a simplified version of the
 * GnomePrint font selector widget.
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Almer S. Tigelaar (almer@gnome.org)
 */

#undef GTK_DISABLE_DEPRECATED
#warning "This file uses GTK_DISABLE_DEPRECATED"
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "widget-font-selector.h"

#include <gnm-marshalers.h>
#include "../global-gnome-font.h"
#include <value.h>
#include <mstyle.h>
#include <preview-grid.h>
#include <style-color.h>
#include <gui-util.h>
#include <mstyle.h>
#include <preview-grid.h>

#include <gsf/gsf-impl-utils.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gtk/gtkhbox.h>
#include <glade/glade.h>

struct _FontSelector {
	GtkHBox box;
	GladeXML *gui;

	GtkWidget *font_name_entry;
	GtkWidget *font_style_entry;
	GtkWidget *font_size_entry;
	GtkWidget *font_name_list;
	GtkWidget *font_style_list;
	GtkWidget *font_size_list;

	FooCanvas *font_preview_canvas;
	FooCanvasItem *font_preview_grid;

	MStyle     *mstyle;
};

typedef struct {
	GtkHBoxClass parent_class;

	void (* font_changed) (FontSelector *fs, MStyle *mstyle);
} FontSelectorClass;

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
fs_modify_style (FontSelector *fs, MStyle *modification)
{
	MStyle *original = fs->mstyle;
	g_return_if_fail (modification != NULL);

	fs->mstyle = mstyle_copy_merge (original, modification);
	gtk_signal_emit (GTK_OBJECT (fs),
		fs_signals[FONT_CHANGED], modification);
	foo_canvas_item_set (fs->font_preview_grid,
		"default-style",  fs->mstyle,
		NULL);
	mstyle_unref (modification);
	mstyle_unref (original);
}

/*
 * We cannot moveto a list element until it is mapped.
 */
static void
list_mapped (GtkWidget *widget, G_GNUC_UNUSED gpointer user_data)
{
	GtkCList * clist = GTK_CLIST (widget);
	int row = 0;
	if (clist->selection)
		row = GPOINTER_TO_UINT (clist->selection->data);
	if (!gtk_clist_row_is_visible (clist, row))
		gtk_clist_moveto (clist, row, 0, 0.5, 0.0);
}

static void
font_selected (GtkCList *font_list,
	       G_GNUC_UNUSED int col,
	       G_GNUC_UNUSED int row,
	       G_GNUC_UNUSED GdkEvent *event, FontSelector *fs)
{
	 gchar *text;
	 MStyle *change;

	 gtk_clist_get_text (font_list, GPOINTER_TO_INT (font_list->selection->data), 0, &text);
	 gtk_entry_set_text (GTK_ENTRY (fs->font_name_entry), text);

	 change = mstyle_new ();
	 mstyle_set_font_name (change, text);
	 fs_modify_style (fs, change);
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
		 G_OBJECT (fs->font_name_list), "select_row",
		 GTK_SIGNAL_FUNC (font_selected), fs);
	 g_signal_connect (
		 G_OBJECT (fs->font_name_list), "map",
		 GTK_SIGNAL_FUNC (list_mapped), NULL);
}

static const char *styles[] = {
	 N_("Normal"),
	 N_("Bold"),
	 N_("Bold italic"),
	 N_("Italic"),
	 NULL
};

static void
style_selected (GtkCList *style_list,
		G_GNUC_UNUSED int col, int row,
		G_GNUC_UNUSED GdkEvent *event, FontSelector *fs)
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
	 fs_modify_style (fs, change);
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
		 G_OBJECT (fs->font_style_list), "select_row",
		 GTK_SIGNAL_FUNC (style_selected), fs);
	 g_signal_connect (
		 G_OBJECT (fs->font_style_list), "map",
		 GTK_SIGNAL_FUNC (list_mapped), NULL);
}

static void
size_selected (GtkCList *size_list,
	       G_GNUC_UNUSED int col, int row,
	       G_GNUC_UNUSED GdkEvent *event, FontSelector *fs)
{
	 MStyle *change = mstyle_new ();
	 gchar *text;

	 row = GPOINTER_TO_INT (size_list->selection->data);
	 gtk_clist_get_text (size_list, row, 0, &text);
	 gtk_entry_set_text (GTK_ENTRY (fs->font_size_entry), text);
	 mstyle_set_font_size (change, atof (text));
	 fs_modify_style (fs, change);
}

static void
size_changed (GtkEntry *entry, FontSelector *fs)
{
	 char const *text = gtk_entry_get_text (entry);
	 double size = atof (text);
	 if (size >= 1. && size < 128) {
		 MStyle *change = mstyle_new ();
		 mstyle_set_font_size (change, size);
		 fs_modify_style (fs, change);
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
		G_OBJECT (fs->font_size_list), "select_row",
		GTK_SIGNAL_FUNC (size_selected), fs);
	g_signal_connect (
		G_OBJECT (fs->font_size_list), "map",
		GTK_SIGNAL_FUNC (list_mapped), NULL);

	g_signal_connect (
		G_OBJECT (fs->font_size_entry), "changed",
		GTK_SIGNAL_FUNC (size_changed), fs);
}

static void
canvas_size_changed (G_GNUC_UNUSED GtkWidget *widget,
		     GtkAllocation *allocation, FontSelector *fs)
{
	int width  = allocation->width - 1;
	int height = allocation->height - 1;

	foo_canvas_item_set (fs->font_preview_grid,
                 "default-col-width",  width,
                 "default-row-height", height,
		 NULL);

	foo_canvas_set_scroll_region (fs->font_preview_canvas, 0, 0,
				      width, height);
}

static void
fs_init (FontSelector *fs)
{
	GtkWidget *w;

	fs->gui = gnm_glade_xml_new (NULL, "font-sel.glade", "toplevel-table", NULL);
	if (fs->gui == NULL)
                return;

	fs->mstyle = mstyle_new_default ();
	mstyle_set_align_v   (fs->mstyle, VALIGN_CENTER);
	mstyle_set_align_h   (fs->mstyle, HALIGN_CENTER);
	mstyle_set_font_size (fs->mstyle, 10);

	gtk_box_pack_start_defaults (GTK_BOX (fs),
		glade_xml_get_widget (fs->gui, "toplevel-table"));

	fs->font_name_entry  = glade_xml_get_widget (fs->gui, "font-name-entry");
	fs->font_style_entry = glade_xml_get_widget (fs->gui, "font-style-entry");
	fs->font_size_entry  = glade_xml_get_widget (fs->gui, "font-size-entry");
	fs->font_name_list  = glade_xml_get_widget (fs->gui, "font-name-list");
	fs->font_style_list = glade_xml_get_widget (fs->gui, "font-style-list");
	fs->font_size_list  = glade_xml_get_widget (fs->gui, "font-size-list");

	w = foo_canvas_new ();
	fs->font_preview_canvas = FOO_CANVAS (w);
	foo_canvas_set_scroll_region (fs->font_preview_canvas, -1, -1, INT_MAX/2, INT_MAX/2);
	foo_canvas_scroll_to (fs->font_preview_canvas, 0, 0);
	gtk_widget_show_all (w);
	w = glade_xml_get_widget (fs->gui, "font-preview-frame");
	gtk_container_add (GTK_CONTAINER (w), GTK_WIDGET (fs->font_preview_canvas));
	fs->font_preview_grid = FOO_CANVAS_ITEM (foo_canvas_item_new (
		foo_canvas_root (fs->font_preview_canvas),
		preview_grid_get_type (),
		"render-gridlines",	FALSE,
		"default-value",	value_new_string ("AaBbCcDdEe12345"),
		"default-style",	fs->mstyle,
		NULL));

	g_signal_connect (G_OBJECT (fs->font_preview_canvas),
		"size-allocate",
		GTK_SIGNAL_FUNC (canvas_size_changed), fs);

	fs_fill_font_style_list (fs);
	fs_fill_font_name_list (fs);
	fs_fill_font_size_list (fs);
}

static void
fs_destroy (GtkObject *object)
{
	FontSelector *fs = FONT_SELECTOR (object);

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
fs_class_init (GObjectClass *klass)
{
	GtkObjectClass *gto_class = (GtkObjectClass *) klass;

	gto_class->destroy = fs_destroy;

	fs_parent_class = g_type_class_peek (gtk_hbox_get_type ());

	fs_signals[FONT_CHANGED] =
		g_signal_new (
			"font_changed",
			G_OBJECT_CLASS_TYPE (klass),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (FontSelectorClass, font_changed),
			NULL, NULL,
			gnm__VOID__POINTER,
			G_TYPE_NONE, 1, G_TYPE_POINTER);
}

GSF_CLASS (FontSelector, font_selector,
	   fs_class_init, fs_init, GTK_TYPE_HBOX)

GtkWidget *
font_selector_new (void)
{
	GtkWidget *w;

	w = g_object_new (FONT_SELECTOR_TYPE, NULL);
	return w;
}

static void
select_row (GtkWidget *list, int row)
{
	GtkCList *cl = GTK_CLIST (list);

	gtk_clist_select_row (cl, row, 0);
}

void
font_selector_set_value (FontSelector *fs, Value const *v)
{
	Value *val;

	g_return_if_fail (IS_FONT_SELECTOR (fs));

	val = (v != NULL)
		? value_duplicate (v)
		: value_new_string ("AaBbCcDdEe12345");
	foo_canvas_item_set (fs->font_preview_grid,
		"default-value",  val,
		NULL);
}

void
font_selector_set_name (FontSelector *fs,
			const char *font_name)
{
	GList *l;
	int row;

	g_return_if_fail (IS_FONT_SELECTOR (fs));
	g_return_if_fail (font_name != NULL);

	for (row = 0, l = gnumeric_font_family_list; l; l = l->next, row++)
		if (g_ascii_strcasecmp (font_name, l->data) == 0)
			break;

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
	fs_modify_style (fs, change);
}

void
font_selector_set_strike (FontSelector *fs, gboolean strikethrough)
{
	MStyle *change;

	g_return_if_fail (IS_FONT_SELECTOR (fs));

	change = mstyle_new ();
	mstyle_set_font_strike (change, strikethrough);
	fs_modify_style (fs, change);
}

void
font_selector_set_underline (FontSelector *fs, StyleUnderlineType underline)
{
	MStyle *change;

	g_return_if_fail (IS_FONT_SELECTOR (fs));

	change = mstyle_new ();
	mstyle_set_font_uline (change, underline);
	fs_modify_style (fs, change);
}

void
font_selector_set_color (FontSelector *fs, StyleColor *color)
{
	MStyle *change;

	g_return_if_fail (IS_FONT_SELECTOR (fs));

	change = mstyle_new ();
	mstyle_set_color (change, MSTYLE_COLOR_FORE, color);
	fs_modify_style (fs, change);
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

void
font_selector_set_from_pango (FontSelector *fs, PangoFontDescription const *desc)
{
	font_selector_set_name (fs, pango_font_description_get_family (desc));
	font_selector_set_style (fs,
		pango_font_description_get_weight (desc) >= PANGO_WEIGHT_BOLD,
		pango_font_description_get_style (desc) != PANGO_STYLE_NORMAL);
	font_selector_set_points (fs,
		pango_font_description_get_size (desc) / PANGO_SCALE);
}

void
font_selector_get_pango (FontSelector *fs, PangoFontDescription *desc)
{
	pango_font_description_set_family (desc,
		mstyle_get_font_name (fs->mstyle));
	pango_font_description_set_weight (desc,
		mstyle_get_font_bold (fs->mstyle) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
	pango_font_description_set_style (desc,
		mstyle_get_font_italic (fs->mstyle) ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
	pango_font_description_set_size (desc,
		mstyle_get_font_size (fs->mstyle) * PANGO_SCALE);
}

void
font_selector_editable_enters (FontSelector *fs, GtkWindow *dialog)
{
	gnumeric_editable_enters (dialog,
		GTK_WIDGET (fs->font_name_entry));
	gnumeric_editable_enters (dialog,
		GTK_WIDGET (fs->font_style_entry));
	gnumeric_editable_enters (dialog,
		GTK_WIDGET (fs->font_size_entry));
}
