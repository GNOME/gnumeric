/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-style.c :
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <gnumeric-config.h>
#include <goffice/graph/gog-style.h>
#include <goffice/utils/go-color.h>

#include <src/gui-util.h>
#include <glade/glade-xml.h>
#include <gtk/gtkspinbutton.h>
#include <widgets/widget-color-combo.h>
#include <widgets/color-palette.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>
#include <string.h>

typedef GObjectClass GogStyleClass;

static GObjectClass *parent_klass;

/**
 * I would have liked to do this differently and have a tighter binding between theme element and style
 * 	eg gog_style_new (theme_element)
 * However that will not work easily in the context of xls import where we do
 * not know what the type is destined for until later.  This structure melds
 * smoothly with both approaches at the expense of a bit of power.
 **/

static void
cb_outline_width_changed (GtkAdjustment *adj, GogObject *gobj)
{
	GogStyle *style = NULL;
	g_object_get (G_OBJECT (gobj), "style", &style, NULL);
	style = gog_style_dup (style);

	g_return_if_fail (style != NULL);

	style->flags |= GOG_STYLE_OUTLINE;
	style->outline.width = adj->value;
	g_object_set (G_OBJECT (gobj), "style", style, NULL);
}

static void
cb_outline_color_changed (GtkWidget *cc,
			  G_GNUC_UNUSED GdkColor *color,
			  G_GNUC_UNUSED gboolean is_custom,
			  G_GNUC_UNUSED gboolean by_user,
			  G_GNUC_UNUSED gboolean is_default,
			  GogObject *gobj)
{
	GogStyle *style = NULL;
	g_object_get (G_OBJECT (gobj), "style", &style, NULL);
	style = gog_style_dup (style);

	g_return_if_fail (style != NULL);

	style->flags |= GOG_STYLE_OUTLINE;
	style->outline.color = color_combo_get_gocolor (cc);
	g_object_set (G_OBJECT (gobj), "style", style, NULL);
}

static void
cb_fill_color_changed (GtkWidget *cc,
		       G_GNUC_UNUSED GdkColor *color,
		       G_GNUC_UNUSED gboolean is_custom,
		       G_GNUC_UNUSED gboolean by_user,
		       G_GNUC_UNUSED gboolean is_default,
		       GogObject *gobj)
{
	GogStyle *style = NULL;
	g_object_get (G_OBJECT (gobj), "style", &style, NULL);
	style = gog_style_dup (style);

	g_return_if_fail (style != NULL);

	style->flags |= GOG_STYLE_FILL;
	style->fill.u.solid.color = color_combo_get_gocolor (cc);
	g_object_set (G_OBJECT (gobj), "style", style, NULL);
}

static void
cb_start_color_changed (GtkWidget *cc,
			G_GNUC_UNUSED GdkColor *color,
			G_GNUC_UNUSED gboolean is_custom,
			G_GNUC_UNUSED gboolean by_user,
			G_GNUC_UNUSED gboolean is_default,
			GogObject *gobj)
{
	GogStyle *style = NULL;
	g_object_get (G_OBJECT (gobj), "style", &style, NULL);
	style = gog_style_dup (style);

	g_return_if_fail (style != NULL);

	style->flags |= GOG_STYLE_FILL;
	style->fill.u.gradient.start = color_combo_get_gocolor (cc);
	g_object_set (G_OBJECT (gobj), "style", style, NULL);
}

static void
cb_end_color_changed (GtkWidget *cc,
		       G_GNUC_UNUSED GdkColor *color,
		       G_GNUC_UNUSED gboolean is_custom,
		       G_GNUC_UNUSED gboolean by_user,
		       G_GNUC_UNUSED gboolean is_default,
		       GogObject *gobj)
{
	GogStyle *style = NULL;
	g_object_get (G_OBJECT (gobj), "style", &style, NULL);
	style = gog_style_dup (style);

	g_return_if_fail (style != NULL);

	style->flags |= GOG_STYLE_FILL;
	style->fill.u.gradient.end = color_combo_get_gocolor (cc);
	g_object_set (G_OBJECT (gobj), "style", style, NULL);
}

