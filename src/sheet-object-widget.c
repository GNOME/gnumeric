/*
 * sheet-object-widget.c: SheetObject wrappers for simple gtk widgets.
 *
 * Copyright (C) 2000-2006 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <application.h>
#include <sheet-object-widget-impl.h>
#include <widgets/gnm-radiobutton.h>
#include <gnm-pane.h>
#include <gnumeric-simple-canvas.h>
#include <gui-util.h>
#include <gutils.h>
#include <dependent.h>
#include <sheet-control-gui.h>
#include <sheet-object-impl.h>
#include <expr.h>
#include <parse-util.h>
#include <value.h>
#include <ranges.h>
#include <selection.h>
#include <wbc-gtk.h>
#include <workbook.h>
#include <sheet.h>
#include <cell.h>
#include <mathfunc.h>
#include <widgets/gnm-expr-entry.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>
#include <xml-sax.h>
#include <commands.h>
#include <gnm-format.h>
#include <number-match.h>

#include <goffice/goffice.h>

#include <gsf/gsf-impl-utils.h>
#include <libxml/globals.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <string.h>

#define CXML2C(s) ((char const *)(s))
#define CC2XML(s) ((xmlChar const *)(s))

static inline gboolean
attr_eq (const xmlChar *a, const char *s)
{
	return !strcmp (CXML2C (a), s);
}

/****************************************************************************/

static void
cb_so_get_ref (GnmDependent *dep, G_GNUC_UNUSED SheetObject *so, gpointer user)
{
	GnmDependent **pdep = user;
	*pdep = dep;
}

static GnmCellRef *
so_get_ref (SheetObject const *so, GnmCellRef *res, gboolean force_sheet)
{
	GnmValue *target;
	GnmDependent *dep = NULL;

	g_return_val_if_fail (so != NULL, NULL);

	/* Let's hope there's just one.  */
	sheet_object_foreach_dep ((SheetObject*)so, cb_so_get_ref, &dep);
	g_return_val_if_fail (dep, NULL);

	if (dep->texpr == NULL)
		return NULL;

	target = gnm_expr_top_get_range (dep->texpr);
	if (target == NULL)
		return NULL;

	*res = target->v_range.cell.a;
	value_release (target);

	if (force_sheet && res->sheet == NULL)
		res->sheet = sheet_object_get_sheet (so);
	return res;
}

static void
cb_so_clear_sheet (GnmDependent *dep, G_GNUC_UNUSED SheetObject *so, G_GNUC_UNUSED gpointer user)
{
	if (dependent_is_linked (dep))
		dependent_unlink (dep);
	dep->sheet = NULL;
}

static gboolean
so_clear_sheet (SheetObject *so)
{
	/* Note: This implements sheet_object_clear_sheet.  */
	sheet_object_foreach_dep (so, cb_so_clear_sheet, NULL);
	return FALSE;
}

static GocWidget *
get_goc_widget (SheetObjectView *view)
{
	GocItem *item = sheet_object_view_get_item (view);
	return item ? GOC_WIDGET (item) : NULL;
}

static void
so_widget_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	GocItem *view = GOC_ITEM (sov);
	double scale = goc_canvas_get_pixels_per_unit (view->canvas);
	double left = MIN (coords [0], coords [2]) / scale;
	double top = MIN (coords [1], coords [3]) / scale;
	double width = (fabs (coords [2] - coords [0]) + 1.) / scale;
	double height = (fabs (coords [3] - coords [1]) + 1.) / scale;

	/* We only need the next check for frames, but it doesn't hurt otherwise. */
	if (width < 8.)
		width = 8.;

	if (visible) {
		/* NOTE : far point is EXCLUDED so we add 1 */
		goc_widget_set_bounds (get_goc_widget (sov),
				       left, top, width, height);
		goc_item_show (view);
	} else
		goc_item_hide (view);
}

static GdkWindow *
so_widget_view_get_window (GocItem *item)
{
	GocItem *item0 = sheet_object_view_get_item (GNM_SO_VIEW (item));
	return goc_item_get_window (item0);
}

static void
so_widget_view_class_init (SheetObjectViewClass *sov_klass)
{
	GocItemClass *item_klass = (GocItemClass *) sov_klass;
	sov_klass->set_bounds	= so_widget_view_set_bounds;
	item_klass->get_window	= so_widget_view_get_window;
}

static GSF_CLASS (SOWidgetView, so_widget_view,
	so_widget_view_class_init, NULL,
	GNM_SO_VIEW_TYPE)

/****************************************************************************/

#define SHEET_OBJECT_CONFIG_KEY "sheet-object-config-dialog"

#define GNM_SOW_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), GNM_SOW_TYPE, SheetObjectWidgetClass))
#define SOW_CLASS(so) (GNM_SOW_CLASS (G_OBJECT_GET_CLASS(so)))

#define SOW_MAKE_TYPE(n1, n2, fn_config, fn_set_sheet, fn_clear_sheet, fn_foreach_dep, \
		      fn_copy, fn_write_sax, fn_prep_sax_parser,	\
		      fn_get_property, fn_set_property,                 \
		      fn_draw_cairo, class_init_code)					\
									\
static void								\
sheet_widget_ ## n1 ## _class_init (GObjectClass *object_class)		\
{									\
	SheetObjectWidgetClass *sow_class = GNM_SOW_CLASS (object_class); \
	SheetObjectClass *so_class = GNM_SO_CLASS (object_class);	\
	object_class->finalize		= &sheet_widget_ ## n1 ## _finalize; \
	object_class->set_property	= fn_set_property;		\
	object_class->get_property	= fn_get_property;		\
	so_class->user_config		= fn_config;			\
        so_class->interactive           = TRUE;				\
	so_class->assign_to_sheet	= fn_set_sheet;			\
	so_class->remove_from_sheet	= fn_clear_sheet;		\
	so_class->foreach_dep		= fn_foreach_dep;		\
	so_class->copy			= fn_copy;			\
	so_class->write_xml_sax		= fn_write_sax;			\
	so_class->prep_sax_parser	= fn_prep_sax_parser;		\
	so_class->draw_cairo            = fn_draw_cairo;                            \
	sow_class->create_widget	= &sheet_widget_ ## n1 ## _create_widget; \
        { class_init_code; }						\
}									\
									\
GSF_CLASS (SheetWidget ## n2, sheet_widget_ ## n1,			\
	   &sheet_widget_ ## n1 ## _class_init,				\
	   &sheet_widget_ ## n1 ## _init,				\
	   GNM_SOW_TYPE)

typedef struct {
	SheetObject so;
} SheetObjectWidget;

typedef struct {
	SheetObjectClass parent_class;
	GtkWidget *(*create_widget)(SheetObjectWidget *);
} SheetObjectWidgetClass;

static GObjectClass *sheet_object_widget_class = NULL;

static GtkWidget *
sow_create_widget (SheetObjectWidget *sow)
{
	GtkWidget *w = SOW_CLASS(sow)->create_widget (sow);
	GtkStyleContext *context = gtk_widget_get_style_context (w);
	gtk_style_context_add_class (context, "sheet-object");
	return w;
}

static void
sheet_widget_draw_cairo (SheetObject const *so, cairo_t *cr,
			 double width, double height)
{
	/* This is the default for so widgets without their own method */
	/* See bugs #705638 and #705640 */
	if (NULL != gdk_screen_get_default ()) {
		GtkWidget *win = gtk_offscreen_window_new ();
		GtkWidget *w = sow_create_widget (GNM_SOW (so));

		gtk_container_add (GTK_CONTAINER (win), w);
		gtk_widget_set_size_request (w, width, height);
		gtk_widget_show_all (win);
		gtk_container_propagate_draw (GTK_CONTAINER (win), w, cr);
		gtk_widget_destroy (win);
	} else
		g_warning (_("Because of GTK bug #705640, a sheet object widget is not being printed."));
}

static void
sax_write_dep (GsfXMLOut *output, GnmDependent const *dep, char const *id,
	       GnmConventions const *convs)
{
	if (dep->texpr != NULL) {
		GnmParsePos pos;
		char *val;

		parse_pos_init_dep (&pos, dep);
		val = gnm_expr_top_as_string (dep->texpr, &pos, convs);
		gsf_xml_out_add_cstr (output, id, val);
		g_free (val);
	}
}

static gboolean
sax_read_dep (xmlChar const * const *attrs, char const *name,
	      GnmDependent *dep, GsfXMLIn *xin, GnmConventions const *convs)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!attr_eq (attrs[0], name))
		return FALSE;

	dep->sheet = NULL;
	if (attrs[1] != NULL && *attrs[1] != '\0') {
		GnmParsePos pp;

		parse_pos_init_sheet (&pp, gnm_xml_in_cur_sheet (xin));
		dep->texpr = gnm_expr_parse_str (CXML2C (attrs[1]), &pp,
						 GNM_EXPR_PARSE_DEFAULT,
						 convs, NULL);
	} else
		dep->texpr = NULL;

	return TRUE;
}

static SheetObjectView *
sheet_object_widget_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GtkWidget *view_widget = sow_create_widget (GNM_SOW (so));
	GocItem *view_item = goc_item_new (
		gnm_pane_object_group (GNM_PANE (container)),
		so_widget_view_get_type (),
		NULL);
	goc_item_new (GOC_GROUP (view_item),
		      GOC_TYPE_WIDGET,
		      "widget", view_widget,
		      NULL);
	/* g_warning ("%p is widget for so %p", (void *)view_widget, (void *)so);*/
	gtk_widget_show_all (view_widget);
	goc_item_hide (view_item);
	gnm_pane_widget_register (so, view_widget, view_item);
	return gnm_pane_object_register (so, view_item, TRUE);
}

static void
sheet_object_widget_class_init (GObjectClass *object_class)
{
	SheetObjectClass *so_class = GNM_SO_CLASS (object_class);
	SheetObjectWidgetClass *sow_class = GNM_SOW_CLASS (object_class);

	sheet_object_widget_class = G_OBJECT_CLASS (object_class);

	/* SheetObject class method overrides */
	so_class->new_view		= sheet_object_widget_new_view;
	so_class->rubber_band_directly	= TRUE;
	so_class->draw_cairo		= sheet_widget_draw_cairo;

	sow_class->create_widget = NULL;
}

static void
sheet_object_widget_init (SheetObjectWidget *sow)
{
	SheetObject *so = GNM_SO (sow);
	so->flags |= SHEET_OBJECT_CAN_PRESS;
}

GSF_CLASS (SheetObjectWidget, sheet_object_widget,
	   sheet_object_widget_class_init,
	   sheet_object_widget_init,
	   GNM_SO_TYPE)

static WorkbookControl *
widget_wbc (GtkWidget *widget)
{
	return scg_wbc (GNM_SIMPLE_CANVAS (gtk_widget_get_ancestor (widget, GNM_SIMPLE_CANVAS_TYPE))->scg);
}


/****************************************************************************/
#define GNM_SOW_FRAME(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_SOW_FRAME_TYPE, SheetWidgetFrame))
typedef struct {
	SheetObjectWidget	sow;
	char *label;
} SheetWidgetFrame;
typedef SheetObjectWidgetClass SheetWidgetFrameClass;

enum {
	SOF_PROP_0 = 0,
	SOF_PROP_TEXT
};

static void
sheet_widget_frame_get_property (GObject *obj, guint param_id,
				  GValue *value, GParamSpec *pspec)
{
	SheetWidgetFrame *swf = GNM_SOW_FRAME (obj);

	switch (param_id) {
	case SOF_PROP_TEXT:
		g_value_set_string (value, swf->label);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
sheet_widget_frame_set_property (GObject *obj, guint param_id,
				 GValue const *value, GParamSpec *pspec)
{
	SheetWidgetFrame *swf = GNM_SOW_FRAME (obj);

	switch (param_id) {
	case SOF_PROP_TEXT:
		sheet_widget_frame_set_label (GNM_SO (swf),
					       g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return;
	}
}


static void
sheet_widget_frame_init_full (SheetWidgetFrame *swf, char const *text)
{
	swf->label = g_strdup (text);
}

static void
sheet_widget_frame_init (SheetWidgetFrame *swf)
{
	sheet_widget_frame_init_full (swf, _("Frame"));
}

static void
sheet_widget_frame_finalize (GObject *obj)
{
	SheetWidgetFrame *swf = GNM_SOW_FRAME (obj);

	g_free (swf->label);
	swf->label = NULL;

	sheet_object_widget_class->finalize (obj);
}

static GtkWidget *
sheet_widget_frame_create_widget (SheetObjectWidget *sow)
{
	GtkWidget *widget = gtk_event_box_new (),
		  *frame = gtk_frame_new (GNM_SOW_FRAME (sow)->label);
	gtk_container_add (GTK_CONTAINER (widget), frame);
	gtk_event_box_set_visible_window (GTK_EVENT_BOX (widget), FALSE);
	return widget;
}

static void
sheet_widget_frame_copy (SheetObject *dst, SheetObject const *src)
{
	sheet_widget_frame_init_full (GNM_SOW_FRAME (dst),
		GNM_SOW_FRAME (src)->label);
}

static void
sheet_widget_frame_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
				  G_GNUC_UNUSED GnmConventions const *convs)
{
	SheetWidgetFrame const *swf = GNM_SOW_FRAME (so);
	gsf_xml_out_add_cstr (output, "Label", swf->label);
}

static void
sheet_widget_frame_prep_sax_parser (SheetObject *so, G_GNUC_UNUSED GsfXMLIn *xin,
				    xmlChar const **attrs,
				    G_GNUC_UNUSED GnmConventions const *convs)
{
	SheetWidgetFrame *swf = GNM_SOW_FRAME (so);
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_eq (attrs[0], "Label")) {
			g_free (swf->label);
			swf->label = g_strdup (CXML2C (attrs[1]));
		}
}

typedef struct {
	GtkWidget          *dialog;
	GtkWidget          *label;

	char               *old_label;
	GtkWidget          *old_focus;

	WBCGtk *wbcg;
	SheetWidgetFrame   *swf;
	Sheet		   *sheet;
} FrameConfigState;

static void
cb_frame_config_destroy (FrameConfigState *state)
{
	g_return_if_fail (state != NULL);

	g_free (state->old_label);
	state->old_label = NULL;
	state->dialog = NULL;
	g_free (state);
}

static void
cb_frame_config_ok_clicked (G_GNUC_UNUSED GtkWidget *button, FrameConfigState *state)
{
	gchar const *text = gtk_entry_get_text(GTK_ENTRY(state->label));

	cmd_so_set_frame_label (GNM_WBC (state->wbcg),
				GNM_SO (state->swf),
				g_strdup (state->old_label), g_strdup (text));
	gtk_widget_destroy (state->dialog);
}

void
sheet_widget_frame_set_label (SheetObject *so, char const* str)
{
	SheetWidgetFrame *swf = GNM_SOW_FRAME (so);
	GList *ptr;

	str = str ? str : "";

	if (go_str_compare (str, swf->label) == 0)
		return;

	g_free (swf->label);
	swf->label = g_strdup (str);

	for (ptr = swf->sow.so.realized_list; ptr != NULL; ptr = ptr->next) {
		SheetObjectView *view = ptr->data;
		GocWidget *item = get_goc_widget (view);
		GList *children = gtk_container_get_children (GTK_CONTAINER (item->widget));
		gtk_frame_set_label (GTK_FRAME (children->data), str);
		g_list_free (children);
	}
}

static void
cb_frame_config_cancel_clicked (G_GNUC_UNUSED GtkWidget *button, FrameConfigState *state)
{
	sheet_widget_frame_set_label (GNM_SO (state->swf), state->old_label);

	gtk_widget_destroy (state->dialog);
}

static void
cb_frame_label_changed (GtkWidget *entry, FrameConfigState *state)
{
	gchar const *text;

	text = gtk_entry_get_text(GTK_ENTRY(entry));
	sheet_widget_frame_set_label (GNM_SO (state->swf), text);
}

static void
sheet_widget_frame_user_config (SheetObject *so, SheetControl *sc)
{
	SheetWidgetFrame *swf = GNM_SOW_FRAME (so);
	WBCGtk   *wbcg = scg_wbcg (GNM_SCG (sc));
	FrameConfigState *state;
	GtkBuilder *gui;

	g_return_if_fail (swf != NULL);

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
		return;

	gui = gnm_gtk_builder_load ("res:ui/so-frame.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (!gui)
		return;
	state = g_new (FrameConfigState, 1);
	state->swf = swf;
	state->wbcg = wbcg;
	state->sheet = sc_sheet	(sc);
	state->old_focus = NULL;
	state->old_label = g_strdup(swf->label);
	state->dialog = go_gtk_builder_get_widget (gui, "so_frame");

	state->label = go_gtk_builder_get_widget (gui, "entry");
	gtk_entry_set_text (GTK_ENTRY(state->label), swf->label);
	gtk_editable_select_region (GTK_EDITABLE(state->label), 0, -1);
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->label));

	g_signal_connect (G_OBJECT(state->label),
			  "changed",
			  G_CALLBACK (cb_frame_label_changed), state);
	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui,
							  "ok_button")),
			  "clicked",
			  G_CALLBACK (cb_frame_config_ok_clicked), state);
	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui,
							  "cancel_button")),
			  "clicked",
			  G_CALLBACK (cb_frame_config_cancel_clicked), state);

	gnm_init_help_button (
		go_gtk_builder_get_widget (gui, "help_button"),
		GNUMERIC_HELP_LINK_SO_FRAME);


	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SHEET_OBJECT_CONFIG_KEY);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_frame_config_destroy);
	g_object_unref (gui);

	gtk_widget_show (state->dialog);
}

