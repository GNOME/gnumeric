/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-error-bar.c :  
 *
 * Copyright (C) 2004 Jean Brefort (jean.brefort@ac-dijon.fr)
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
#include <src/gui-util.h>
#include "gog-error-bar.h"
#include "gog-series-impl.h"
#include "gog-plot-impl.h"
#include "gog-object-xml.h"
#include "gog-data-allocator.h"
#include "gog-style.h"
#include "gog-renderer.h"
#include "go-data-impl.h"
#include "go-data.h"
#include <goffice/gui-utils/go-color-palette.h>
#include <goffice/gui-utils/go-combo-color.h>
#include <goffice/gui-utils/go-combo-pixmaps.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtkcombobox.h>
#include <gtk/gtktable.h>
#include <gtk/gtklabel.h>
#include <glade/glade-xml.h>
#include <glib/gi18n.h>

#define CC2XML(s) ((const xmlChar *)(s))

typedef GObjectClass GogErrorBarClass;
static GObjectClass *error_bar_parent_klass;

typedef struct {
	GogSeries *series;
	GogErrorBar *bar;
	char const* property;
	GogErrorBarDisplay display;
	GOColor color;
	double width, line_width;
} GogErrorBarEditor;

static void
cb_destroy (G_GNUC_UNUSED GtkWidget *w, GogErrorBarEditor *editor)
{
	g_free (editor);
}

static void
cb_width_changed (GtkAdjustment *adj, GogErrorBarEditor *editor)
{
	editor->width = adj->value;
	if (editor->bar) {
		editor->bar->width = adj->value;
		gog_object_request_update (GOG_OBJECT (editor->series));
	}
}

static void
cb_line_width_changed (GtkAdjustment *adj, GogErrorBarEditor *editor)
{
	editor->line_width = adj->value;
	if (editor->bar) {
		editor->bar->style->line.width = adj->value;
		gog_object_request_update (GOG_OBJECT (editor->series));
	}
}

static void
cb_color_changed (G_GNUC_UNUSED GOComboColor *cc, GOColor color,
		     G_GNUC_UNUSED gboolean is_custom,
		     G_GNUC_UNUSED gboolean by_user,
		     G_GNUC_UNUSED gboolean is_default, GogErrorBarEditor *editor)
{
	editor->color = color;
	if (editor->bar) {
		editor->bar->style->line.color = color;
		gog_object_request_update (GOG_OBJECT (editor->series));
	}
}

static void
cb_display_changed (G_GNUC_UNUSED GOComboPixmaps *combo, GogErrorBarDisplay display, GogErrorBarEditor *editor)
{
	editor->display = display;
	if (editor->bar) {
		editor->bar->display = display;
		gog_object_request_update (GOG_OBJECT (editor->series));
	}
}

