/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#include <value.h>
#include <mstyle.h>
#include <preview-grid.h>
#include <style-color.h>
#include <gui-util.h>
#include <mstyle.h>
#include <preview-grid.h>

#include <goffice/utils/go-font.h>
#include <goffice/utils/go-glib-extras.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <glib/gi18n-lib.h>
#include <stdlib.h>
#include <string.h>

struct _FontSelector {
	GtkHBox box;
	GladeXML *gui;

	GtkWidget *font_name_entry;
	GtkWidget *font_style_entry;
	GtkWidget *font_size_entry;
	GtkTreeView *font_name_list;
	GtkTreeView *font_style_list;
	GtkTreeView *font_size_list;

	FooCanvas *font_preview_canvas;
	FooCanvasItem *font_preview_grid;

	GnmStyle     *mstyle;

	GSList       *family_names;
	GSList       *font_sizes;
};

typedef struct {
	GtkHBoxClass parent_class;

	void (* font_changed) (FontSelector *fs, GnmStyle *mstyle);
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
fs_modify_style (FontSelector *fs, GnmStyle *modification)
{
	GnmStyle *original = fs->mstyle;
	g_return_if_fail (modification != NULL);

	fs->mstyle = gnm_style_new_merged (original, modification);
	g_signal_emit (G_OBJECT (fs),
		fs_signals[FONT_CHANGED], 0, modification);
	foo_canvas_item_set (fs->font_preview_grid,
		"default-style",  fs->mstyle,
		NULL);
	gnm_style_unref (modification);
	gnm_style_unref (original);
}

/*
 * We cannot moveto a list element until it is mapped.
 */
/*static void
list_mapped (GtkWidget *widget, G_GNUC_UNUSED gpointer user_data)
{
	GtkCList * clist = GTK_CLIST (widget);
	int row = 0;
	if (clist->selection)
		row = GPOINTER_TO_UINT (clist->selection->data);
	if (!gtk_clist_row_is_visible (clist, row))
		gtk_clist_moveto (clist, row, 0, 0.5, 0.0);
}*/

static void
cb_list_adjust (GtkTreeView* view)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkScrolledWindow* scroll;
	GdkRectangle rect;
	GtkAdjustment *adj;
	int pos, height, child_height;

	if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (view), &model, &iter)) {
		path = gtk_tree_model_get_path (model, &iter);
		scroll = GTK_SCROLLED_WINDOW (gtk_widget_get_parent (GTK_WIDGET (view)));
		height = GTK_WIDGET (view)->allocation.height;
		child_height = GTK_WIDGET (view)->requisition.height;
		if (height < child_height) {
			gtk_tree_view_get_cell_area (view, path, NULL, &rect);
			adj = gtk_scrolled_window_get_vadjustment (scroll);
			pos = gtk_adjustment_get_value (adj);
			if (rect.y < 0)
				pos += rect.y;
			else if (rect.y + rect.height > height)
				pos += rect.y + rect.height - height;
			gtk_adjustment_set_value (adj, pos);
			gtk_scrolled_window_set_vadjustment (scroll, adj);
		}
		gtk_tree_path_free (path);
	}
}

static void
list_init (GtkTreeView* view)
{
	GtkCellRenderer *renderer;
	GtkListStore *store;
	GtkTreeViewColumn *column;

	gtk_tree_view_set_headers_visible (view, FALSE);
	store = gtk_list_store_new (1, G_TYPE_STRING);
	gtk_tree_view_set_model (view, GTK_TREE_MODEL (store));
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (
								NULL,
								renderer, "text", 0, NULL);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_append_column (view, column);
	g_signal_connect (view, "realize", G_CALLBACK (cb_list_adjust), NULL);
}

static void
font_selected (GtkTreeSelection *selection,
	       FontSelector *fs)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		GnmStyle *change = gnm_style_new ();
		gchar *text;

		gtk_tree_model_get (model, &iter, 0, &text, -1);
		gtk_entry_set_text (GTK_ENTRY (fs->font_name_entry), text);

		gnm_style_set_font_name (change, text);
		g_free (text);
		fs_modify_style (fs, change);
	}
}

static void
fs_fill_font_name_list (FontSelector *fs)
{
	GSList *l;
	GtkListStore *store;
	GtkTreeIter iter;
	PangoContext *context;

#warning "FIXME: We need to do this when we realize the widget as we don't have a screen until then."
	context = gtk_widget_get_pango_context (GTK_WIDGET (fs));

	fs->family_names = go_fonts_list_families (context);
	list_init (fs->font_name_list);
	store = GTK_LIST_STORE (gtk_tree_view_get_model (fs->font_name_list));
	for (l = fs->family_names; l; l = l->next) {
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, 0, l->data, -1);
	}

	g_signal_connect (
		 G_OBJECT (gtk_tree_view_get_selection (fs->font_name_list)), "changed",
		 G_CALLBACK (font_selected), fs);
}