static PangoFontDescription *
get_font (void)
{
	// Note: Under gnumeric, we get a proper font using GtkStyleContext.
	// Under ssconvert, we try GSettings.
	// The 'sans 10' is just insurance

	PangoFontDescription *desc;
	PangoFontMask mask;
	int size = 0;

	if (gdk_screen_get_default ()) {
		// Without a default screen, the following will crash
		// with newer gtk+.
		GtkStyleContext *style = gtk_style_context_new ();
		GtkWidgetPath *path = gtk_widget_path_new ();

		gtk_style_context_set_path (style, path);
		gtk_widget_path_unref (path);

		gtk_style_context_get (style, GTK_STATE_FLAG_NORMAL,
				       GTK_STYLE_PROPERTY_FONT, &desc, NULL);
		g_object_unref (style);
	} else
		desc = pango_font_description_new ();

	mask = pango_font_description_get_set_fields (desc);
	if ((mask & PANGO_FONT_MASK_SIZE) != 0)
		size = pango_font_description_get_size (desc);

	if (gnm_debug_flag ("so-font")) {
		char *s = pango_font_description_to_string (desc);
		g_printerr ("from GtkStyleContext font=\"%s\", family set = %i,"
			    " size set = %i, size = %i\n",
			    s, ((mask & PANGO_FONT_MASK_FAMILY) != 0),
			    ((mask & PANGO_FONT_MASK_SIZE) != 0), size);
		g_free (s);
	}

	if ((mask & PANGO_FONT_MASK_FAMILY) == 0 || size == 0) {
		/* Trying gsettings */
		GSettings *set = g_settings_new ("org.gnome.desktop.interface");
		char *font_name = g_settings_get_string (set, "font-name");
		if (font_name != NULL) {
			pango_font_description_free (desc);
			desc = pango_font_description_from_string (font_name);
			g_free (font_name);
			mask = pango_font_description_get_set_fields (desc);
			if ((mask & PANGO_FONT_MASK_SIZE) != 0)
				size = pango_font_description_get_size (desc);
			else
				size = 0;
			if (gnm_debug_flag ("so-font")) {
				char *s = pango_font_description_to_string (desc);
				g_printerr ("from GSettings: font=\"%s\", family set = %i,"
					    " size set = %i, size = %i\n",
					    s, ((mask & PANGO_FONT_MASK_FAMILY) != 0),
					    ((mask & PANGO_FONT_MASK_SIZE) != 0), size);
				g_free (s);
			}
		}
	}

	if ((mask & PANGO_FONT_MASK_FAMILY) == 0 || size == 0) {
		pango_font_description_free (desc);
		desc = pango_font_description_from_string ("sans 10");
		if (gnm_debug_flag ("so-font"))
			g_printerr ("Using \"sans 10\" instead.\n");
	}

	return desc;
}

static void
draw_cairo_text (cairo_t *cr, char const *text, int *pwidth, int *pheight,
		 gboolean centered_v, gboolean centered_h, gboolean single, gint highlight_n, gboolean scale)
{
	PangoLayout *layout = pango_cairo_create_layout (cr);
	double const scale_h = 72. / gnm_app_display_dpi_get (TRUE);
	double const scale_v = 72. / gnm_app_display_dpi_get (FALSE);
	PangoFontDescription *desc = get_font ();
	int width, height;

	pango_context_set_font_description
		(pango_layout_get_context (layout), desc);
	pango_layout_set_spacing (layout, 3 * PANGO_SCALE);
	pango_layout_set_single_paragraph_mode (layout, single);
	pango_layout_set_text (layout, text, -1);
	pango_layout_get_pixel_size (layout, &width, &height);

	cairo_scale (cr, scale_h, scale_v);

	if (scale && pwidth != NULL && pheight != NULL) {
		double sc_x = ((double) *pwidth)/(width * scale_h);
		double sc_y = ((double) *pheight)/(height * scale_v);
		double sc = MIN(sc_x, sc_y);

		if (sc < 1.)
			cairo_scale (cr, sc, sc);
	}

	if (centered_v)
		cairo_rel_move_to (cr, 0., 0.5 - ((double)height)/2.);
	if (centered_h)
		cairo_rel_move_to (cr, 0.5 - ((double)width)/2., 0.);
	if (highlight_n > 0 && pheight != NULL && pwidth != NULL) {
		PangoLayoutIter *pliter;
		gboolean got_line = TRUE;
		int i;
		pliter = pango_layout_get_iter (layout);
		for (i = 1; i < highlight_n; i++)
			got_line = pango_layout_iter_next_line (pliter);

		if (got_line) {
			int y0, y1;
			double dy0 = 0, dy1 = 0;
			pango_layout_iter_get_line_yrange (pliter, &y0, &y1);
			dy0 = y0 / (double)PANGO_SCALE;
			dy1 = y1 / (double)PANGO_SCALE;

			if (dy1 > (*pheight - 4)/scale_v)
				cairo_translate (cr, 0, (*pheight - 4)/scale_v - dy1);

			cairo_new_path (cr);
			cairo_rectangle (cr, -4/scale_h, dy0,
					 *pwidth/scale_h, dy1 - dy0);
			cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
			cairo_fill (cr);
		}
		pango_layout_iter_free (pliter);
		cairo_set_source_rgb(cr, 0, 0, 0);
	}
	pango_cairo_show_layout (cr, layout);
	pango_font_description_free (desc);
	g_object_unref (layout);

	if (pwidth)
		*pwidth = width * scale_h;
	if (pheight)
		*pheight = height * scale_v;
}

static void
sheet_widget_frame_draw_cairo (SheetObject const *so, cairo_t *cr,
			       double width, double height)
{
	SheetWidgetFrame *swf = GNM_SOW_FRAME (so);

	int theight = 0, twidth = 0;
	cairo_save (cr);
	cairo_move_to (cr, 10, 0);

	cairo_save (cr);
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	draw_cairo_text (cr, swf->label, &twidth, &theight, FALSE, FALSE, TRUE, 0, FALSE);
	cairo_restore (cr);

	cairo_set_line_width (cr, 1);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
	cairo_new_path (cr);
	cairo_move_to (cr, 6, theight/2);
	cairo_line_to (cr, 0, theight/2);
	cairo_line_to (cr, 0, height);
	cairo_line_to (cr, width, height);
	cairo_line_to (cr, width, theight/2);
	cairo_line_to (cr, 14 + twidth, theight/2);
	cairo_stroke (cr);

	cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
	cairo_new_path (cr);
	cairo_move_to (cr, 6, theight/2 + 1);
	cairo_line_to (cr, 1, theight/2 + 1);
	cairo_line_to (cr, 1, height - 1);
	cairo_line_to (cr, width - 1, height - 1);
	cairo_line_to (cr, width - 1, theight/2 + 1);
	cairo_line_to (cr, 14 + twidth, theight/2 + 1);
	cairo_stroke (cr);

	cairo_new_path (cr);
	cairo_restore (cr);
}

SOW_MAKE_TYPE (frame, Frame,
	       sheet_widget_frame_user_config,
	       NULL,
	       NULL,
	       NULL,
	       sheet_widget_frame_copy,
	       sheet_widget_frame_write_xml_sax,
	       sheet_widget_frame_prep_sax_parser,
	       sheet_widget_frame_get_property,
	       sheet_widget_frame_set_property,
	       sheet_widget_frame_draw_cairo,
	       {
		       g_object_class_install_property
			       (object_class, SOF_PROP_TEXT,
				g_param_spec_string ("text", NULL, NULL, NULL,
						     GSF_PARAM_STATIC | G_PARAM_READWRITE));
	       })

/****************************************************************************/
#define GNM_SOW_BUTTON(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_SOW_BUTTON_TYPE, SheetWidgetButton))
#define DEP_TO_BUTTON(d_ptr)		(SheetWidgetButton *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetButton, dep))
typedef struct {
	SheetObjectWidget	sow;

	GnmDependent	 dep;
	char *label;
	PangoAttrList *markup;
	gboolean	 value;
} SheetWidgetButton;
typedef SheetObjectWidgetClass SheetWidgetButtonClass;

enum {
	SOB_PROP_0 = 0,
	SOB_PROP_TEXT,
	SOB_PROP_MARKUP
};

static void
sheet_widget_button_get_property (GObject *obj, guint param_id,
				  GValue *value, GParamSpec *pspec)
{
	SheetWidgetButton *swb = GNM_SOW_BUTTON (obj);

	switch (param_id) {
	case SOB_PROP_TEXT:
		g_value_set_string (value, swb->label);
		break;
	case SOB_PROP_MARKUP:
		g_value_set_boxed (value, NULL); /* swb->markup */
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
sheet_widget_button_set_property (GObject *obj, guint param_id,
				    GValue const *value, GParamSpec *pspec)
{
	SheetWidgetButton *swb = GNM_SOW_BUTTON (obj);

	switch (param_id) {
	case SOB_PROP_TEXT:
		sheet_widget_button_set_label (GNM_SO (swb),
					       g_value_get_string (value));
		break;
	case SOB_PROP_MARKUP:
#if 0
		sheet_widget_button_set_markup (GNM_SO (swb),
						g_value_peek_pointer (value));
#endif
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return;
	}
}

static void
button_eval (GnmDependent *dep)
{
	GnmValue *v;
	GnmEvalPos pos;
	gboolean err, result;

	v = gnm_expr_top_eval (dep->texpr, eval_pos_init_dep (&pos, dep),
			       GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	result = value_get_as_bool (v, &err);
	value_release (v);
	if (!err) {
		SheetWidgetButton *swb = DEP_TO_BUTTON(dep);

		swb->value = result;
	}
}

static void
button_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "Button%p", (void *)dep);
}

static DEPENDENT_MAKE_TYPE (button, .eval = button_eval, .debug_name = button_debug_name )

static void
sheet_widget_button_init_full (SheetWidgetButton *swb,
			       GnmCellRef const *ref,
			       char const *text,
			       PangoAttrList *markup)
{
	SheetObject *so = GNM_SO (swb);

	so->flags &= ~SHEET_OBJECT_PRINT;
	swb->label = g_strdup (text);
	swb->markup = markup;
	swb->value = FALSE;
	swb->dep.sheet = NULL;
	swb->dep.flags = button_get_dep_type ();
	swb->dep.texpr = (ref != NULL)
		? gnm_expr_top_new (gnm_expr_new_cellref (ref))
		: NULL;
	if (markup) pango_attr_list_ref (markup);
}

static void
sheet_widget_button_init (SheetWidgetButton *swb)
{
	sheet_widget_button_init_full (swb, NULL, _("Button"), NULL);
}

static void
sheet_widget_button_finalize (GObject *obj)
{
	SheetWidgetButton *swb = GNM_SOW_BUTTON (obj);

	g_free (swb->label);
	swb->label = NULL;

	if (swb->markup) {
		pango_attr_list_unref (swb->markup);
		swb->markup = NULL;
	}

	dependent_set_expr (&swb->dep, NULL);

	sheet_object_widget_class->finalize (obj);
}

static void
cb_button_pressed (GtkToggleButton *button, SheetWidgetButton *swb)
{
	GnmCellRef ref;

	swb->value = TRUE;

	if (so_get_ref (GNM_SO (swb), &ref, TRUE) != NULL) {
		cmd_so_set_value (widget_wbc (GTK_WIDGET (button)),
				  _("Pressed Button"),
				  &ref, value_new_bool (TRUE),
				  sheet_object_get_sheet (GNM_SO (swb)));
	}
}

static void
cb_button_released (GtkToggleButton *button, SheetWidgetButton *swb)
{
	GnmCellRef ref;

	swb->value = FALSE;

	if (so_get_ref (GNM_SO (swb), &ref, TRUE) != NULL) {
		cmd_so_set_value (widget_wbc (GTK_WIDGET (button)),
				  _("Released Button"),
				  &ref, value_new_bool (FALSE),
				  sheet_object_get_sheet (GNM_SO (swb)));
	}
}

static GtkWidget *
sheet_widget_button_create_widget (SheetObjectWidget *sow)
{
	SheetWidgetButton *swb = GNM_SOW_BUTTON (sow);
	GtkWidget *w = gtk_button_new_with_label (swb->label);
	gtk_widget_set_can_focus (w, FALSE);
	gtk_label_set_attributes (GTK_LABEL (gtk_bin_get_child (GTK_BIN (w))),
				  swb->markup);
	g_signal_connect (G_OBJECT (w),
			  "pressed",
			  G_CALLBACK (cb_button_pressed), swb);
	g_signal_connect (G_OBJECT (w),
			  "released",
			  G_CALLBACK (cb_button_released), swb);
	return w;
}

static void
sheet_widget_button_copy (SheetObject *dst, SheetObject const *src)
{
	SheetWidgetButton const *src_swb = GNM_SOW_BUTTON (src);
	SheetWidgetButton       *dst_swb = GNM_SOW_BUTTON (dst);
	GnmCellRef ref;
	sheet_widget_button_init_full (dst_swb,
				       so_get_ref (src, &ref, FALSE),
				       src_swb->label,
				       src_swb->markup);
	dst_swb->value = src_swb->value;
}

typedef struct {
	GtkWidget *dialog;
	GnmExprEntry *expression;
	GtkWidget *label;

	char *old_label;
	GtkWidget *old_focus;

	WBCGtk  *wbcg;
	SheetWidgetButton *swb;
	Sheet		    *sheet;
} ButtonConfigState;

static void
cb_button_set_focus (G_GNUC_UNUSED GtkWidget *window, GtkWidget *focus_widget,
		     ButtonConfigState *state)
{
	/* Note:  half of the set-focus action is handle by the default
	 *        callback installed by wbc_gtk_attach_guru */

	/* Force an update of the content in case it needs tweaking (eg make it
	 * absolute) */
	if (state->old_focus != NULL &&
	    GNM_EXPR_ENTRY_IS (gtk_widget_get_parent (state->old_focus))) {
		GnmParsePos  pp;
		GnmExprTop const *texpr = gnm_expr_entry_parse
			(GNM_EXPR_ENTRY (gtk_widget_get_parent (state->old_focus)),
			 parse_pos_init_sheet (&pp, state->sheet),
			 NULL, FALSE, GNM_EXPR_PARSE_DEFAULT);
		if (texpr != NULL)
			gnm_expr_top_unref (texpr);
	}
	state->old_focus = focus_widget;
}

static void
cb_button_config_destroy (ButtonConfigState *state)
{
	g_return_if_fail (state != NULL);

	g_free (state->old_label);
	state->old_label = NULL;
	state->dialog = NULL;
	g_free (state);
}

static void
cb_button_config_ok_clicked (G_GNUC_UNUSED GtkWidget *button, ButtonConfigState *state)
{
	SheetObject *so = GNM_SO (state->swb);
	GnmParsePos  pp;
	GnmExprTop const *texpr = gnm_expr_entry_parse (state->expression,
		parse_pos_init_sheet (&pp, so->sheet),
		NULL, FALSE, GNM_EXPR_PARSE_DEFAULT);
	gchar const *text = gtk_entry_get_text(GTK_ENTRY(state->label));

	cmd_so_set_button (GNM_WBC (state->wbcg), so,
			     texpr, g_strdup (state->old_label), g_strdup (text));

	gtk_widget_destroy (state->dialog);
}

static void
cb_button_config_cancel_clicked (G_GNUC_UNUSED GtkWidget *button, ButtonConfigState *state)
{
	sheet_widget_button_set_label	(GNM_SO (state->swb),
					 state->old_label);
	gtk_widget_destroy (state->dialog);
}

static void
cb_button_label_changed (GtkEntry *entry, ButtonConfigState *state)
{
	sheet_widget_button_set_label	(GNM_SO (state->swb),
					 gtk_entry_get_text (entry));
}

static void
sheet_widget_button_user_config (SheetObject *so, SheetControl *sc)
{
	SheetWidgetButton *swb = GNM_SOW_BUTTON (so);
	WBCGtk  *wbcg = scg_wbcg (GNM_SCG (sc));
	ButtonConfigState *state;
	GtkWidget *grid;
	GtkBuilder *gui;

	g_return_if_fail (swb != NULL);

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
		return;

	gui = gnm_gtk_builder_load ("res:ui/so-button.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (!gui)
		return;
	state = g_new (ButtonConfigState, 1);
	state->swb = swb;
	state->wbcg = wbcg;
	state->sheet = sc_sheet	(sc);
	state->old_focus = NULL;
	state->old_label = g_strdup (swb->label);
	state->dialog = go_gtk_builder_get_widget (gui, "SO-Button");

	grid = go_gtk_builder_get_widget (gui, "main-grid");

	state->expression = gnm_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (state->expression,
		GNM_EE_FORCE_ABS_REF | GNM_EE_SHEET_OPTIONAL | GNM_EE_SINGLE_RANGE,
		GNM_EE_MASK);
	gnm_expr_entry_load_from_dep (state->expression, &swb->dep);
	go_atk_setup_label (go_gtk_builder_get_widget (gui, "label_linkto"),
			     GTK_WIDGET (state->expression));
	gtk_grid_attach (GTK_GRID (grid),
	                 GTK_WIDGET (state->expression), 1, 0, 1, 1);
	gtk_widget_show (GTK_WIDGET (state->expression));

	state->label = go_gtk_builder_get_widget (gui, "label_entry");
	gtk_entry_set_text (GTK_ENTRY (state->label), swb->label);
	gtk_editable_select_region (GTK_EDITABLE(state->label), 0, -1);
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->expression));
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->label));

	g_signal_connect (G_OBJECT (state->label),
		"changed",
		G_CALLBACK (cb_button_label_changed), state);
	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "ok_button")),
		"clicked",
		G_CALLBACK (cb_button_config_ok_clicked), state);
	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cb_button_config_cancel_clicked), state);

	gnm_init_help_button (
		go_gtk_builder_get_widget (gui, "help_button"),
		GNUMERIC_HELP_LINK_SO_BUTTON);

	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SHEET_OBJECT_CONFIG_KEY);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_button_config_destroy);

	/* Note:  half of the set-focus action is handle by the default */
	/*        callback installed by wbc_gtk_attach_guru */
	g_signal_connect (G_OBJECT (state->dialog), "set-focus",
		G_CALLBACK (cb_button_set_focus), state);
	g_object_unref (gui);

	gtk_widget_show (state->dialog);
}

