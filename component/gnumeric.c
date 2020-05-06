/*
 * Gnumeric GOffice component
 * gnumeric.c
 *
 * Copyright (C) 2006-2010
 *
 * Developed by Jean Bréfort <jean.brefort@normalesup.org>
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
#include <application.h>
#include <gnumeric.h>
#include <gnm-plugin.h>
#include <gnumeric-conf.h>
#include <gui-file.h>
#include <gui-util.h>
#include <gutils.h>
#include <print-cell.h>
#include <print.h>
#include <ranges.h>
#include <selection.h>
#include <sheet.h>
#include <wbc-gtk-impl.h>
#include <workbook-view.h>
#include <workbook.h>

#include <goffice/goffice.h>
#include <goffice/component/goffice-component.h>
#include <goffice/component/go-component-factory.h>
#include <goffice/component/go-component.h>
#include <goffice/app/module-plugin-defs.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-input-memory.h>
#include <gsf/gsf-output-memory.h>


#include <glib/gi18n-lib.h>
#include <cairo.h>
#include <pango/pangocairo.h>

GOPluginModuleDepend const go_plugin_depends [] = {
	{ "goffice", GOFFICE_API_VERSION }
};
GOPluginModuleHeader const go_plugin_header =
	{ GOFFICE_MODULE_PLUGIN_MAGIC_NUMBER, G_N_ELEMENTS (go_plugin_depends) };

G_MODULE_EXPORT void go_plugin_init (GOPlugin *plugin, GOCmdContext *cc);
G_MODULE_EXPORT void go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc);

typedef struct {
	GOComponent parent;

	WorkbookView	*wv;
	Workbook	*wb;
	WBCGtk		*edited;
	Sheet		*sheet;
	int col_start, col_end, row_start, row_end;
	int width, height;
} GOGnmComponent;

typedef GOComponentClass GOGnmComponentClass;

#define GO_TYPE_GNM_COMPONENT	(go_gnm_component_get_type ())
#define GO_GNM_COMPONENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GO_TYPE_GNM_COMPONENT, GOGnmComponent))
#define GO_IS_GNM_COMPONENT(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GO_TYPE_GNM_COMPONENT))

GType go_gnm_component_get_type (void);

static GObjectClass *gognm_parent_klass;

static gboolean
go_gnm_component_get_data (GOComponent *component, gpointer *data, int *length,
									void (**clearfunc) (gpointer), gpointer *user_data)
{
	GOGnmComponent *gognm = GO_GNM_COMPONENT (component);
	if (gognm->wv) {
		GOCmdContext *cc = go_component_get_command_context (component);
		GOIOContext *io_context = go_io_context_new (cc);
		GsfOutput *output = gsf_output_memory_new ();
		GOFileSaver *gfs = workbook_get_file_saver (gognm->wb);
		if (gfs == NULL)
			gfs = go_file_saver_get_default ();
		workbook_view_save_to_output (gognm->wv, gfs, output,
					      io_context);
		*data = (gpointer) gsf_output_memory_get_bytes (GSF_OUTPUT_MEMORY (output));
		*length = gsf_output_size (output);
		*clearfunc = g_object_unref;
		*user_data = output;
		g_object_unref (io_context);
		return TRUE;
	}
	return FALSE;
}

static void
go_gnm_component_update_data (GOGnmComponent *gognm)
{
	SheetView *sv;
	GnmRange const *range;
	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (gognm->wv));
	gognm->sheet = wb_view_cur_sheet (gognm->wv);
	sv = sheet_get_view (gognm->sheet, gognm->wv);
	range = selection_first_range (sv, NULL, NULL);
	gognm->col_start = range->start.col;
	gognm->row_start = range->start.row;
	gognm->col_end = range->end.col;
	gognm->row_end = range->end.row;
	gognm->width = sheet_col_get_distance_pts (
		gognm->sheet, gognm->col_start, gognm->col_end + 1);
	gognm->parent.width = gognm->width / 72.;
	gognm->parent.descent = 0.;
	gognm->height = sheet_row_get_distance_pts (
		gognm->sheet, gognm->row_start, gognm->row_end + 1);
	gognm->parent.ascent = gognm->parent.height = gognm->height  / 72.;
}

static void
go_gnm_component_set_data (GOComponent *component)
{
	GOGnmComponent *gognm = GO_GNM_COMPONENT (component);
	GOCmdContext *cc = go_component_get_command_context (component);
	GOIOContext *io_context = go_io_context_new (cc);
	GsfInput *input = gsf_input_memory_new (component->data, component->length, FALSE);

	g_object_set (G_OBJECT (io_context), "exec-main-loop", FALSE, NULL);
	if (gognm->wv != NULL) {
		g_object_unref (gognm->wv);
		g_object_unref (gognm->wb);
		gognm->wv = NULL;
	}
	gognm->wv = workbook_view_new_from_input (input, NULL, NULL, io_context, NULL);
	if (!GNM_IS_WORKBOOK_VIEW (gognm->wv)) {
		g_warning ("Invalid component data");
		gognm->wv = NULL;
		gognm->wb = NULL;
	} else {
		gognm->wb = wb_view_get_workbook (gognm->wv);
		gnm_app_workbook_list_remove (gognm->wb);
	}
	g_object_unref (io_context);
	go_gnm_component_update_data (gognm);
}

static void
go_gnm_component_render (GOComponent *component, cairo_t *cr, double width_pixels, double height_pixels)
{
	GOGnmComponent *gognm = GO_GNM_COMPONENT (component);
	GnmRange range;

	g_return_if_fail (GNM_IS_WORKBOOK_VIEW (gognm->wv));
	if (!gognm->sheet)
		go_gnm_component_update_data (gognm);

	range_init (&range, gognm->col_start, gognm->row_start, gognm->col_end, gognm->row_end);
	cairo_save (cr);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);
	cairo_scale (cr, ((double) width_pixels) / gognm->width, ((double) height_pixels) / gognm->height);
	cairo_rectangle (cr, 0., 0., gognm->width, gognm->height);
	cairo_clip (cr); /* not sure it is necessary */
	gnm_gtk_print_cell_range (cr, gognm->sheet, &range, 0., 0.,
				  gognm->sheet->print_info);
	/* Now render objects */
	gnm_print_sheet_objects (cr, gognm->sheet, &range, 0., 0.);
	cairo_restore (cr);
}