static void
cb_type_changed (GtkWidget *w, GogErrorBarEditor *editor)
{
	GladeXML *gui = GLADE_XML (g_object_get_data (G_OBJECT (w), "gui"));
	gpointer data;
	GogDataset *set;
	GogDataAllocator *dalloc;
	int type = gtk_combo_box_get_active (GTK_COMBO_BOX (w));
	dalloc = GOG_DATA_ALLOCATOR (g_object_get_data (G_OBJECT (w), "allocator"));
	if (type == GOG_ERROR_BAR_TYPE_NONE) {
		set = GOG_DATASET (editor->bar->series);
		gog_dataset_set_dim (set, editor->bar->error_i, NULL, NULL);
		gog_dataset_set_dim (set, editor->bar->error_i + 1, NULL, NULL);
		g_object_set (editor->series, editor->property, NULL, NULL);
		editor->bar = NULL;
		data = g_object_get_data (G_OBJECT (w), "plus");
		if (GTK_IS_WIDGET (data))
			gtk_widget_destroy (GTK_WIDGET(data));
		data = g_object_get_data (G_OBJECT (w), "minus");
		if (GTK_IS_WIDGET (data))
			gtk_widget_destroy (GTK_WIDGET(data));
		g_object_set_data (G_OBJECT (w), "plus", NULL);
		g_object_set_data (G_OBJECT (w), "minus", NULL);
		gtk_widget_hide (glade_xml_get_widget (gui, "plus_label"));
		gtk_widget_hide (glade_xml_get_widget (gui, "minus_label"));
		gtk_widget_hide (glade_xml_get_widget (gui, "values_label"));
	} else {
		GtkWidget *table = glade_xml_get_widget (gui, "gog_error_bar_prefs");
		if (!editor->bar) {
			editor->bar = g_object_new (GOG_ERROR_BAR_TYPE, NULL);
			editor->bar->style->line.color = editor->color;
			editor->bar->style->line.width = editor->line_width;
			editor->bar->width = editor->width;
			editor->bar->display = editor->display;
			editor->bar->type = type;
			g_object_set (editor->series, editor->property, editor->bar, NULL);
			g_object_unref (editor->bar);
			g_object_get (editor->series, editor->property, &editor->bar, NULL);
		}
		editor->bar->type = type;
		set = GOG_DATASET (editor->bar->series);
		data = g_object_get_data (G_OBJECT (w), "plus");
		if (!data) {
			GtkWidget* al = GTK_WIDGET (gog_data_allocator_editor (dalloc, set, editor->bar->error_i, FALSE));
			gtk_table_attach (GTK_TABLE (table), al, 1, 5, 7, 8, GTK_FILL | GTK_EXPAND, 0, 5, 3);
			g_object_set_data (G_OBJECT (w), "plus", al);
		}
		data = g_object_get_data (G_OBJECT (w), "minus");
		if (!data) {
			GtkWidget* al = GTK_WIDGET (gog_data_allocator_editor (dalloc, set, editor->bar->error_i + 1, FALSE));
			gtk_table_attach (GTK_TABLE (table), al, 1, 5, 8, 9, GTK_FILL | GTK_EXPAND, 0, 5, 3);
			g_object_set_data (G_OBJECT (w), "minus", al);
		}
		gtk_widget_show_all (table);
	}
	gog_object_request_update (GOG_OBJECT (editor->series));
}