static gboolean
sheet_widget_button_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetButton *swb = GNM_SOW_BUTTON (so);

	dependent_set_sheet (&swb->dep, sheet);

	return FALSE;
}

static void
sheet_widget_button_foreach_dep (SheetObject *so,
				   SheetObjectForeachDepFunc func,
				   gpointer user)
{
	SheetWidgetButton *swb = GNM_SOW_BUTTON (so);
	func (&swb->dep, so, user);
}

static void
sheet_widget_button_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
				   GnmConventions const *convs)
{
	/* FIXME: markup */
	SheetWidgetButton *swb = GNM_SOW_BUTTON (so);
	gsf_xml_out_add_cstr (output, "Label", swb->label);
	gsf_xml_out_add_int (output, "Value", swb->value);
	sax_write_dep (output, &swb->dep, "Input", convs);
}

static void
sheet_widget_button_prep_sax_parser (SheetObject *so, GsfXMLIn *xin,
				     xmlChar const **attrs,
				     GnmConventions const *convs)
{
	SheetWidgetButton *swb = GNM_SOW_BUTTON (so);
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_eq (attrs[0], "Label"))
			g_object_set (G_OBJECT (swb), "text", attrs[1], NULL);
		else if (gnm_xml_attr_int (attrs, "Value", &swb->value))
			;
		else if (sax_read_dep (attrs, "Input", &swb->dep, xin, convs))
			;
}

void
sheet_widget_button_set_link (SheetObject *so, GnmExprTop const *texpr)
{
 	SheetWidgetButton *swb = GNM_SOW_BUTTON (so);
 	dependent_set_expr (&swb->dep, texpr);
 	if (texpr && swb->dep.sheet)
 		dependent_link (&swb->dep);
}

GnmExprTop const *
sheet_widget_button_get_link	 (SheetObject *so)
{
 	SheetWidgetButton *swb = GNM_SOW_BUTTON (so);
 	GnmExprTop const *texpr = swb->dep.texpr;

 	if (texpr)
 		gnm_expr_top_ref (texpr);

 	return texpr;
}


void
sheet_widget_button_set_label (SheetObject *so, char const *str)
{
	GList *ptr;
	SheetWidgetButton *swb = GNM_SOW_BUTTON (so);
	char *new_label;

	if (go_str_compare (str, swb->label) == 0)
		return;

	new_label = g_strdup (str);
	g_free (swb->label);
	swb->label = new_label;

	for (ptr = swb->sow.so.realized_list; ptr != NULL; ptr = ptr->next) {
		SheetObjectView *view = ptr->data;
		GocWidget *item = get_goc_widget (view);
		gtk_button_set_label (GTK_BUTTON (item->widget), swb->label);
	}
}

void
sheet_widget_button_set_markup (SheetObject *so, PangoAttrList *markup)
{
	GList *ptr;
	SheetWidgetButton *swb = GNM_SOW_BUTTON (so);

	if (markup == swb->markup)
		return;

	if (swb->markup) pango_attr_list_unref (swb->markup);
	swb->markup = markup;
	if (markup) pango_attr_list_ref (markup);

	for (ptr = swb->sow.so.realized_list; ptr != NULL; ptr = ptr->next) {
		SheetObjectView *view = ptr->data;
		GocWidget *item = get_goc_widget (view);
		GtkLabel *lab =
			GTK_LABEL (gtk_bin_get_child (GTK_BIN (item->widget)));
		gtk_label_set_attributes (lab, swb->markup);
	}
}

static void
sheet_widget_button_draw_cairo (SheetObject const *so, cairo_t *cr,
				double width, double height)
{
	SheetWidgetButton *swb = GNM_SOW_BUTTON (so);
	int twidth, theight;
	double half_line;
	double radius = 10;

	if (height < 3 * radius)
		radius = height / 3.;
	if (width < 3 * radius)
		radius = width / 3.;
	if (radius < 1)
		radius = 1;
	half_line = radius * 0.15;

	cairo_save (cr);
	cairo_set_line_width (cr, 2 * half_line);
	cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);

	cairo_new_path (cr);
	cairo_arc (cr, radius + half_line, radius + half_line, radius, M_PI, - M_PI/2);
	cairo_arc (cr, width - (radius + half_line), radius + half_line,
		   radius, - M_PI/2, 0);
	cairo_arc (cr, width - (radius + half_line), height - (radius + half_line),
		   radius, 0, M_PI/2);
	cairo_arc (cr, (radius + half_line), height - (radius + half_line),
		   radius, M_PI/2, M_PI);
	cairo_close_path (cr);
	cairo_stroke (cr);

	cairo_set_source_rgb(cr, 0, 0, 0);

	cairo_move_to (cr, width/2., height/2.);

	twidth = 0.8 * width;
	theight = 0.8 * height;
	draw_cairo_text (cr, swb->label, &twidth, &theight, TRUE, TRUE, TRUE, 0, TRUE);

	cairo_new_path (cr);
	cairo_restore (cr);
}

SOW_MAKE_TYPE (button, Button,
	       sheet_widget_button_user_config,
	       sheet_widget_button_set_sheet,
	       so_clear_sheet,
	       sheet_widget_button_foreach_dep,
	       sheet_widget_button_copy,
	       sheet_widget_button_write_xml_sax,
	       sheet_widget_button_prep_sax_parser,
	       sheet_widget_button_get_property,
	       sheet_widget_button_set_property,
	       sheet_widget_button_draw_cairo,
	       {
		       g_object_class_install_property
			       (object_class, SOB_PROP_TEXT,
				g_param_spec_string ("text", NULL, NULL, NULL,
						     GSF_PARAM_STATIC | G_PARAM_READWRITE));
		       g_object_class_install_property
			       (object_class, SOB_PROP_MARKUP,
				g_param_spec_boxed ("markup", NULL, NULL, PANGO_TYPE_ATTR_LIST,
						    GSF_PARAM_STATIC | G_PARAM_READWRITE));
	       })

/****************************************************************************/

#define DEP_TO_ADJUSTMENT(d_ptr)	(SheetWidgetAdjustment *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetAdjustment, dep))
#define GNM_SOW_ADJUSTMENT_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), GNM_SOW_ADJUSTMENT_TYPE, SheetWidgetAdjustmentClass))
#define SWA_CLASS(so)		     (GNM_SOW_ADJUSTMENT_CLASS (G_OBJECT_GET_CLASS(so)))

typedef struct {
	SheetObjectWidget	sow;

	gboolean  being_updated;
	GnmDependent dep;
	GtkAdjustment *adjustment;

	gboolean horizontal;
} SheetWidgetAdjustment;

typedef struct {
	SheetObjectWidgetClass parent_class;
	GType type;
	gboolean has_orientation;
} SheetWidgetAdjustmentClass;

enum {
	SWA_PROP_0 = 0,
	SWA_PROP_HORIZONTAL
};

#ifndef g_signal_handlers_disconnect_by_data
#define g_signal_handlers_disconnect_by_data(instance, data) \
  g_signal_handlers_disconnect_matched ((instance), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, (data))
#endif
static void
cb_range_destroyed (GtkWidget *w, SheetWidgetAdjustment *swa)
{
	GObject *accessible = G_OBJECT (gtk_widget_get_accessible (w));
	if (accessible)
		g_signal_handlers_disconnect_by_data (swa->adjustment, accessible);
}

static void
sheet_widget_adjustment_set_value (SheetWidgetAdjustment *swa, double new_val)
{
	if (swa->being_updated)
		return;
	swa->being_updated = TRUE;
	gtk_adjustment_set_value (swa->adjustment, new_val);
	swa->being_updated = FALSE;
}

/**
 * sheet_widget_adjustment_get_adjustment:
 * @so: #SheetObject
 *
 * Returns: (transfer none): the associated #GtkAdjustment.
 **/
GtkAdjustment *
sheet_widget_adjustment_get_adjustment (SheetObject *so)
{
	g_return_val_if_fail (GNM_IS_SOW_ADJUSTMENT (so), NULL);
	return (GNM_SOW_ADJUSTMENT (so)->adjustment);
}

gboolean
sheet_widget_adjustment_get_horizontal (SheetObject *so)
{
	g_return_val_if_fail (GNM_IS_SOW_ADJUSTMENT (so), TRUE);
	return (GNM_SOW_ADJUSTMENT (so)->horizontal);
}

void
sheet_widget_adjustment_set_link (SheetObject *so, GnmExprTop const *texpr)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (so);
	dependent_set_expr (&swa->dep, texpr);
	if (texpr && swa->dep.sheet)
		dependent_link (&swa->dep);
}

GnmExprTop const *
sheet_widget_adjustment_get_link (SheetObject *so)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (so);
	GnmExprTop const *texpr = swa->dep.texpr;

	if (texpr)
		gnm_expr_top_ref (texpr);

	return texpr;
}


static void
adjustment_eval (GnmDependent *dep)
{
	GnmValue *v;
	GnmEvalPos pos;

	v = gnm_expr_top_eval (dep->texpr, eval_pos_init_dep (&pos, dep),
			       GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	sheet_widget_adjustment_set_value (DEP_TO_ADJUSTMENT(dep),
		value_get_as_float (v));
	value_release (v);
}

static void
adjustment_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "Adjustment%p", (void *)dep);
}

static DEPENDENT_MAKE_TYPE (adjustment, .eval = adjustment_eval, .debug_name = adjustment_debug_name )

static void
cb_adjustment_widget_value_changed (GtkWidget *widget,
				    SheetWidgetAdjustment *swa)
{
	GnmCellRef ref;

	if (swa->being_updated)
		return;

	if (so_get_ref (GNM_SO (swa), &ref, TRUE) != NULL) {
		GnmCell *cell = sheet_cell_fetch (ref.sheet, ref.col, ref.row);
		/* TODO : add more control for precision, XL is stupid */
		int new_val = gnm_fake_round (gtk_adjustment_get_value (swa->adjustment));
		if (cell->value != NULL &&
		    VALUE_IS_FLOAT (cell->value) &&
		    value_get_as_float (cell->value) == new_val)
			return;

		swa->being_updated = TRUE;
		cmd_so_set_value (widget_wbc (widget),
				  /* FIXME: This text sucks:  */
				  _("Change widget"),
				  &ref, value_new_int (new_val),
				  sheet_object_get_sheet (GNM_SO (swa)));
		swa->being_updated = FALSE;
	}
}

void
sheet_widget_adjustment_set_horizontal (SheetObject *so,
					gboolean horizontal)
{
	SheetWidgetAdjustment *swa = (SheetWidgetAdjustment *)so;
	GList *ptr;
	GtkOrientation o;

	if (!SWA_CLASS (swa)->has_orientation)
		return;
	horizontal = !!horizontal;
	if (horizontal == swa->horizontal)
		return;
	swa->horizontal = horizontal;
	o = horizontal ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;

	/* Change direction for all realized widgets.  */
	for (ptr = swa->sow.so.realized_list; ptr != NULL; ptr = ptr->next) {
		SheetObjectView *view = ptr->data;
		GocWidget *item = get_goc_widget (view);
		gtk_orientable_set_orientation (GTK_ORIENTABLE (item->widget), o);
	}
}


static void
sheet_widget_adjustment_get_property (GObject *obj, guint param_id,
				      GValue *value, GParamSpec *pspec)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (obj);

	switch (param_id) {
	case SWA_PROP_HORIZONTAL:
		g_value_set_boolean (value, swa->horizontal);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
sheet_widget_adjustment_set_property (GObject *obj, guint param_id,
				      GValue const *value, GParamSpec *pspec)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (obj);

	switch (param_id) {
	case SWA_PROP_HORIZONTAL:
		sheet_widget_adjustment_set_horizontal (GNM_SO (swa), g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return;
	}
}

static void
sheet_widget_adjustment_init_full (SheetWidgetAdjustment *swa,
				   GnmCellRef const *ref,
				   gboolean horizontal)
{
	SheetObject *so;
	g_return_if_fail (swa != NULL);

	so = GNM_SO (swa);
	so->flags &= ~SHEET_OBJECT_PRINT;

	swa->adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0., 0., 100., 1., 10., 0.));
	g_object_ref_sink (swa->adjustment);

	swa->horizontal = horizontal;
	swa->being_updated = FALSE;
	swa->dep.sheet = NULL;
	swa->dep.flags = adjustment_get_dep_type ();
	swa->dep.texpr = (ref != NULL)
		? gnm_expr_top_new (gnm_expr_new_cellref (ref))
		: NULL;
}

static void
sheet_widget_adjustment_init (SheetWidgetAdjustment *swa)
{
	sheet_widget_adjustment_init_full (swa, NULL, FALSE);
}

static void
sheet_widget_adjustment_finalize (GObject *obj)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (obj);

	g_return_if_fail (swa != NULL);

	dependent_set_expr (&swa->dep, NULL);
	if (swa->adjustment != NULL) {
		g_object_unref (swa->adjustment);
		swa->adjustment = NULL;
	}

	sheet_object_widget_class->finalize (obj);
}

static void
sheet_widget_adjustment_copy (SheetObject *dst, SheetObject const *src)
{
	SheetWidgetAdjustment const *src_swa = GNM_SOW_ADJUSTMENT (src);
	SheetWidgetAdjustment       *dst_swa = GNM_SOW_ADJUSTMENT (dst);
	GtkAdjustment *dst_adjust, *src_adjust;
	GnmCellRef ref;

	sheet_widget_adjustment_init_full (dst_swa,
					   so_get_ref (src, &ref, FALSE),
					   src_swa->horizontal);
	dst_adjust = dst_swa->adjustment;
	src_adjust = src_swa->adjustment;

	gtk_adjustment_configure
		(dst_adjust,
		 gtk_adjustment_get_value (src_adjust),
		 gtk_adjustment_get_lower (src_adjust),
		 gtk_adjustment_get_upper (src_adjust),
		 gtk_adjustment_get_step_increment (src_adjust),
		 gtk_adjustment_get_page_increment (src_adjust),
		 gtk_adjustment_get_page_size (src_adjust));
}

typedef struct {
	GtkWidget          *dialog;
	GnmExprEntry       *expression;
	GtkWidget          *min;
	GtkWidget          *max;
	GtkWidget          *inc;
	GtkWidget          *page;
	GtkWidget          *direction_h;
	GtkWidget          *direction_v;

	char               *undo_label;
	GtkWidget          *old_focus;

	WBCGtk *wbcg;
	SheetWidgetAdjustment *swa;
	Sheet		   *sheet;
} AdjustmentConfigState;

static void
cb_adjustment_set_focus (G_GNUC_UNUSED GtkWidget *window, GtkWidget *focus_widget,
			 AdjustmentConfigState *state)
{
	GtkWidget *ofp;

	/* Note:  half of the set-focus action is handle by the default
	 *        callback installed by wbc_gtk_attach_guru. */

	ofp = state->old_focus
		? gtk_widget_get_parent (state->old_focus)
		: NULL;
	/* Force an update of the content in case it needs tweaking (eg make it
	 * absolute) */
	if (ofp && GNM_EXPR_ENTRY_IS (ofp)) {
		GnmParsePos  pp;
		GnmExprTop const *texpr = gnm_expr_entry_parse (
			GNM_EXPR_ENTRY (ofp),
			parse_pos_init_sheet (&pp, state->sheet),
			NULL, FALSE, GNM_EXPR_PARSE_DEFAULT);
		if (texpr != NULL)
			gnm_expr_top_unref (texpr);
	}
	state->old_focus = focus_widget;
}

static void
cb_adjustment_config_destroy (AdjustmentConfigState *state)
{
	g_return_if_fail (state != NULL);

	g_free (state->undo_label);

	state->dialog = NULL;
	g_free (state);
}

