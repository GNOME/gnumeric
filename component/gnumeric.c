/* 
 * Gnumeric GOffice component
 * gnumeric.c
 *
 * Copyright (C) 2006
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
 
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <libgnumeric.h>
#include <gnumeric-gconf.h>
#include <cairo/cairo.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtkactiongroup.h>
#include <gtk/gtkstock.h>
#include <goffice/app/io-context.h>
#include <goffice/component/goffice-component.h>
#include <goffice/component/go-component.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-color.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-input-memory.h>
#include <gsf/gsf-output-memory.h>
#include <goffice/app/go-cmd-context.h>
#include <goffice/app/module-plugin-defs.h>
#include <application.h>
#include <cell.h>
#include <cell-draw.h>
#include <colrow.h>
#include <rendered-value.h>
#include <workbook-view.h>
#include <workbook-control-gui-priv.h>
#include <workbook.h>
#include <sheet.h>
#include <print-cell.h>
#include <sheet-object.h>
#include <command-context.h>
#include <command-context-stderr.h>
#include <cairo.h>
#include <pango/pangocairo.h>

GOPluginModuleDepend const go_plugin_depends [] = {
    { "goffice", GOFFICE_API_VERSION }
};
GOPluginModuleHeader const go_plugin_header =
	{ GOFFICE_MODULE_PLUGIN_MAGIC_NUMBER, G_N_ELEMENTS (go_plugin_depends) };

G_MODULE_EXPORT void go_plugin_init (GOPlugin *plugin, GOCmdContext *cc);
G_MODULE_EXPORT void go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc);

enum {
	COMPONENT_PROP_0,
	COMPONENT_PROP_START_ROW,
	COMPONENT_PROP_END_ROW,
	COMPONENT_PROP_START_COL,
	COMPONENT_PROP_END_COL,
	COMPONENT_PROP_SHEET,
};

typedef struct
{
	GOComponent parent;

	WorkbookView *wv;
	WorkbookControlGUI *edited;
	int sheet, col_start, col_end, row_start, row_end;
	int width, height;
} GOGnmComponent;

/* Red and blue are inverted in a pixbuf compared to cairo */
static void
pixbuf_to_cairo (unsigned char *p, int width, int height, int rowstride)
{
	int i,j;
	unsigned char a;
	guint t;
	
#define MULT(d,c,a,t) G_STMT_START { t = c * a + 0x7f; d = ((t >> 8) + t) >> 8; } G_STMT_END
	
	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
			MULT(a,    p[2], p[3], t);
			MULT(p[1], p[1], p[3], t);
			MULT(p[2], p[0], p[3], t);
			p[0] = a;
#else	  
			a = p[3];
			MULT(p[3], p[2], a, t);
			MULT(p[2], p[1], a, t);
			MULT(p[1], p[0], a, t);
			p[0] = a;
#endif
			p += 4;
		}
		p += rowstride - width * 4;
	}
#undef MULT
}

typedef GOComponentClass GOGnmComponentClass;

#define GO_GNM_COMPONENT_TYPE	(go_gnm_component_get_type ())
#define GO_GNM_COMPONENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GO_GNM_COMPONENT_TYPE, GOGnmComponent))
#define GO_IS_GNM_COMPONENT(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GO_GNM_COMPONENT_TYPE))

GType go_gnm_component_get_type (void);

static GObjectClass *gognm_parent_klass;

static gboolean
go_gnm_component_get_data (GOComponent *component, gpointer *data, int *length,
									void (**clearfunc) (gpointer), gpointer *user_data)
{
	GOGnmComponent *gognm = GO_GNM_COMPONENT (component);
	if (gognm->edited) {
		GOCmdContext *cc = go_component_get_command_context ();
		IOContext *io_context = gnumeric_io_context_new (cc);
		GsfOutput *output = gsf_output_memory_new ();
		WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (gognm->edited));
		Workbook *wb = wb_control_get_workbook (WORKBOOK_CONTROL (gognm->edited));
		GOFileSaver *gfs = workbook_get_file_saver (wb);
		if (gfs == NULL)
			gfs = go_file_saver_get_default ();
		wbv_save_to_output (wbv, gfs, output, io_context);
		*data = (gpointer) gsf_output_memory_get_bytes (GSF_OUTPUT_MEMORY (output));
		*length = gsf_output_size (output);
		*clearfunc = g_object_unref;
		*user_data = output;
		return TRUE;
	}
	return FALSE;
}