GtkWidget*
gog_error_bar_prefs (GogSeries *series,
			char const* property,
			gboolean horizontal,
			GogDataAllocator *dalloc,
			GnmCmdContext *cc)
{
	GladeXML *gui;
	GtkWidget *w;
	GOComboPixmaps *cpx;
	GtkTable *table;
	GogDataset *set;
	GdkPixbuf *pixbuf;
	GogErrorBarEditor *editor;
	
	g_return_val_if_fail (IS_GOG_SERIES (series), NULL);
	
	editor = g_new0 (GogErrorBarEditor, 1);
	editor->series = series;
	editor->property = property;
	g_object_get (series, property, &editor->bar, NULL);
	if (editor->bar) {
		editor->color = editor->bar->style->line.color;
		editor->line_width = editor->bar->style->line.width;
		editor->width = editor->bar->width;
		editor->display = editor->bar->display;
	} else {
		editor->color = RGBA_BLACK;
		editor->line_width = 1.;
		editor->width = 5.;
		editor->display = GOG_ERROR_BAR_DISPLAY_BOTH;
	}
	set = GOG_DATASET (series);

	gui = gnm_glade_xml_new (cc, "gog-error-bar-prefs.glade", "gog_error_bar_prefs", NULL);

	w = glade_xml_get_widget (gui, "width");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (w), editor->width);
	g_signal_connect (gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (w)),
		"value_changed",
		G_CALLBACK (cb_width_changed), editor);

	w = glade_xml_get_widget (gui, "line_width");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (w), editor->line_width);
	g_signal_connect (gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (w)),
		"value_changed",
		G_CALLBACK (cb_line_width_changed), editor);

	table = GTK_TABLE (glade_xml_get_widget (gui, "gog_error_bar_prefs"));
	g_signal_connect (table, "destroy", G_CALLBACK (cb_destroy), editor);

	w = go_combo_color_new (NULL, _("Automatic"), RGBA_BLACK,
		go_color_group_fetch ("color", NULL));
	go_combo_color_set_instant_apply (GO_COMBO_COLOR (w), FALSE);
	go_combo_color_set_allow_alpha (GO_COMBO_COLOR (w), TRUE);
	gtk_label_set_mnemonic_widget (
		GTK_LABEL (glade_xml_get_widget (gui, "color_label")), w);
	go_combo_color_set_gocolor (GO_COMBO_COLOR (w), editor->color);
	g_signal_connect (G_OBJECT (w),
		"color_changed",
		G_CALLBACK (cb_color_changed), editor);
	gtk_table_attach (GTK_TABLE (table), w, 4, 5, 4, 5, 0, 0, 0, 0);
	cpx = go_combo_pixmaps_new (4);
	pixbuf = gdk_pixbuf_new_from_file (GNUMERICGLADEDIR"/bar-none.png", NULL);
	go_combo_pixmaps_add_element  (cpx,
					  pixbuf,
					  GOG_ERROR_BAR_DISPLAY_NONE,
					  _("No error bar displayed"));
	if (horizontal) {
		pixbuf = gdk_pixbuf_new_from_file (GNUMERICGLADEDIR"/bar-hplus.png", NULL);
		go_combo_pixmaps_add_element  (cpx,
						pixbuf,
						GOG_ERROR_BAR_DISPLAY_POSITIVE,
						_("Positive error bar displayed"));
		pixbuf = gdk_pixbuf_new_from_file (GNUMERICGLADEDIR"/bar-hminus.png", NULL);
		go_combo_pixmaps_add_element  (cpx,
						pixbuf,
						GOG_ERROR_BAR_DISPLAY_NEGATIVE,
						_("Negative error bar displayed"));
		pixbuf = gdk_pixbuf_new_from_file (GNUMERICGLADEDIR"/bar-hboth.png", NULL);
		go_combo_pixmaps_add_element  (cpx,
						pixbuf,
						GOG_ERROR_BAR_DISPLAY_BOTH,
						_("Full error bar displayed"));
	} else {
		pixbuf = gdk_pixbuf_new_from_file (GNUMERICGLADEDIR"/bar-vplus.png", NULL);
		go_combo_pixmaps_add_element  (cpx,
						pixbuf,
						GOG_ERROR_BAR_DISPLAY_POSITIVE,
						_("Positive error bar displayed"));
		pixbuf = gdk_pixbuf_new_from_file (GNUMERICGLADEDIR"/bar-vminus.png", NULL);
		go_combo_pixmaps_add_element  (cpx,
						pixbuf,
						GOG_ERROR_BAR_DISPLAY_NEGATIVE,
						_("Negative error bar displayed"));
		pixbuf = gdk_pixbuf_new_from_file (GNUMERICGLADEDIR"/bar-vboth.png", NULL);
		go_combo_pixmaps_add_element  (cpx,
						pixbuf,
						GOG_ERROR_BAR_DISPLAY_BOTH,
						_("Full error bar displayed"));
	}
	gtk_table_attach (GTK_TABLE (table), GTK_WIDGET(cpx), 4, 5, 1, 2, 0, 0, 0, 0);
	go_combo_pixmaps_select_id (cpx, editor->display);
	g_signal_connect (G_OBJECT (cpx), "changed", G_CALLBACK (cb_display_changed), editor);

	w = gtk_combo_box_new_text ();
	gtk_combo_box_append_text (GTK_COMBO_BOX (w), _("None"));
	gtk_combo_box_append_text (GTK_COMBO_BOX (w), _("Absolute"));
	gtk_combo_box_append_text (GTK_COMBO_BOX (w), _("Relative"));
	gtk_combo_box_append_text (GTK_COMBO_BOX (w), _("Percent"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (w), (editor->bar)? (int) editor->bar->type: 0);
	gtk_table_attach (table, w, 0, 2, 1, 2, 0, 0, 5, 3);
	g_object_set_data (G_OBJECT (w), "gui", gui);
	g_object_set_data (G_OBJECT (w), "allocator", dalloc);
	g_signal_connect (G_OBJECT (w), "changed", G_CALLBACK (cb_type_changed), editor);
	gtk_widget_show_all (GTK_WIDGET(table));

	if (editor->bar) {
		GtkWidget* al = GTK_WIDGET (gog_data_allocator_editor (dalloc, set, editor->bar->error_i, FALSE));
		gtk_widget_show (al);
		gtk_table_attach (table, al, 1, 5, 7, 8, GTK_FILL | GTK_EXPAND, 0, 5, 3);
		g_object_set_data (G_OBJECT (w), "plus", al);
		al = GTK_WIDGET (gog_data_allocator_editor (dalloc, set, editor->bar->error_i + 1, FALSE));
		gtk_widget_show (al);
		gtk_table_attach (table, al, 1, 5, 8, 9, GTK_FILL | GTK_EXPAND, 0, 5, 3);
		g_object_set_data (G_OBJECT (w), "minus", al);
	} else {
		gtk_widget_hide (glade_xml_get_widget (gui, "plus_label"));
		gtk_widget_hide (glade_xml_get_widget (gui, "minus_label"));
		gtk_widget_hide (glade_xml_get_widget (gui, "values_label"));
	}
	return GTK_WIDGET(table);
}