static void
init_solid_page (GogObject *gobj, GladeXML *gui, GogStyle *style)
{
	GtkWidget *table, *w;
	CommandContext *cc;
  	table = glade_xml_get_widget (gui, "table");
	cc = (CommandContext*) g_object_get_data (G_OBJECT (table), "command-context");
	table = glade_xml_get_widget (gui, "fill_table");
	w = color_combo_new (NULL, _("Transparent"),
		NULL, color_group_fetch ("fill_color", cc));
	gnome_color_picker_set_use_alpha (COLOR_COMBO (w)->palette->picker, TRUE);
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (gui, "fill_label")), w);
	switch (style->fill.type) {
	case GOG_FILL_STYLE_SOLID:
		color_combo_set_gocolor (w, style->fill.u.solid.color);
		break;
	case GOG_FILL_STYLE_PATTERN:
		color_combo_set_gocolor (w, style->fill.u.pattern.back);
		break;
	case GOG_FILL_STYLE_GRADIENT:
		color_combo_set_gocolor (w, style->fill.u.gradient.start);
		break;
	default:
		color_combo_set_gocolor (w, RGB_WHITE);
	}
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 0, 1, 0, 0, 0, 0);
	g_signal_connect (G_OBJECT (w),
		"color_changed",
		G_CALLBACK (cb_fill_color_changed), gobj);
	g_object_set_data (G_OBJECT (table), "color", w);
	gtk_widget_show_all (table);
}

static void
cb_gradient_type_changed (GtkWidget *cc,
		       GogObject *gobj)
{
	GogStyle *style = NULL;
	g_object_get (G_OBJECT (gobj), "style", &style, NULL);
	style = gog_style_dup (style);

	g_return_if_fail (style != NULL);

	style->fill.u.gradient.type = gtk_option_menu_get_history (GTK_OPTION_MENU (cc));
	g_object_set (G_OBJECT (gobj), "style", style, NULL);
}

static void
init_gradient_page (GogObject *gobj, GladeXML *gui, GogStyle *style)
{
	GtkWidget *table, *w;
	GtkOptionMenu *menu;
 	CommandContext *cc;
  	table = glade_xml_get_widget (gui, "table");
	cc = (CommandContext*) g_object_get_data (G_OBJECT (table), "command-context");
	table = glade_xml_get_widget (gui, "gradient_table");
	w = color_combo_new (NULL, _("Transparent"),
		NULL, color_group_fetch ("start_color", cc));
	gnome_color_picker_set_use_alpha (COLOR_COMBO (w)->palette->picker, TRUE);
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (gui, "start_label")), w);
	switch (style->fill.type) {
	case GOG_FILL_STYLE_SOLID:
		color_combo_set_gocolor (w, style->fill.u.solid.color);
		break;
	case GOG_FILL_STYLE_PATTERN:
		color_combo_set_gocolor (w, style->fill.u.pattern.back);
		break;
	case GOG_FILL_STYLE_GRADIENT:
		color_combo_set_gocolor (w, style->fill.u.gradient.start);
		break;
	default:
		color_combo_set_gocolor (w, RGB_WHITE);
	}
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 0, 1, 0, 0, 0, 0);
	g_signal_connect (G_OBJECT (w),
		"color_changed",
		G_CALLBACK (cb_start_color_changed), gobj);
	g_object_set_data (G_OBJECT (table), "start", w);
	w = color_combo_new (NULL, _("Transparent"),
		NULL, color_group_fetch ("end_color", cc));
	gnome_color_picker_set_use_alpha (COLOR_COMBO (w)->palette->picker, TRUE);
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (gui, "end_label")), w);
	switch (style->fill.type) {
	case GOG_FILL_STYLE_SOLID:
		color_combo_set_gocolor (w, style->fill.u.solid.color);
		break;
	case GOG_FILL_STYLE_PATTERN:
		color_combo_set_gocolor (w, style->fill.u.pattern.fore);
		break;
	case GOG_FILL_STYLE_GRADIENT:
		color_combo_set_gocolor (w, style->fill.u.gradient.end);
		break;
	default:
		color_combo_set_gocolor (w, RGB_BLACK);
	}
	color_combo_set_gocolor (w, style->fill.u.gradient.end);
	gtk_table_attach (GTK_TABLE (table), w, 3, 4, 0, 1, 0, 0, 0, 0);
	g_signal_connect (G_OBJECT (w),
		"color_changed",
		G_CALLBACK (cb_end_color_changed), gobj);
	g_object_set_data (G_OBJECT (table), "end", w);
	menu = GTK_OPTION_MENU (glade_xml_get_widget (gui, "gradient_type"));
	gtk_option_menu_set_history (menu,
					(style->fill.type == GOG_FILL_STYLE_GRADIENT)?
						style->fill.u.gradient.type: GOG_GRADIENT_N_TO_S);
	g_signal_connect (G_OBJECT (menu),
		"changed",
		G_CALLBACK (cb_gradient_type_changed), gobj);
	g_object_set_data (G_OBJECT (menu), "state", gui);
	gtk_widget_show_all (table);
}

