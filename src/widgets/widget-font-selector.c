/*
 * A font selector widget.  This is a simplified version of the
 * GnomePrint font selector widget.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include "widget-font-selector.h"
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>

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

static void
reload_preview (FontSelector *fs)
{
	char *name = gtk_entry_get_text (GTK_ENTRY (fs->font_name_entry));
	GtkStyle *style;
	GnomeFont *gnome_font;
	GnomeDisplayFont *display_font;
	
	gnome_font = gnome_font_new_closest (
		name,
		fs->is_bold ? GNOME_FONT_BOLD : GNOME_FONT_BOOK,
		fs->is_italic,
		fs->size);
	if (!gnome_font){
		g_warning ("Uh oh, can not get the font!");
		return;
	}

	display_font = gnome_font_get_display_font (gnome_font);
	if (!display_font){
		g_warning ("Uh oh, can not get the display font");
		return;
	}

	if (!display_font->gdk_font){
		gtk_object_unref (GTK_OBJECT (gnome_font));
		return;
	}

	fs->gnome_font = gnome_font;
	fs->display_font = display_font;
		
	style = gtk_style_new ();
	gdk_font_unref (style->font);
	style->font = fs->display_font->gdk_font;
	gdk_font_ref (style->font);

	gtk_widget_set_style (fs->font_preview, style);
	gtk_style_unref (style);
}

static void
font_selected (GtkCList *font_list, int col, int row, GdkEvent *event, FontSelector *fs)
{
	int row;
	gchar *text;
	
	if (font_list->selection == NULL)
		return;

	row = GPOINTER_TO_INT (font_list->selection->data);
	gtk_clist_get_text (font_list, row, 0, &text);
	printf ("Text at %d, %s\n", row, text);
	gtk_entry_set_text (GTK_ENTRY (fs->font_name_entry), text);

	reload_preview (fs);
}

static void
fs_fill_font_name_list (FontSelector *fs)
{
	GnomeFontClass *gfc = gtk_type_class (gnome_font_get_type ());
	GList *font_list = gnome_font_family_list (gfc);
	GList *l;
	
	for (l = font_list; l; l = l->next){
		char *name = l->data;
		char *array [1];

		array [0] = name;
		
		gtk_clist_append (GTK_CLIST (fs->font_name_list), array);
	}
	gtk_signal_connect (
		GTK_OBJECT (fs->font_name_list), "select_row",
		GTK_SIGNAL_FUNC(font_selected), fs);
}

static char *styles [] = {
	N_("Normal"),
	N_("Bold"),
	N_("Bold italic"),
	N_("Italic"),
	NULL
};

static void
style_selected (GtkCList *style_list, int col, int row, GdkEvent *event, FontSelector *fs)
{
	int row;
	
	if (style_list->selection == NULL)
		return;

	row = GPOINTER_TO_INT (style_list->selection->data);
	switch (row){
	case 0:
		fs->is_bold = fs->is_italic = FALSE;
		break;
	case 1:
		fs->is_bold = TRUE;
		fs->is_italic = FALSE;
		break;

	case 2:
		fs->is_bold = fs->is_italic = TRUE;
		break;
	case 3:
		fs->is_italic = TRUE;
		fs->is_bold = FALSE;
		break;
	}

	gtk_entry_set_text (GTK_ENTRY (fs->font_style_entry), _(styles [row]));
	reload_preview (fs);
}

static void
fs_fill_font_style_list (FontSelector *fs)
{
	GtkCList *style_list = GTK_CLIST (fs->font_style_list);
	int i;
	
	for (i = 0; styles [i] != NULL; i++){
		char *array [1];
		array [0] = _(styles [i]);
		
		gtk_clist_append (style_list, array);
	}
	gtk_signal_connect (
		GTK_OBJECT (fs->font_style_list), "select_row",
		GTK_SIGNAL_FUNC(style_selected), fs);
}

static void
size_selected (GtkCList *size_list, int col, int row, GdkEvent *event, FontSelector *fs)
{
	int row;
	gchar *text;

	if (size_list->selection == NULL)
		return;

	row = GPOINTER_TO_INT (size_list->selection->data);
	gtk_clist_get_text (size_list, row, 0, &text);
	gtk_entry_set_text (GTK_ENTRY (fs->font_size_entry), text);
}

static void
size_changed (GtkEntry *entry, FontSelector *fs)
{
	char *text;
	double size;
	
	text = gtk_entry_get_text (entry);
	size = atof (text);
	if (size > 0 && size < 128){
		fs->size = size;
		reload_preview (fs);
	}
}

static void
fs_fill_font_size_list (FontSelector *fs)
{
	int i;
	static int point_sizes [] = {
		4, 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 22,
		24, 26, 28, 32, 36, 40, 48, 56, 64, 72, 0 };

	for (i = 0; point_sizes [i] != 0; i++){
		char buffer [10];
		char *array [1];
		
		sprintf (buffer, "%d", point_sizes [i]);
		array [0] = buffer;
		gtk_clist_append (GTK_CLIST (fs->font_size_list), array);
	}
	gtk_signal_connect (
		GTK_OBJECT (fs->font_size_list), "select_row",
		GTK_SIGNAL_FUNC(size_selected), fs);

	gtk_signal_connect (
		GTK_OBJECT (fs->font_size_entry), "changed",
		GTK_SIGNAL_FUNC (size_changed), fs);
}

static void
fs_init (FontSelector *fs)
{
	GtkWidget *toplevel;
	GtkWidget *old_parent;
	
	fs->gui = glade_xml_new (GNUMERIC_GLADEDIR "/font-sel.glade", NULL);
	if (!fs->gui){
		g_error ("Could not load font-sel.glade");
		return;
	}
	toplevel = glade_xml_get_widget (fs->gui, "toplevel-table");
	old_parent = gtk_widget_get_toplevel (toplevel);
	gtk_widget_reparent (toplevel, GTK_WIDGET (fs));
	gtk_widget_destroy (old_parent);
	gtk_widget_queue_resize (toplevel);

	fs->font_name_entry  = glade_xml_get_widget (fs->gui, "font-name-entry");
	fs->font_style_entry = glade_xml_get_widget (fs->gui, "font-style-entry");
	fs->font_size_entry  = glade_xml_get_widget (fs->gui, "font-size-entry");
	fs->font_name_list  = glade_xml_get_widget (fs->gui, "font-name-list");
	fs->font_style_list = glade_xml_get_widget (fs->gui, "font-style-list");
	fs->font_size_list  = glade_xml_get_widget (fs->gui, "font-size-list");
	fs->font_preview = glade_xml_get_widget (fs->gui, "preview-entry");
	fs_fill_font_style_list (fs);
	fs_fill_font_name_list (fs);
	fs_fill_font_size_list (fs);
}

static void
fs_destroy (GtkObject *object)
{
	FontSelector *fs = FONT_SELECTOR (object);

	if (fs->gnome_font)
		gtk_object_unref (GTK_OBJECT (fs->gnome_font));
	if (fs->display_font)
		gtk_object_unref (GTK_OBJECT (fs->display_font));		
	((GtkObjectClass *)fs_parent_class)->destroy (object);
}

static void
fs_class_init (GtkObjectClass *class)
{
	class->destroy = fs_destroy;

	fs_parent_class = gtk_type_class (gtk_hbox_get_type ());
}

GtkType
font_selector_get_type (void)
{
	static GtkType fs_type = 0;

	if (!fs_type){
		GtkTypeInfo fs_info = {
			"FontSelector",
			sizeof (FontSelector),
			sizeof (FontSelectorClass),
			(GtkClassInitFunc) fs_class_init,
			(GtkObjectInitFunc) fs_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		fs_type = gtk_type_unique (gtk_hbox_get_type (), &fs_info);
	}

	return fs_type;
}

GtkWidget *
font_selector_new (void)
{
	GtkWidget *w;

	w = gtk_type_new (FONT_SELECTOR_TYPE);
	return w;
}