static void
gog_error_bar_init (GogErrorBar* bar)
{
	bar->type = GOG_ERROR_BAR_TYPE_NONE;
	bar->display = GOG_ERROR_BAR_DISPLAY_BOTH;
	bar->width = 5.;
	bar->style = gog_style_new ();
	bar->style->line.color = RGBA_BLACK;
	bar->style->line.width = 1.;
}

static void
gog_error_bar_finalize (GObject *obj)
{
	GogErrorBar *bar = GOG_ERROR_BAR (obj);
	if (bar->style) g_object_unref (bar->style);
	if (error_bar_parent_klass->finalize != NULL)
		(error_bar_parent_klass->finalize) (obj);
}

static void
gog_error_bar_class_init (GogErrorBarClass *klass)
{
	GObjectClass *gobject_klass = (GObjectClass *) klass;
	error_bar_parent_klass = g_type_class_peek_parent (klass);

	gobject_klass->finalize		= gog_error_bar_finalize;
}

static gboolean
gog_error_bar_persist_dom_load (GogPersistDOM *gpd, xmlNode *node)
{
	GogErrorBar *bar = GOG_ERROR_BAR (gpd);

	gchar* str;
	str = xmlGetProp (node, CC2XML ("error_type"));
	if (str) {
		if (!strcmp (str, "absolute"))
			bar->type = GOG_ERROR_BAR_TYPE_ABSOLUTE;
		else if (!strcmp (str, "relative"))
			bar->type = GOG_ERROR_BAR_TYPE_RELATIVE;
		else if (!strcmp (str, "percent"))
			bar->type = GOG_ERROR_BAR_TYPE_PERCENT;
		xmlFree (str);
	}
	str = xmlGetProp (node, CC2XML ("display"));
	if (str) {
		if (!strcmp (str, "none"))
			bar->display = GOG_ERROR_BAR_DISPLAY_NONE;
		else if (!strcmp (str, "positive"))
			bar->display = GOG_ERROR_BAR_DISPLAY_POSITIVE;
		else if (!strcmp (str, "negative"))
			bar->display = GOG_ERROR_BAR_DISPLAY_NEGATIVE;
		xmlFree (str);
	}
	str = xmlGetProp (node, CC2XML ("width"));
	if (str) {
		bar->width = g_strtod (str, NULL);
		xmlFree (str);
	}
	str = xmlGetProp (node, CC2XML ("line_width"));
	if (str) {
		bar->style->line.width = g_strtod (str, NULL);
		xmlFree (str);
	}
	str = xmlGetProp (node, CC2XML ("color"));
	if (str != NULL) {
		bar->style->line.color = go_color_from_str (str);
		xmlFree (str);
	}

	return TRUE;
}

static void
gog_error_bar_persist_dom_save (GogPersistDOM *gpd, xmlNode *parent)
{
	GogErrorBar *bar = GOG_ERROR_BAR (gpd);

	{
		const char *str = NULL;
		xmlSetProp (parent, CC2XML ("type"), CC2XML ("GogErrorBar"));
		switch (bar->type) {
		case GOG_ERROR_BAR_TYPE_ABSOLUTE:
			str = "absolute";
			break;
		case GOG_ERROR_BAR_TYPE_RELATIVE:
			str = "relative";
			break;
		case GOG_ERROR_BAR_TYPE_PERCENT:
			str = "percent";
			break;
		default:
			break;
		}
		if (str)
			xmlSetProp (parent, CC2XML ("error_type"), CC2XML (str));
	}

	{
		const char *str = NULL;
		switch (bar->display) {
		case GOG_ERROR_BAR_DISPLAY_NONE:
			str = "none";
			break;
		case GOG_ERROR_BAR_DISPLAY_POSITIVE:
			str = "positive";
			break;
		case GOG_ERROR_BAR_DISPLAY_NEGATIVE:
			str = "negative";
			break;
		default:
			break;
		}
		if (str)
			xmlSetProp (parent, CC2XML ("display"), CC2XML (str));
	}

	if (bar->width != 5.) {
		char *str = g_strdup_printf ("%f",  bar->width);
		xmlSetProp (parent, CC2XML ("width"), CC2XML (str));
		g_free (str);
	}

	if (bar->style->line.width != 1.) {
		char *str = g_strdup_printf ("%f",  bar->style->line.width);
		xmlSetProp (parent, CC2XML ("line_width"), CC2XML (str));
		g_free (str);
	}
	if (bar->style->line.color != RGBA_BLACK) {
		char *str = go_color_as_str (bar->style->line.color);
		xmlSetProp (parent, CC2XML ("color"), CC2XML (str));
		g_free (str);
	}
}