static void
cb_adjustment_config_ok_clicked (G_GNUC_UNUSED GtkWidget *button, AdjustmentConfigState *state)
{
	SheetObject *so = GNM_SO (state->swa);
	GnmParsePos pp;
	GnmExprTop const *texpr = gnm_expr_entry_parse (state->expression,
		parse_pos_init_sheet (&pp, so->sheet),
		NULL, FALSE, GNM_EXPR_PARSE_DEFAULT);
	gboolean horizontal;

	horizontal = state->direction_h
		? gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->direction_h))
		: state->swa->horizontal;

	cmd_so_set_adjustment (GNM_WBC (state->wbcg), so,
			       texpr,
			       horizontal,
			       gtk_spin_button_get_value_as_int (
				       GTK_SPIN_BUTTON (state->min)),
			       gtk_spin_button_get_value_as_int (
				       GTK_SPIN_BUTTON (state->max)),
			       gtk_spin_button_get_value_as_int (
				       GTK_SPIN_BUTTON (state->inc)),
			       gtk_spin_button_get_value_as_int (
				       GTK_SPIN_BUTTON (state->page)),
			       state->undo_label);

	gtk_widget_destroy (state->dialog);
}

static void
cb_adjustment_config_cancel_clicked (G_GNUC_UNUSED GtkWidget *button, AdjustmentConfigState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
sheet_widget_adjustment_user_config_impl (SheetObject *so, SheetControl *sc, char const *undo_label, char const *dialog_label)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (so);
	SheetWidgetAdjustmentClass *swa_class = SWA_CLASS (swa);
	WBCGtk *wbcg = scg_wbcg (GNM_SCG (sc));
	AdjustmentConfigState *state;
	GtkWidget *grid;
	GtkBuilder *gui;
	gboolean has_directions = swa_class->has_orientation;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
		return;

	gui = gnm_gtk_builder_load ("res:ui/so-scrollbar.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (!gui)
		return;
	state = g_new (AdjustmentConfigState, 1);
	state->swa = swa;
	state->wbcg = wbcg;
	state->sheet = sc_sheet	(sc);
	state->old_focus = NULL;
	state->undo_label = (undo_label == NULL) ? NULL : g_strdup (undo_label);
	state->dialog = go_gtk_builder_get_widget (gui, "SO-Scrollbar");

	if (dialog_label != NULL)
		gtk_window_set_title (GTK_WINDOW (state->dialog), dialog_label);

	grid = go_gtk_builder_get_widget (gui, "main-grid");

	state->expression = gnm_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (state->expression,
		GNM_EE_FORCE_ABS_REF | GNM_EE_SHEET_OPTIONAL | GNM_EE_SINGLE_RANGE,
		GNM_EE_MASK);
	gnm_expr_entry_load_from_dep (state->expression, &swa->dep);
	go_atk_setup_label (go_gtk_builder_get_widget (gui, "label_linkto"),
			     GTK_WIDGET (state->expression));
	gtk_grid_attach (GTK_GRID (grid),
	                 GTK_WIDGET (state->expression), 1, 0, 2, 1);
	gtk_widget_show (GTK_WIDGET (state->expression));

	if (has_directions) {
		state->direction_h = go_gtk_builder_get_widget (gui, "direction_h");
		state->direction_v = go_gtk_builder_get_widget (gui, "direction_v");
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (swa->horizontal
					    ? state->direction_h
					    : state->direction_v),
			 TRUE);
	} else {
		state->direction_h = NULL;
		state->direction_v = NULL;
		gtk_widget_destroy (go_gtk_builder_get_widget (gui, "direction_label"));
		gtk_widget_destroy (go_gtk_builder_get_widget (gui, "direction_h"));
		gtk_widget_destroy (go_gtk_builder_get_widget (gui, "direction_v"));
	}

	/* TODO : This is silly, no need to be similar to XL here. */
	state->min = go_gtk_builder_get_widget (gui, "spin_min");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->min),
				   gtk_adjustment_get_lower (swa->adjustment));
	state->max = go_gtk_builder_get_widget (gui, "spin_max");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->max),
				   gtk_adjustment_get_upper (swa->adjustment));
	state->inc = go_gtk_builder_get_widget (gui, "spin_increment");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->inc),
				   gtk_adjustment_get_step_increment (swa->adjustment));
	state->page = go_gtk_builder_get_widget (gui, "spin_page");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->page),
				   gtk_adjustment_get_page_increment (swa->adjustment));

	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->expression));
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->min));
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->max));
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->inc));
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->page));
	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "ok_button")),
		"clicked",
		G_CALLBACK (cb_adjustment_config_ok_clicked), state);
	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cb_adjustment_config_cancel_clicked), state);

	gnm_init_help_button (
		go_gtk_builder_get_widget (gui, "help_button"),
		GNUMERIC_HELP_LINK_SO_ADJUSTMENT);

	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SHEET_OBJECT_CONFIG_KEY);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_adjustment_config_destroy);

	/* Note:  half of the set-focus action is handle by the default */
	/*        callback installed by wbc_gtk_attach_guru           */
	g_signal_connect (G_OBJECT (state->dialog), "set-focus",
		G_CALLBACK (cb_adjustment_set_focus), state);
	g_object_unref (gui);

	gtk_widget_show (state->dialog);
}

static void
sheet_widget_adjustment_user_config (SheetObject *so, SheetControl *sc)
{
	sheet_widget_adjustment_user_config_impl (so, sc, N_("Configure Adjustment"),
						  N_("Adjustment Properties"));
}

static gboolean
sheet_widget_adjustment_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (so);

	dependent_set_sheet (&swa->dep, sheet);

	return FALSE;
}

static void
sheet_widget_adjustment_foreach_dep (SheetObject *so,
				     SheetObjectForeachDepFunc func,
				     gpointer user)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (so);
	func (&swa->dep, so, user);
}

static void
sheet_widget_adjustment_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
				       GnmConventions const *convs)
{
	SheetWidgetAdjustment const *swa = GNM_SOW_ADJUSTMENT (so);
	SheetWidgetAdjustmentClass *swa_class = SWA_CLASS (so);

	go_xml_out_add_double (output, "Min", gtk_adjustment_get_lower (swa->adjustment));
	go_xml_out_add_double (output, "Max", gtk_adjustment_get_upper (swa->adjustment));
	go_xml_out_add_double (output, "Inc", gtk_adjustment_get_step_increment (swa->adjustment));
	go_xml_out_add_double (output, "Page", gtk_adjustment_get_page_increment (swa->adjustment));
	go_xml_out_add_double (output, "Value", gtk_adjustment_get_value (swa->adjustment));

	if (swa_class->has_orientation)
		gsf_xml_out_add_bool (output, "Horizontal", swa->horizontal);

	sax_write_dep (output, &swa->dep, "Input", convs);
}

static void
sheet_widget_adjustment_prep_sax_parser (SheetObject *so, GsfXMLIn *xin,
					 xmlChar const **attrs,
					 GnmConventions const *convs)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (so);
	SheetWidgetAdjustmentClass *swa_class = SWA_CLASS (so);
	swa->horizontal = FALSE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		double tmp;
		gboolean b;

		if (gnm_xml_attr_double (attrs, "Min", &tmp))
			gtk_adjustment_set_lower (swa->adjustment, tmp);
		else if (gnm_xml_attr_double (attrs, "Max", &tmp))
			gtk_adjustment_set_upper (swa->adjustment, tmp);  /* allow scrolling to max */
		else if (gnm_xml_attr_double (attrs, "Inc", &tmp))
			gtk_adjustment_set_step_increment (swa->adjustment, tmp);
		else if (gnm_xml_attr_double (attrs, "Page", &tmp))
			gtk_adjustment_set_page_increment (swa->adjustment, tmp);
		else if (gnm_xml_attr_double (attrs, "Value", &tmp))
			gtk_adjustment_set_value (swa->adjustment, tmp);
		else if (sax_read_dep (attrs, "Input", &swa->dep, xin, convs))
			;
		else if (swa_class->has_orientation &&
			 gnm_xml_attr_bool (attrs, "Horizontal", &b))
			swa->horizontal = b;
	}

	swa->dep.flags = adjustment_get_dep_type ();
}

void
sheet_widget_adjustment_set_details (SheetObject *so, GnmExprTop const *tlink,
				     int value, int min, int max,
				     int inc, int page)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (so);
	double page_size;

	g_return_if_fail (swa != NULL);

	dependent_set_expr (&swa->dep, tlink);
	if (tlink && swa->dep.sheet)
		dependent_link (&swa->dep);

	page_size = gtk_adjustment_get_page_size (swa->adjustment); /* ??? */
	gtk_adjustment_configure (swa->adjustment,
				  value, min, max, inc, page, page_size);
}

static GtkWidget *
sheet_widget_adjustment_create_widget (G_GNUC_UNUSED SheetObjectWidget *sow)
{
	g_assert_not_reached ();
	return NULL;
}

SOW_MAKE_TYPE (adjustment, Adjustment,
	       sheet_widget_adjustment_user_config,
	       sheet_widget_adjustment_set_sheet,
	       so_clear_sheet,
	       sheet_widget_adjustment_foreach_dep,
	       sheet_widget_adjustment_copy,
	       sheet_widget_adjustment_write_xml_sax,
	       sheet_widget_adjustment_prep_sax_parser,
	       sheet_widget_adjustment_get_property,
	       sheet_widget_adjustment_set_property,
	       sheet_widget_draw_cairo,
	       {
		       ((SheetWidgetAdjustmentClass *) object_class)->has_orientation = TRUE;
		       g_object_class_install_property
			       (object_class, SWA_PROP_HORIZONTAL,
				g_param_spec_boolean ("horizontal", NULL, NULL,
						      FALSE,
						      GSF_PARAM_STATIC | G_PARAM_READWRITE));
	       })

/****************************************************************************/

#define GNM_SOW_SCROLLBAR(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_SOW_SCROLLBAR_TYPE, SheetWidgetScrollbar))
#define DEP_TO_SCROLLBAR(d_ptr)		(SheetWidgetScrollbar *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetScrollbar, dep))

typedef SheetWidgetAdjustment  SheetWidgetScrollbar;
typedef SheetWidgetAdjustmentClass SheetWidgetScrollbarClass;

static GtkWidget *
sheet_widget_scrollbar_create_widget (SheetObjectWidget *sow)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (sow);
	GtkWidget *bar;

	swa->being_updated = TRUE;
	bar = gtk_scrollbar_new (swa->horizontal? GTK_ORIENTATION_HORIZONTAL: GTK_ORIENTATION_VERTICAL, swa->adjustment);
	gtk_widget_set_can_focus (bar, FALSE);
	g_signal_connect (G_OBJECT (bar),
		"value_changed",
		G_CALLBACK (cb_adjustment_widget_value_changed), swa);
	g_signal_connect (G_OBJECT (bar), "destroy",
	                  G_CALLBACK (cb_range_destroyed), swa);
	swa->being_updated = FALSE;

	return bar;
}

static void
sheet_widget_scrollbar_user_config (SheetObject *so, SheetControl *sc)
{
	sheet_widget_adjustment_user_config_impl (so, sc, N_("Configure Scrollbar"),
						  N_("Scrollbar Properties"));
}

static void sheet_widget_slider_horizontal_draw_cairo
(SheetObject const *so, cairo_t *cr, double width, double height);

static void
sheet_widget_scrollbar_horizontal_draw_cairo (SheetObject const *so, cairo_t *cr,
					      double width, double height)
{
	cairo_save (cr);
	cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);

	cairo_new_path (cr);
	cairo_move_to (cr, 0., height/2);
	cairo_rel_line_to (cr, 15., 7.5);
	cairo_rel_line_to (cr, 0, -15);
	cairo_close_path (cr);
	cairo_fill (cr);

	cairo_new_path (cr);
	cairo_move_to (cr, width, height/2);
	cairo_rel_line_to (cr, -15., 7.5);
	cairo_rel_line_to (cr, 0, -15);
	cairo_close_path (cr);
	cairo_fill (cr);

	cairo_new_path (cr);
	cairo_translate (cr, 15., 0.);
	sheet_widget_slider_horizontal_draw_cairo (so, cr, width - 30, height);
	cairo_restore (cr);
}

static void
sheet_widget_scrollbar_vertical_draw_cairo (SheetObject const *so, cairo_t *cr,
					    double width, double height)
{
	cairo_save (cr);
	cairo_rotate (cr, M_PI/2);
	cairo_translate (cr, 0., -width);
	sheet_widget_scrollbar_horizontal_draw_cairo (so, cr, height, width);
	cairo_restore (cr);
}

static void
sheet_widget_scrollbar_draw_cairo (SheetObject const *so, cairo_t *cr,
				   double width, double height)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (so);
	if (swa->horizontal)
		sheet_widget_scrollbar_horizontal_draw_cairo
			(so, cr, width, height);
	else
		sheet_widget_scrollbar_vertical_draw_cairo
			(so, cr, width, height);
}

static void
sheet_widget_scrollbar_class_init (SheetObjectWidgetClass *sow_class)
{
	SheetWidgetAdjustmentClass *swa_class = (SheetWidgetAdjustmentClass *)sow_class;
	SheetObjectClass *so_class = GNM_SO_CLASS (sow_class);

        sow_class->create_widget = &sheet_widget_scrollbar_create_widget;
	so_class->user_config = &sheet_widget_scrollbar_user_config;
	so_class->draw_cairo = &sheet_widget_scrollbar_draw_cairo;
	swa_class->type = GTK_TYPE_SCROLLBAR;
}

GSF_CLASS (SheetWidgetScrollbar, sheet_widget_scrollbar,
	   &sheet_widget_scrollbar_class_init, NULL,
	   GNM_SOW_ADJUSTMENT_TYPE)

/****************************************************************************/

#define GNM_SOW_SPIN_BUTTON(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_SOW_SPIN_BUTTON_TYPE, SheetWidgetSpinbutton))
#define DEP_TO_SPINBUTTON(d_ptr)		(SheetWidgetSpinbutton *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetSpinbutton, dep))

typedef SheetWidgetAdjustment		SheetWidgetSpinbutton;
typedef SheetWidgetAdjustmentClass	SheetWidgetSpinbuttonClass;

static GtkWidget *
sheet_widget_spinbutton_create_widget (SheetObjectWidget *sow)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (sow);
	GtkWidget *spinbutton;

	swa->being_updated = TRUE;
	spinbutton = gtk_spin_button_new
		(swa->adjustment,
		 gtk_adjustment_get_step_increment (swa->adjustment),
		 0);
	gtk_widget_set_can_focus (spinbutton, FALSE);
	g_signal_connect (G_OBJECT (spinbutton),
		"value_changed",
		G_CALLBACK (cb_adjustment_widget_value_changed), swa);
	g_signal_connect (G_OBJECT (spinbutton), "destroy",
	                  G_CALLBACK (cb_range_destroyed), swa);
	swa->being_updated = FALSE;
	return spinbutton;
}

static void
sheet_widget_spinbutton_user_config (SheetObject *so, SheetControl *sc)
{
           sheet_widget_adjustment_user_config_impl (so, sc, N_("Configure Spinbutton"),
						     N_("Spinbutton Properties"));
}

static void
sheet_widget_spinbutton_draw_cairo (SheetObject const *so, cairo_t *cr,
				    double width, double height)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (so);
	GtkAdjustment *adjustment = swa->adjustment;
	double value = gtk_adjustment_get_value (adjustment);
	int ivalue = (int) value;
	double halfheight = height/2;
	char *str;

	cairo_save (cr);
	cairo_set_line_width (cr, 0.5);
	cairo_set_source_rgb(cr, 0, 0, 0);

	cairo_new_path (cr);
	cairo_move_to (cr, 0, 0);
	cairo_line_to (cr, width, 0);
	cairo_line_to (cr, width, height);
	cairo_line_to (cr, 0, height);
	cairo_close_path (cr);
	cairo_stroke (cr);

	cairo_new_path (cr);
	cairo_move_to (cr, width - 10, 0);
	cairo_rel_line_to (cr, 0, height);
	cairo_stroke (cr);

	cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);

	cairo_new_path (cr);
	cairo_move_to (cr, width - 5, 3);
	cairo_rel_line_to (cr, 3, 3);
	cairo_rel_line_to (cr, -6, 0);
	cairo_close_path (cr);
	cairo_fill (cr);

	cairo_new_path (cr);
	cairo_move_to (cr, width - 5, height - 3);
	cairo_rel_line_to (cr, 3, -3);
	cairo_rel_line_to (cr, -6, 0);
	cairo_close_path (cr);
	cairo_fill (cr);

	str = g_strdup_printf ("%i", ivalue);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_move_to (cr, 4., halfheight);
	draw_cairo_text (cr, str, NULL, NULL, TRUE, FALSE, TRUE, 0, FALSE);
	g_free (str);

	cairo_new_path (cr);
	cairo_restore (cr);
}