static void
go_gnm_component_set_data (GOComponent *component)
{
	GOGnmComponent *gognm = GO_GNM_COMPONENT (component);
	Sheet *sheet;
	GOCmdContext *cc = go_component_get_command_context ();
	IOContext *io_context = gnumeric_io_context_new (cc);
	GsfInput *input = gsf_input_memory_new (component->data, component->length, FALSE);

	g_object_set (G_OBJECT (io_context), "exec-main-loop", FALSE, NULL);
 	gognm->wv = wb_view_new_from_input (input, NULL, io_context, NULL);
	g_object_unref (io_context);
	sheet = workbook_sheet_by_index (wb_view_get_workbook (gognm->wv), gognm->sheet);
	gognm->width = sheet_col_get_distance_pts (
		sheet, gognm->col_start, gognm->col_end + 1);
	component->width = gognm->width / 72.;
	component->descent = 0.;
	gognm->height = sheet_row_get_distance_pts (
		sheet, gognm->row_start, gognm->row_end + 1);
	component->ascent = gognm->height  / 72.;
}

static void
cell_render (cairo_t *cairo, GnmCell *cell)
{
	GOColor fore_color;
	int x, y;
	RenderedValue *rv = cell->rendered_value;
	ColRowInfo const * const ci = cell->col_info;
	ColRowInfo const * const ri = cell->row_info;

	int width  = ci->size_pixels - (ci->margin_b + ci->margin_a + 1);
	int height = ri->size_pixels - (ri->margin_b + ri->margin_a + 1);
	int h_center = -1;
	int x1 = 1 + ci->margin_a;
	int y1 = 1 + ri->margin_a;

	if (!rv) {
		cell_render_value (cell, FALSE);
		rv = cell->rendered_value;
	}

	if (cell_calc_layout (cell, rv, +1,
			width * PANGO_SCALE,
			height * PANGO_SCALE,
			h_center == -1 ? -1 : (h_center * PANGO_SCALE),
			&fore_color, &x, &y)) {
		cairo_set_source_rgb (cairo, UINT_RGBA_R(fore_color), UINT_RGBA_G(fore_color),  UINT_RGBA_B(fore_color));
		cairo_new_path (cairo);
		cairo_move_to (cairo, x1, y1);
		cairo_line_to (cairo, x1 + width, y1);
		cairo_line_to (cairo, x1 + width, y1 + height);
		cairo_line_to (cairo, x1, y1 + height);
		cairo_close_path (cairo);
		cairo_clip (cairo);

		cairo_set_source_rgb (cairo, UINT_RGBA_B(fore_color), UINT_RGBA_G(fore_color),  UINT_RGBA_R(fore_color));
		if (rv->rotation) {
			RenderedRotatedValue *rrv = (RenderedRotatedValue *)rv;
			struct RenderedRotatedValueInfo const *li = rrv->lines;
			GSList *lines;
			cairo_matrix_t m;
			m.xx = rrv->rotmat.xx;
			m.xy = rrv->rotmat.xy;
			m.yx = rrv->rotmat.yx;
			m.yy = rrv->rotmat.yy;
			m.x0 = rrv->rotmat.x0;
			m.y0 = rrv->rotmat.y0;

			for (lines = pango_layout_get_lines (rv->layout);
					lines;
					lines = lines->next, li++) {
				cairo_save (cairo);
				cairo_move_to (cairo, PANGO_PIXELS (x + li->dx), PANGO_PIXELS (y + li->dy));
				cairo_transform (cairo, &m);
				pango_cairo_show_layout_line (cairo, lines->data);
				cairo_restore (cairo);
			}
		} else {
			cairo_move_to (cairo, x / PANGO_SCALE, y / PANGO_SCALE);
			pango_cairo_show_layout (cairo, rv->layout);
		}
	}
}