static void
gog_error_bar_persist_dom_init (GogPersistDOMClass *iface)
{
	iface->load = gog_error_bar_persist_dom_load;
	iface->save = gog_error_bar_persist_dom_save;
}

GSF_CLASS_FULL (GogErrorBar, gog_error_bar,
		gog_error_bar_class_init, gog_error_bar_init,
		G_TYPE_OBJECT, 0,
		GSF_INTERFACE (gog_error_bar_persist_dom_init, GOG_PERSIST_DOM_TYPE))


/**
 * gog_error_bar_get_bounds :
 * @bar : A GogErrorBar
 * @index : the index corresponding to the value which error limits are 
 * @min : where the minimum value will be stored 
 * @max : where the maximum value will be stored
 *
 * If the value correponding to @index is valid, fills min and max with the bounds for the values:
 * -> value + positive_error in @max.
 * -> value - negative_error in @min.
 * If one of the errors is not valid or not defined, *max will be lower than value or *min greater than value
 * (or both). The differencies, after mapping according to the axis settings, will be used as "plus" and "minus"
 * parameters for #gog_error_bar_render.
 *
 * Return value : FALSE if the @bar->type is GOG_ERROR_BAR_TYPE_NONE or if the value is not valid,
 * TRUE otherwise.
 **/
gboolean
gog_error_bar_get_bounds (GogErrorBar const *bar, int index, double *min, double *max)
{
	double value = go_data_vector_get_values (GO_DATA_VECTOR (bar->series->values[bar->dim_i].data))[index];
	GOData *data = bar->series->values[bar->error_i].data;
	int length = (IS_GO_DATA (data))? go_data_vector_get_len (GO_DATA_VECTOR (data)): 0;
	if ((bar->type == GOG_ERROR_BAR_TYPE_NONE) || isnan (value) || !finite (value))
		return FALSE;
	/* -1 ensures that the bar will not be displayed if the error is not a correct one.
		With a 0 value, it might be, because of rounding errors */
	*max = -1.; 
	*min = -1.;
	if (length == 1) 
		*max = *go_data_vector_get_values (GO_DATA_VECTOR (data));
	else if (length > index)
		*max = go_data_vector_get_values (GO_DATA_VECTOR (data))[index];
	data = bar->series->values[bar->error_i + 1].data;
	length = (IS_GO_DATA (data))? go_data_vector_get_len (GO_DATA_VECTOR (data)): 0;
	if (length == 0)
		*min = *max; /* use same values for + and - */
	else if (length == 1)
		*min = *go_data_vector_get_values (GO_DATA_VECTOR (data));
	else if (length > index)
		*min = go_data_vector_get_values (GO_DATA_VECTOR (data))[index];
	if (isnan (*min) || !finite (*min) || (*min <= 0))
		*min = -1.;
	if (isnan (*max) || !finite (*max) || (*max <= 0))
		*max = -1.;
	switch (bar->type)
	{
	case GOG_ERROR_BAR_TYPE_RELATIVE:
		*min *= fabs (value);
		*max *= fabs (value);
		break;
	case GOG_ERROR_BAR_TYPE_PERCENT:
		*min *= fabs (value) / 100;
		*max *= fabs (value) / 100;
		break;
	default:
		break;
	}
	*max += value;
	*min = value - *min;
	return TRUE;
}

