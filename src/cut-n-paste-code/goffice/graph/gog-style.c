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
#include <gal/widgets/widget-color-combo.h>
#include <gal/widgets/color-palette.h>

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
	style->fill.u.gradient.start = color_combo_get_gocolor (cc);
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
	GtkWidget *table, *w;
	GladeXML *gui;

	g_object_get (G_OBJECT (gobj), "style", &style, NULL);
	g_return_val_if_fail (style != NULL, NULL);

	/* glade file life cycle */
	gui = gnm_glade_xml_new (cc, "so-fill.glade", "table", NULL);
	if (gui == NULL)
		return NULL;

 	table = glade_xml_get_widget (gui, "table");
	g_object_set_data_full (G_OBJECT (table),
		"state", gui, (GDestroyNotify)g_object_unref);

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

	/* fill colour */
	w = color_combo_new (NULL, _("Transparent"),
		NULL, color_group_fetch ("fill_color", cc));
	gnome_color_picker_set_use_alpha (COLOR_COMBO (w)->palette->picker, TRUE);
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (gui, "fill_label")), w);
	color_combo_set_gocolor (w, style->fill.u.solid.color);
	gtk_table_attach (GTK_TABLE (table), w, 1, 2, 1, 2, 0, 0, 0, 0);
	g_signal_connect (G_OBJECT (w),
		"color_changed",
		G_CALLBACK (cb_fill_color_changed), gobj);

	gtk_widget_show_all (GTK_WIDGET (table));
	return table;
}

static void
gog_style_finalize (GObject *obj)
{
	GogStyle *style = GOG_STYLE (obj);

	if ((style->flags & GOG_STYLE_FILL) &&
	    GOG_FILL_STYLE_IMAGE == style->fill.type) {
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
	GogSeriesElementStyle *pt;
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
	GogSeriesElementStyle *pt;
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