static gboolean
cb_image_filename_changed (GtkWidget *cc,
				GdkEventFocus *ev,
		       GogObject *gobj)
{
	GogStyle *style = NULL;
	char const *filename;

	g_object_get (G_OBJECT (gobj), "style", &style, NULL);
	style = gog_style_dup (style);

	g_return_val_if_fail (style != NULL, FALSE);
	
	filename = gtk_entry_get_text (GTK_ENTRY (cc));
	
	style->fill.u.image.image_file = (filename)? g_strdup (filename): NULL;
	g_object_set (G_OBJECT (gobj), "style", style, NULL);
	return FALSE;
}

static void
cb_image_file_select (GtkWidget *cc, GogObject *gobj)
{
	GogStyle *style = NULL;
	GtkWidget *fs, *w;
	gint result;
	const gchar* filename;
	GladeXML *gui;

	g_object_get (G_OBJECT (gobj), "style", &style, NULL);
	style = gog_style_dup (style);

	g_return_if_fail (style != NULL);

	fs = gtk_file_selection_new (_("Select an image file"));
	gtk_window_set_modal (GTK_WINDOW (fs), TRUE);
	if (style->fill.u.image.image_file)
		gtk_file_selection_set_filename (GTK_FILE_SELECTION (fs), style->fill.u.image.image_file);
	result = gtk_dialog_run (GTK_DIALOG (fs));
	if (result == GTK_RESPONSE_OK) {
		filename = gtk_file_selection_get_filename (GTK_FILE_SELECTION (fs));
		if (filename && !*filename) filename = NULL;
		if (style->fill.u.image.image_file)
				g_free (style->fill.u.image.image_file);
		style->fill.u.image.image_file = (filename)? g_strdup (filename): NULL;
		gui = (GladeXML*) g_object_get_data (G_OBJECT (cc), "state");
		w = glade_xml_get_widget (gui, "image_filename");
		gtk_entry_set_text (GTK_ENTRY (w), (filename)? filename: "");
		g_object_set (G_OBJECT (gobj), "style", style, NULL);
	}
	gtk_widget_destroy (fs);
	g_return_if_fail (style != NULL);
}

static void
cb_image_style_changed (GtkWidget *cc,
		       GogObject *gobj)
{
	GogStyle *style = NULL;
	g_object_get (G_OBJECT (gobj), "style", &style, NULL);
	style = gog_style_dup (style);

	g_return_if_fail (style != NULL);
	
	style->fill.u.image.type = gtk_option_menu_get_history (GTK_OPTION_MENU (cc));
	
	g_object_set (G_OBJECT (gobj), "style", style, NULL);
}