static void
sheet_widget_spinbutton_class_init (SheetObjectWidgetClass *sow_class)
{
	SheetWidgetAdjustmentClass *swa_class = (SheetWidgetAdjustmentClass *)sow_class;
	SheetObjectClass *so_class = GNM_SO_CLASS (sow_class);

        sow_class->create_widget = &sheet_widget_spinbutton_create_widget;
	so_class->user_config = &sheet_widget_spinbutton_user_config;
	so_class->draw_cairo = &sheet_widget_spinbutton_draw_cairo;

	swa_class->type = GTK_TYPE_SPIN_BUTTON;
	swa_class->has_orientation = FALSE;
}

GSF_CLASS (SheetWidgetSpinbutton, sheet_widget_spinbutton,
	   &sheet_widget_spinbutton_class_init, NULL,
	   GNM_SOW_ADJUSTMENT_TYPE)

/****************************************************************************/

#define GNM_SOW_SLIDER(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_SOW_SLIDER_TYPE, SheetWidgetSlider))
#define DEP_TO_SLIDER(d_ptr)		(SheetWidgetSlider *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetSlider, dep))

typedef SheetWidgetAdjustment		SheetWidgetSlider;
typedef SheetWidgetAdjustmentClass	SheetWidgetSliderClass;

static GtkWidget *
sheet_widget_slider_create_widget (SheetObjectWidget *sow)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (sow);
	GtkWidget *slider;

	swa->being_updated = TRUE;
	slider = gtk_scale_new (swa->horizontal? GTK_ORIENTATION_HORIZONTAL: GTK_ORIENTATION_VERTICAL, swa->adjustment);
	gtk_scale_set_draw_value (GTK_SCALE (slider), FALSE);
	gtk_widget_set_can_focus (slider, FALSE);
	g_signal_connect (G_OBJECT (slider),
		"value_changed",
		G_CALLBACK (cb_adjustment_widget_value_changed), swa);
	g_signal_connect (G_OBJECT (slider), "destroy",
	                  G_CALLBACK (cb_range_destroyed), swa);
	swa->being_updated = FALSE;

	return slider;
}

static void
sheet_widget_slider_user_config (SheetObject *so, SheetControl *sc)
{
           sheet_widget_adjustment_user_config_impl (so, sc, N_("Configure Slider"),
			   N_("Slider Properties"));
}

static void
sheet_widget_slider_horizontal_draw_cairo (SheetObject const *so, cairo_t *cr,
					   double width, double height)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (so);
	GtkAdjustment *adjustment = swa->adjustment;
	double value = gtk_adjustment_get_value (adjustment);
	double upper = gtk_adjustment_get_upper (adjustment);
	double lower = gtk_adjustment_get_lower (adjustment);
	double fraction = (upper == lower) ? 0.0 : (value - lower)/(upper- lower);

	cairo_save (cr);
	cairo_set_line_width (cr, 5);
	cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);

	cairo_new_path (cr);
	cairo_move_to (cr, 4, height/2);
	cairo_rel_line_to (cr, width - 8., 0);
	cairo_stroke (cr);

	cairo_set_line_width (cr, 15);
	cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);

	cairo_new_path (cr);
	cairo_move_to (cr, fraction * (width - 8. - 1. - 5. - 5. + 2.5 + 2.5)
		       - 10. + 10. + 4. + 5. - 2.5, height/2);
	cairo_rel_line_to (cr, 1, 0);
	cairo_stroke (cr);

	cairo_new_path (cr);
	cairo_restore (cr);
}

static void
sheet_widget_slider_vertical_draw_cairo (SheetObject const *so, cairo_t *cr,
					 double width, double height)
{
	cairo_save (cr);
	cairo_rotate (cr, M_PI/2);
	cairo_translate (cr, 0., -width);
	sheet_widget_slider_horizontal_draw_cairo (so, cr, height, width);
	cairo_restore (cr);
}

static void
sheet_widget_slider_draw_cairo (SheetObject const *so, cairo_t *cr,
				double width, double height)
{
	SheetWidgetAdjustment *swa = GNM_SOW_ADJUSTMENT (so);
	if (swa->horizontal)
		sheet_widget_slider_horizontal_draw_cairo (so, cr, width, height);
	else
		sheet_widget_slider_vertical_draw_cairo (so, cr, width, height);
}

static void
sheet_widget_slider_class_init (SheetObjectWidgetClass *sow_class)
{
	SheetWidgetAdjustmentClass *swa_class = (SheetWidgetAdjustmentClass *)sow_class;
	SheetObjectClass *so_class = GNM_SO_CLASS (sow_class);

        sow_class->create_widget = &sheet_widget_slider_create_widget;
	so_class->user_config = &sheet_widget_slider_user_config;
	so_class->draw_cairo = &sheet_widget_slider_draw_cairo;

	swa_class->type = GTK_TYPE_SCALE;
}

GSF_CLASS (SheetWidgetSlider, sheet_widget_slider,
	   &sheet_widget_slider_class_init, NULL,
	   GNM_SOW_ADJUSTMENT_TYPE)

/****************************************************************************/

#define GNM_SOW_CHECKBOX(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_SOW_CHECKBOX_TYPE, SheetWidgetCheckbox))
#define DEP_TO_CHECKBOX(d_ptr)		(SheetWidgetCheckbox *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetCheckbox, dep))

typedef struct {
	SheetObjectWidget	sow;

	GnmDependent	 dep;
	char		*label;
	gboolean	 value;
	gboolean	 being_updated;
} SheetWidgetCheckbox;
typedef SheetObjectWidgetClass SheetWidgetCheckboxClass;

enum {
	SOC_PROP_0 = 0,
	SOC_PROP_ACTIVE,
	SOC_PROP_TEXT,
	SOC_PROP_MARKUP
};

static void
sheet_widget_checkbox_set_active (SheetWidgetCheckbox *swc)
{
	GList *ptr;

	swc->being_updated = TRUE;

	for (ptr = swc->sow.so.realized_list; ptr != NULL ; ptr = ptr->next) {
		SheetObjectView *view = ptr->data;
		GocWidget *item = get_goc_widget (view);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (item->widget),
					      swc->value);
	}

	g_object_notify (G_OBJECT (swc), "active");

	swc->being_updated = FALSE;
}

static void
sheet_widget_checkbox_get_property (GObject *obj, guint param_id,
				    GValue *value, GParamSpec *pspec)
{
	SheetWidgetCheckbox *swc = GNM_SOW_CHECKBOX (obj);

	switch (param_id) {
	case SOC_PROP_ACTIVE:
		g_value_set_boolean (value, swc->value);
		break;
	case SOC_PROP_TEXT:
		g_value_set_string (value, swc->label);
		break;
	case SOC_PROP_MARKUP:
		g_value_set_boxed (value, NULL); /* swc->markup */
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
sheet_widget_checkbox_set_property (GObject *obj, guint param_id,
				    GValue const *value, GParamSpec *pspec)
{
	SheetWidgetCheckbox *swc = GNM_SOW_CHECKBOX (obj);

	switch (param_id) {
	case SOC_PROP_ACTIVE:
		swc->value = g_value_get_boolean (value);
		sheet_widget_checkbox_set_active (swc);
		break;
	case SOC_PROP_TEXT:
		sheet_widget_checkbox_set_label (GNM_SO (swc),
						 g_value_get_string (value));
		break;
	case SOC_PROP_MARKUP:
#if 0
		sheet_widget_checkbox_set_markup (GNM_SO (swc),
						g_value_peek_pointer (value));
#endif
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return;
	}
}

static void
checkbox_eval (GnmDependent *dep)
{
	GnmValue *v;
	GnmEvalPos pos;
	gboolean err, result;

	v = gnm_expr_top_eval (dep->texpr, eval_pos_init_dep (&pos, dep),
			       GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	result = value_get_as_bool (v, &err);
	value_release (v);
	if (!err) {
		SheetWidgetCheckbox *swc = DEP_TO_CHECKBOX(dep);

		swc->value = result;
		sheet_widget_checkbox_set_active (swc);
	}
}

static void
checkbox_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "Checkbox%p", (void *)dep);
}

static DEPENDENT_MAKE_TYPE (checkbox, .eval = checkbox_eval, .debug_name = checkbox_debug_name)

static void
sheet_widget_checkbox_init_full (SheetWidgetCheckbox *swc,
				 GnmCellRef const *ref, char const *label)
{
	static int counter = 0;

	g_return_if_fail (swc != NULL);

	swc->label = label ? g_strdup (label) : g_strdup_printf (_("CheckBox %d"), ++counter);
	swc->being_updated = FALSE;
	swc->value = FALSE;
	swc->dep.sheet = NULL;
	swc->dep.flags = checkbox_get_dep_type ();
	swc->dep.texpr = (ref != NULL)
		? gnm_expr_top_new (gnm_expr_new_cellref (ref))
		: NULL;
}

static void
sheet_widget_checkbox_init (SheetWidgetCheckbox *swc)
{
	sheet_widget_checkbox_init_full (swc, NULL, NULL);
}

static void
sheet_widget_checkbox_finalize (GObject *obj)
{
	SheetWidgetCheckbox *swc = GNM_SOW_CHECKBOX (obj);

	g_return_if_fail (swc != NULL);

	g_free (swc->label);
	swc->label = NULL;

	dependent_set_expr (&swc->dep, NULL);

	sheet_object_widget_class->finalize (obj);
}

static void
cb_checkbox_toggled (GtkToggleButton *button, SheetWidgetCheckbox *swc)
{
	GnmCellRef ref;

	if (swc->being_updated)
		return;
	swc->value = gtk_toggle_button_get_active (button);
	sheet_widget_checkbox_set_active (swc);

	if (so_get_ref (GNM_SO (swc), &ref, TRUE) != NULL) {
		gboolean new_val = gtk_toggle_button_get_active (button);
		cmd_so_set_value (widget_wbc (GTK_WIDGET (button)),
				  /* FIXME: This text sucks:  */
				  _("Clicking checkbox"),
				  &ref, value_new_bool (new_val),
				  sheet_object_get_sheet (GNM_SO (swc)));
	}
}

static GtkWidget *
sheet_widget_checkbox_create_widget (SheetObjectWidget *sow)
{
	SheetWidgetCheckbox *swc = GNM_SOW_CHECKBOX (sow);
	GtkWidget *button;

	g_return_val_if_fail (swc != NULL, NULL);

	button = gtk_check_button_new_with_label (swc->label);
	gtk_widget_set_can_focus (button, FALSE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), swc->value);
	g_signal_connect (G_OBJECT (button),
			  "toggled",
			  G_CALLBACK (cb_checkbox_toggled), swc);

	return button;
}

static void
sheet_widget_checkbox_copy (SheetObject *dst, SheetObject const *src)
{
	SheetWidgetCheckbox const *src_swc = GNM_SOW_CHECKBOX (src);
	SheetWidgetCheckbox       *dst_swc = GNM_SOW_CHECKBOX (dst);
	GnmCellRef ref;
	sheet_widget_checkbox_init_full (dst_swc,
					 so_get_ref (src, &ref, FALSE),
					 src_swc->label);
	dst_swc->value = src_swc->value;
}

typedef struct {
	GtkWidget *dialog;
	GnmExprEntry *expression;
	GtkWidget *label;

	char *old_label;
	GtkWidget *old_focus;

	WBCGtk  *wbcg;
	SheetWidgetCheckbox *swc;
	Sheet		    *sheet;
} CheckboxConfigState;

static void
cb_checkbox_set_focus (G_GNUC_UNUSED GtkWidget *window, GtkWidget *focus_widget,
		       CheckboxConfigState *state)
{
	GtkWidget *ofp;

	/* Note:  half of the set-focus action is handle by the default
	 *        callback installed by wbc_gtk_attach_guru. */

	ofp = state->old_focus
		? gtk_widget_get_parent (state->old_focus)
		: NULL;

	/* Force an update of the content in case it needs tweaking (eg make it
	 * absolute) */
	if (ofp && GNM_EXPR_ENTRY_IS (ofp)) {
		GnmParsePos  pp;
		GnmExprTop const *texpr = gnm_expr_entry_parse (
			GNM_EXPR_ENTRY (ofp),
			parse_pos_init_sheet (&pp, state->sheet),
			NULL, FALSE, GNM_EXPR_PARSE_DEFAULT);
		if (texpr != NULL)
			gnm_expr_top_unref (texpr);
	}
	state->old_focus = focus_widget;
}

static void
cb_checkbox_config_destroy (CheckboxConfigState *state)
{
	g_return_if_fail (state != NULL);

	g_free (state->old_label);
	state->old_label = NULL;
	state->dialog = NULL;
	g_free (state);
}

static void
cb_checkbox_config_ok_clicked (G_GNUC_UNUSED GtkWidget *button, CheckboxConfigState *state)
{
	SheetObject *so = GNM_SO (state->swc);
	GnmParsePos  pp;
	GnmExprTop const *texpr = gnm_expr_entry_parse (state->expression,
		parse_pos_init_sheet (&pp, so->sheet),
		NULL, FALSE, GNM_EXPR_PARSE_DEFAULT);
	gchar const *text = gtk_entry_get_text(GTK_ENTRY(state->label));

	cmd_so_set_checkbox (GNM_WBC (state->wbcg), so,
			     texpr, g_strdup (state->old_label), g_strdup (text));

	gtk_widget_destroy (state->dialog);
}

static void
cb_checkbox_config_cancel_clicked (G_GNUC_UNUSED GtkWidget *button, CheckboxConfigState *state)
{
	sheet_widget_checkbox_set_label	(GNM_SO (state->swc),
					 state->old_label);
	gtk_widget_destroy (state->dialog);
}

static void
cb_checkbox_label_changed (GtkEntry *entry, CheckboxConfigState *state)
{
	sheet_widget_checkbox_set_label	(GNM_SO (state->swc),
					 gtk_entry_get_text (entry));
}

static void
sheet_widget_checkbox_user_config (SheetObject *so, SheetControl *sc)
{
	SheetWidgetCheckbox *swc = GNM_SOW_CHECKBOX (so);
	WBCGtk  *wbcg = scg_wbcg (GNM_SCG (sc));
	CheckboxConfigState *state;
	GtkWidget *grid;
	GtkBuilder *gui;

	g_return_if_fail (swc != NULL);

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
		return;

	gui = gnm_gtk_builder_load ("res:ui/so-checkbox.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (!gui)
		return;
	state = g_new (CheckboxConfigState, 1);
	state->swc = swc;
	state->wbcg = wbcg;
	state->sheet = sc_sheet	(sc);
	state->old_focus = NULL;
	state->old_label = g_strdup (swc->label);
	state->dialog = go_gtk_builder_get_widget (gui, "SO-Checkbox");

	grid = go_gtk_builder_get_widget (gui, "main-grid");

	state->expression = gnm_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (state->expression,
		GNM_EE_FORCE_ABS_REF | GNM_EE_SHEET_OPTIONAL | GNM_EE_SINGLE_RANGE,
		GNM_EE_MASK);
	gnm_expr_entry_load_from_dep (state->expression, &swc->dep);
	go_atk_setup_label (go_gtk_builder_get_widget (gui, "label_linkto"),
			     GTK_WIDGET (state->expression));
	gtk_grid_attach (GTK_GRID (grid),
	                 GTK_WIDGET (state->expression), 1, 0, 1, 1);
	gtk_widget_show (GTK_WIDGET (state->expression));

	state->label = go_gtk_builder_get_widget (gui, "label_entry");
	gtk_entry_set_text (GTK_ENTRY (state->label), swc->label);
	gtk_editable_select_region (GTK_EDITABLE(state->label), 0, -1);
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->expression));
	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->label));

	g_signal_connect (G_OBJECT (state->label),
		"changed",
		G_CALLBACK (cb_checkbox_label_changed), state);
	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "ok_button")),
		"clicked",
		G_CALLBACK (cb_checkbox_config_ok_clicked), state);
	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cb_checkbox_config_cancel_clicked), state);

	gnm_init_help_button (
		go_gtk_builder_get_widget (gui, "help_button"),
		GNUMERIC_HELP_LINK_SO_CHECKBOX);

	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SHEET_OBJECT_CONFIG_KEY);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_checkbox_config_destroy);

	/* Note:  half of the set-focus action is handle by the default */
	/*        callback installed by wbc_gtk_attach_guru */
	g_signal_connect (G_OBJECT (state->dialog), "set-focus",
		G_CALLBACK (cb_checkbox_set_focus), state);
	g_object_unref (gui);

	gtk_widget_show (state->dialog);
}

static gboolean
sheet_widget_checkbox_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetCheckbox *swc = GNM_SOW_CHECKBOX (so);

	dependent_set_sheet (&swc->dep, sheet);
	sheet_widget_checkbox_set_active (swc);

	return FALSE;
}

static void
sheet_widget_checkbox_foreach_dep (SheetObject *so,
				   SheetObjectForeachDepFunc func,
				   gpointer user)
{
	SheetWidgetCheckbox *swc = GNM_SOW_CHECKBOX (so);
	func (&swc->dep, so, user);
}

static void
sheet_widget_checkbox_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
				     GnmConventions const *convs)
{
	SheetWidgetCheckbox const *swc = GNM_SOW_CHECKBOX (so);
	gsf_xml_out_add_cstr (output, "Label", swc->label);
	gsf_xml_out_add_int (output, "Value", swc->value);
	sax_write_dep (output, &swc->dep, "Input", convs);
}

