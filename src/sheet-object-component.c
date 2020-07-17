/*
 * sheet-object-component.c
 *
 * Copyright (C) 2008 Jean Bréfort <jean.brefort@normalesup.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <application.h>
#include <commands.h>
#include <gnm-pane-impl.h>
#include <gui-util.h>
#include <sheet-control-gui.h>
#include <sheet-object-component.h>
#include <sheet-object-impl.h>
#include <wbc-gtk.h>
#include <goffice/goffice.h>
#include <goffice/component/go-component.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-output-gio.h>
#include <glib/gi18n-lib.h>


static void
so_component_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	GocItem *item = sheet_object_view_get_item (sov);
	double scale = goc_canvas_get_pixels_per_unit (item->canvas);

	if (visible) {
		GOComponent *component = sheet_object_component_get_component (sheet_object_view_get_so (sov));
		double width, height;
		goc_item_set (GOC_ITEM (sov),
			"x", MIN (coords [0], coords[2]) / scale,
			"y", MIN (coords [3], coords[1]) / scale,
			NULL);
		if (component && ! go_component_is_resizable (component)) {
			go_component_get_size (component, &width, &height);

			goc_item_set (item,
				"width", width * gnm_app_display_dpi_get (TRUE),
				"height", height * gnm_app_display_dpi_get (FALSE),
				NULL);
		} else
			goc_item_set (item,
				"width", (fabs (coords [2] - coords [0]) + 1.) / scale,
				"height", (fabs (coords [3] - coords [1]) + 1.) / scale,
				NULL);

		goc_item_show (item);
	} else
		goc_item_hide (item);
}

typedef SheetObjectView		SOComponentGocView;
typedef SheetObjectViewClass	SOComponentGocViewClass;

static void
so_component_goc_view_class_init (SheetObjectViewClass *sov_klass)
{
	sov_klass->set_bounds	= so_component_view_set_bounds;
}

static GSF_CLASS (SOComponentGocView, so_component_goc_view,
	so_component_goc_view_class_init, NULL,
	GNM_SO_VIEW_TYPE)


/****************************************************************************/
#define SHEET_OBJECT_CONFIG_KEY "sheet-object-component-key"

#define SHEET_OBJECT_COMPONENT_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), GNM_SO_COMPONENT_TYPE, SheetObjectComponentClass))

typedef struct {
	SheetObject  base;
	GOComponent	*component;
} SheetObjectComponent;
typedef SheetObjectClass SheetObjectComponentClass;

static GObjectClass *parent_klass;

static void
gnm_soc_finalize (GObject *obj)
{
	SheetObjectComponent *soc = GNM_SO_COMPONENT (obj);

	g_object_unref (soc->component);

	parent_klass->finalize (obj);
}

static SheetObjectView *
gnm_soc_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmPane *pane = GNM_PANE (container);
	SheetObjectComponent *soc = GNM_SO_COMPONENT (so);
	GocItem *view = goc_item_new (pane->object_views,
		so_component_goc_view_get_type (),
		NULL);
	goc_item_hide (goc_item_new (GOC_GROUP (view),
		      GOC_TYPE_COMPONENT,
		      "object", soc->component,
		      NULL));
	return gnm_pane_object_register (so, view, TRUE);
}

static GtkTargetList *
gnm_soc_get_target_list (SheetObject const *so)
{
	GtkTargetList *tl = gtk_target_list_new (NULL, 0);
	char *mime_str = go_image_format_to_mime ("svg");
	GSList *mimes, *ptr;

	mimes = go_strsplit_to_slist (mime_str, ',');
	for (ptr = mimes; ptr != NULL; ptr = ptr->next) {
		const char *mime = ptr->data;

		if (mime != NULL && *mime != '\0')
			gtk_target_list_add (tl, gdk_atom_intern (mime, FALSE),
					     0, 0);
	}
	g_free (mime_str);
	g_slist_free_full (mimes, g_free);
	/* No need to eliminate duplicates. */
	gtk_target_list_add_image_targets (tl, 0, TRUE);

	return tl;
}

static GtkTargetList *
gnm_soc_get_object_target_list (SheetObject const *so)
{
	SheetObjectComponent *soc = GNM_SO_COMPONENT (so);
	GtkTargetList *tl = gtk_target_list_new (NULL, 0);
	if (soc && soc->component)
		gtk_target_list_add (tl,
			gdk_atom_intern (go_component_get_mime_type (soc->component), FALSE), 0, 0);
	return tl;
}