static void
go_gnm_component_draw (GOComponent *component, int width_pixels, int height_pixels)
{
	GOGnmComponent *gognm = GO_GNM_COMPONENT (component);
	int col, row;
	cairo_surface_t* surface ;
	cairo_t *cairo;
	GnmCell *cell;
	Sheet *sheet;
	double xoffset = 0., yoffset;
	GSList *l;
	SheetObject *so;
	SheetObjectAnchor const *anchor;

	gdk_pixbuf_fill (component->pixbuf, 0);
	if (gognm->wv == NULL)
		return;
	surface = cairo_image_surface_create_for_data (
              				gdk_pixbuf_get_pixels (component->pixbuf),
							CAIRO_FORMAT_ARGB32,
							width_pixels, height_pixels, 
               				gdk_pixbuf_get_rowstride (component->pixbuf));
	cairo = cairo_create (surface);
	cairo_scale (cairo, ((double) width_pixels) / gognm->width, ((double) height_pixels) / gognm->height);
	sheet = workbook_sheet_by_index (wb_view_get_workbook (gognm->wv), gognm->sheet);
	for (col = gognm->col_start; col <= gognm->col_end; col++) {
		yoffset = 0.;
		for (row = gognm->row_start; row <= gognm->row_end; row++) {
			cell = sheet_cell_get (sheet, col, row);
			if (cell) {
				cairo_save (cairo);
				cairo_translate (cairo, xoffset, yoffset);
				cairo_scale (cairo,
					72. / gnm_app_display_dpi_get (TRUE),
					72. / gnm_app_display_dpi_get (FALSE));
				cell_render (cairo, cell);
				cairo_restore (cairo);
			}
			yoffset += sheet_row_get_distance_pts (sheet, row, row + 1);
		}
		xoffset += sheet_col_get_distance_pts (sheet, col, col + 1);
	}
	/* Now render objects */
	l = sheet->sheet_objects;
	while (l) {
		so = SHEET_OBJECT (l->data);
		anchor = sheet_object_get_anchor (so);
		/* test if the object overlaps the exposed range */
		if ((anchor->cell_bound.start.col <= gognm->col_end) &&
			(anchor->cell_bound.end.col >= gognm->col_start) &&
			(anchor->cell_bound.start.row <= gognm->row_end) &&
			(anchor->cell_bound.end.row >= gognm->row_start)) {
			/* translate the origin to start cell of object */
			xoffset = sheet_col_get_distance_pts (sheet, gognm->col_start,
				anchor->cell_bound.start.col);
			yoffset = sheet_row_get_distance_pts (sheet, gognm->row_start,
				anchor->cell_bound.start.row);
			cairo_save (cairo);
			cairo_translate (cairo, xoffset, yoffset);
			sheet_object_draw_cairo (so, (gpointer) cairo);
			cairo_restore (cairo);
		}
		l = l->next;
	}
	cairo_destroy (cairo);
	cairo_surface_destroy (surface);
	pixbuf_to_cairo (gdk_pixbuf_get_pixels (component->pixbuf), 
					width_pixels, height_pixels, 
					gdk_pixbuf_get_rowstride (component->pixbuf));
}

static void
go_gnm_component_print (GOComponent *component, GnomePrintContext *gpc,
												double width, double height)
{
	GOGnmComponent *gognm = GO_GNM_COMPONENT (component);
	GnmRange range;
	Sheet *sheet = workbook_sheet_by_index (wb_view_get_workbook (gognm->wv), gognm->sheet);
	if (sheet == NULL)
		return;
	range.start.row = gognm->row_start;
	range.start.col = gognm->col_start;
	range.end.row = gognm->row_end;
	range.end.col = gognm->col_end;
	print_cell_range (gpc, sheet, &range, 0., height, TRUE);
}