static void
cb_type_changed (GtkWidget *cc,
		       GogObject *gobj)
{
	GtkNotebook* notebook;
	gint page;
	GladeXML *gui;
	GtkWidget *w, *table;
	GogStyle *style = NULL;
	const gchar* filename;
	g_object_get (G_OBJECT (gobj), "style", &style, NULL);
	style = gog_style_dup (style);

	g_return_if_fail (style != NULL);
	
	if ((style->flags & GOG_STYLE_FILL) &&
	    GOG_FILL_STYLE_IMAGE == style->fill.type &&
		style->fill.u.image.image_file) {
			g_free (style->fill.u.image.image_file);
	}

	gui = (GladeXML*) g_object_get_data (G_OBJECT (cc), "state");
	notebook = GTK_NOTEBOOK (glade_xml_get_widget (gui, "notebook"));
	page = gtk_option_menu_get_history(GTK_OPTION_MENU (cc));
	switch (page) {
	case 0:
		style->fill.type = GOG_FILL_STYLE_NONE;
		break;
	case 1:
		table = glade_xml_get_widget (gui, "fill_table");
		w = (GtkWidget*) g_object_get_data (G_OBJECT (table), "color");
		if (! w) {
			init_solid_page (gobj, gui, style);
			w = (GtkWidget*) g_object_get_data (G_OBJECT (table), "color");
		}
		style->fill.type = GOG_FILL_STYLE_SOLID;
		style->fill.u.solid.color = color_combo_get_gocolor (w);
		style->fill.u.solid.is_auto = FALSE;
		break;
	case 2:
		table = glade_xml_get_widget (gui, "gradient_table");
		w = (GtkWidget*) g_object_get_data (G_OBJECT (table), "start");
		if (! w)
		{
			init_gradient_page (gobj, gui, style);
			w = (GtkWidget*) g_object_get_data (G_OBJECT (table), "start");
		}
		style->fill.type = GOG_FILL_STYLE_GRADIENT;
		style->fill.u.gradient.start = color_combo_get_gocolor (w);
		w = GTK_WIDGET (g_object_get_data (G_OBJECT (table), "end"));
		style->fill.u.gradient.end = color_combo_get_gocolor (w);
		w = glade_xml_get_widget (gui, "gradient_type");
		style->fill.u.gradient.type = gtk_option_menu_get_history (GTK_OPTION_MENU (w));
		break;
	case 3:
		style->fill.type = GOG_FILL_STYLE_PATTERN;
		break;
	case 4:
		style->fill.type = GOG_FILL_STYLE_IMAGE;
		w = glade_xml_get_widget (gui, "image_filename");
		filename = gtk_entry_get_text (GTK_ENTRY (w));
		style->fill.u.image.image_file = (filename && *filename)? g_strdup (filename): NULL;
		w = glade_xml_get_widget (gui, "image_option");
		style->fill.u.image.type = gtk_option_menu_get_history (GTK_OPTION_MENU (w));
		break;
	}
	gtk_notebook_set_current_page (notebook, page);
	g_object_set (G_OBJECT (gobj), "style", style, NULL);
}

GogStyle *
gog_style_new (void)
{
	return g_object_new (GOG_STYLE_TYPE, NULL);
}

/**
 * gog_style_dup :
 * @src : #GogStyle
 *
 **/
GogStyle *
gog_style_dup (GogStyle const *src)
{
	GogStyle *dst = gog_style_new ();
	gog_style_copy (dst, src);
	return dst;
}

void
gog_style_copy (GogStyle *dst, GogStyle const *src)
{
	g_return_if_fail (GOG_STYLE (src) != NULL);
	g_return_if_fail (GOG_STYLE (dst) != NULL);

	dst->flags   = src->flags;
	dst->outline = src->outline;
	dst->fill    = src->fill;
	if ((dst->flags & GOG_STYLE_FILL) &&
	    GOG_FILL_STYLE_IMAGE == dst->fill.type) {
	}
	dst->marker  = src->marker;
}