static void
gnm_soc_write_image (SheetObject const *so, char const *format, double resolution,
		     GsfOutput *output, GError **err)
{
	SheetObjectComponent *soc = GNM_SO_COMPONENT (so);
	gboolean res = FALSE;
	double coords[4];
	double w, h;

	if (so->sheet) {
		sheet_object_position_pts_get (GNM_SO (so), coords);
		w = fabs (coords[2] - coords[0]) + 1.;
		h = fabs (coords[3] - coords[1]) + 1.;
	} else {
		w = GPOINTER_TO_UINT
			(g_object_get_data (G_OBJECT (so), "pt-width-at-copy"));
		h = GPOINTER_TO_UINT
			(g_object_get_data (G_OBJECT (so), "pt-height-at-copy"));
	}

	g_return_if_fail (w > 0 && h > 0);

	res = go_component_export_image (soc->component, go_image_get_format_from_name (format),
				      output, resolution, resolution);

	if (!res && err && *err == NULL)
		*err = g_error_new (gsf_output_error_id (), 0,
				    _("Unknown failure while saving image"));
}

static void
soc_cb_save_as (SheetObject *so, SheetControl *sc)
{
	SheetObjectComponent *soc = GNM_SO_COMPONENT (so);
	/* FIXME: This violates model gui barrier */
	WBCGtk *wbcg = scg_wbcg (GNM_SCG (sc));
	GtkWidget *dlg = gtk_file_chooser_dialog_new (_("Save as"),
	                                              GTK_WINDOW (wbcg_toplevel (wbcg)),
	                                              GTK_FILE_CHOOSER_ACTION_SAVE,
	                                              GNM_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
	                                              GNM_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                              NULL);
	GtkFileFilter *filter = gtk_file_filter_new ();
	gtk_file_filter_add_mime_type (filter, go_component_get_mime_type (soc->component));
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dlg), filter);
	if (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_ACCEPT) {
		char *uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dlg));
		GError *err = NULL;
		GsfOutput *output = gsf_output_gio_new_for_uri (uri, &err);
		if (err != NULL)
			go_cmd_context_error (GO_CMD_CONTEXT (wbcg), err);
		else {
			char *buf;
			int length;
			gpointer user_data = NULL;
			void (*clearfunc) (gpointer) = NULL;
			go_component_get_data (soc->component, (gpointer) &buf, &length, &clearfunc, &user_data);
			gsf_output_write (output, length, buf);
			if (clearfunc)
				clearfunc ((user_data)? user_data: buf);
			gsf_output_close (output);
			g_object_unref (output);
		}
		g_free (uri);
	}
	gtk_widget_destroy (dlg);
}

static void
soc_cb_save_as_image (SheetObject *so, SheetControl *sc)
{
	WBCGtk *wbcg;
	char *uri;
	GError *err = NULL;
	GsfOutput *output;
	GSList *l;
	GOImageFormat selected_format;
	GOImageFormatInfo const *format_info;
	SheetObjectComponent *soc = GNM_SO_COMPONENT (so);
	double resolution;

	g_return_if_fail (soc != NULL);

	/* assuming that components support the same image formats than graphs */
	l = gog_graph_get_supported_image_formats ();
	g_return_if_fail (l != NULL);
	selected_format = GPOINTER_TO_UINT (l->data);

	/* FIXME: This violates model gui barrier */
	wbcg = scg_wbcg (GNM_SCG (sc));
	uri = go_gui_get_image_save_info (wbcg_toplevel (wbcg), l, &selected_format, &resolution);
	if (!uri)
		goto out;
	output = go_file_create (uri, &err);
	if (!output)
		goto out;
	format_info = go_image_get_format_info (selected_format);
	sheet_object_write_image (so, format_info->name, resolution, output, &err);
	g_object_unref (output);

	if (err != NULL)
		go_cmd_context_error (GO_CMD_CONTEXT (wbcg), err);

out:
	g_free (uri);
	g_slist_free (l);
}

static void
gnm_soc_populate_menu (SheetObject *so, GPtrArray *actions)
{
	static SheetObjectAction const soc_actions[] = {
		{ "document-save",    GNM_N_STOCK_SAVE, NULL, 0, soc_cb_save_as },
		{ "document-save-as", N_("_Save As Image"), NULL, 0, soc_cb_save_as_image }
	};

	unsigned int i;

	GNM_SO_CLASS (parent_klass)->populate_menu (so, actions);

	for (i = 0; i < G_N_ELEMENTS (soc_actions); i++)
		g_ptr_array_insert (actions, 1 + i, (gpointer)(soc_actions + i));
}