static void
cb_gognm_save (G_GNUC_UNUSED GtkAction *a, WBCGtk *wbcg)
{
	gpointer data = g_object_get_data (G_OBJECT (wbcg), "component");
	if (GO_IS_COMPONENT (data)) {
		GOComponent *component = GO_COMPONENT (data);
		/* update the component data since not all clients will call set_data */
		GOGnmComponent *gognm = GO_GNM_COMPONENT (component);
		WorkbookView *wv = wb_control_view (GNM_WBC (wbcg));
		if (wv != gognm->wv) {
			if (gognm->wv != NULL) {
				g_object_unref (gognm->wv);
				g_object_unref (gognm->wb);
			}
			gognm->wv = g_object_ref (wv);
			gognm->wb = wb_view_get_workbook (wv);
			gnm_app_workbook_list_remove (gognm->wb); /* no need to have this one in the list */
		}
		go_doc_set_dirty (GO_DOC (gognm->wb), FALSE);
		go_gnm_component_update_data (gognm);
		go_component_emit_changed (component);
	} else
		gui_file_save (wbcg, wb_control_view (GNM_WBC (wbcg)));
}

/* File */
static GnmActionEntry const actions[] = {
/* File */
	{ .name = "FileSaveEmbed",
	  .icon = "document-save",
	  .label = GNM_N_STOCK_SAVE,
	  .label_context = GNM_STOCK_LABEL_CONTEXT,
	  .accelerator = "<control>s",
	  .tooltip = N_("Save the current workbook"),
	  .callback = G_CALLBACK (cb_gognm_save)
	}
};

static void
cb_editor_destroyed (GOGnmComponent *gognm)
{
	if (gognm->edited && G_OBJECT (gognm->edited)->ref_count > 0)
		g_object_unref (gognm->edited);
	gognm->edited = NULL;
}