static void
sheet_widget_checkbox_prep_sax_parser (SheetObject *so, GsfXMLIn *xin,
				       xmlChar const **attrs,
				       GnmConventions const *convs)
{
	SheetWidgetCheckbox *swc = GNM_SOW_CHECKBOX (so);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_eq (attrs[0], "Label")) {
			g_free (swc->label);
			swc->label = g_strdup (CXML2C (attrs[1]));
		} else if (gnm_xml_attr_int (attrs, "Value", &swc->value))
			; /* ??? */
		else if (sax_read_dep (attrs, "Input", &swc->dep, xin, convs))
			; /* ??? */
}

void
sheet_widget_checkbox_set_link (SheetObject *so, GnmExprTop const *texpr)
{
	SheetWidgetCheckbox *swc = GNM_SOW_CHECKBOX (so);
	dependent_set_expr (&swc->dep, texpr);
	if (texpr && swc->dep.sheet)
		dependent_link (&swc->dep);
}

GnmExprTop const *
sheet_widget_checkbox_get_link	 (SheetObject *so)
{
	SheetWidgetCheckbox *swc = GNM_SOW_CHECKBOX (so);
	GnmExprTop const *texpr = swc->dep.texpr;

	if (texpr)
		gnm_expr_top_ref (texpr);

	return texpr;
}


void
sheet_widget_checkbox_set_label	(SheetObject *so, char const *str)
{
	GList *list;
	SheetWidgetCheckbox *swc = GNM_SOW_CHECKBOX (so);
	char *new_label;

	if (go_str_compare (str, swc->label) == 0)
		return;

	new_label = g_strdup (str);
	g_free (swc->label);
	swc->label = new_label;

	for (list = swc->sow.so.realized_list; list; list = list->next) {
		SheetObjectView *view = list->data;
		GocWidget *item = get_goc_widget (view);
		gtk_button_set_label (GTK_BUTTON (item->widget), swc->label);
	}
}

static void
sheet_widget_checkbox_draw_cairo (SheetObject const *so, cairo_t *cr,
				  double width, double height)
{
	SheetWidgetCheckbox const *swc = GNM_SOW_CHECKBOX (so);
	double halfheight = height/2;
	double dx = 8., dxh, pm;
	int pw, ph;

	pm = MIN (height - 2, width - 12);
	if (dx > pm)
		dx = MAX (pm, 3);
	dxh = dx/2;

	cairo_save (cr);
	cairo_set_line_width (cr, 0.5);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);

	cairo_new_path (cr);
	cairo_move_to (cr, dxh, halfheight - dxh);
	cairo_rel_line_to (cr, 0, dx);
	cairo_rel_line_to (cr, dx, 0);
	cairo_rel_line_to (cr, 0., -dx);
	cairo_rel_line_to (cr, -dx, 0.);
	cairo_close_path (cr);
	cairo_fill_preserve (cr);
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	cairo_stroke (cr);

	if (swc->value) {
		cairo_new_path (cr);
		cairo_move_to (cr, dxh, halfheight - dxh);
		cairo_rel_line_to (cr, dx, dx);
		cairo_rel_line_to (cr, -dx, 0.);
		cairo_rel_line_to (cr, dx, -dx);
		cairo_rel_line_to (cr, -dx, 0.);
		cairo_close_path (cr);
		cairo_set_line_join (cr, CAIRO_LINE_JOIN_BEVEL);
		cairo_stroke (cr);
	}

	cairo_move_to (cr, 2 * dx, halfheight);

	pw = width - 2 * dx;
	ph = height;

	draw_cairo_text (cr, swc->label, &pw, &ph, TRUE, FALSE, TRUE, 0, TRUE);

	cairo_new_path (cr);
	cairo_restore (cr);
}


SOW_MAKE_TYPE (checkbox, Checkbox,
	       sheet_widget_checkbox_user_config,
	       sheet_widget_checkbox_set_sheet,
	       so_clear_sheet,
	       sheet_widget_checkbox_foreach_dep,
	       sheet_widget_checkbox_copy,
	       sheet_widget_checkbox_write_xml_sax,
	       sheet_widget_checkbox_prep_sax_parser,
	       sheet_widget_checkbox_get_property,
	       sheet_widget_checkbox_set_property,
	       sheet_widget_checkbox_draw_cairo,
	       {
		       g_object_class_install_property
			       (object_class, SOC_PROP_ACTIVE,
				g_param_spec_boolean ("active", NULL, NULL,
						      FALSE,
						      GSF_PARAM_STATIC | G_PARAM_READWRITE));
		       g_object_class_install_property
			       (object_class, SOC_PROP_TEXT,
				g_param_spec_string ("text", NULL, NULL, NULL,
						     GSF_PARAM_STATIC | G_PARAM_READWRITE));
		       g_object_class_install_property
			       (object_class, SOC_PROP_MARKUP,
				g_param_spec_boxed ("markup", NULL, NULL, PANGO_TYPE_ATTR_LIST,
						    GSF_PARAM_STATIC | G_PARAM_READWRITE));
	       })

/****************************************************************************/
typedef SheetWidgetCheckbox		SheetWidgetToggleButton;
typedef SheetWidgetCheckboxClass	SheetWidgetToggleButtonClass;
static GtkWidget *
sheet_widget_toggle_button_create_widget (SheetObjectWidget *sow)
{
	SheetWidgetCheckbox *swc = GNM_SOW_CHECKBOX (sow);
	GtkWidget *button = gtk_toggle_button_new_with_label (swc->label);
	gtk_widget_set_can_focus (button, FALSE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), swc->value);
	g_signal_connect (G_OBJECT (button),
		"toggled",
		G_CALLBACK (cb_checkbox_toggled), swc);
	return button;
}
static void
sheet_widget_toggle_button_class_init (SheetObjectWidgetClass *sow_class)
{
        sow_class->create_widget = &sheet_widget_toggle_button_create_widget;
}

GSF_CLASS (SheetWidgetToggleButton, sheet_widget_toggle_button,
	   &sheet_widget_toggle_button_class_init, NULL,
	   GNM_SOW_CHECKBOX_TYPE)

/****************************************************************************/

#define GNM_SOW_RADIO_BUTTON(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_SOW_RADIO_BUTTON_TYPE, SheetWidgetRadioButton))
#define DEP_TO_RADIO_BUTTON(d_ptr)	(SheetWidgetRadioButton *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetRadioButton, dep))

typedef struct {
	SheetObjectWidget sow;

	gboolean	 being_updated;
	char		*label;
	GnmValue        *value;
	gboolean	 active;
	GnmDependent	 dep;
} SheetWidgetRadioButton;
typedef SheetObjectWidgetClass SheetWidgetRadioButtonClass;

enum {
	SOR_PROP_0 = 0,
	SOR_PROP_ACTIVE,
	SOR_PROP_TEXT,
	SOR_PROP_MARKUP,
	SOR_PROP_VALUE
};

static void
sheet_widget_radio_button_set_active (SheetWidgetRadioButton *swrb,
				      gboolean active)
{
	GList *ptr;

	if (swrb->active == active)
		return;
	swrb->active = active;

	swrb->being_updated = TRUE;

	for (ptr = swrb->sow.so.realized_list; ptr != NULL ; ptr = ptr->next) {
		SheetObjectView *view = ptr->data;
		GocWidget *item = get_goc_widget (view);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (item->widget),
					      active);
	}

	g_object_notify (G_OBJECT (swrb), "active");

	swrb->being_updated = FALSE;
}


static void
sheet_widget_radio_button_get_property (GObject *obj, guint param_id,
					GValue *value, GParamSpec *pspec)
{
	SheetWidgetRadioButton *swrb = GNM_SOW_RADIO_BUTTON (obj);

	switch (param_id) {
	case SOR_PROP_ACTIVE:
		g_value_set_boolean (value, swrb->active);
		break;
	case SOR_PROP_TEXT:
		g_value_set_string (value, swrb->label);
		break;
	case SOR_PROP_MARKUP:
		g_value_set_boxed (value, NULL); /* swrb->markup */
		break;
	case SOR_PROP_VALUE:
		g_value_set_boxed (value, swrb->value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
sheet_widget_radio_button_set_property (GObject *obj, guint param_id,
					GValue const *value, GParamSpec *pspec)
{
	SheetWidgetRadioButton *swrb = GNM_SOW_RADIO_BUTTON (obj);

	switch (param_id) {
	case SOR_PROP_ACTIVE:
		sheet_widget_radio_button_set_active (swrb,
						      g_value_get_boolean (value));
		break;
	case SOR_PROP_TEXT:
		sheet_widget_radio_button_set_label (GNM_SO (swrb),
						     g_value_get_string (value));
		break;
	case SOR_PROP_MARKUP:
#if 0
		sheet_widget_radio_button_set_markup (GNM_SO (swrb),
						      g_value_peek_pointer (value));
#endif
		break;
	case SOR_PROP_VALUE:
		sheet_widget_radio_button_set_value (GNM_SO (swrb),
						     g_value_get_boxed (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return;
	}
}

GnmValue const *
sheet_widget_radio_button_get_value (SheetObject *so)
{
	SheetWidgetRadioButton *swrb;

	g_return_val_if_fail (GNM_IS_SOW_RADIO_BUTTON (so), NULL);

	swrb = GNM_SOW_RADIO_BUTTON (so);
	return swrb->value;
}

void
sheet_widget_radio_button_set_value (SheetObject *so, GnmValue const *val)
{
	SheetWidgetRadioButton *swrb = GNM_SOW_RADIO_BUTTON (so);

	value_release (swrb->value);
	swrb->value = value_dup (val);
}

static void
radio_button_eval (GnmDependent *dep)
{
	GnmValue *v;
	GnmEvalPos pos;
	SheetWidgetRadioButton *swrb = DEP_TO_RADIO_BUTTON (dep);

	v = gnm_expr_top_eval (dep->texpr, eval_pos_init_dep (&pos, dep),
			       GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	if (v && swrb->value) {
		gboolean active = value_equal (swrb->value, v);
		sheet_widget_radio_button_set_active (swrb, active);
	}
	value_release (v);
}

static void
radio_button_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "RadioButton%p", (void *)dep);
}

static DEPENDENT_MAKE_TYPE (radio_button, .eval = radio_button_eval, .debug_name = radio_button_debug_name)

static void
sheet_widget_radio_button_init_full (SheetWidgetRadioButton *swrb,
				     GnmCellRef const *ref,
				     char const *label,
				     GnmValue const *value,
				     gboolean active)
{
	g_return_if_fail (swrb != NULL);

	swrb->being_updated = FALSE;
	swrb->label = g_strdup (label ? label : _("RadioButton"));
	swrb->value = value ? value_dup (value) : value_new_empty ();
	swrb->active = active;

	swrb->dep.sheet = NULL;
	swrb->dep.flags = radio_button_get_dep_type ();
	swrb->dep.texpr = (ref != NULL)
		? gnm_expr_top_new (gnm_expr_new_cellref (ref))
		: NULL;
}

static void
sheet_widget_radio_button_init (SheetWidgetRadioButton *swrb)
{
	sheet_widget_radio_button_init_full (swrb, NULL, NULL, NULL, TRUE);
}

static void
sheet_widget_radio_button_finalize (GObject *obj)
{
	SheetWidgetRadioButton *swrb = GNM_SOW_RADIO_BUTTON (obj);

	g_return_if_fail (swrb != NULL);

	g_free (swrb->label);
	swrb->label = NULL;
	value_release (swrb->value);
	swrb->value = NULL;

	dependent_set_expr (&swrb->dep, NULL);

	sheet_object_widget_class->finalize (obj);
}

static void
sheet_widget_radio_button_toggled (GtkToggleButton *button,
				   SheetWidgetRadioButton *swrb)
{
	GnmCellRef ref;

	if (swrb->being_updated)
		return;
	swrb->active = gtk_toggle_button_get_active (button);

	if (so_get_ref (GNM_SO (swrb), &ref, TRUE) != NULL) {
		cmd_so_set_value (widget_wbc (GTK_WIDGET (button)),
				  /* FIXME: This text sucks:  */
				  _("Clicking radiobutton"),
				  &ref, value_dup (swrb->value),
				  sheet_object_get_sheet (GNM_SO (swrb)));
	}
}

static GtkWidget *
sheet_widget_radio_button_create_widget (SheetObjectWidget *sow)
{
	SheetWidgetRadioButton *swrb = GNM_SOW_RADIO_BUTTON (sow);
	GtkWidget *w = g_object_new (GNM_TYPE_RADIO_BUTTON,
				     "label", swrb->label,
				     NULL) ;

	gtk_widget_set_can_focus (w, FALSE);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), swrb->active);

	g_signal_connect (G_OBJECT (w),
			  "toggled",
			  G_CALLBACK (sheet_widget_radio_button_toggled), sow);
	return w;
}

static void
sheet_widget_radio_button_copy (SheetObject *dst, SheetObject const *src)
{
	SheetWidgetRadioButton const *src_swrb = GNM_SOW_RADIO_BUTTON (src);
	SheetWidgetRadioButton       *dst_swrb = GNM_SOW_RADIO_BUTTON (dst);
	GnmCellRef ref;

	sheet_widget_radio_button_init_full (dst_swrb,
					     so_get_ref (src, &ref, FALSE),
					     src_swrb->label,
					     src_swrb->value,
					     src_swrb->active);
}

static gboolean
sheet_widget_radio_button_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetRadioButton *swrb = GNM_SOW_RADIO_BUTTON (so);

	dependent_set_sheet (&swrb->dep, sheet);

	return FALSE;
}

static void
sheet_widget_radio_button_foreach_dep (SheetObject *so,
				       SheetObjectForeachDepFunc func,
				       gpointer user)
{
	SheetWidgetRadioButton *swrb = GNM_SOW_RADIO_BUTTON (so);
	func (&swrb->dep, so, user);
}

static void
sheet_widget_radio_button_write_xml_sax (SheetObject const *so,
					 GsfXMLOut *output,
					 GnmConventions const *convs)
{
	SheetWidgetRadioButton const *swrb = GNM_SOW_RADIO_BUTTON (so);
	GString *valstr = g_string_new (NULL);

	value_get_as_gstring (swrb->value, valstr, convs);

	gsf_xml_out_add_cstr (output, "Label", swrb->label);
	gsf_xml_out_add_cstr (output, "Value", valstr->str);
	gsf_xml_out_add_int (output, "ValueType", swrb->value->v_any.type);
	gsf_xml_out_add_int (output, "Active", swrb->active);
	sax_write_dep (output, &swrb->dep, "Input", convs);

	g_string_free (valstr, TRUE);
}

static void
sheet_widget_radio_button_prep_sax_parser (SheetObject *so, GsfXMLIn *xin,
					   xmlChar const **attrs,
					   GnmConventions const *convs)
{
	SheetWidgetRadioButton *swrb = GNM_SOW_RADIO_BUTTON (so);
	const char *valstr = NULL;
	int value_type = 0;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_eq (attrs[0], "Label")) {
			g_free (swrb->label);
			swrb->label = g_strdup (CXML2C (attrs[1]));
		} else if (attr_eq (attrs[0], "Value")) {
			valstr = CXML2C (attrs[1]);
		} else if (gnm_xml_attr_bool (attrs, "Active", &swrb->active) ||
			   gnm_xml_attr_int (attrs, "ValueType", &value_type) ||
			   sax_read_dep (attrs, "Input", &swrb->dep, xin, convs))
			; /* Nothing */
	}

	value_release (swrb->value);
	swrb->value = NULL;
	if (valstr) {
		swrb->value = value_type
			? value_new_from_string (value_type, valstr, NULL, FALSE)
			: format_match (valstr, NULL, NULL);
	}
	if (!swrb->value)
		swrb->value = value_new_empty ();
}

void
sheet_widget_radio_button_set_link (SheetObject *so, GnmExprTop const *texpr)
{
	SheetWidgetRadioButton *swrb = GNM_SOW_RADIO_BUTTON (so);
	dependent_set_expr (&swrb->dep, texpr);
	if (texpr && swrb->dep.sheet)
		dependent_link (&swrb->dep);
}

GnmExprTop const *
sheet_widget_radio_button_get_link (SheetObject *so)
{
	SheetWidgetRadioButton *swrb = GNM_SOW_RADIO_BUTTON (so);
	GnmExprTop const *texpr = swrb->dep.texpr;

	if (texpr)
		gnm_expr_top_ref (texpr);

	return texpr;
}

void
sheet_widget_radio_button_set_label (SheetObject *so, char const *str)
{
	GList *list;
	SheetWidgetRadioButton *swrb = GNM_SOW_RADIO_BUTTON (so);
	char *new_label;

	if (go_str_compare (str, swrb->label) == 0)
		return;

	new_label = g_strdup (str);
	g_free (swrb->label);
	swrb->label = new_label;

	for (list = swrb->sow.so.realized_list; list; list = list->next) {
		SheetObjectView *view = list->data;
		GocWidget *item = get_goc_widget (view);
		gtk_button_set_label (GTK_BUTTON (item->widget), swrb->label);
	}
}