static void
gnm_soc_write_object (SheetObject const *so, char const *format,
		      GsfOutput *output, GError **err,
		      GnmConventions const *convs)
{
	SheetObjectComponent *soc = GNM_SO_COMPONENT (so);
	char *buf;
	int length;
	gpointer user_data = NULL;
	void (*clearfunc) (gpointer) = NULL;
	go_component_get_data (soc->component, (gpointer) &buf, &length, &clearfunc, &user_data);
	gsf_output_write (output, length, buf);
	if (clearfunc)
		clearfunc ((user_data)? user_data: buf);
}

static void
gnm_soc_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
		       GnmConventions const *convs)
{
	SheetObjectComponent const *soc = GNM_SO_COMPONENT (so);
	go_component_write_xml_sax (soc->component, output);
}

static void
soc_xml_finish (GOComponent *component, SheetObject *so)
{
	sheet_object_component_set_component (so, component);
	g_object_unref (component);
}

static void
gnm_soc_prep_sax_parser (SheetObject *so, GsfXMLIn *xin, xmlChar const **attrs,
			 GnmConventions const *convs)
{
	go_component_sax_push_parser (xin, attrs,
				    (GOComponentSaxHandler) soc_xml_finish, so);
}

static void
gnm_soc_copy (SheetObject *dst, SheetObject const *src)
{
	SheetObjectComponent *soc = GNM_SO_COMPONENT (src);
	GOComponent *component = go_component_duplicate (soc->component);
	sheet_object_component_set_component (dst, component);
	g_object_unref (component);
}

static void
gnm_soc_default_size (SheetObject const *so, double *w, double *h)
{
	SheetObjectComponent const *soc = GNM_SO_COMPONENT (so);
	if (soc->component && !go_component_is_resizable (soc->component)) {
		go_component_get_size (soc->component, w, h);
		*w = GO_IN_TO_PT (*w);
		*h = GO_IN_TO_PT (*h);
	} else {
		*w = GO_CM_TO_PT ((double)5);
		*h = GO_CM_TO_PT ((double)5);
	}
}

typedef struct {
	SheetObject *so;
	WorkbookControl *wbc;
	GOComponent *component;
	gulong signal;
} gnm_soc_user_config_t;

static void
component_changed_cb (GOComponent *component, gnm_soc_user_config_t *data)
{
	SheetObjectComponent *soc = GNM_SO_COMPONENT (data->so);
	cmd_so_component_config (data->wbc, data->so, G_OBJECT (component), G_OBJECT (soc->component));
}

static void
destroy_cb ( gnm_soc_user_config_t *data)
{
	wbcg_edit_finish (WBC_GTK (data->wbc), WBC_EDIT_REJECT, NULL);
	go_component_set_command_context (data->component, NULL);
	g_object_unref (data->component);
	g_free (data);
}

static void
gnm_soc_user_config (SheetObject *so, SheetControl *sc)
{
	SheetObjectComponent *soc = GNM_SO_COMPONENT (so);
	GtkWidget *w;
	GOComponent *new_comp;

	g_return_if_fail (soc && soc->component);

	go_component_set_command_context (soc->component, GO_CMD_CONTEXT (scg_wbcg (GNM_SCG (sc))));
	new_comp = go_component_duplicate (soc->component);
	go_component_set_command_context (new_comp, GO_CMD_CONTEXT (scg_wbcg (GNM_SCG (sc))));
	w = (GtkWidget *) go_component_edit (new_comp);
	go_component_set_command_context (soc->component, NULL);
	if (w) {
		gnm_soc_user_config_t *data = g_new0 (gnm_soc_user_config_t, 1);
		data->so = so;
		data->component = new_comp;
		data->wbc = GNM_WBC (scg_wbcg (GNM_SCG (sc)));
		data->signal = g_signal_connect (new_comp, "changed", G_CALLBACK (component_changed_cb), data);
		g_object_set_data_full (G_OBJECT (w), "editor", data, (GDestroyNotify) destroy_cb);
		wbc_gtk_attach_guru (scg_wbcg (GNM_SCG (sc)), w);
	}
}