GtkWidget *
gog_style_editor (GogObject *gobj, CommandContext *cc, guint32 enable)
{
	GogStyle *style = NULL;
	GtkWidget *table, *w, *notebook, *menu;
	GladeXML *gui;

	g_object_get (G_OBJECT (gobj), "style", &style, NULL);
	g_return_val_if_fail (style != NULL, NULL);

	/* glade file life cycle */
	gui = gnm_glade_xml_new (cc, "gog-style-prefs.glade", "table", NULL);
	if (gui == NULL)
		return NULL;

 	table = glade_xml_get_widget (gui, "table");
	g_object_set_data_full (G_OBJECT (table),
		"state", gui, (GDestroyNotify)g_object_unref);
	g_object_set_data (G_OBJECT (table), "command-context", cc);

	notebook = glade_xml_get_widget (gui, "notebook");
	menu = glade_xml_get_widget (gui, "menu_type");
	g_signal_connect (G_OBJECT (menu),
		"changed",
		G_CALLBACK (cb_type_changed), gobj);
	g_object_set_data (G_OBJECT (menu), "state", gui);

	/* outline width */
	w = glade_xml_get_widget (gui, "spin_border_width");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (w), style->outline.width);
	g_signal_connect (G_OBJECT (gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (w))),
		"value_changed",
		G_CALLBACK (cb_outline_width_changed), gobj);

	/* outline colour */
	w = color_combo_new (NULL, _("Transparent"),
		NULL, color_group_fetch ("border_color", cc));
	gnome_color_picker_set_use_alpha (COLOR_COMBO (w)->palette->picker, TRUE);
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (gui, "border_label")), w);
	color_combo_set_gocolor (w, style->outline.color);
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 0, 1, 0, 0, 0, 0);
	g_signal_connect (G_OBJECT (w),
		"color_changed",
		G_CALLBACK (cb_outline_color_changed), gobj);

	/* initialization of the image related widgets */
	w = glade_xml_get_widget (gui, "image_filename");
	g_signal_connect (G_OBJECT (w),
		"focus_out_event",
		G_CALLBACK (cb_image_filename_changed), gobj);
	w = glade_xml_get_widget (gui, "image_button");
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_image_file_select), gobj);
	g_object_set_data (G_OBJECT (w), "state", gui);
	w = glade_xml_get_widget (gui, "image_option");
	g_signal_connect (G_OBJECT (w),
		"changed",
		G_CALLBACK (cb_image_style_changed), gobj);

	if (enable & GOG_STYLE_FILL) {
		switch (style->fill.type) {
		case GOG_FILL_STYLE_NONE:
			gtk_option_menu_set_history (GTK_OPTION_MENU (menu), 0);
			gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 0);
			break;
		case GOG_FILL_STYLE_SOLID:
			init_solid_page (gobj, gui, style);
			gtk_option_menu_set_history (GTK_OPTION_MENU (menu), 1);
			gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 1);
			break;
		case GOG_FILL_STYLE_GRADIENT:
			init_gradient_page (gobj, gui, style);
			gtk_option_menu_set_history (GTK_OPTION_MENU (menu), 2);
			gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 2);
			break;
		case GOG_FILL_STYLE_PATTERN:
			break;
		case GOG_FILL_STYLE_IMAGE:
			gtk_option_menu_set_history (GTK_OPTION_MENU (menu), 4);
			gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 4);
			w = glade_xml_get_widget (gui, "image_filename");
			gtk_entry_set_text (GTK_ENTRY (w), style->fill.u.image.image_file);
			w = glade_xml_get_widget (gui, "image_style");
			gtk_option_menu_set_history (GTK_OPTION_MENU (w), style->fill.u.image.type);
			break;
		default :
			break;
		}
	} else {
		gtk_option_menu_set_history (GTK_OPTION_MENU (menu), 0);
		gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 0);
		gtk_widget_set_sensitive (menu, FALSE);
	}
	gtk_widget_show_all (GTK_WIDGET (table));
	return table;
}

static void
gog_style_finalize (GObject *obj)
{
	GogStyle *style = GOG_STYLE (obj);

	if ((style->flags & GOG_STYLE_FILL) &&
	    GOG_FILL_STYLE_IMAGE == style->fill.type &&
		style->fill.u.image.image_file) {
			g_free (style->fill.u.image.image_file);
	}

	(parent_klass->finalize) (obj);
}

static void
gog_style_class_init (GogStyleClass *klass)
{
	GObjectClass *gobject_klass = (GObjectClass *)klass;
	parent_klass = g_type_class_peek_parent (klass);
	gobject_klass->finalize	= gog_style_finalize;
}