static void
cb_gognm_save (GtkAction *a, WorkbookControlGUI *wbcg)
{
	GOComponent *component = GO_COMPONENT (g_object_get_data (G_OBJECT (wbcg), "component"));
	go_component_emit_changed (component);
}

extern char const *uifilename;
extern GtkActionEntry const *extra_actions;
extern int nb_extra_actions;
static GtkActionEntry const actions[] = {
/* File */
	{ "FileSaveEmbed", GTK_STOCK_SAVE, NULL,
		NULL, N_("Save the embedded workbook"),
		G_CALLBACK (cb_gognm_save) }
};

static void
editor_destroyed_cb (GOGnmComponent *gognm)
{
	gognm->edited = NULL;
}

static gboolean
go_gnm_component_edit (GOComponent *component)
{
	GOGnmComponent *gognm = GO_GNM_COMPONENT (component);
	WorkbookView *wv;
	if (gognm->edited) {
		gdk_window_raise (gognm->edited->toplevel->window);
		return TRUE;
	}
	if (!gognm->wv) {
		component->ascent = 0.;
		component->descent = 0.;
		component->width = 0.;
		wv = workbook_view_new (workbook_new_with_sheets (1));
	} else {
		GOCmdContext *cc = go_component_get_command_context ();
		IOContext *io_context = gnumeric_io_context_new (cc);
		GsfInput *input = gsf_input_memory_new (component->data, component->length, FALSE);
	
		g_object_set (G_OBJECT (io_context), "exec-main-loop", FALSE, NULL);
		wv = wb_view_new_from_input (input, NULL, io_context, NULL);
		g_object_unref (io_context);
	}
	uifilename =  "Gnumeric-embed.xml";
	extra_actions = actions;
	nb_extra_actions = G_N_ELEMENTS (actions);
	gognm->edited = WORKBOOK_CONTROL_GUI (workbook_control_gui_new (wv, NULL, NULL));
	g_object_set_data (G_OBJECT (gognm->edited), "component", gognm);
	g_signal_connect_swapped (gognm->edited->toplevel, "destroy",
					G_CALLBACK (editor_destroyed_cb), gognm);
	return TRUE;
}

static void
go_gnm_component_set_property (GObject *obj, guint param_id,
		       GValue const *value, GParamSpec *pspec)
{
	/* GOGnmComponent *gognm = GO_GNM_COMPONENT (obj);*/

	switch (param_id) {
	case COMPONENT_PROP_START_ROW:
		break;
	case COMPONENT_PROP_END_ROW:
		break;
	case COMPONENT_PROP_START_COL:
		break;
	case COMPONENT_PROP_END_COL:
		break;
	case COMPONENT_PROP_SHEET:
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 return; /* NOTE : RETURN */
	}
}