static void
gnm_soc_draw_cairo (SheetObject const *so, cairo_t *cr,
			  double width, double height)
{
	SheetObjectComponent *soc = GNM_SO_COMPONENT (so);
	g_return_if_fail (soc && soc->component);

	go_component_render (soc->component, cr, width, height);
}

static void
gnm_soc_class_init (GObjectClass *klass)
{
	SheetObjectClass	*so_class  = GNM_SO_CLASS (klass);

	parent_klass = g_type_class_peek_parent (klass);

	/* Object class method overrides */
	klass->finalize = gnm_soc_finalize;

	/* SheetObject class method overrides */
	so_class->new_view		= gnm_soc_new_view;
	so_class->populate_menu		= gnm_soc_populate_menu;
	so_class->write_xml_sax		= gnm_soc_write_xml_sax;
	so_class->prep_sax_parser	= gnm_soc_prep_sax_parser;
	so_class->copy			= gnm_soc_copy;
	so_class->user_config		= gnm_soc_user_config;
	so_class->default_size		= gnm_soc_default_size;
	so_class->draw_cairo		= gnm_soc_draw_cairo;

	so_class->rubber_band_directly = FALSE;
}

static void
gnm_soc_init (GObject *obj)
{
	SheetObject *so = GNM_SO (obj);
	so->anchor.base.direction = GOD_ANCHOR_DIR_DOWN_RIGHT;
}

static void
soc_imageable_init (SheetObjectImageableIface *soi_iface)
{
	soi_iface->get_target_list = gnm_soc_get_target_list;
	soi_iface->write_image	   = gnm_soc_write_image;
}

static void
soc_exportable_init (SheetObjectExportableIface *soe_iface)
{
	soe_iface->get_target_list = gnm_soc_get_object_target_list;
	soe_iface->write_object	   = gnm_soc_write_object;
}

GSF_CLASS_FULL (SheetObjectComponent, sheet_object_component,
		NULL, NULL, gnm_soc_class_init,NULL,
		gnm_soc_init, GNM_SO_TYPE, 0,
		GSF_INTERFACE (soc_imageable_init, GNM_SO_IMAGEABLE_TYPE) \
		GSF_INTERFACE (soc_exportable_init, GNM_SO_EXPORTABLE_TYPE));

/**
 * sheet_object_component_new:
 * @component: #GOComponent
 *
 * Returns: (transfer full): the newly allocated #SheetObject.
 **/
SheetObject *
sheet_object_component_new (GOComponent *component)
{
	SheetObjectComponent *soc = g_object_new (GNM_SO_COMPONENT_TYPE, NULL);
	sheet_object_component_set_component (GNM_SO (soc), component);
	if (component)
		g_object_unref (component);
	return GNM_SO (soc);
}

/**
 * sheet_object_component_get_component:
 * @soc: #SheetObject
 *
 * Returns: (transfer none): the embedded #GOComponent or %NULL on error.
 **/
GOComponent*
sheet_object_component_get_component (SheetObject *soc)
{
	g_return_val_if_fail (GNM_IS_SO_COMPONENT (soc), NULL);

	return ((SheetObjectComponent *) soc)->component;
}

void
sheet_object_component_set_component (SheetObject *so, GOComponent *component)
{
	SheetObjectComponent *soc;
	GList *l = so->realized_list;

	g_return_if_fail (GNM_IS_SO_COMPONENT (so));
	soc = GNM_SO_COMPONENT (so);
	if (soc->component != NULL) {
		go_component_stop_editing (soc->component);
		g_object_unref (soc->component);
	}

	soc->component = component;

	for (; l; l = l->next) {
		SheetObjectView *sov = l->data;
		if (sov) {
			GocItem *item = sheet_object_view_get_item (sov);
			if (item)
				g_object_set (item, "object", component, NULL);
		}
	}
	if (component) {
		g_object_ref (component);
		go_component_stop_editing (component);
		if (go_component_is_resizable (component))
			so->flags |= SHEET_OBJECT_CAN_RESIZE;
		else {
			so->flags &= ~(SHEET_OBJECT_CAN_RESIZE | SHEET_OBJECT_SIZE_WITH_CELLS);
			so->anchor.mode = GNM_SO_ANCHOR_ONE_CELL;
		}
		if (go_component_is_editable (component))
			so->flags |= SHEET_OBJECT_CAN_EDIT;
		else
			so->flags &= ~SHEET_OBJECT_CAN_EDIT;
	}

}