static char const *styles[] = {
	 N_("Normal"),
	 N_("Bold"),
	 N_("Bold italic"),
	 N_("Italic"),
	 NULL
};

static void
style_selected (GtkTreeSelection *selection,
		FontSelector *fs)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		GnmStyle *change = gnm_style_new ();
		GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
		int row = *gtk_tree_path_get_indices (path);
		gtk_tree_path_free (path);

		switch (row) {
		case 0:
			gnm_style_set_font_bold (change, FALSE);
			gnm_style_set_font_italic (change, FALSE);
			break;
		case 1:
			gnm_style_set_font_bold (change, TRUE);
			gnm_style_set_font_italic (change, FALSE);
			break;
		case 2:
			gnm_style_set_font_bold (change, TRUE);
			gnm_style_set_font_italic (change, TRUE);
			break;
		case 3:
			gnm_style_set_font_bold (change, FALSE);
			gnm_style_set_font_italic (change, TRUE);
			break;
		}

		gtk_entry_set_text (GTK_ENTRY (fs->font_style_entry), _(styles[row]));
		fs_modify_style (fs, change);
	}
}

static void
fs_fill_font_style_list (FontSelector *fs)
{
	 int i;
	GtkListStore *store;
	GtkTreeIter iter;

	list_init (fs->font_style_list);
	store = GTK_LIST_STORE (gtk_tree_view_get_model (fs->font_style_list));
	for (i = 0; styles[i] != NULL; i++) {
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, 0, _(styles[i]), -1);
	}
	g_signal_connect (
		G_OBJECT (gtk_tree_view_get_selection (fs->font_style_list)), "changed",
		G_CALLBACK (style_selected), fs);
}

static void
select_row (GtkTreeView *list, int row)
{
	GtkTreePath *path;

	if (row < 0)
		gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (list));
	else {
		path = gtk_tree_path_new_from_indices (row, -1);

		gtk_tree_selection_select_path (gtk_tree_view_get_selection (list), path);
		if (GTK_WIDGET_REALIZED (list))
			cb_list_adjust (list);
		gtk_tree_path_free (path);
	}
}

static void
size_selected (GtkTreeSelection *selection,
	       FontSelector *fs)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		GnmStyle *change = gnm_style_new ();
		gchar *text;

		gtk_tree_model_get (model, &iter, 0, &text, -1);
		gtk_entry_set_text (GTK_ENTRY (fs->font_size_entry), text);
		gnm_style_set_font_size (change, atof (text));
		g_free (text);
		fs_modify_style (fs, change);
	}
}

static void
size_changed (GtkEntry *entry, FontSelector *fs)
{
	int i;
	char const *text = gtk_entry_get_text (entry);
	double size = atof (text);
	int psize = (int)(size * PANGO_SCALE + 0.5);
	GSList *l;

	if (size >= 1. && size < 128) {
		GnmStyle *change = gnm_style_new ();
		gnm_style_set_font_size (change, size);
		fs_modify_style (fs, change);
	}
	g_signal_handlers_block_by_func (
		gtk_tree_view_get_selection (fs->font_size_list),
		size_selected, fs);

	for (i = 0, l = fs->font_sizes; l; i++, l = l->next) {
		int this_psize = GPOINTER_TO_INT (l->data);
		if (this_psize == psize)
			break;
	}

	select_row (fs->font_size_list, l ? i : -1);
	g_signal_handlers_unblock_by_func (
				gtk_tree_view_get_selection (fs->font_size_list),
				size_selected, fs);
}