static void
go_gnm_component_get_property (GObject *obj, guint param_id,
		       GValue *value, GParamSpec *pspec)
{
	/* GOGnmComponent *gognm = GO_GNM_COMPONENT (obj);*/

	switch (param_id) {
	case COMPONENT_PROP_START_ROW:
		break;
	case COMPONENT_PROP_END_ROW:
		break;
	case COMPONENT_PROP_START_COL:
		break;
	case COMPONENT_PROP_END_COL:
		break;
	case COMPONENT_PROP_SHEET:
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
go_gnm_component_finalize (GObject *obj)
{
	GOGnmComponent *gognm = GO_GNM_COMPONENT (obj);
	if (gognm->wv != NULL) {
		g_object_unref (gognm->wv);
		gognm->wv = NULL;
	}
	if (gognm->edited != NULL) {
		g_object_unref (wb_control_view (WORKBOOK_CONTROL (gognm->edited)));
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
	component->needs_window = FALSE;
	gognm->row_start = gognm->col_start = gognm->sheet = 0;
	gognm->row_end = 9;
	gognm->col_end = 4;
}

static void
go_gnm_component_class_init (GOComponentClass *klass)
{
	GObjectClass *obj_klass = (GObjectClass *) klass;
	obj_klass->finalize = go_gnm_component_finalize;
	obj_klass->set_property = go_gnm_component_set_property;
	obj_klass->get_property = go_gnm_component_get_property;

	gognm_parent_klass = (GObjectClass*) g_type_class_peek_parent (klass);

	klass->get_data = go_gnm_component_get_data;
	klass->set_data = go_gnm_component_set_data;
	klass->draw = go_gnm_component_draw;
	klass->print = go_gnm_component_print;
	klass->edit = go_gnm_component_edit;

/* Better to do this as a GnmRange */
	g_object_class_install_property (obj_klass, COMPONENT_PROP_START_ROW,
		g_param_spec_int ("gnm-start-row", _("Start row"),
			_("First displayed row"),
			0, SHEET_MAX_ROWS - 1, 0,
			G_PARAM_READWRITE | GOC_PARAM_PERSISTENT));
	g_object_class_install_property (obj_klass, COMPONENT_PROP_END_ROW,
		g_param_spec_int ("gnm-end-row", _("End row"),
			_("Last displayed row"),
			0, SHEET_MAX_ROWS - 1, 9,
			G_PARAM_READWRITE | GOC_PARAM_PERSISTENT));
	g_object_class_install_property (obj_klass, COMPONENT_PROP_START_COL,
		g_param_spec_int ("gnm-start-col", _("Start column"),
			_("First displayed column"),
			0, SHEET_MAX_COLS - 1, 0,
			G_PARAM_READWRITE | GOC_PARAM_PERSISTENT));
	g_object_class_install_property (obj_klass, COMPONENT_PROP_END_COL,
		g_param_spec_int ("gnm-end-col", _("End column"),
			_("Last displayed column"),
			0, SHEET_MAX_COLS - 1, 4,
			G_PARAM_READWRITE | GOC_PARAM_PERSISTENT));

/* or even a GnmSheetRange */
	g_object_class_install_property (obj_klass, COMPONENT_PROP_END_COL,
		g_param_spec_int ("gnm-sheet", _("Sheet"),
			_("Index of dislpayed sheet"),
			0,  G_MAXINT32, 0,
			G_PARAM_READWRITE | GOC_PARAM_PERSISTENT));
}

GSF_DYNAMIC_CLASS (GOGnmComponent, go_gnm_component,
	go_gnm_component_class_init, go_gnm_component_init,
	GO_COMPONENT_TYPE)

/*************************************************************************************/
extern GType gnm_plugin_loader_module_get_type (void);

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
	GTypeModule *module;
	char const *env_var;
	GSList *dir_list;
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
#ifdef ENABLE_NLS
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif
	module = go_plugin_get_type_module (plugin);
	go_gnm_component_register_type (module);
	gnm_common_init (FALSE);
	dir_list = go_slist_create (
		g_build_filename (gnm_sys_lib_dir (), PLUGIN_SUBDIR, NULL),
		(gnm_usr_dir () == NULL ? NULL :
			g_build_filename (gnm_usr_dir (), PLUGIN_SUBDIR, NULL)),
		NULL);
	dir_list = g_slist_concat (dir_list,
		go_string_slist_copy (gnm_app_prefs->plugin_extra_dirs));

	env_var = g_getenv ("GNUMERIC_PLUGIN_PATH");
	if (env_var != NULL)
		GO_SLIST_CONCAT (dir_list, go_strsplit_to_slist (env_var, G_SEARCHPATH_SEPARATOR));

/* WHERE IS THIS DEFINED */
	go_plugins_add (go_component_get_command_context (),
		gnm_app_prefs->plugin_file_states,
		gnm_app_prefs->active_plugins,
		dir_list,
		gnm_plugin_loader_module_get_type ());
}

G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
}