typedef struct {
 	GtkWidget *dialog;
 	GnmExprEntry *expression;
 	GtkWidget *label, *value;

 	char *old_label;
	GnmValue *old_value;
 	GtkWidget *old_focus;

 	WBCGtk  *wbcg;
 	SheetWidgetRadioButton *swrb;
 	Sheet		    *sheet;
} RadioButtonConfigState;

static void
cb_radio_button_set_focus (G_GNUC_UNUSED GtkWidget *window, GtkWidget *focus_widget,
 			   RadioButtonConfigState *state)
{
	GtkWidget *ofp;

 	/* Note:  half of the set-focus action is handle by the default
 	 *        callback installed by wbc_gtk_attach_guru */

	ofp = state->old_focus
		? gtk_widget_get_parent (state->old_focus)
		: NULL;

 	/* Force an update of the content in case it needs tweaking (eg make it
 	 * absolute) */
 	if (ofp && GNM_EXPR_ENTRY_IS (ofp)) {
 		GnmParsePos  pp;
 		GnmExprTop const *texpr = gnm_expr_entry_parse (
 			GNM_EXPR_ENTRY (ofp),
 			parse_pos_init_sheet (&pp, state->sheet),
 			NULL, FALSE, GNM_EXPR_PARSE_DEFAULT);
 		if (texpr != NULL)
 			gnm_expr_top_unref (texpr);
  	}
 	state->old_focus = focus_widget;
}

static void
cb_radio_button_config_destroy (RadioButtonConfigState *state)
{
 	g_return_if_fail (state != NULL);

 	g_free (state->old_label);
 	state->old_label = NULL;

 	value_release (state->old_value);
 	state->old_value = NULL;

 	state->dialog = NULL;

 	g_free (state);
}

static GnmValue *
so_parse_value (SheetObject *so, const char *s)
{
	Sheet *sheet = so->sheet;
	return format_match (s, NULL, sheet_date_conv (sheet));
}

static void
cb_radio_button_config_ok_clicked (G_GNUC_UNUSED GtkWidget *button, RadioButtonConfigState *state)
{
	SheetObject *so = GNM_SO (state->swrb);
	GnmParsePos  pp;
 	GnmExprTop const *texpr = gnm_expr_entry_parse
		(state->expression,
		 parse_pos_init_sheet (&pp, so->sheet),
		 NULL, FALSE, GNM_EXPR_PARSE_DEFAULT);
 	gchar const *text = gtk_entry_get_text (GTK_ENTRY (state->label));
 	gchar const *val = gtk_entry_get_text (GTK_ENTRY (state->value));
	GnmValue *new_val = so_parse_value (so, val);

 	cmd_so_set_radio_button (GNM_WBC (state->wbcg), so,
 				 texpr,
				 g_strdup (state->old_label), g_strdup (text),
				 value_dup (state->old_value), new_val);

 	gtk_widget_destroy (state->dialog);
}

static void
cb_radio_button_config_cancel_clicked (G_GNUC_UNUSED GtkWidget *button, RadioButtonConfigState *state)
{
 	sheet_widget_radio_button_set_label (GNM_SO (state->swrb),
 					     state->old_label);
 	sheet_widget_radio_button_set_value (GNM_SO (state->swrb),
 					     state->old_value);
 	gtk_widget_destroy (state->dialog);
}

static void
cb_radio_button_label_changed (GtkEntry *entry, RadioButtonConfigState *state)
{
 	sheet_widget_radio_button_set_label (GNM_SO (state->swrb),
 					     gtk_entry_get_text (entry));
}

static void
cb_radio_button_value_changed (GtkEntry *entry, RadioButtonConfigState *state)
{
	const char *text = gtk_entry_get_text (entry);
	SheetObject *so = GNM_SO (state->swrb);
	GnmValue *val = so_parse_value (so, text);

 	sheet_widget_radio_button_set_value (so, val);
	value_release (val);
}

static void
sheet_widget_radio_button_user_config (SheetObject *so, SheetControl *sc)
{
 	SheetWidgetRadioButton *swrb = GNM_SOW_RADIO_BUTTON (so);
 	WBCGtk  *wbcg = scg_wbcg (GNM_SCG (sc));
 	RadioButtonConfigState *state;
 	GtkWidget *grid;
	GString *valstr;
	GtkBuilder *gui;

 	g_return_if_fail (swrb != NULL);

	/* Only pop up one copy per workbook */
 	if (gnm_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
 		return;

	gui = gnm_gtk_builder_load ("res:ui/so-radiobutton.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (!gui)
		return;
 	state = g_new (RadioButtonConfigState, 1);
 	state->swrb = swrb;
 	state->wbcg = wbcg;
 	state->sheet = sc_sheet	(sc);
 	state->old_focus = NULL;
	state->old_label = g_strdup (swrb->label);
 	state->old_value = value_dup (swrb->value);
 	state->dialog = go_gtk_builder_get_widget (gui, "SO-Radiobutton");

 	grid = go_gtk_builder_get_widget (gui, "main-grid");

 	state->expression = gnm_expr_entry_new (wbcg, TRUE);
 	gnm_expr_entry_set_flags (state->expression,
				  GNM_EE_FORCE_ABS_REF | GNM_EE_SHEET_OPTIONAL | GNM_EE_SINGLE_RANGE,
				  GNM_EE_MASK);
 	gnm_expr_entry_load_from_dep (state->expression, &swrb->dep);
 	go_atk_setup_label (go_gtk_builder_get_widget (gui, "label_linkto"),
			    GTK_WIDGET (state->expression));
 	gtk_grid_attach (GTK_GRID (grid),
	                  GTK_WIDGET (state->expression), 1, 0, 1, 1);
 	gtk_widget_show (GTK_WIDGET (state->expression));

 	state->label = go_gtk_builder_get_widget (gui, "label_entry");
 	gtk_entry_set_text (GTK_ENTRY (state->label), swrb->label);
 	gtk_editable_select_region (GTK_EDITABLE(state->label), 0, -1);
 	state->value = go_gtk_builder_get_widget (gui, "value_entry");

	valstr = g_string_new (NULL);
	value_get_as_gstring (swrb->value, valstr, so->sheet->convs);
 	gtk_entry_set_text (GTK_ENTRY (state->value), valstr->str);
	g_string_free (valstr, TRUE);

  	gnm_editable_enters (GTK_WINDOW (state->dialog),
 				  GTK_WIDGET (state->expression));
 	gnm_editable_enters (GTK_WINDOW (state->dialog),
 				  GTK_WIDGET (state->label));
 	gnm_editable_enters (GTK_WINDOW (state->dialog),
 				  GTK_WIDGET (state->value));

 	g_signal_connect (G_OBJECT (state->label),
			  "changed",
			  G_CALLBACK (cb_radio_button_label_changed), state);
 	g_signal_connect (G_OBJECT (state->value),
			  "changed",
			  G_CALLBACK (cb_radio_button_value_changed), state);
 	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "ok_button")),
			  "clicked",
			  G_CALLBACK (cb_radio_button_config_ok_clicked), state);
 	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "cancel_button")),
			  "clicked",
			  G_CALLBACK (cb_radio_button_config_cancel_clicked), state);

 	gnm_init_help_button (
 		go_gtk_builder_get_widget (gui, "help_button"),
 		GNUMERIC_HELP_LINK_SO_RADIO_BUTTON);

 	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
 			       SHEET_OBJECT_CONFIG_KEY);

 	wbc_gtk_attach_guru (state->wbcg, state->dialog);
 	g_object_set_data_full (G_OBJECT (state->dialog),
				"state", state, (GDestroyNotify) cb_radio_button_config_destroy);
	g_object_unref (gui);

	/* Note:  half of the set-focus action is handle by the default */
 	/*        callback installed by wbc_gtk_attach_guru */
 	g_signal_connect (G_OBJECT (state->dialog), "set-focus",
			  G_CALLBACK (cb_radio_button_set_focus), state);

 	gtk_widget_show (state->dialog);
}

static void
sheet_widget_radio_button_draw_cairo (SheetObject const *so, cairo_t *cr,
				      G_GNUC_UNUSED double width, double height)
{
	SheetWidgetRadioButton const *swr = GNM_SOW_RADIO_BUTTON (so);
	double halfheight = height/2;
	double dx = 8., dxh, pm;
	int pw, ph;

	pm = MIN (height - 2, width - 12);
	if (dx > pm)
		dx = MAX (pm, 3);
	dxh = dx/2;

	cairo_save (cr);
	cairo_set_line_width (cr, 0.5);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);

	cairo_new_path (cr);
	cairo_move_to (cr, dxh + dx, halfheight);
	cairo_arc (cr, dx, halfheight, dxh, 0., 2*M_PI);
	cairo_close_path (cr);
	cairo_fill_preserve (cr);
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	cairo_stroke (cr);

	if (swr->active) {
		cairo_new_path (cr);
		cairo_move_to (cr, dx + dxh/2 + 0.5, halfheight);
		cairo_arc (cr, dx, halfheight, dxh/2 + 0.5, 0., 2*M_PI);
		cairo_close_path (cr);
		cairo_fill (cr);
	}

	cairo_move_to (cr, 2 * dx, halfheight);

	pw = width - 2 * dx;
	ph = height;

	draw_cairo_text (cr, swr->label, &pw, &ph, TRUE, FALSE, TRUE, 0, TRUE);

	cairo_new_path (cr);
	cairo_restore (cr);
}

SOW_MAKE_TYPE (radio_button, RadioButton,
 	       sheet_widget_radio_button_user_config,
  	       sheet_widget_radio_button_set_sheet,
  	       so_clear_sheet,
  	       sheet_widget_radio_button_foreach_dep,
 	       sheet_widget_radio_button_copy,
 	       sheet_widget_radio_button_write_xml_sax,
 	       sheet_widget_radio_button_prep_sax_parser,
  	       sheet_widget_radio_button_get_property,
  	       sheet_widget_radio_button_set_property,
	       sheet_widget_radio_button_draw_cairo,
	       {
		       g_object_class_install_property
			       (object_class, SOR_PROP_ACTIVE,
				g_param_spec_boolean ("active", NULL, NULL,
						      FALSE,
						      GSF_PARAM_STATIC | G_PARAM_READWRITE));
		       g_object_class_install_property
			       (object_class, SOR_PROP_TEXT,
				g_param_spec_string ("text", NULL, NULL, NULL,
						     GSF_PARAM_STATIC | G_PARAM_READWRITE));
		       g_object_class_install_property
			       (object_class, SOR_PROP_MARKUP,
				g_param_spec_boxed ("markup", NULL, NULL, PANGO_TYPE_ATTR_LIST,
						    GSF_PARAM_STATIC | G_PARAM_READWRITE));
		       g_object_class_install_property
			       (object_class, SOR_PROP_VALUE,
				g_param_spec_boxed ("value", NULL, NULL,
						    gnm_value_get_type (),
						    GSF_PARAM_STATIC | G_PARAM_READWRITE));
	       })

/****************************************************************************/

#define GNM_SOW_LIST_BASE_TYPE     (sheet_widget_list_base_get_type ())
#define GNM_SOW_LIST_BASE(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_SOW_LIST_BASE_TYPE, SheetWidgetListBase))
#define DEP_TO_LIST_BASE_CONTENT(d_ptr)	(SheetWidgetListBase *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetListBase, content_dep))
#define DEP_TO_LIST_BASE_OUTPUT(d_ptr)	(SheetWidgetListBase *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetListBase, output_dep))

typedef struct {
	SheetObjectWidget	sow;

	GnmDependent	content_dep;	/* content of the list */
	GnmDependent	output_dep;	/* selected element */

	GtkTreeModel	*model;
	int		 selection;
	gboolean        result_as_index;
} SheetWidgetListBase;
typedef struct {
	SheetObjectWidgetClass base;

	void (*model_changed)     (SheetWidgetListBase *list);
	void (*selection_changed) (SheetWidgetListBase *list);
} SheetWidgetListBaseClass;

enum {
	LIST_BASE_MODEL_CHANGED,
	LIST_BASE_SELECTION_CHANGED,
	LIST_BASE_LAST_SIGNAL
};

static guint list_base_signals [LIST_BASE_LAST_SIGNAL] = { 0 };
static GType sheet_widget_list_base_get_type (void);

static void
sheet_widget_list_base_set_selection (SheetWidgetListBase *swl, int selection,
				      WorkbookControl *wbc)
{
	GnmCellRef ref;

	if (selection >= 0 && swl->model != NULL) {
		int n = gtk_tree_model_iter_n_children (swl->model, NULL);
		if (selection > n)
			selection = n;
	} else
		selection = 0;

	if (swl->selection != selection) {
		swl->selection = selection;
		if (NULL!= wbc &&
		    so_get_ref (GNM_SO (swl), &ref, TRUE) != NULL) {
			GnmValue *v;
			if (swl->result_as_index)
				v = value_new_int (swl->selection);
			else if (selection != 0) {
				GtkTreeIter iter;
				char *content;
				gtk_tree_model_iter_nth_child
					(swl->model, &iter, NULL, selection - 1);
				gtk_tree_model_get (swl->model, &iter,
						    0, &content, -1);
				v = value_new_string_nocopy (content);
			} else
				v = value_new_string ("");
			cmd_so_set_value (wbc, _("Clicking in list"), &ref, v,
					  sheet_object_get_sheet (GNM_SO (swl)));
		}
		g_signal_emit (G_OBJECT (swl),
			list_base_signals [LIST_BASE_SELECTION_CHANGED], 0);
	}
}

static void
sheet_widget_list_base_set_selection_value (SheetWidgetListBase *swl, GnmValue *v)
{
	GtkTreeIter iter;
	int selection = 0, i = 1;

	if (swl->model != NULL && gtk_tree_model_get_iter_first (swl->model, &iter)) {
		char *str = value_get_as_string (v);
		do {
			char *content;
			gboolean match;
			gtk_tree_model_get (swl->model, &iter,
					    0, &content, -1);
			match = 0 == g_ascii_strcasecmp (str, content);
			g_free (content);
			if (match) {
				selection = i;
				break;
			}
			i++;
		} while (gtk_tree_model_iter_next (swl->model, &iter));
		g_free (str);
	}

	if (swl->selection != selection) {
		swl->selection = selection;
		g_signal_emit (G_OBJECT (swl),
			list_base_signals [LIST_BASE_SELECTION_CHANGED], 0);
	}
}

static void
list_output_eval (GnmDependent *dep)
{
	GnmEvalPos pos;
	GnmValue *v = gnm_expr_top_eval (dep->texpr,
		eval_pos_init_dep (&pos, dep),
		GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	SheetWidgetListBase *swl = DEP_TO_LIST_BASE_OUTPUT (dep);

	if (swl->result_as_index)
		sheet_widget_list_base_set_selection
			(swl, floor (value_get_as_float (v)), NULL);
	else
		sheet_widget_list_base_set_selection_value (swl, v);
	value_release (v);
}

static void
list_output_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "ListOutput%p", (void *)dep);
}

static DEPENDENT_MAKE_TYPE (list_output, .eval = list_output_eval, .debug_name = list_output_debug_name)

/*-----------*/
static GnmValue *
cb_collect (GnmValueIter const *iter, GtkListStore *model)
{
	GtkTreeIter list_iter;

	gtk_list_store_append (model, &list_iter);
	if (NULL != iter->v) {
		GOFormat const *fmt = (NULL != iter->cell_iter)
			? gnm_cell_get_format (iter->cell_iter->cell) : NULL;
		char *label = format_value (fmt, iter->v, -1, NULL);
		gtk_list_store_set (model, &list_iter, 0, label, -1);
		g_free (label);
	} else
		gtk_list_store_set (model, &list_iter, 0, "", -1);

	return NULL;
}
static void
list_content_eval (GnmDependent *dep)
{
	SheetWidgetListBase *swl = DEP_TO_LIST_BASE_CONTENT (dep);
	GnmEvalPos ep;
	GnmValue *v = NULL;
	GtkListStore *model;

	if (dep->texpr != NULL) {
		v = gnm_expr_top_eval (dep->texpr,
				       eval_pos_init_dep (&ep, dep),
				       GNM_EXPR_EVAL_PERMIT_NON_SCALAR |
				       GNM_EXPR_EVAL_PERMIT_EMPTY);
	}
	model = gtk_list_store_new (1, G_TYPE_STRING);
	if (v) {
		value_area_foreach (v, &ep, CELL_ITER_ALL,
				    (GnmValueIterFunc) cb_collect, model);
		value_release (v);
	}

	if (NULL != swl->model)
		g_object_unref (swl->model);
	swl->model = GTK_TREE_MODEL (model);
	g_signal_emit (G_OBJECT (swl), list_base_signals [LIST_BASE_MODEL_CHANGED], 0);
}

static void
list_content_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "ListContent%p", (void *)dep);
}

static DEPENDENT_MAKE_TYPE (list_content, .eval = list_content_eval, .debug_name = list_content_debug_name)

/*-----------*/