static GtkWindow*
go_gnm_component_edit (GOComponent *component)
{
	GOGnmComponent *gognm = GO_GNM_COMPONENT (component);
	WorkbookView *wv;
	if (gognm->edited) {
		gdk_window_raise (gtk_widget_get_parent_window (GTK_WIDGET (wbcg_toplevel (gognm->edited))));
		return wbcg_toplevel (gognm->edited);
	}
	if (!gognm->wv) {
		component->ascent = 0.;
		component->descent = 0.;
		component->width = 0.;
		wv = workbook_view_new (workbook_new_with_sheets (1));
	} else {
		GOCmdContext *cc = go_component_get_command_context (component);
		GOIOContext *io_context = GO_IS_IO_CONTEXT (cc)? GO_IO_CONTEXT (g_object_ref (cc)): go_io_context_new (cc);
		GsfInput *input = gsf_input_memory_new (component->data, component->length, FALSE);

		g_object_set (G_OBJECT (io_context), "exec-main-loop", FALSE, NULL);
		wv = workbook_view_new_from_input (input, NULL, NULL, io_context, NULL);
		g_object_unref (io_context);
	}
	set_uifilename ("Gnumeric-embed.xml", actions, G_N_ELEMENTS (actions));
	gognm->edited = wbc_gtk_new (wv, NULL, NULL, NULL);

	g_object_set_data (G_OBJECT (gognm->edited), "component", gognm);
	g_signal_connect_swapped (gognm->edited->toplevel, "destroy",
		G_CALLBACK (cb_editor_destroyed), gognm);
	gtk_window_set_title (wbcg_toplevel (gognm->edited), _("Embedded spreadsheet"));
	return wbcg_toplevel (gognm->edited);
}

static void
go_gnm_component_finalize (GObject *obj)
{
	GOGnmComponent *gognm = GO_GNM_COMPONENT (obj);
	if (gognm->wv != NULL) {
		g_object_unref (gognm->wv);
		g_object_unref (gognm->wb);
		gognm->wv = NULL;
	}
	if (gognm->edited != NULL) {
		g_object_unref (wb_control_view (GNM_WBC (gognm->edited)));
		gognm->edited = NULL;
	}
	G_OBJECT_CLASS (gognm_parent_klass)->finalize (obj);
}

static void
go_gnm_component_init (GOComponent *component)
{
	GOGnmComponent *gognm = GO_GNM_COMPONENT (component);
	component->resizable = FALSE;
	component->editable = TRUE;
	component->snapshot_type = GO_SNAPSHOT_SVG;
	gognm->row_start = gognm->col_start = 0;
	gognm->sheet = NULL;
	gognm->row_end = 9;
	gognm->col_end = 4;
}

static void
go_gnm_component_class_init (GOComponentClass *klass)
{
	GObjectClass *obj_klass = (GObjectClass *) klass;
	obj_klass->finalize = go_gnm_component_finalize;

	gognm_parent_klass = (GObjectClass*) g_type_class_peek_parent (klass);

	klass->get_data = go_gnm_component_get_data;
	klass->set_data = go_gnm_component_set_data;
	klass->render = go_gnm_component_render;
	klass->edit = go_gnm_component_edit;
}

GSF_DYNAMIC_CLASS (GOGnmComponent, go_gnm_component,
	go_gnm_component_class_init, go_gnm_component_init,
	GO_TYPE_COMPONENT)

/*************************************************************************************/

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, G_GNUC_UNUSED GOCmdContext *cc)
{
	GTypeModule *module;
	char const *env_var;
	GSList *dir_list;
	const char *usr_dir = gnm_usr_dir (TRUE);

	bindtextdomain (GETTEXT_PACKAGE, gnm_locale_dir ());
	bindtextdomain (GETTEXT_PACKAGE "-functions", gnm_locale_dir ());
#ifdef ENABLE_NLS
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif
	module = go_plugin_get_type_module (plugin);
	go_gnm_component_register_type (module);
	gnm_init ();
	if (!gnm_sys_data_dir ())
		gutils_init ();
	dir_list = go_slist_create (
		g_build_filename (gnm_sys_lib_dir (), PLUGIN_SUBDIR, NULL),
		(usr_dir == NULL ? NULL :
			g_build_filename (usr_dir, PLUGIN_SUBDIR, NULL)),
		NULL);
	dir_list = g_slist_concat
		(dir_list,
		 go_string_slist_copy (gnm_conf_get_autoformat_extra_dirs ()));

	env_var = g_getenv ("GNUMERIC_PLUGIN_PATH");
	if (env_var != NULL)
		GO_SLIST_CONCAT (dir_list, go_strsplit_to_slist (env_var, G_SEARCHPATH_SEPARATOR));

	go_components_set_mime_suffix ("application/x-gnumeric", "*.gnumeric");

	go_plugins_init (go_component_get_command_context (NULL),
			gnm_conf_get_plugins_file_states (),
			gnm_conf_get_plugins_active (),
			dir_list,
			gnm_conf_get_plugins_activate_newplugins (),
			gnm_plugin_loader_module_get_type ());
}

G_MODULE_EXPORT void
go_plugin_shutdown (G_GNUC_UNUSED GOPlugin *plugin,
		    G_GNUC_UNUSED GOCmdContext *cc)
{
}