static void
gog_style_init (GogStyle *style)
{
	style->flags = 0; /* everything is auto */
}

GSF_CLASS (GogStyle, gog_style,
	   gog_style_class_init, gog_style_init,
	   G_TYPE_OBJECT)

gboolean
gog_style_has_marker (GogStyle const *style)
{
	return (style->flags & GOG_STYLE_MARKER) != 0;
}

gboolean
gog_style_is_different_size (GogStyle const *a, GogStyle const *b)
{
	if (a == NULL || b == NULL)
		return TRUE;
	return a->outline.width != b->outline.width;
}

/************************************************************************/

static GogSeriesElementStyle *
go_series_element_style_new (GogStyle *style, unsigned i)
{
	GogSeriesElementStyle *pt = g_new (GogSeriesElementStyle, 1);
	pt->i = pt->i;
	pt->style = pt->style;
	g_object_ref (pt->style);
	return pt;
}

static int
go_series_element_style_cmp (GogSeriesElementStyle const *a,
			     GogSeriesElementStyle const *b)
{
	return a->i - b->i;
}

static void
go_series_element_style_free (GogSeriesElementStyle *pt)
{
	g_return_if_fail (pt != NULL);

	if (pt->style != NULL) {
		g_object_unref (pt->style);
		pt->style = NULL;
	}
	g_free (pt);
}

void
gog_series_element_style_list_free (GogSeriesElementStyleList *list)
{
	g_slist_foreach (list, (GFunc) go_series_element_style_free, NULL);
	g_slist_free (list);
}

/**
 * go_series_element_style_list_copy :
 * @list : #GogSeriesElementStyleList
 *
 * Returns a copy of the list that references the same styles as the original
 **/
GogSeriesElementStyleList *
gog_series_element_style_list_copy (GogSeriesElementStyleList *list)
{
	GogSeriesElementStyle *pt = NULL;
	GSList *ptr, *res = NULL;

	for (ptr = list; ptr != NULL ; ptr = ptr->next) {
		pt = ptr->data;
		res = g_slist_prepend (res,
			go_series_element_style_new (pt->style, pt->i));
	}
	return g_slist_reverse (res);
}

GogSeriesElementStyleList *
gog_series_element_style_list_add (GogSeriesElementStyleList *list,
				   unsigned i, GogStyle *style)
{
	GogSeriesElementStyle *pt = NULL;
	GSList *ptr;

	for (ptr = list ; ptr != NULL ; ptr = ptr->next) {
		pt = ptr->data;
		if (pt->i == i)
			break;
	}

	/* Setting NULL clears existing */
	if (style == NULL) {
		if (ptr != NULL) {
			go_series_element_style_free (pt);
			return g_slist_remove (list, pt);
		}
		return list;
	}
	pt = go_series_element_style_new (style, i);
	return g_slist_insert_sorted  (list, pt,
				       (GCompareFunc) go_series_element_style_cmp);
}

/***************************************************************************/

GOColor
color_combo_get_gocolor (GtkWidget *cc)
{
	GOColor res = 0; /* transparent */
	GdkColor *gdk = color_combo_get_color (COLOR_COMBO (cc), NULL);
	if (gdk != NULL) {
		res = GDK_TO_UINT (*gdk);
		gdk_color_free (gdk);
	}
	return res;
}

void
color_combo_set_gocolor (GtkWidget *cc, GOColor c)
{
	if (UINT_RGBA_A (c) != 0) {
		GdkColor gdk;
		gdk.red    = UINT_RGBA_R(c);
		gdk.red   |= (gdk.red << 8);
		gdk.green  = UINT_RGBA_G(c);
		gdk.green |= (gdk.green << 8);
		gdk.blue   = UINT_RGBA_B(c);
		gdk.blue  |= (gdk.blue << 8);
		/* should not be necessary.  The CC should do it for itself */
		gdk_rgb_find_color (gtk_widget_get_colormap (cc), &gdk);
		color_combo_set_color (COLOR_COMBO (cc), &gdk);
	} else
		color_combo_set_color (COLOR_COMBO (cc), NULL);
}