static void
sheet_widget_list_base_init (SheetObjectWidget *sow)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (sow);
	SheetObject *so = GNM_SO (sow);

	so->flags &= ~SHEET_OBJECT_PRINT;

	swl->content_dep.sheet = NULL;
	swl->content_dep.flags = list_content_get_dep_type ();
	swl->content_dep.texpr = NULL;

	swl->output_dep.sheet = NULL;
	swl->output_dep.flags = list_output_get_dep_type ();
	swl->output_dep.texpr = NULL;

	swl->model = NULL;
	swl->selection = 0;
	swl->result_as_index = TRUE;
}

static void
sheet_widget_list_base_finalize (GObject *obj)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (obj);
	dependent_set_expr (&swl->content_dep, NULL);
	dependent_set_expr (&swl->output_dep, NULL);
	if (swl->model != NULL) {
		g_object_unref (swl->model);
		swl->model = NULL;
	}
	sheet_object_widget_class->finalize (obj);
}

static void
sheet_widget_list_base_user_config (SheetObject *so, SheetControl *sc)
{
	dialog_so_list (scg_wbcg (GNM_SCG (sc)), G_OBJECT (so));
}
static gboolean
sheet_widget_list_base_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (so);

	g_return_val_if_fail (swl != NULL, TRUE);
	g_return_val_if_fail (swl->content_dep.sheet == NULL, TRUE);
	g_return_val_if_fail (swl->output_dep.sheet == NULL, TRUE);

	dependent_set_sheet (&swl->content_dep, sheet);
	dependent_set_sheet (&swl->output_dep, sheet);

	list_content_eval (&swl->content_dep); /* populate the list */

	return FALSE;
}

static void
sheet_widget_list_base_foreach_dep (SheetObject *so,
				    SheetObjectForeachDepFunc func,
				    gpointer user)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (so);
	func (&swl->content_dep, so, user);
	func (&swl->output_dep, so, user);
}

static void
sheet_widget_list_base_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
				      GnmConventions const *convs)
{
	SheetWidgetListBase const *swl = GNM_SOW_LIST_BASE (so);
	sax_write_dep (output, &swl->content_dep, "Content", convs);
	sax_write_dep (output, &swl->output_dep, "Output", convs);
	gsf_xml_out_add_int (output, "OutputAsIndex", swl->result_as_index ? 1 : 0);
}

static void
sheet_widget_list_base_prep_sax_parser (SheetObject *so, GsfXMLIn *xin,
					xmlChar const **attrs,
					GnmConventions const *convs)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (so);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (sax_read_dep (attrs, "Content", &swl->content_dep, xin, convs))
			;
		else if (sax_read_dep (attrs, "Output", &swl->output_dep, xin, convs))
			;
		else if (gnm_xml_attr_bool (attrs, "OutputAsIndex", &swl->result_as_index))
			;
}

static GtkWidget *
sheet_widget_list_base_create_widget (G_GNUC_UNUSED SheetObjectWidget *sow)
{
	g_warning("ERROR: sheet_widget_list_base_create_widget SHOULD NEVER BE CALLED (but it has been)!\n");
	return gtk_frame_new ("invisiwidget(WARNING: I AM A BUG!)");
}

SOW_MAKE_TYPE (list_base, ListBase,
	       sheet_widget_list_base_user_config,
	       sheet_widget_list_base_set_sheet,
	       so_clear_sheet,
	       sheet_widget_list_base_foreach_dep,
	       NULL,
	       sheet_widget_list_base_write_xml_sax,
	       sheet_widget_list_base_prep_sax_parser,
	       NULL,
	       NULL,
	       sheet_widget_draw_cairo,
	       {
	       list_base_signals[LIST_BASE_MODEL_CHANGED] = g_signal_new ("model-changed",
			GNM_SOW_LIST_BASE_TYPE,
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (SheetWidgetListBaseClass, model_changed),
			NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);
	       list_base_signals[LIST_BASE_SELECTION_CHANGED] = g_signal_new ("selection-changed",
			GNM_SOW_LIST_BASE_TYPE,
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (SheetWidgetListBaseClass, selection_changed),
			NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);
	       })

void
sheet_widget_list_base_set_links (SheetObject *so,
				  GnmExprTop const *output,
				  GnmExprTop const *content)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (so);
	dependent_set_expr (&swl->output_dep, output);
	if (output && swl->output_dep.sheet)
		dependent_link (&swl->output_dep);
	dependent_set_expr (&swl->content_dep, content);
	if (content && swl->content_dep.sheet) {
		dependent_link (&swl->content_dep);
		list_content_eval (&swl->content_dep); /* populate the list */
	}
}

GnmExprTop const *
sheet_widget_list_base_get_result_link  (SheetObject const *so)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (so);
	GnmExprTop const *texpr = swl->output_dep.texpr;

 	if (texpr)
		gnm_expr_top_ref (texpr);

 	return texpr;
}

GnmExprTop const *
sheet_widget_list_base_get_content_link (SheetObject const *so)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (so);
	GnmExprTop const *texpr = swl->content_dep.texpr;

 	if (texpr)
		gnm_expr_top_ref (texpr);

 	return texpr;
}

gboolean
sheet_widget_list_base_result_type_is_index (SheetObject const *so)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (so);

	return swl->result_as_index;
}

void
sheet_widget_list_base_set_result_type (SheetObject *so, gboolean as_index)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (so);

	if (swl->result_as_index == as_index)
		return;

	swl->result_as_index = as_index;

}

/**
 * sheet_widget_list_base_get_adjustment:
 * @so: #SheetObject
 *
 * Note: allocates a new adjustment.
 * Returns: (transfer full): the newly created #GtkAdjustment.
 **/
GtkAdjustment *
sheet_widget_list_base_get_adjustment (SheetObject *so)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (so);
	GtkAdjustment *adj;

	g_return_val_if_fail (swl, NULL);

	adj = (GtkAdjustment*)gtk_adjustment_new
		(swl->selection,
		 1,
		 1 + gtk_tree_model_iter_n_children (swl->model, NULL),
		 1,
		 5,
		 5);
	g_object_ref_sink (adj);

	return adj;
}

/****************************************************************************/

#define GNM_SOW_LIST(o)	(G_TYPE_CHECK_INSTANCE_CAST((o), GNM_SOW_LIST_TYPE, SheetWidgetList))

typedef SheetWidgetListBase		SheetWidgetList;
typedef SheetWidgetListBaseClass	SheetWidgetListClass;

static void
cb_list_selection_changed (SheetWidgetListBase *swl,
			   GtkTreeSelection *selection)
{
	if (swl->selection > 0) {
		GtkTreePath *path = gtk_tree_path_new_from_indices (swl->selection-1, -1);
		gtk_tree_selection_select_path (selection, path);
		gtk_tree_path_free (path);
	} else
		gtk_tree_selection_unselect_all (selection);
}

static void
cb_list_model_changed (SheetWidgetListBase *swl, GtkTreeView *list)
{
	int old_selection = swl->selection;
	swl->selection = -1;
	gtk_tree_view_set_model (GTK_TREE_VIEW (list), swl->model);
	sheet_widget_list_base_set_selection (swl, old_selection, NULL);
}
static void
cb_selection_changed (GtkTreeSelection *selection,
		      SheetWidgetListBase *swl)
{
	GtkWidget    *view = (GtkWidget *)gtk_tree_selection_get_tree_view (selection);
	GnmSimpleCanvas *scanvas = GNM_SIMPLE_CANVAS (gtk_widget_get_ancestor (view, GNM_SIMPLE_CANVAS_TYPE));
	GtkTreeModel *model;
	GtkTreeIter   iter;
	int	      pos = 0;
	if (swl->selection != -1) {
		if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
			GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
			if (NULL != path) {
				pos = *gtk_tree_path_get_indices (path) + 1;
				gtk_tree_path_free (path);
			}
		}
		sheet_widget_list_base_set_selection
			(swl, pos, scg_wbc (scanvas->scg));
	}
}

static GtkWidget *
sheet_widget_list_create_widget (SheetObjectWidget *sow)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (sow);
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	GtkWidget *list = gtk_tree_view_new_with_model (swl->model);
	GtkWidget *sw = gtk_scrolled_window_new (
		gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (list)),
		gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (list)));
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
		GTK_POLICY_AUTOMATIC,
		GTK_POLICY_ALWAYS);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (list), FALSE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list),
		gtk_tree_view_column_new_with_attributes ("ID",
			gtk_cell_renderer_text_new (), "text", 0,
			NULL));

	gtk_container_add (GTK_CONTAINER (sw), list);

	g_signal_connect_object (G_OBJECT (swl), "model-changed",
		G_CALLBACK (cb_list_model_changed), list, 0);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list));
	if ((swl->model != NULL) && (swl->selection > 0) &&
	    gtk_tree_model_iter_nth_child (swl->model, &iter, NULL, swl->selection - 1))
		gtk_tree_selection_select_iter (selection, &iter);
	g_signal_connect_object (G_OBJECT (swl), "selection-changed",
		G_CALLBACK (cb_list_selection_changed), selection, 0);
	g_signal_connect (selection, "changed",
		G_CALLBACK (cb_selection_changed), swl);
	return sw;
}

static void
sheet_widget_list_draw_cairo (SheetObject const *so, cairo_t *cr,
			      double width, double height)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (so);

	cairo_save (cr);
	cairo_set_line_width (cr, 0.5);
	cairo_set_source_rgb(cr, 0, 0, 0);

	cairo_new_path (cr);
	cairo_move_to (cr, 0, 0);
	cairo_line_to (cr, width, 0);
	cairo_line_to (cr, width, height);
	cairo_line_to (cr, 0, height);
	cairo_close_path (cr);
	cairo_stroke (cr);

	cairo_new_path (cr);
	cairo_move_to (cr, width - 10, 0);
	cairo_rel_line_to (cr, 0, height);
	cairo_stroke (cr);

	cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);

	cairo_new_path (cr);
	cairo_move_to (cr, width - 5 -3, height - 12);
	cairo_rel_line_to (cr, 6, 0);
	cairo_rel_line_to (cr, -3, 8);
	cairo_close_path (cr);
	cairo_fill (cr);

	cairo_new_path (cr);
	cairo_move_to (cr, width - 5 -3, 12);
	cairo_rel_line_to (cr, 6, 0);
	cairo_rel_line_to (cr, -3, -8);
	cairo_close_path (cr);
	cairo_fill (cr);

	if (swl->model != NULL) {
		GtkTreeIter iter;
		GString*str = g_string_new (NULL);
		int twidth = width, theight = height;


		cairo_new_path (cr);
		cairo_rectangle (cr, 2, 1, width - 2 - 12, height - 2);
		cairo_clip (cr);
		if (gtk_tree_model_get_iter_first (swl->model, &iter))
			do {
				char *astr = NULL, *newline;
				gtk_tree_model_get (swl->model, &iter, 0, &astr, -1);
				while (NULL != (newline = strchr (astr, '\n')))
					*newline = ' ';
				g_string_append (str, astr);
				g_string_append_c (str, '\n');
				g_free (astr);
			} while (gtk_tree_model_iter_next (swl->model, &iter));

		cairo_translate (cr, 4., 2.);

		draw_cairo_text (cr, str->str, &twidth, &theight, FALSE, FALSE, FALSE,
				 swl->selection, FALSE);

		g_string_free (str, TRUE);
	}

	cairo_new_path (cr);
	cairo_restore (cr);
}

static void
sheet_widget_list_class_init (SheetObjectWidgetClass *sow_class)
{
	SheetObjectClass *so_class = GNM_SO_CLASS (sow_class);

	so_class->draw_cairo = &sheet_widget_list_draw_cairo;
        sow_class->create_widget = &sheet_widget_list_create_widget;
}

GSF_CLASS (SheetWidgetList, sheet_widget_list,
	   &sheet_widget_list_class_init, NULL,
	   GNM_SOW_LIST_BASE_TYPE)

/****************************************************************************/

#define GNM_SOW_COMBO(o)	(G_TYPE_CHECK_INSTANCE_CAST((o), GNM_SOW_COMBO_TYPE, SheetWidgetCombo))

typedef SheetWidgetListBase		SheetWidgetCombo;
typedef SheetWidgetListBaseClass	SheetWidgetComboClass;

static void
cb_combo_selection_changed (SheetWidgetListBase *swl,
			    GtkComboBox *combo)
{
	int pos = swl->selection - 1;
	if (pos < 0) {
		gtk_entry_set_text (GTK_ENTRY (gtk_bin_get_child (GTK_BIN (combo))), "");
		pos = -1;
	}
	gtk_combo_box_set_active (combo, pos);
}

static void
cb_combo_model_changed (SheetWidgetListBase *swl, GtkComboBox *combo)
{
	gtk_combo_box_set_model (GTK_COMBO_BOX (combo), swl->model);

	/* we cannot set this until we have a model,
	 * but after that we cannot reset it */
	if (gtk_combo_box_get_entry_text_column (GTK_COMBO_BOX (combo)) < 0)
		gtk_combo_box_set_entry_text_column (GTK_COMBO_BOX (combo), 0);

	/* force entry to reload */
	cb_combo_selection_changed (swl, combo);
}

static void
cb_combo_changed (GtkComboBox *combo, SheetWidgetListBase *swl)
{
	int pos = gtk_combo_box_get_active (combo) + 1;
	sheet_widget_list_base_set_selection (swl, pos,
		widget_wbc (GTK_WIDGET (combo)));
}

static GtkWidget *
sheet_widget_combo_create_widget (SheetObjectWidget *sow)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (sow);
	GtkWidget *widget = gtk_event_box_new (), *combo;

	combo = gtk_combo_box_new_with_entry ();
	gtk_widget_set_can_focus (gtk_bin_get_child (GTK_BIN (combo)),
				  FALSE);
	if (swl->model != NULL)
		g_object_set (G_OBJECT (combo),
                      "model",		swl->model,
                      "entry-text-column",	0,
                      "active",	swl->selection - 1,
                      NULL);

	g_signal_connect_object (G_OBJECT (swl), "model-changed",
		G_CALLBACK (cb_combo_model_changed), combo, 0);
	g_signal_connect_object (G_OBJECT (swl), "selection-changed",
		G_CALLBACK (cb_combo_selection_changed), combo, 0);
	g_signal_connect (G_OBJECT (combo), "changed",
		G_CALLBACK (cb_combo_changed), swl);

	gtk_container_add (GTK_CONTAINER (widget), combo);
	gtk_event_box_set_visible_window (GTK_EVENT_BOX (widget), FALSE);
	return widget;
}

static void
sheet_widget_combo_draw_cairo (SheetObject const *so, cairo_t *cr,
			       double width, double height)
{
	SheetWidgetListBase *swl = GNM_SOW_LIST_BASE (so);
	double halfheight = height/2;

	cairo_save (cr);
	cairo_set_line_width (cr, 0.5);
	cairo_set_source_rgb(cr, 0, 0, 0);

	cairo_new_path (cr);
	cairo_move_to (cr, 0, 0);
	cairo_line_to (cr, width, 0);
	cairo_line_to (cr, width, height);
	cairo_line_to (cr, 0, height);
	cairo_close_path (cr);
	cairo_stroke (cr);

	cairo_new_path (cr);
	cairo_move_to (cr, width - 10, 0);
	cairo_rel_line_to (cr, 0, height);
	cairo_stroke (cr);

	cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);

	cairo_new_path (cr);
	cairo_move_to (cr, width - 5 -3, halfheight - 4);
	cairo_rel_line_to (cr, 6, 0);
	cairo_rel_line_to (cr, -3, 8);
	cairo_close_path (cr);
	cairo_fill (cr);

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_move_to (cr, 4., halfheight);

	if (swl->model != NULL) {
		GtkTreeIter iter;
		if (gtk_tree_model_iter_nth_child (swl->model, &iter, NULL,
						   swl->selection - 1)) {
			char *str = NULL;
			gtk_tree_model_get (swl->model, &iter, 0, &str, -1);
			draw_cairo_text (cr, str, NULL, NULL, TRUE, FALSE, TRUE, 0, FALSE);
			g_free (str);
		}
	}

	cairo_new_path (cr);
	cairo_restore (cr);
}

static void
sheet_widget_combo_class_init (SheetObjectWidgetClass *sow_class)
{
	SheetObjectClass *so_class = GNM_SO_CLASS (sow_class);

	so_class->draw_cairo = &sheet_widget_combo_draw_cairo;
        sow_class->create_widget = &sheet_widget_combo_create_widget;
}

GSF_CLASS (SheetWidgetCombo, sheet_widget_combo,
	   &sheet_widget_combo_class_init, NULL,
	   GNM_SOW_LIST_BASE_TYPE)





/**************************************************************************/

/**
 * sheet_object_widget_register:
 *
 * Initialize the classes for the sheet-object-widgets. We need to initialize
 * them before we try loading a sheet that might contain sheet-object-widgets
 **/
void
sheet_object_widget_register (void)
{
	GNM_SOW_FRAME_TYPE;
	GNM_SOW_BUTTON_TYPE;
	GNM_SOW_SCROLLBAR_TYPE;
	GNM_SOW_CHECKBOX_TYPE;
	GNM_SOW_RADIO_BUTTON_TYPE;
	GNM_SOW_LIST_TYPE;
	GNM_SOW_COMBO_TYPE;
	GNM_SOW_SPIN_BUTTON_TYPE;
	GNM_SOW_SLIDER_TYPE;
}