static void
fs_fill_font_size_list (FontSelector *fs)
{
	GtkListStore *store;
	GtkTreeIter iter;
	GSList *ptr;

	fs->font_sizes = go_fonts_list_sizes ();

	list_init (fs->font_size_list);
	store = GTK_LIST_STORE (gtk_tree_view_get_model (fs->font_size_list));
	for (ptr = fs->font_sizes ; ptr != NULL ; ptr = ptr->next) {
		int psize = GPOINTER_TO_INT (ptr->data);
		char *size_text = g_strdup_printf ("%g", psize / (double)PANGO_SCALE);
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, 0, size_text, -1);
		g_free (size_text);
	}
	g_signal_connect (
		G_OBJECT (gtk_tree_view_get_selection (fs->font_size_list)), "changed",
		G_CALLBACK (size_selected), fs);

	g_signal_connect (
		G_OBJECT (fs->font_size_entry), "changed",
		G_CALLBACK (size_changed), fs);
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

	fs->mstyle = gnm_style_new_default ();
	gnm_style_set_align_v   (fs->mstyle, VALIGN_CENTER);
	gnm_style_set_align_h   (fs->mstyle, HALIGN_CENTER);
	gnm_style_set_font_size (fs->mstyle, 10);

	gtk_box_pack_start_defaults (GTK_BOX (fs),
		glade_xml_get_widget (fs->gui, "toplevel-table"));

	fs->font_name_entry  = glade_xml_get_widget (fs->gui, "font-name-entry");
	fs->font_style_entry = glade_xml_get_widget (fs->gui, "font-style-entry");
	fs->font_size_entry  = glade_xml_get_widget (fs->gui, "font-size-entry");
	fs->font_name_list  = GTK_TREE_VIEW (glade_xml_get_widget (fs->gui, "font-name-list"));
	fs->font_style_list = GTK_TREE_VIEW (glade_xml_get_widget (fs->gui, "font-style-list"));
	fs->font_size_list  = GTK_TREE_VIEW (glade_xml_get_widget (fs->gui, "font-size-list"));

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
		G_CALLBACK (canvas_size_changed), fs);

	fs_fill_font_name_list (fs);
	fs_fill_font_style_list (fs);
	fs_fill_font_size_list (fs);
}

static void
fs_destroy (GtkObject *object)
{
	FontSelector *fs = FONT_SELECTOR (object);

	if (fs->mstyle) {
		gnm_style_unref (fs->mstyle);
		fs->mstyle = NULL;
	}

	if (fs->gui) {
		g_object_unref (G_OBJECT (fs->gui));
		fs->gui = NULL;
	}

	go_slist_free_custom (fs->family_names, g_free);
	fs->family_names = NULL;

	g_slist_free (fs->font_sizes);
	fs->font_sizes = NULL;

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
			g_cclosure_marshal_VOID__POINTER,
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

void
font_selector_set_value (FontSelector *fs, GnmValue const *v)
{
	GnmValue *val;

	g_return_if_fail (IS_FONT_SELECTOR (fs));

	val = (v != NULL)
		? value_dup (v)
		: value_new_string ("AaBbCcDdEe12345");
	foo_canvas_item_set (fs->font_preview_grid,
		"default-value",  val,
		NULL);
}

void
font_selector_set_name (FontSelector *fs,
			const char *font_name)
{
	GSList *l;
	int row;

	g_return_if_fail (IS_FONT_SELECTOR (fs));
	g_return_if_fail (font_name != NULL);

	for (row = 0, l = fs->family_names; l; l = l->next, row++)
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
	GnmStyle *change;

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

	change = gnm_style_new ();
	gnm_style_set_font_bold   (change, is_bold);
	gnm_style_set_font_italic (change, is_italic);
	fs_modify_style (fs, change);
}

void
font_selector_set_strike (FontSelector *fs, gboolean strikethrough)
{
	GnmStyle *change;

	g_return_if_fail (IS_FONT_SELECTOR (fs));

	change = gnm_style_new ();
	gnm_style_set_font_strike (change, strikethrough);
	fs_modify_style (fs, change);
}

void
font_selector_set_script (FontSelector *fs, GOFontScript script)
{
	GnmStyle *change;

	g_return_if_fail (IS_FONT_SELECTOR (fs));

	change = gnm_style_new ();
	gnm_style_set_font_script (change, script);
	fs_modify_style (fs, change);
}

void
font_selector_set_underline (FontSelector *fs, GnmUnderline underline)
{
	GnmStyle *change;

	g_return_if_fail (IS_FONT_SELECTOR (fs));

	change = gnm_style_new ();
	gnm_style_set_font_uline (change, underline);
	fs_modify_style (fs, change);
}

void
font_selector_set_color (FontSelector *fs, GnmColor *color)
{
	GnmStyle *change;

	g_return_if_fail (IS_FONT_SELECTOR (fs));

	change = gnm_style_new ();
	gnm_style_set_font_color (change, color);
	fs_modify_style (fs, change);
}

void
font_selector_set_points (FontSelector *fs,
			  double point_size)
{
	const char *old_text = gtk_entry_get_text (GTK_ENTRY (fs->font_size_entry));
	char *buffer = g_strdup_printf ("%g", point_size);
	if (strcmp (old_text, buffer) != 0)
		gtk_entry_set_text (GTK_ENTRY (fs->font_size_entry), buffer);
	g_free (buffer);
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
		gnm_style_get_font_name (fs->mstyle));
	pango_font_description_set_weight (desc,
		gnm_style_get_font_bold (fs->mstyle) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
	pango_font_description_set_style (desc,
		gnm_style_get_font_italic (fs->mstyle) ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
	pango_font_description_set_size (desc,
		gnm_style_get_font_size (fs->mstyle) * PANGO_SCALE);
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