void
gog_error_bar_get_minmax (const GogErrorBar *bar, double *min, double *max)
{
	int i, imax = go_data_vector_get_len (GO_DATA_VECTOR (bar->series->values[bar->dim_i].data));
	double tmp_min, tmp_max;
	go_data_vector_get_minmax (GO_DATA_VECTOR (bar->series->values[bar->dim_i].data), min, max);
	for (i = 0; i < imax; i++)
		if  (gog_error_bar_get_bounds (bar, i, &tmp_min, &tmp_max)) {
			if (tmp_min < *min)
				*min = tmp_min;
			if (tmp_max > *max)
				*max = tmp_max;
		}
}

GogErrorBar  *
gog_error_bar_dup		(GogErrorBar const *bar)
{
	GogErrorBar* dbar;

	g_return_val_if_fail (IS_GOG_ERROR_BAR (bar), NULL);

	dbar = g_object_new (GOG_ERROR_BAR_TYPE, NULL);
	dbar->type = bar->type;
	dbar->series = bar->series;
	dbar->dim_i = bar->dim_i;
	dbar->error_i = bar->error_i;
	dbar->display = bar->display;
	dbar->width = bar->width;
	if (dbar->style) g_object_unref (dbar->style);
	dbar->style = gog_style_dup (bar->style);
	return dbar;
}

/**
 * gog_error_bar_get_bounds :
 * @bar : A GogErrorBar
 * @rend : A GogRenderer 
 * @x : x coordinate of the origin of the bar 
 * @y : y coordinate of the origin of the bar
 * @plus : distance from the origin to the positive end of the bar 
 * @minus : distance from the origin to the negative end of the bar 
 * @horizontal : whether the bar is horizontal or not.
 *
 * Displays the error bar. If @plus is negative, the positive side of the bar is not displayed,
 * and if @minus is negative, the negative side of the bar is not displayed.
 * This function must not be called if #gog_error_bar_get_bounds returned FALSE.
 **/
void gog_error_bar_render (const GogErrorBar *bar,
			GogRenderer *rend,
			double x, double y,
			double plus, double minus,
			gboolean horizontal)
{
	ArtVpath path [7];
	int n;
	double x_start, y_start, x_end, y_end;
	gboolean start = ((plus > 0.) && (bar ->display & GOG_ERROR_BAR_DISPLAY_POSITIVE)),
					  end = ((minus > 0.) && (bar ->display & GOG_ERROR_BAR_DISPLAY_NEGATIVE));

	if (!start && !end) return;

	if (horizontal) {
		x_start = (start)? x + plus: x;
		x_end = (end)? x - minus: x;
		y_start = y_end = y;
	} else {
		x_start = x_end = x;
		y_start = (start)? y - plus: y;
		y_end = (end)? y + minus: y;
	}

	path[0].code = ART_MOVETO;
	path[1].code = ART_LINETO;
	path[0].x = x_start;
	path[1].x = x_end;
	path[0].y = path[1].y = y_start;
	path[0].y = y_start;
	path[1].y = y_end;

	if (bar->width > bar->style->line.width) {
		double width = bar->width / 2.;
		if (start && end) {
			path[2].code = ART_MOVETO;
			path[3].code = ART_LINETO;
			n = 4;
		} else
		n = 2;
		path[n].code = ART_MOVETO;
		path[n + 1].code = ART_LINETO;
		path[n + 2].code = ART_END;
		if (horizontal) {
			if (start) {
				path[2].x =path[3].x = x_start;
				path[2].y = y - width;
				path[3].y = y + width;
			}
			if (end) {
				path[n].x =path[n+1].x = x_end;
				path[n].y = y - width;
				path[n+1].y = y + width;
			}
		} else {
			if (start) {
				path[2].x = x - width;
				path[3].x = x + width;
				path[2].y =path[3].y = y_start;
			}
			if (end) {
				path[n].x = x - width;
				path[n+1].x = x + width;
				path[n].y =path[n+1].y = y_end;
			}
		}
	} else
		path[2].code = ART_END;

	gog_renderer_push_style (rend, bar->style);
	gog_renderer_draw_path (rend, path, NULL);
	gog_renderer_pop_style (rend);
}

gboolean
gog_error_bar_is_visible (GogErrorBar *bar)
{
	return (bar != NULL) &&
				(bar->type != GOG_ERROR_BAR_TYPE_NONE) &&
				(bar->display != GOG_ERROR_BAR_DISPLAY_NONE);
}
