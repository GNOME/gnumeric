/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

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
#include "gnumeric.h"
#include "sheet-object-widget.h"

#include "gnm-pane.h"
#include "gnumeric-simple-canvas.h"
#include "gui-util.h"
#include "dependent.h"
#include "sheet-control-gui.h"
#include "sheet-object-impl.h"
#include "expr.h"
#include "parse-util.h"
#include "value.h"
#include "ranges.h"
#include "selection.h"
#include "wbc-gtk.h"
#include "workbook.h"
#include "sheet.h"
#include "cell.h"
#include "mathfunc.h"
#include "gnumeric-expr-entry.h"
#include "dialogs.h"
#include "dialogs/help.h"
#include "xml-io.h"
#include "commands.h"
#include "gnm-format.h"

#include <goffice/gtk/go-combo-text.h>
#include <goffice/utils/go-libxml-extras.h>
#include <goffice/utils/go-glib-extras.h>

#include <gsf/gsf-impl-utils.h>
#include <libxml/globals.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-widget.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>

#define CXML2C(s) ((char const *)(s))

static inline gboolean
attr_eq (const xmlChar *a, const char *s)
{
	return !strcmp (CXML2C (a), s);
}

/****************************************************************************/

static void
so_widget_view_destroy (SheetObjectView *sov)
{
	gtk_object_destroy (GTK_OBJECT (sov));
}
static void
so_widget_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	FooCanvasItem *view = FOO_CANVAS_ITEM (sov);
	if (visible) {
		/* NOTE : far point is EXCLUDED so we add 1 */
		foo_canvas_item_set (view,
			"x",	  MIN (coords [0], coords [2]),
			"y",	  MIN (coords [1], coords [3]),
			"width",  fabs (coords [2] - coords [0]) + 1.,
			"height", fabs (coords [3] - coords [1]) + 1.,
			NULL);
		foo_canvas_item_show (view);
	} else
		foo_canvas_item_hide (view);
}

static void
so_widget_foo_view_init (SheetObjectViewIface *sov_iface)
{
	sov_iface->destroy	= so_widget_view_destroy;
	sov_iface->set_bounds	= so_widget_view_set_bounds;
}
typedef FooCanvasWidget		SOWidgetFooView;
typedef FooCanvasWidgetClass	SOWidgetFooViewClass;
static GSF_CLASS_FULL (SOWidgetFooView, so_widget_foo_view,
	NULL, NULL, NULL, NULL,
	NULL, FOO_TYPE_CANVAS_WIDGET, 0,
	GSF_INTERFACE (so_widget_foo_view_init, SHEET_OBJECT_VIEW_TYPE))

/****************************************************************************/

#define SHEET_OBJECT_CONFIG_KEY "sheet-object-config-dialog"

#define SHEET_OBJECT_WIDGET_TYPE     (sheet_object_widget_get_type ())
#define SHEET_OBJECT_WIDGET(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_OBJECT_WIDGET_TYPE, SheetObjectWidget))
#define SHEET_OBJECT_WIDGET_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_OBJECT_WIDGET_TYPE, SheetObjectWidgetClass))
#define IS_SHEET_WIDGET_OBJECT(o)    (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_WIDGET_TYPE))
#define SOW_CLASS(so)		     (SHEET_OBJECT_WIDGET_CLASS (G_OBJECT_GET_CLASS(so)))

#define SOW_MAKE_TYPE(n1, n2, fn_config, fn_set_sheet, fn_clear_sheet, fn_foreach_dep,	\
		      fn_copy, fn_read_dom, fn_write_sax, fn_prep_sax_parser,		\
		      fn_get_property, fn_set_property, class_init_code)		\
static void										\
sheet_widget_ ## n1 ## _class_init (GObjectClass *object_class)				\
{											\
	SheetObjectWidgetClass *sow_class = SHEET_OBJECT_WIDGET_CLASS (object_class);	\
	SheetObjectClass *so_class = SHEET_OBJECT_CLASS (object_class);			\
	object_class->finalize		= &sheet_widget_ ## n1 ## _finalize;		\
	object_class->set_property	= fn_set_property;				\
	object_class->get_property	= fn_get_property;				\
	so_class->user_config		= fn_config;					\
        so_class->interactive           = TRUE;						\
	so_class->assign_to_sheet	= fn_set_sheet;					\
	so_class->remove_from_sheet	= fn_clear_sheet;				\
	so_class->foreach_dep		= fn_foreach_dep;				\
	so_class->copy			= fn_copy;					\
	so_class->read_xml_dom		= fn_read_dom;					\
	so_class->write_xml_sax		= fn_write_sax;					\
	so_class->prep_sax_parser	= fn_prep_sax_parser;				\
	sow_class->create_widget	= &sheet_widget_ ## n1 ## _create_widget;	\
        { class_init_code; }								\
}											\
											\
GSF_CLASS (SheetWidget ## n2, sheet_widget_ ## n1,					\
	   &sheet_widget_ ## n1 ## _class_init,						\
	   &sheet_widget_ ## n1 ## _init,						\
	   SHEET_OBJECT_WIDGET_TYPE);

typedef SheetObject SheetObjectWidget;
typedef struct {
	SheetObjectClass parent_class;
	GtkWidget *(*create_widget)(SheetObjectWidget *);
} SheetObjectWidgetClass;

static SheetObjectClass *sheet_object_widget_parent_class = NULL;
static GObjectClass *sheet_object_widget_class = NULL;

static GType sheet_object_widget_get_type	(void);

static void
sax_write_dep (GsfXMLOut *output, GnmDependent const *dep, char const *id)
{
	if (dep->texpr != NULL) {
		GnmParsePos pos;
		char *val = gnm_expr_top_as_string (dep->texpr,
			parse_pos_init_sheet (&pos, dep->sheet),
			gnm_conventions_default);
		gsf_xml_out_add_cstr (output, id, val);
		g_free (val);
	}
}

static gboolean
sax_read_dep (xmlChar const * const *attrs, char const *name,
	      GnmDependent *dep, GsfXMLIn *xin)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!attr_eq (attrs[0], name))
		return FALSE;

	dep->sheet = NULL;
	if (attrs[1] != NULL && *attrs[1] != '\0') {
		GnmParsePos pp;
		dep->texpr = gnm_expr_parse_str_simple (CXML2C (attrs[1]),
			parse_pos_init_sheet (&pp, gnm_xml_in_cur_sheet (xin)));
	} else
		dep->texpr = NULL;

	return TRUE;
}

static void
read_dep (GnmDependent *dep, char const *name,
	  xmlNodePtr tree, XmlParseContext const *context)
{
	char *txt = (gchar *)xmlGetProp (tree, (xmlChar *)name);

	dep->sheet = NULL;
	dep->texpr = NULL;
	if (txt != NULL && *txt != '\0') {
		GnmParsePos pos;
		dep->texpr = gnm_expr_parse_str_simple (txt,
			parse_pos_init_sheet (&pos, context->sheet));
		xmlFree (txt);
	}
}

static SheetObjectView *
sheet_object_widget_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GtkWidget *view_widget =
		SOW_CLASS(so)->create_widget (SHEET_OBJECT_WIDGET (so));
	FooCanvasItem *view_item = foo_canvas_item_new (
		gnm_pane_object_group (GNM_PANE (container)),
		so_widget_foo_view_get_type (),
		"widget", view_widget,
		"size_pixels", FALSE,
		NULL);
	/* g_warning ("%p is widget for so %p", view_widget, so);*/
	gtk_widget_show_all (view_widget);
	foo_canvas_item_hide (view_item);
	gnm_pane_widget_register (so, view_widget, view_item);
	return gnm_pane_object_register (so, view_item, TRUE);
}

static void
sheet_object_widget_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *so_class = SHEET_OBJECT_CLASS (object_class);
	SheetObjectWidgetClass *sow_class = SHEET_OBJECT_WIDGET_CLASS (object_class);

	sheet_object_widget_class = G_OBJECT_CLASS (object_class);
	sheet_object_widget_parent_class = g_type_class_peek_parent (object_class);

	/* SheetObject class method overrides */
	so_class->new_view		= sheet_object_widget_new_view;
	so_class->rubber_band_directly	= TRUE;

	sow_class->create_widget = NULL;
}

static void
sheet_object_widget_init (SheetObjectWidget *sow)
{
	SheetObject *so = SHEET_OBJECT (sow);
	so->flags |= SHEET_OBJECT_CAN_PRESS;
}

static GSF_CLASS (SheetObjectWidget, sheet_object_widget,
		  sheet_object_widget_class_init,
		  sheet_object_widget_init,
		  SHEET_OBJECT_TYPE);

static WorkbookControl *
widget_wbc (GtkWidget *widget)
{
	return scg_wbc (GNM_SIMPLE_CANVAS (widget->parent)->scg);
}


/****************************************************************************/
#define SHEET_WIDGET_FRAME_TYPE     (sheet_widget_frame_get_type ())
#define SHEET_WIDGET_FRAME(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_WIDGET_FRAME_TYPE, SheetWidgetFrame))
typedef struct {
	SheetObjectWidget	sow;
	char *label;
} SheetWidgetFrame;
typedef SheetObjectWidgetClass SheetWidgetFrameClass;

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
	SheetWidgetFrame *swf = SHEET_WIDGET_FRAME (obj);

	g_free (swf->label);
	swf->label = NULL;

	sheet_object_widget_class->finalize (obj);
}

static GtkWidget *
sheet_widget_frame_create_widget (SheetObjectWidget *sow)
{
	return gtk_frame_new (SHEET_WIDGET_FRAME (sow)->label);
}

static void
sheet_widget_frame_copy (SheetObject *dst, SheetObject const *src)
{
	sheet_widget_frame_init_full (SHEET_WIDGET_FRAME (dst),
		SHEET_WIDGET_FRAME (src)->label);
}

static void
sheet_widget_frame_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
{
	SheetWidgetFrame const *swf = SHEET_WIDGET_FRAME (so);
	gsf_xml_out_add_cstr (output, "Label", swf->label);
}

static void
sheet_widget_frame_prep_sax_parser (SheetObject *so, GsfXMLIn *xin, xmlChar const **attrs)
{
	SheetWidgetFrame *swf = SHEET_WIDGET_FRAME (so);
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_eq (attrs[0], "Label")) {
			g_free (swf->label);
			swf->label = g_strdup (CXML2C (attrs[1]));
		}
}

static gboolean
sheet_widget_frame_read_xml_dom (SheetObject *so, char const *typename,
				 XmlParseContext const *context,
				 xmlNodePtr tree)
{
	SheetWidgetFrame *swf = SHEET_WIDGET_FRAME (so);
	gchar *label = (gchar *)xmlGetProp (tree, (xmlChar *)"Label");

	if (!label) {
		g_warning ("Could not read a SheetWidgetFrame because it lacks a label property.");
		return TRUE;
	}

	g_free (swf->label);
	swf->label = g_strdup (label);
	xmlFree (label);

	return FALSE;
}

typedef struct {
	GladeXML           *gui;
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

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	g_free (state->old_label);
	state->old_label = NULL;
	state->dialog = NULL;
	g_free (state);
}

static void
cb_frame_config_ok_clicked (GtkWidget *button, FrameConfigState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_frame_config_cancel_clicked (GtkWidget *button, FrameConfigState *state)
{
	GList *ptr;
	SheetWidgetFrame *swf = state->swf;

	g_free (swf->label);

	swf->label = g_strdup (state->old_label);
	for (ptr = swf->sow.realized_list; ptr != NULL ; ptr = ptr->next)
		gtk_frame_set_label
			(GTK_FRAME (FOO_CANVAS_WIDGET (ptr->data)->widget),
			 state->old_label);

	gtk_widget_destroy (state->dialog);
}

static void
cb_frame_label_changed (GtkWidget *entry, FrameConfigState *state)
{
	GList *ptr;
	SheetWidgetFrame *swf;
	gchar const *text;

	text = gtk_entry_get_text(GTK_ENTRY(entry));
	swf = state->swf;

	g_free (swf->label);
	swf->label = g_strdup (text);

	for (ptr = swf->sow.realized_list; ptr != NULL; ptr = ptr->next) {
		gtk_frame_set_label
			(GTK_FRAME (FOO_CANVAS_WIDGET (ptr->data)->widget),
			 text);
	}
}

static void
sheet_widget_frame_user_config (SheetObject *so, SheetControl *sc)
{
	SheetWidgetFrame *swf = SHEET_WIDGET_FRAME (so);
	WBCGtk   *wbcg = scg_wbcg (SHEET_CONTROL_GUI (sc));
	FrameConfigState *state;
	GtkWidget *table;

	g_return_if_fail (swf != NULL);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
		return;

	state = g_new (FrameConfigState, 1);
	state->swf = swf;
	state->wbcg = wbcg;
	state->sheet = sc_sheet	(sc);
	state->old_focus = NULL;
	state->old_label = g_strdup(swf->label);
	state->gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"so-frame.glade", NULL, NULL);
	state->dialog = glade_xml_get_widget (state->gui, "so_frame");

	table = glade_xml_get_widget(state->gui, "table");

	state->label = glade_xml_get_widget (state->gui, "entry");
	gtk_entry_set_text (GTK_ENTRY(state->label), swf->label);
	gtk_editable_select_region (GTK_EDITABLE(state->label), 0, -1);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->label));

	g_signal_connect (G_OBJECT(state->label),
			  "changed",
			  G_CALLBACK (cb_frame_label_changed), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui,
							  "ok_button")),
			  "clicked",
			  G_CALLBACK (cb_frame_config_ok_clicked), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui,
							  "cancel_button")),
			  "clicked",
			  G_CALLBACK (cb_frame_config_cancel_clicked), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_SO_FRAME);


	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SHEET_OBJECT_CONFIG_KEY);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_frame_config_destroy);

	gtk_widget_show (state->dialog);
}

SOW_MAKE_TYPE (frame, Frame,
	       &sheet_widget_frame_user_config,
	       NULL,
	       NULL,
	       NULL,
	       &sheet_widget_frame_copy,
	       &sheet_widget_frame_read_xml_dom,
	       &sheet_widget_frame_write_xml_sax,
	       &sheet_widget_frame_prep_sax_parser,
	       NULL,
	       NULL,
	       {})

/****************************************************************************/
#define SHEET_WIDGET_BUTTON_TYPE     (sheet_widget_button_get_type ())
#define SHEET_WIDGET_BUTTON(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_WIDGET_BUTTON_TYPE, SheetWidgetButton))
typedef struct {
	SheetObjectWidget	sow;
	char *label;
	PangoAttrList *markup;
} SheetWidgetButton;
typedef SheetObjectWidgetClass SheetWidgetButtonClass;

static void
sheet_widget_button_init_full (SheetWidgetButton *swb,
			       char const *text,
			       PangoAttrList *markup)
{
	swb->label = g_strdup (text);
	swb->markup = markup;
	if (markup) pango_attr_list_ref (markup);
}

static void
sheet_widget_button_init (SheetWidgetButton *swb)
{
	sheet_widget_button_init_full (swb, _("Button"), NULL);
}

static void
sheet_widget_button_finalize (GObject *obj)
{
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (obj);

	g_free (swb->label);
	swb->label = NULL;

	if (swb->markup) {
		pango_attr_list_unref (swb->markup);
		swb->markup = NULL;
	}

	(*sheet_object_widget_class->finalize)(obj);
}

static GtkWidget *
sheet_widget_button_create_widget (SheetObjectWidget *sow)
{
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (sow);
	GtkWidget *w = gtk_button_new_with_label (swb->label);
	gtk_label_set_attributes (GTK_LABEL (GTK_BIN (w)->child),
				  swb->markup);
	return w;
}

static void
sheet_widget_button_copy (SheetObject *dst, SheetObject const *src_swb)
{
	sheet_widget_button_init_full (SHEET_WIDGET_BUTTON (dst),
				       SHEET_WIDGET_BUTTON (src_swb)->label,
				       SHEET_WIDGET_BUTTON (src_swb)->markup);
}

static void
sheet_widget_button_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
{
	// FIXME: markup
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (so);
	gsf_xml_out_add_cstr (output, "Label", swb->label);
}

static void
sheet_widget_button_prep_sax_parser (SheetObject *so, GsfXMLIn *xin, xmlChar const **attrs)
{
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (so);
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_eq (attrs[0], "Label"))
			g_object_set (G_OBJECT (swb), "text", attrs[1], NULL);
}

static gboolean
sheet_widget_button_read_xml_dom (SheetObject *so, char const *typename,
				  XmlParseContext const *context,
				  xmlNodePtr tree)
{
	// FIXME: markup
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (so);
	gchar *label = (gchar *)xmlGetProp (tree, (xmlChar *)"Label");

	if (!label) {
		g_warning ("Could not read a SheetWidgetButton because it lacks a label property.");
		return TRUE;
	}

	swb->label = g_strdup (label);
	xmlFree (label);

	return FALSE;
}

void
sheet_widget_button_set_label (SheetObject *so, char const *str)
{
	GList *ptr;
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (so);
	char *new_label;

	if (go_str_compare (str, swb->label) == 0)
		return;

	new_label = g_strdup (str);
	g_free (swb->label);
	swb->label = new_label;

	for (ptr = swb->sow.realized_list; ptr != NULL; ptr = ptr->next) {
		FooCanvasWidget *item = FOO_CANVAS_WIDGET (ptr->data);
		gtk_button_set_label (GTK_BUTTON (item->widget), swb->label);
	}
}

void
sheet_widget_button_set_markup (SheetObject *so, PangoAttrList *markup)
{
	GList *ptr;
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (so);

	if (markup == swb->markup)
		return;

	if (swb->markup) pango_attr_list_unref (swb->markup);
	swb->markup = markup;
	if (markup) pango_attr_list_ref (markup);

	for (ptr = swb->sow.realized_list; ptr != NULL; ptr = ptr->next) {
		FooCanvasWidget *item = FOO_CANVAS_WIDGET (ptr->data);
		gtk_label_set_attributes (GTK_LABEL (GTK_BIN (item->widget)->child),
					  swb->markup);
	}
}

enum {
	SOB_PROP_0 = 0,
	SOB_PROP_TEXT,
	SOB_PROP_MARKUP
};

static void
sheet_widget_button_get_property (GObject *obj, guint param_id,
				  GValue  *value, GParamSpec *pspec)
{
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (obj);

	switch (param_id) {
	case SOB_PROP_TEXT:
		g_value_set_string (value, swb->label);
		break;
	case SOB_PROP_MARKUP:
		g_value_set_boxed (value, swb->markup);
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
sheet_widget_button_set_property (GObject *obj, guint param_id,
				  GValue const *value, GParamSpec *pspec)
{
	SheetWidgetButton *swb = SHEET_WIDGET_BUTTON (obj);

	switch (param_id) {
	case SOB_PROP_TEXT:
		sheet_widget_button_set_label (SHEET_OBJECT (swb),
					       g_value_get_string (value));
		break;
	case SOB_PROP_MARKUP:
		sheet_widget_button_set_markup (SHEET_OBJECT (swb),
						g_value_peek_pointer (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return;
	}
}

SOW_MAKE_TYPE (button, Button,
	       NULL,
	       NULL,
	       NULL,
	       NULL,
	       sheet_widget_button_copy,
	       sheet_widget_button_read_xml_dom,
	       sheet_widget_button_write_xml_sax,
	       sheet_widget_button_prep_sax_parser,
	       sheet_widget_button_get_property,
	       sheet_widget_button_set_property,
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
#define SHEET_WIDGET_ADJUSTMENT_TYPE	(sheet_widget_adjustment_get_type())
#define SHEET_WIDGET_ADJUSTMENT(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), SHEET_WIDGET_ADJUSTMENT_TYPE, SheetWidgetAdjustment))
#define DEP_TO_ADJUSTMENT(d_ptr)	(SheetWidgetAdjustment *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetAdjustment, dep))

typedef struct {
	SheetObjectWidget	sow;

	gboolean  being_updated;
	GnmDependent dep;
	GtkAdjustment *adjustment;
} SheetWidgetAdjustment;
typedef SheetObjectWidgetClass SheetWidgetAdjustmentClass;

static GType sheet_widget_adjustment_get_type (void);

static void
sheet_widget_adjustment_set_value (SheetWidgetAdjustment *swa, double new_val)
{
	if (swa->being_updated)
		return;
	swa->adjustment->value = new_val;

	swa->being_updated = TRUE;
	gtk_adjustment_value_changed (swa->adjustment);
	swa->being_updated = FALSE;
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
	g_string_append_printf (target, "Adjustment%p", dep);
}

static DEPENDENT_MAKE_TYPE (adjustment, NULL)

static GnmCellRef *
sheet_widget_adjustment_get_ref (SheetWidgetAdjustment const *swa,
				GnmCellRef *res, gboolean force_sheet)
{
	GnmValue *target;
	g_return_val_if_fail (swa != NULL, NULL);

	if (swa->dep.texpr == NULL)
		return NULL;

	target = gnm_expr_top_get_range (swa->dep.texpr);
	if (target == NULL)
		return NULL;

	*res = target->v_range.cell.a;
	value_release (target);

	if (force_sheet && res->sheet == NULL)
		res->sheet = sheet_object_get_sheet (SHEET_OBJECT (swa));
	return res;
}

static void
cb_adjustment_widget_value_changed (GtkWidget *widget,
				    SheetWidgetAdjustment *swa)
{
	GnmCellRef ref;

	if (swa->being_updated)
		return;

	if (sheet_widget_adjustment_get_ref (swa, &ref, TRUE) != NULL) {
		GnmCell *cell = sheet_cell_fetch (ref.sheet, ref.col, ref.row);
		/* TODO : add more control for precision, XL is stupid */
		int new_val = gnm_fake_round (swa->adjustment->value);
		if (cell->value != NULL &&
		    VALUE_IS_FLOAT (cell->value) &&
		    value_get_as_float (cell->value) == new_val)
			return;

		swa->being_updated = TRUE;
		cmd_so_set_value (widget_wbc (widget),
				  /* FIXME: This text sucks:  */
				  _("Change widget"),
				  &ref, value_new_int (new_val));
		swa->being_updated = FALSE;
	}
}

static void
sheet_widget_adjustment_init_full (SheetWidgetAdjustment *swa, GnmCellRef const *ref)
{
	g_return_if_fail (swa != NULL);

	swa->adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0., 0., 100., 1., 10., 0.));
#if GLIB_CHECK_VERSION(2,10,0) && GTK_CHECK_VERSION(2,8,14)
	g_object_ref_sink (swa->adjustment);
#else
	g_object_ref (swa->adjustment);
	gtk_object_sink (GTK_OBJECT (swa->adjustment));
#endif

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
	sheet_widget_adjustment_init_full (swa, NULL);
}

static void
sheet_widget_adjustment_finalize (GObject *obj)
{
	SheetWidgetAdjustment *swa = SHEET_WIDGET_ADJUSTMENT (obj);

	g_return_if_fail (swa != NULL);

	dependent_set_expr (&swa->dep, NULL);
	if (swa->adjustment != NULL) {
		g_object_unref (G_OBJECT (swa->adjustment));
		swa->adjustment = NULL;
	}

	(*sheet_object_widget_class->finalize)(obj);
}

static void
sheet_widget_adjustment_copy (SheetObject *dst, SheetObject const *src)
{
	SheetWidgetAdjustment const *src_swa = SHEET_WIDGET_ADJUSTMENT (src);
	SheetWidgetAdjustment       *dst_swa = SHEET_WIDGET_ADJUSTMENT (dst);
	GtkAdjustment *dst_adjust, *src_adjust;
	GnmCellRef ref;

	sheet_widget_adjustment_init_full (dst_swa,
		sheet_widget_adjustment_get_ref (src_swa, &ref, FALSE));
	dst_adjust = dst_swa->adjustment;
	src_adjust = src_swa->adjustment;

	dst_adjust->lower = src_adjust->lower;
	dst_adjust->upper = src_adjust->upper;
	dst_adjust->value = src_adjust->value;
	dst_adjust->step_increment = src_adjust->step_increment;
	dst_adjust->page_increment = src_adjust->page_increment;
}

typedef struct {
	GladeXML           *gui;
	GtkWidget          *dialog;
	GnmExprEntry  *expression;
	GtkWidget          *min;
	GtkWidget          *max;
	GtkWidget          *inc;
	GtkWidget          *page;

	GtkWidget          *old_focus;

	WBCGtk *wbcg;
	SheetWidgetAdjustment *swa;
	Sheet		   *sheet;
} AdjustmentConfigState;

static void
cb_adjustment_set_focus (GtkWidget *window, GtkWidget *focus_widget,
			 AdjustmentConfigState *state)
{
	/* Note:  half of the set-focus action is handle by the default
	 *        callback installed by wbc_gtk_attach_guru. */

	/* Force an update of the content in case it needs tweaking (eg make it
	 * absolute) */
	if (state->old_focus != NULL &&
	    IS_GNM_EXPR_ENTRY (state->old_focus->parent)) {
		GnmParsePos  pp;
		GnmExprTop const *texpr = gnm_expr_entry_parse (
			GNM_EXPR_ENTRY (state->old_focus->parent),
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

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);
}

static void
cb_adjustment_config_ok_clicked (GtkWidget *button, AdjustmentConfigState *state)
{
	SheetObject *so = SHEET_OBJECT (state->swa);
	GnmParsePos  pp;
	GnmExprTop const *texpr = gnm_expr_entry_parse (state->expression,
		parse_pos_init_sheet (&pp, so->sheet),
		NULL, FALSE, GNM_EXPR_PARSE_DEFAULT);
	if (texpr != NULL) {
		dependent_set_expr (&state->swa->dep, texpr);
		dependent_link (&state->swa->dep);
		gnm_expr_top_unref (texpr);
	}

	state->swa->adjustment->lower = gtk_spin_button_get_value_as_int (
		GTK_SPIN_BUTTON (state->min));
	state->swa->adjustment->upper = gtk_spin_button_get_value_as_int (
		GTK_SPIN_BUTTON (state->max));
	state->swa->adjustment->step_increment = gtk_spin_button_get_value_as_int (
		GTK_SPIN_BUTTON (state->inc));
	state->swa->adjustment->page_increment = gtk_spin_button_get_value_as_int (
		GTK_SPIN_BUTTON (state->page));

	gtk_adjustment_changed	(state->swa->adjustment);

	gtk_widget_destroy (state->dialog);
}

static void
cb_adjustment_config_cancel_clicked (GtkWidget *button, AdjustmentConfigState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
sheet_widget_adjustment_user_config (SheetObject *so, SheetControl *sc)
{
	SheetWidgetAdjustment *swa = SHEET_WIDGET_ADJUSTMENT (so);
	WBCGtk   *wbcg = scg_wbcg (SHEET_CONTROL_GUI (sc));
	AdjustmentConfigState *state;
	GtkWidget *table;

	g_return_if_fail (swa != NULL);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
		return;

	state = g_new (AdjustmentConfigState, 1);
	state->swa = swa;
	state->wbcg = wbcg;
	state->sheet = sc_sheet	(sc);
	state->old_focus = NULL;
	state->gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"so-scrollbar.glade", NULL, NULL);
	state->dialog = glade_xml_get_widget (state->gui, "SO-Scrollbar");

	table = glade_xml_get_widget (state->gui, "table");

	state->expression = gnm_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (state->expression,
		GNM_EE_FORCE_ABS_REF | GNM_EE_SHEET_OPTIONAL | GNM_EE_SINGLE_RANGE,
		GNM_EE_MASK);
	gnm_expr_entry_load_from_dep (state->expression, &swa->dep);
	go_atk_setup_label (glade_xml_get_widget (state->gui, "label_linkto"),
			     GTK_WIDGET (state->expression));
	gtk_table_attach (GTK_TABLE (table), GTK_WIDGET (state->expression),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_widget_show (GTK_WIDGET (state->expression));

	/* TODO : This is silly, no need to be similar to XL here. */
	state->min = glade_xml_get_widget (state->gui, "spin_min");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->min), swa->adjustment->lower);
	state->max = glade_xml_get_widget (state->gui, "spin_max");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->max), swa->adjustment->upper);
	state->inc = glade_xml_get_widget (state->gui, "spin_increment");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->inc), swa->adjustment->step_increment);
	state->page = glade_xml_get_widget (state->gui, "spin_page");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->page), swa->adjustment->page_increment);

	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->expression));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->min));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->max));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->inc));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->page));
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "ok_button")),
		"clicked",
		G_CALLBACK (cb_adjustment_config_ok_clicked), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cb_adjustment_config_cancel_clicked), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_SO_ADJUSTMENT);

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SHEET_OBJECT_CONFIG_KEY);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_adjustment_config_destroy);

	/* Note:  half of the set-focus action is handle by the default */
	/*        callback installed by wbc_gtk_attach_guru           */
	g_signal_connect (G_OBJECT (state->dialog), "set-focus",
		G_CALLBACK (cb_adjustment_set_focus), state);

	gtk_widget_show (state->dialog);
}

static gboolean
sheet_widget_adjustment_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetAdjustment *swa = SHEET_WIDGET_ADJUSTMENT (so);

	dependent_set_sheet (&swa->dep, sheet);

	return FALSE;
}

static gboolean
sheet_widget_adjustment_clear_sheet (SheetObject *so)
{
	SheetWidgetAdjustment *swa = SHEET_WIDGET_ADJUSTMENT (so);

	if (dependent_is_linked (&swa->dep))
		dependent_unlink (&swa->dep);
	swa->dep.sheet = NULL;
	return FALSE;
}

static void
sheet_widget_adjustment_foreach_dep (SheetObject *so,
				     SheetObjectForeachDepFunc func,
				     gpointer user)
{
	SheetWidgetAdjustment *swa = SHEET_WIDGET_ADJUSTMENT (so);
	func (&swa->dep, so, user);
}

static void
sheet_widget_adjustment_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
{
	SheetWidgetAdjustment const *swa = SHEET_WIDGET_ADJUSTMENT (so);
	gsf_xml_out_add_float (output, "Min",   swa->adjustment->lower, 2);
	gsf_xml_out_add_float (output, "Max",   swa->adjustment->upper, 2); /* allow scrolling to max */
	gsf_xml_out_add_float (output, "Inc",   swa->adjustment->step_increment, 2);
	gsf_xml_out_add_float (output, "Page",  swa->adjustment->page_increment, 2);
	gsf_xml_out_add_float (output, "Value", swa->adjustment->value, 2);
	sax_write_dep (output, &swa->dep, "Input");
}

static void
sheet_widget_adjustment_prep_sax_parser (SheetObject *so, GsfXMLIn *xin, xmlChar const **attrs)
{
	SheetWidgetAdjustment *swa = SHEET_WIDGET_ADJUSTMENT (so);
	double tmp;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gnm_xml_attr_double (attrs, "Min", &tmp))
			swa->adjustment->lower = tmp;
		else if (gnm_xml_attr_double (attrs, "Max", &tmp))
			swa->adjustment->upper = tmp;  /* allow scrolling to max */
		else if (gnm_xml_attr_double (attrs, "Inc", &tmp))
			swa->adjustment->step_increment = tmp;
		else if (gnm_xml_attr_double (attrs, "Page", &tmp))
			swa->adjustment->page_increment = tmp;
		else if (gnm_xml_attr_double (attrs, "Value", &tmp))
			swa->adjustment->value = tmp;
		else if (sax_read_dep (attrs, "Input", &swa->dep, xin))
			;

	swa->dep.flags = adjustment_get_dep_type ();
	gtk_adjustment_changed	(swa->adjustment);
}

static gboolean
sheet_widget_adjustment_read_xml_dom (SheetObject *so, char const *typename,
				      XmlParseContext const *context,
				      xmlNodePtr tree)
{
	SheetWidgetAdjustment *swa = SHEET_WIDGET_ADJUSTMENT (so);
	double tmp;

	read_dep (&swa->dep, "Input", tree, context);
	swa->dep.flags = adjustment_get_dep_type ();

	if (xml_node_get_double (tree, "Min", &tmp))
		swa->adjustment->lower = tmp;
	if (xml_node_get_double (tree, "Max", &tmp))
		swa->adjustment->upper = tmp;  /* allow scrolling to max */
	if (xml_node_get_double (tree, "Inc", &tmp))
		swa->adjustment->step_increment = tmp;
	if (xml_node_get_double (tree, "Page", &tmp))
		swa->adjustment->page_increment = tmp;
	if (xml_node_get_double  (tree, "Value", &tmp))
		swa->adjustment->value = tmp;
	gtk_adjustment_changed	(swa->adjustment);

	return FALSE;
}

void
sheet_widget_adjustment_set_details (SheetObject *so, GnmExprTop const *tlink,
				     int value, int min, int max,
				     int inc, int page)
{
	SheetWidgetAdjustment *swa = SHEET_WIDGET_ADJUSTMENT (so);
	g_return_if_fail (swa != NULL);
	swa->adjustment->value = value;
	swa->adjustment->lower = min;
	swa->adjustment->upper = max; /* allow scrolling to max */
	swa->adjustment->step_increment = inc;
	swa->adjustment->page_increment = page;
	dependent_set_expr (&swa->dep, tlink);
	if (NULL != tlink)
		dependent_link (&swa->dep);
	else
		gtk_adjustment_changed (swa->adjustment);
}

static GtkWidget *
sheet_widget_adjustment_create_widget (SheetObjectWidget *sow)
{
	g_warning("ERROR: sheet_widget_adjustment_create_widget SHOULD NEVER BE CALLED (but it has been)!\n");
	return gtk_frame_new ("invisiwidget(WARNING: I AM A BUG!)");
}

SOW_MAKE_TYPE (adjustment, Adjustment,
	       sheet_widget_adjustment_user_config,
	       sheet_widget_adjustment_set_sheet,
	       sheet_widget_adjustment_clear_sheet,
	       sheet_widget_adjustment_foreach_dep,
	       sheet_widget_adjustment_copy,
	       sheet_widget_adjustment_read_xml_dom,
	       sheet_widget_adjustment_write_xml_sax,
	       sheet_widget_adjustment_prep_sax_parser,
	       NULL,
	       NULL,
	       {})

/****************************************************************************/
#define SHEET_WIDGET_SCROLLBAR_TYPE	(sheet_widget_scrollbar_get_type ())
#define SHEET_WIDGET_SCROLLBAR(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_WIDGET_SCROLLBAR_TYPE, SheetWidgetScrollbar))
#define DEP_TO_SCROLLBAR(d_ptr)		(SheetWidgetScrollbar *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetScrollbar, dep))

typedef SheetWidgetAdjustment		SheetWidgetScrollbar;
typedef SheetWidgetAdjustmentClass	SheetWidgetScrollbarClass;

static GtkWidget *
sheet_widget_scrollbar_create_widget (SheetObjectWidget *sow)
{
	SheetObject *so = SHEET_OBJECT (sow);
	SheetWidgetAdjustment *swa = SHEET_WIDGET_ADJUSTMENT (sow);
	GtkWidget *bar;

	/* TODO : this is not exactly accurate, but should catch the worst of it
	 * However we do not have a way to handle resizes.
	 */
	gboolean is_horizontal = range_width (&so->anchor.cell_bound) > range_height (&so->anchor.cell_bound);

	swa->being_updated = TRUE;
	bar = is_horizontal
		? gtk_hscrollbar_new (swa->adjustment)
		: gtk_vscrollbar_new (swa->adjustment);
	GTK_WIDGET_UNSET_FLAGS (bar, GTK_CAN_FOCUS);
	g_signal_connect (G_OBJECT (bar),
		"value_changed",
		G_CALLBACK (cb_adjustment_widget_value_changed), swa);
	swa->being_updated = FALSE;

	return bar;
}

static void
sheet_widget_scrollbar_class_init (SheetObjectWidgetClass *sow_class)
{
        sow_class->create_widget = &sheet_widget_scrollbar_create_widget;
}

GSF_CLASS (SheetWidgetScrollbar, sheet_widget_scrollbar,
	   &sheet_widget_scrollbar_class_init, NULL,
	   SHEET_WIDGET_ADJUSTMENT_TYPE);

/****************************************************************************/
#define SHEET_WIDGET_SPINBUTTON_TYPE	(sheet_widget_spinbutton_get_type ())
#define SHEET_WIDGET_SPINBUTTON(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_WIDGET_SPINBUTTON_TYPE, SheetWidgetSpinbutton))
#define DEP_TO_SPINBUTTON(d_ptr)		(SheetWidgetSpinbutton *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetSpinbutton, dep))

typedef SheetWidgetAdjustment		SheetWidgetSpinbutton;
typedef SheetWidgetAdjustmentClass	SheetWidgetSpinbuttonClass;

static GtkWidget *
sheet_widget_spinbutton_create_widget (SheetObjectWidget *sow)
{
	SheetWidgetAdjustment *swa = SHEET_WIDGET_ADJUSTMENT (sow);
	GtkWidget *spinbutton;

	swa->being_updated = TRUE;
	spinbutton = gtk_spin_button_new (swa->adjustment,
		swa->adjustment->step_increment, 0);
	GTK_WIDGET_UNSET_FLAGS (spinbutton, GTK_CAN_FOCUS);
	g_signal_connect (G_OBJECT (spinbutton),
		"value_changed",
		G_CALLBACK (cb_adjustment_widget_value_changed), swa);
	swa->being_updated = FALSE;
	return spinbutton;
}

static void
sheet_widget_spinbutton_class_init (SheetObjectWidgetClass *sow_class)
{
        sow_class->create_widget = &sheet_widget_spinbutton_create_widget;
}

GSF_CLASS (SheetWidgetSpinbutton, sheet_widget_spinbutton,
	   &sheet_widget_spinbutton_class_init, NULL,
	   SHEET_WIDGET_ADJUSTMENT_TYPE);

/****************************************************************************/
#define SHEET_WIDGET_SLIDER_TYPE	(sheet_widget_slider_get_type ())
#define SHEET_WIDGET_SLIDER(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_WIDGET_SLIDER_TYPE, SheetWidgetSlider))
#define DEP_TO_SLIDER(d_ptr)		(SheetWidgetSlider *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetSlider, dep))

typedef SheetWidgetAdjustment		SheetWidgetSlider;
typedef SheetWidgetAdjustmentClass	SheetWidgetSliderClass;

static GtkWidget *
sheet_widget_slider_create_widget (SheetObjectWidget *sow)
{
	SheetObject *so = SHEET_OBJECT (sow);
	SheetWidgetAdjustment *swa = SHEET_WIDGET_ADJUSTMENT (sow);
	GtkWidget *slider;
	/* TODO : this is not exactly accurate, but should catch the worst of it
	 * However we do not have a way to handle resizes.
	 */
	gboolean is_horizontal = range_width (&so->anchor.cell_bound) > range_height (&so->anchor.cell_bound);

	swa->being_updated = TRUE;
	slider = is_horizontal
		? gtk_hscale_new (swa->adjustment)
		: gtk_vscale_new (swa->adjustment);
	gtk_scale_set_draw_value (GTK_SCALE (slider), FALSE);
	GTK_WIDGET_UNSET_FLAGS (slider, GTK_CAN_FOCUS);
	g_signal_connect (G_OBJECT (slider),
		"value_changed",
		G_CALLBACK (cb_adjustment_widget_value_changed), swa);
	swa->being_updated = FALSE;

	return slider;
}

static void
sheet_widget_slider_class_init (SheetObjectWidgetClass *sow_class)
{
        sow_class->create_widget = &sheet_widget_slider_create_widget;
}

GSF_CLASS (SheetWidgetSlider, sheet_widget_slider,
	   &sheet_widget_slider_class_init, NULL,
	   SHEET_WIDGET_ADJUSTMENT_TYPE);

/****************************************************************************/
#define SHEET_WIDGET_CHECKBOX_TYPE	(sheet_widget_checkbox_get_type ())
#define SHEET_WIDGET_CHECKBOX(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_WIDGET_CHECKBOX_TYPE, SheetWidgetCheckbox))
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
	SOC_PROP_TEXT,
	SOC_PROP_MARKUP
};

static void
sheet_widget_checkbox_get_property (GObject *obj, guint param_id,
				    GValue  *value, GParamSpec *pspec)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (obj);

	switch (param_id) {
	case SOC_PROP_TEXT:
		g_value_set_string (value, swc->label);
		break;
	case SOC_PROP_MARKUP:
		g_value_set_boxed (value, NULL); /* swc->markup */
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
sheet_widget_checkbox_set_property (GObject *obj, guint param_id,
				    GValue const *value, GParamSpec *pspec)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (obj);

	switch (param_id) {
	case SOC_PROP_TEXT:
		sheet_widget_checkbox_set_label (SHEET_OBJECT (swc),
					       g_value_get_string (value));
		break;
	case SOC_PROP_MARKUP:
#if 0
		sheet_widget_checkbox_set_markup (SHEET_OBJECT (swc),
						g_value_peek_pointer (value));
#endif
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return;
	}
}

static void
sheet_widget_checkbox_set_active (SheetWidgetCheckbox *swc)
{
	GList *ptr;

	swc->being_updated = TRUE;

	ptr = swc->sow.realized_list;
	for (; ptr != NULL ; ptr = ptr->next) {
		FooCanvasWidget *item = FOO_CANVAS_WIDGET (ptr->data);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (item->widget),
					      swc->value);
	}

	swc->being_updated = FALSE;
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
	g_string_append_printf (target, "Checkbox%p", dep);
}

static DEPENDENT_MAKE_TYPE (checkbox, NULL)

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
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (obj);

	g_return_if_fail (swc != NULL);

	g_free (swc->label);
	swc->label = NULL;

	dependent_set_expr (&swc->dep, NULL);

	sheet_object_widget_class->finalize (obj);
}

static GnmCellRef *
sheet_widget_checkbox_get_ref (SheetWidgetCheckbox const *swc,
			       GnmCellRef *res, gboolean force_sheet)
{
	GnmValue *target;
	g_return_val_if_fail (swc != NULL, NULL);

	if (swc->dep.texpr == NULL)
		return NULL;

	target = gnm_expr_top_get_range (swc->dep.texpr);
	if (target == NULL)
		return NULL;

	*res = target->v_range.cell.a;
	value_release (target);

	if (force_sheet && res->sheet == NULL)
		res->sheet = sheet_object_get_sheet (SHEET_OBJECT (swc));
	return res;
}

static void
cb_checkbox_toggled (GtkToggleButton *button, SheetWidgetCheckbox *swc)
{
	GnmCellRef ref;

	if (swc->being_updated)
		return;
	swc->value = gtk_toggle_button_get_active (button);
	sheet_widget_checkbox_set_active (swc);

	if (sheet_widget_checkbox_get_ref (swc, &ref, TRUE) != NULL) {
		gboolean new_val = gtk_toggle_button_get_active (button);
		cmd_so_set_value (widget_wbc (GTK_WIDGET (button)),
				  /* FIXME: This text sucks:  */
				  _("Clicking checkbox"),
				  &ref, value_new_bool (new_val));
	}
}

static GtkWidget *
sheet_widget_checkbox_create_widget (SheetObjectWidget *sow)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (sow);
	GtkWidget *button;

	g_return_val_if_fail (swc != NULL, NULL);

	button = gtk_check_button_new_with_label (swc->label);
	GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), swc->value);
	g_signal_connect (G_OBJECT (button),
		"toggled",
		G_CALLBACK (cb_checkbox_toggled), swc);

	return button;
}

static void
sheet_widget_checkbox_copy (SheetObject *dst, SheetObject const *src)
{
	SheetWidgetCheckbox const *src_swc = SHEET_WIDGET_CHECKBOX (src);
	SheetWidgetCheckbox       *dst_swc = SHEET_WIDGET_CHECKBOX (dst);
	GnmCellRef ref;
	sheet_widget_checkbox_init_full (dst_swc,
		sheet_widget_checkbox_get_ref (src_swc, &ref, FALSE),
		src_swc->label);
	dst_swc->value = src_swc->value;
}

typedef struct {
	GladeXML           *gui;
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
cb_checkbox_set_focus (GtkWidget *window, GtkWidget *focus_widget,
		       CheckboxConfigState *state)
{
	/* Note:  half of the set-focus action is handle by the default
	 *        callback installed by wbc_gtk_attach_guru */

	/* Force an update of the content in case it needs tweaking (eg make it
	 * absolute) */
	if (state->old_focus != NULL &&
	    IS_GNM_EXPR_ENTRY (state->old_focus->parent)) {
		GnmParsePos  pp;
		GnmExprTop const *texpr = gnm_expr_entry_parse (
			GNM_EXPR_ENTRY (state->old_focus->parent),
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

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	g_free (state->old_label);
	state->old_label = NULL;
	state->dialog = NULL;
	g_free (state);
}

static void
cb_checkbox_config_ok_clicked (GtkWidget *button, CheckboxConfigState *state)
{
	SheetObject *so = SHEET_OBJECT (state->swc);
	GnmParsePos  pp;
	GnmExprTop const *texpr = gnm_expr_entry_parse (state->expression,
		parse_pos_init_sheet (&pp, so->sheet),
		NULL, FALSE, GNM_EXPR_PARSE_DEFAULT);
	if (texpr != NULL) {
		dependent_set_expr (&state->swc->dep, texpr);
		dependent_link (&state->swc->dep);
		gnm_expr_top_unref (texpr);
	}

	gtk_widget_destroy (state->dialog);
}

static void
cb_checkbox_config_cancel_clicked (GtkWidget *button, CheckboxConfigState *state)
{
	sheet_widget_checkbox_set_label	(SHEET_OBJECT (state->swc),
					 state->old_label);
	gtk_widget_destroy (state->dialog);
}

static void
cb_checkbox_label_changed (GtkEntry *entry, CheckboxConfigState *state)
{
	sheet_widget_checkbox_set_label	(SHEET_OBJECT (state->swc),
					 gtk_entry_get_text (entry));
}

static void
sheet_widget_checkbox_user_config (SheetObject *so, SheetControl *sc)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);
	WBCGtk  *wbcg = scg_wbcg (SHEET_CONTROL_GUI (sc));
	CheckboxConfigState *state;
	GtkWidget *table;

	g_return_if_fail (swc != NULL);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
		return;

	state = g_new (CheckboxConfigState, 1);
	state->swc = swc;
	state->wbcg = wbcg;
	state->sheet = sc_sheet	(sc);
	state->old_focus = NULL;
	state->old_label = g_strdup (swc->label);
	state->gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"so-checkbox.glade", NULL, NULL);
	state->dialog = glade_xml_get_widget (state->gui, "SO-Checkbox");

	table = glade_xml_get_widget (state->gui, "table");

	state->expression = gnm_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (state->expression,
		GNM_EE_FORCE_ABS_REF | GNM_EE_SHEET_OPTIONAL | GNM_EE_SINGLE_RANGE,
		GNM_EE_MASK);
	gnm_expr_entry_load_from_dep (state->expression, &swc->dep);
	go_atk_setup_label (glade_xml_get_widget (state->gui, "label_linkto"),
			     GTK_WIDGET (state->expression));
	gtk_table_attach (GTK_TABLE (table), GTK_WIDGET (state->expression),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_widget_show (GTK_WIDGET (state->expression));

	state->label = glade_xml_get_widget (state->gui, "label_entry");
	gtk_entry_set_text (GTK_ENTRY (state->label), swc->label);
	gtk_editable_select_region (GTK_EDITABLE(state->label), 0, -1);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->expression));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->label));

	g_signal_connect (G_OBJECT (state->label),
		"changed",
		G_CALLBACK (cb_checkbox_label_changed), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "ok_button")),
		"clicked",
		G_CALLBACK (cb_checkbox_config_ok_clicked), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (state->gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cb_checkbox_config_cancel_clicked), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_SO_CHECKBOX);

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SHEET_OBJECT_CONFIG_KEY);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_checkbox_config_destroy);

	/* Note:  half of the set-focus action is handle by the default */
	/*        callback installed by wbc_gtk_attach_guru */
	g_signal_connect (G_OBJECT (state->dialog), "set-focus",
		G_CALLBACK (cb_checkbox_set_focus), state);

	gtk_widget_show (state->dialog);
}

static gboolean
sheet_widget_checkbox_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);

	dependent_set_sheet (&swc->dep, sheet);
	sheet_widget_checkbox_set_active (swc);

	return FALSE;
}

static gboolean
sheet_widget_checkbox_clear_sheet (SheetObject *so)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);

	if (dependent_is_linked (&swc->dep))
		dependent_unlink (&swc->dep);
	swc->dep.sheet = NULL;
	return FALSE;
}

static void
sheet_widget_checkbox_foreach_dep (SheetObject *so,
				   SheetObjectForeachDepFunc func,
				   gpointer user)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);
	func (&swc->dep, so, user);
}

static void
sheet_widget_checkbox_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
{
	SheetWidgetCheckbox const *swc = SHEET_WIDGET_CHECKBOX (so);

	gsf_xml_out_add_cstr (output, "Label", swc->label);
	gsf_xml_out_add_int (output, "Value", swc->value);
	sax_write_dep (output, &swc->dep, "Input");
}

static void
sheet_widget_checkbox_prep_sax_parser (SheetObject *so, GsfXMLIn *xin, xmlChar const **attrs)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (attr_eq (attrs[0], "Label")) {
			g_free (swc->label);
			swc->label = g_strdup (CXML2C (attrs[1]));
		} else if (gnm_xml_attr_int (attrs, "Value", &swc->value))
			; /* ??? */
		else if (sax_read_dep (attrs, "Input", &swc->dep, xin))
			; /* ??? */
}

static gboolean
sheet_widget_checkbox_read_xml_dom (SheetObject *so, char const *typename,
				    XmlParseContext const *context,
				    xmlNodePtr tree)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);
	gchar *label = (gchar *)xmlGetProp (tree, (xmlChar *)"Label");

	if (!label) {
		g_warning ("Could not read a CheckBoxWidget object because it lacks a label property");
		return TRUE;
	}

	swc->label = g_strdup (label);
	xmlFree (label);

	read_dep (&swc->dep, "Input", tree, context);
	swc->dep.flags = checkbox_get_dep_type ();
	xml_node_get_int (tree, "Value", &swc->value);

	return FALSE;
}

void
sheet_widget_checkbox_set_link (SheetObject *so, GnmExprTop const *texpr)
{
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);
	dependent_set_expr (&swc->dep, texpr);
	if (NULL != texpr)
		dependent_link (&swc->dep);
}

void
sheet_widget_checkbox_set_label	(SheetObject *so, char const *str)
{
	GList *list;
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (so);
	char *new_label;

	if (go_str_compare (str, swc->label) == 0)
		return;

	new_label = g_strdup (str);
	g_free (swc->label);
	swc->label = new_label;

	list = swc->sow.realized_list;
	for (; list != NULL; list = list->next) {
		FooCanvasWidget *item = FOO_CANVAS_WIDGET (list->data);
		gtk_button_set_label (GTK_BUTTON (item->widget), swc->label);
	}
}

SOW_MAKE_TYPE (checkbox, Checkbox,
	       sheet_widget_checkbox_user_config,
	       sheet_widget_checkbox_set_sheet,
	       sheet_widget_checkbox_clear_sheet,
	       sheet_widget_checkbox_foreach_dep,
	       sheet_widget_checkbox_copy,
	       sheet_widget_checkbox_read_xml_dom,
	       sheet_widget_checkbox_write_xml_sax,
	       sheet_widget_checkbox_prep_sax_parser,
	       sheet_widget_checkbox_get_property,
	       sheet_widget_checkbox_set_property,
	       {
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
	SheetWidgetCheckbox *swc = SHEET_WIDGET_CHECKBOX (sow);
	GtkWidget *button = gtk_toggle_button_new_with_label (swc->label);
	GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);
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
	   SHEET_WIDGET_CHECKBOX_TYPE);
/****************************************************************************/

#define SHEET_WIDGET_RADIO_BUTTON_TYPE	(sheet_widget_radio_button_get_type ())
#define SHEET_WIDGET_RADIO_BUTTON(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_WIDGET_RADIO_BUTTON_TYPE, SheetWidgetRadioButton))
#define DEP_TO_RADIO_BUTTON(d_ptr)	(SheetWidgetRadioButton *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetRadioButton, dep))

typedef struct {
	SheetObjectWidget	sow;

	gboolean	 being_updated;
	char		*label;
	GnmDependent	 dep;
} SheetWidgetRadioButton;
typedef SheetObjectWidgetClass SheetWidgetRadioButtonClass;

static void
radio_button_eval (GnmDependent *dep)
{
	GnmValue *v;
	GnmEvalPos pos;
	gnm_float result;

	v = gnm_expr_top_eval (dep->texpr, eval_pos_init_dep (&pos, dep),
			       GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	result = value_get_as_float (v);
	value_release (v);
#if 0
	if (!err) {
		/* FIXME : finish this when I have a better idea of a group */
		/* SheetWidgetRadioButton *swrb = DEP_TO_RADIO_BUTTON (dep); */
	}
#endif
}

static void
radio_button_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "RadioButton%p", dep);
}

static DEPENDENT_MAKE_TYPE (radio_button, NULL)

static void
sheet_widget_radio_button_init (SheetObjectWidget *sow)
{
	SheetWidgetRadioButton *swrb = SHEET_WIDGET_RADIO_BUTTON (sow);

	swrb->being_updated = FALSE;
	swrb->label = g_strdup (_("RadioButton"));

	swrb->dep.sheet = NULL;
	swrb->dep.flags = radio_button_get_dep_type ();
	swrb->dep.texpr = NULL;
}

static void
sheet_widget_radio_button_finalize (GObject *obj)
{
	SheetWidgetRadioButton *swrb = SHEET_WIDGET_RADIO_BUTTON (obj);

	g_free (swrb->label);
	swrb->label = NULL;

	dependent_set_expr (&swrb->dep, NULL);
	(*sheet_object_widget_class->finalize) (obj);
}

static void
sheet_widget_radio_button_toggled (GtkToggleButton *button,
				   SheetWidgetRadioButton *swrb)
{
	if (swrb->being_updated || !gtk_toggle_button_get_active (button))
		return;
#if 0
	swrb->value = gtk_toggle_button_get_active (button);
	sheet_widget_checkbox_set_active (swrb);
#endif
}

static GtkWidget *
sheet_widget_radio_button_create_widget (SheetObjectWidget *sow)
{
	GtkWidget *w = gtk_radio_button_new_with_label (NULL, "RadioButton");
	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (sheet_widget_radio_button_toggled), sow);
	return w;
}

static gboolean
sheet_widget_radio_button_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetRadioButton *swrb = SHEET_WIDGET_RADIO_BUTTON (so);

	dependent_set_sheet (&swrb->dep, sheet);

	return FALSE;
}

static gboolean
sheet_widget_radio_button_clear_sheet (SheetObject *so)
{
	SheetWidgetRadioButton *swrb = SHEET_WIDGET_RADIO_BUTTON (so);

	g_return_val_if_fail (swrb != NULL, TRUE);

	if (dependent_is_linked (&swrb->dep))
		dependent_unlink (&swrb->dep);
	swrb->dep.sheet = NULL;
	return FALSE;
}

static void
sheet_widget_radio_button_foreach_dep (SheetObject *so,
				       SheetObjectForeachDepFunc func,
				       gpointer user)
{
	SheetWidgetRadioButton *swrb = SHEET_WIDGET_RADIO_BUTTON (so);
	func (&swrb->dep, so, user);
}

void
sheet_widget_radio_button_set_label (SheetObject *so, char const *str)
{
	GList *list;
	SheetWidgetRadioButton *swrb = SHEET_WIDGET_RADIO_BUTTON (so);
	char *new_label;

	if (go_str_compare (str, swrb->label) == 0)
		return;

	new_label = g_strdup (str);
	g_free (swrb->label);
	swrb->label = new_label;

	list = swrb->sow.realized_list;
	for (; list != NULL; list = list->next) {
		FooCanvasWidget *item = FOO_CANVAS_WIDGET (list->data);
		gtk_button_set_label (GTK_BUTTON (item->widget), swrb->label);
	}
}

enum {
	SOR_PROP_0 = 0,
	SOR_PROP_TEXT,
	SOR_PROP_MARKUP
};

static void
sheet_widget_radio_button_get_property (GObject *obj, guint param_id,
				    GValue  *value, GParamSpec *pspec)
{
	SheetWidgetRadioButton *swrb = SHEET_WIDGET_RADIO_BUTTON (obj);

	switch (param_id) {
	case SOC_PROP_TEXT:
		g_value_set_string (value, swrb->label);
		break;
	case SOC_PROP_MARKUP:
		g_value_set_boxed (value, NULL); /* swrb->markup */
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
sheet_widget_radio_button_set_property (GObject *obj, guint param_id,
					GValue const *value, GParamSpec *pspec)
{
	SheetWidgetRadioButton *swrb = SHEET_WIDGET_RADIO_BUTTON (obj);

	switch (param_id) {
	case SOC_PROP_TEXT:
		sheet_widget_radio_button_set_label (SHEET_OBJECT (swrb),
			g_value_get_string (value));
		break;
	case SOC_PROP_MARKUP:
#if 0
		sheet_widget_radio_button_set_markup (SHEET_OBJECT (swc),
			g_value_peek_pointer (value));
#endif
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return;
	}
}

SOW_MAKE_TYPE (radio_button, RadioButton,
	       NULL,
	       sheet_widget_radio_button_set_sheet,
	       sheet_widget_radio_button_clear_sheet,
	       sheet_widget_radio_button_foreach_dep,
	       NULL,
	       NULL,
	       NULL,
	       NULL,
	       sheet_widget_radio_button_get_property,
	       sheet_widget_radio_button_set_property,
	       {
		       g_object_class_install_property
			       (object_class, SOR_PROP_TEXT,
				g_param_spec_string ("text", NULL, NULL, NULL,
						     GSF_PARAM_STATIC | G_PARAM_READWRITE));
		       g_object_class_install_property
			       (object_class, SOC_PROP_MARKUP,
				g_param_spec_boxed ("markup", NULL, NULL, PANGO_TYPE_ATTR_LIST,
						    GSF_PARAM_STATIC | G_PARAM_READWRITE));
	       })

/****************************************************************************/
#define SHEET_WIDGET_LIST_BASE_TYPE     (sheet_widget_list_base_get_type ())
#define SHEET_WIDGET_LIST_BASE(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), SHEET_WIDGET_LIST_BASE_TYPE, SheetWidgetListBase))
#define DEP_TO_LIST_BASE_CONTENT(d_ptr)	(SheetWidgetListBase *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetListBase, content_dep))
#define DEP_TO_LIST_BASE_OUTPUT(d_ptr)	(SheetWidgetListBase *)(((char *)d_ptr) - G_STRUCT_OFFSET(SheetWidgetListBase, output_dep))

typedef struct {
	SheetObjectWidget	sow;

	GnmDependent	content_dep;	/* content of the list */
	GnmDependent	output_dep;	/* selected element */

	GtkTreeModel	*model;
	int		 selection;
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

static GnmCellRef *
sheet_widget_list_base_get_ref (SheetWidgetListBase const *swl,
				GnmCellRef *res, gboolean force_sheet)
{
	GnmValue *target;

	g_return_val_if_fail (swl != NULL, NULL);

	if (swl->output_dep.texpr == NULL)
		return NULL;

	target = gnm_expr_top_get_range (swl->output_dep.texpr);
	if (target == NULL)
		return NULL;

	*res = target->v_range.cell.a;
	value_release (target);

	if (force_sheet && res->sheet == NULL)
		res->sheet = sheet_object_get_sheet (SHEET_OBJECT (swl));
	return res;
}

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
		    sheet_widget_list_base_get_ref (swl, &ref, TRUE) != NULL)
			cmd_so_set_value (wbc,
				_("Clicking in list"),
				&ref, value_new_int (swl->selection));

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
	sheet_widget_list_base_set_selection (DEP_TO_LIST_BASE_OUTPUT (dep),
		floor (value_get_as_float (v)), NULL);
	value_release (v);
}

static void
list_output_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "ListOutput%p", dep);
}

static DEPENDENT_MAKE_TYPE (list_output, NULL)

/*-----------*/
static GnmValue *
cb_collect (GnmValueIter const *iter, GtkListStore *model)
{
	GtkTreeIter list_iter;

	gtk_list_store_append (model, &list_iter);
	if (NULL != iter->v) {
		GOFormat const *fmt = (NULL != iter->cell_iter)
			? gnm_cell_get_format (iter->cell_iter->cell) : NULL;
		char *label = format_value (fmt, iter->v, NULL, -1, NULL);
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
	GnmValue *v = gnm_expr_top_eval (dep->texpr,
		eval_pos_init_dep (&ep, dep),
		GNM_EXPR_EVAL_PERMIT_NON_SCALAR | GNM_EXPR_EVAL_PERMIT_EMPTY);
	GtkListStore *model;

	if (NULL == v)
		return;
	model = gtk_list_store_new (1, G_TYPE_STRING);
	value_area_foreach (v, &ep, CELL_ITER_ALL,
		 (GnmValueIterFunc) cb_collect, model);
	value_release (v);

	if (NULL != swl->model)
		g_object_unref (G_OBJECT (swl->model));
	swl->model = GTK_TREE_MODEL (model);
	g_signal_emit (G_OBJECT (swl), list_base_signals [LIST_BASE_MODEL_CHANGED], 0);
}

static void
list_content_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "ListContent%p", dep);
}

static DEPENDENT_MAKE_TYPE (list_content, NULL)

/*-----------*/

static void
sheet_widget_list_base_init (SheetObjectWidget *sow)
{
	SheetWidgetListBase *swl = SHEET_WIDGET_LIST_BASE (sow);

	swl->content_dep.sheet = NULL;
	swl->content_dep.flags = list_content_get_dep_type ();
	swl->content_dep.texpr = NULL;

	swl->output_dep.sheet = NULL;
	swl->output_dep.flags = list_output_get_dep_type ();
	swl->output_dep.texpr = NULL;

	swl->model = NULL;
	swl->selection = 0;
}

static void
sheet_widget_list_base_finalize (GObject *obj)
{
	SheetWidgetListBase *swl = SHEET_WIDGET_LIST_BASE (obj);
	dependent_set_expr (&swl->content_dep, NULL);
	dependent_set_expr (&swl->output_dep, NULL);
	if (swl->model != NULL)
		g_object_unref (G_OBJECT (swl->model)), swl->model = NULL;
	(*sheet_object_widget_class->finalize) (obj);
}

static void
sheet_widget_list_base_user_config (SheetObject *so, SheetControl *sc)
{
	dialog_so_list (scg_wbcg (SHEET_CONTROL_GUI (sc)), G_OBJECT (so));
}
static gboolean
sheet_widget_list_base_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetWidgetListBase *swl = SHEET_WIDGET_LIST_BASE (so);

	g_return_val_if_fail (swl != NULL, TRUE);
	g_return_val_if_fail (swl->content_dep.sheet == NULL, TRUE);
	g_return_val_if_fail (swl->output_dep.sheet == NULL, TRUE);

	dependent_set_sheet (&swl->content_dep, sheet);
	dependent_set_sheet (&swl->output_dep, sheet);

	return FALSE;
}

static gboolean
sheet_widget_list_base_clear_sheet (SheetObject *so)
{
	SheetWidgetListBase *swl = SHEET_WIDGET_LIST_BASE (so);

	g_return_val_if_fail (swl != NULL, TRUE);

	if (dependent_is_linked (&swl->content_dep))
		dependent_unlink (&swl->content_dep);
	if (dependent_is_linked (&swl->output_dep))
		dependent_unlink (&swl->output_dep);
	swl->content_dep.sheet = swl->output_dep.sheet = NULL;
	return FALSE;
}

static void
sheet_widget_list_base_foreach_dep (SheetObject *so,
				    SheetObjectForeachDepFunc func,
				    gpointer user)
{
	SheetWidgetListBase *swl = SHEET_WIDGET_LIST_BASE (so);
	func (&swl->content_dep, so, user);
	func (&swl->output_dep, so, user);
}

static void
sheet_widget_list_base_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
{
	SheetWidgetListBase const *swl = SHEET_WIDGET_LIST_BASE (so);
	sax_write_dep (output, &swl->content_dep, "Content");
	sax_write_dep (output, &swl->output_dep, "Output");
}

static void
sheet_widget_list_base_prep_sax_parser (SheetObject *so, GsfXMLIn *xin, xmlChar const **attrs)
{
	SheetWidgetListBase *swl = SHEET_WIDGET_LIST_BASE (so);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (sax_read_dep (attrs, "Content", &swl->content_dep, xin)) ;
		else if (sax_read_dep (attrs, "Output", &swl->output_dep, xin)) ;
}

static gboolean
sheet_widget_list_base_read_xml_dom (SheetObject *so, char const *typename,
				     XmlParseContext const *context,
				     xmlNodePtr tree)
{
	SheetWidgetListBase *swl = SHEET_WIDGET_LIST_BASE (so);

	read_dep (&swl->content_dep, "Content", tree, context);
	swl->content_dep.flags = list_content_get_dep_type ();
	read_dep (&swl->output_dep, "Output", tree, context);
	swl->output_dep.flags  = list_output_get_dep_type ();

	return FALSE;
}

static GtkWidget *
sheet_widget_list_base_create_widget (SheetObjectWidget *sow)
{
	g_warning("ERROR: sheet_widget_list_base_create_widget SHOULD NEVER BE CALLED (but it has been)!\n");
	return gtk_frame_new ("invisiwidget(WARNING: I AM A BUG!)");
}

SOW_MAKE_TYPE (list_base, ListBase,
	       sheet_widget_list_base_user_config,
	       sheet_widget_list_base_set_sheet,
	       sheet_widget_list_base_clear_sheet,
	       sheet_widget_list_base_foreach_dep,
	       NULL,
	       sheet_widget_list_base_read_xml_dom,
	       sheet_widget_list_base_write_xml_sax,
	       sheet_widget_list_base_prep_sax_parser,
	       NULL,
	       NULL,
	       {
	       list_base_signals[LIST_BASE_MODEL_CHANGED] = g_signal_new ("model-changed",
			SHEET_WIDGET_LIST_BASE_TYPE,
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (SheetWidgetListBaseClass, model_changed),
			NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);
	       list_base_signals[LIST_BASE_SELECTION_CHANGED] = g_signal_new ("selection-changed",
			SHEET_WIDGET_LIST_BASE_TYPE,
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
	SheetWidgetListBase *swl = SHEET_WIDGET_LIST_BASE (so);
	dependent_set_expr (&swl->output_dep, output);
	if (NULL != output)
		dependent_link (&swl->output_dep);
	dependent_set_expr (&swl->content_dep, content);
	if (NULL != content) {
		dependent_link (&swl->content_dep);
		list_content_eval (&swl->content_dep); /* populate the list */
	}
}
GnmDependent const *
sheet_widget_list_base_get_result_dep  (SheetObject const *so)
{
	SheetWidgetListBase *swl = SHEET_WIDGET_LIST_BASE (so);
	return &swl->output_dep;
}

GnmDependent const *
sheet_widget_list_base_get_content_dep (SheetObject const *so)
{
	SheetWidgetListBase *swl = SHEET_WIDGET_LIST_BASE (so);
	return &swl->content_dep;
}

/****************************************************************************/
#define SHEET_WIDGET_LIST_TYPE	(sheet_widget_list_get_type ())
#define SHEET_WIDGET_LIST(o)	(G_TYPE_CHECK_INSTANCE_CAST((o), SHEET_WIDGET_LIST_TYPE, SheetWidgetList))

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
	gtk_tree_view_set_model (GTK_TREE_VIEW (list), swl->model);
}
static void
cb_selection_changed (GtkTreeSelection *selection,
		      SheetWidgetListBase *swl)
{
	GtkWidget    *view = (GtkWidget *)gtk_tree_selection_get_tree_view (selection);
	GtkTreeModel *model;
	GtkTreeIter   iter;
	int	      pos = 0;
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
		if (NULL != path) {
			pos = *gtk_tree_path_get_indices (path) + 1;
			gtk_tree_path_free (path);
		}
	}
	sheet_widget_list_base_set_selection (swl, pos,
		scg_wbc (GNM_SIMPLE_CANVAS (view->parent->parent->parent)->scg));
}

static GtkWidget *
sheet_widget_list_create_widget (SheetObjectWidget *sow)
{
	SheetWidgetListBase *swl = SHEET_WIDGET_LIST_BASE (sow);
	GtkTreeSelection *selection;
	GtkWidget *frame = g_object_new (GTK_TYPE_FRAME, NULL);
	GtkWidget *list = gtk_tree_view_new_with_model (swl->model);
	GtkWidget *sw = gtk_scrolled_window_new (
		gtk_tree_view_get_hadjustment (GTK_TREE_VIEW (list)),
		gtk_tree_view_get_vadjustment (GTK_TREE_VIEW (list)));
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
		GTK_POLICY_AUTOMATIC,
		GTK_POLICY_ALWAYS);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (list), FALSE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list),
		gtk_tree_view_column_new_with_attributes ("ID",
			gtk_cell_renderer_text_new (), "text", 0,
			NULL));

	gtk_container_add (GTK_CONTAINER (sw), list);
	gtk_container_add (GTK_CONTAINER (frame), sw);

	g_signal_connect_object (G_OBJECT (swl), "model-changed",
		G_CALLBACK (cb_list_model_changed), list, 0);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list));
	g_signal_connect_object (G_OBJECT (swl), "selection-changed",
		G_CALLBACK (cb_list_selection_changed), selection, 0);
	g_signal_connect (selection, "changed",
		G_CALLBACK (cb_selection_changed), swl);
	return frame;
}

static void
sheet_widget_list_class_init (SheetObjectWidgetClass *sow_class)
{
        sow_class->create_widget = &sheet_widget_list_create_widget;
}

GSF_CLASS (SheetWidgetList, sheet_widget_list,
	   &sheet_widget_list_class_init, NULL,
	   SHEET_WIDGET_LIST_BASE_TYPE);

/****************************************************************************/
#define SHEET_WIDGET_COMBO_TYPE	(sheet_widget_combo_get_type ())
#define SHEET_WIDGET_COMBO(o)	(G_TYPE_CHECK_INSTANCE_CAST((o), SHEET_WIDGET_COMBO_TYPE, SheetWidgetCombo))

typedef SheetWidgetListBase		SheetWidgetCombo;
typedef SheetWidgetListBaseClass	SheetWidgetComboClass;

static void
cb_combo_selection_changed (SheetWidgetListBase *swl,
			    GtkComboBox *combo)
{
	int pos = swl->selection - 1;
	if (pos < 0) {
		gtk_entry_set_text (GTK_ENTRY (GTK_BIN (combo)->child), "");
		pos = -1;
	}
	gtk_combo_box_set_active (combo, pos);
}

static void
cb_combo_model_changed (SheetWidgetListBase *swl, GtkComboBox *combo)
{
	gtk_combo_box_set_model (GTK_COMBO_BOX (combo), swl->model);

	/* we can not set this until we have a model,
	 * but after that we can not reset it */
	if (gtk_combo_box_entry_get_text_column (GTK_COMBO_BOX_ENTRY (combo)) < 0)
		gtk_combo_box_entry_set_text_column (GTK_COMBO_BOX_ENTRY (combo), 0);

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
	SheetWidgetListBase *swl = SHEET_WIDGET_LIST_BASE (sow);
	GtkWidget *combo;

	combo = g_object_new (gtk_combo_box_entry_get_type (), NULL);
	GTK_WIDGET_UNSET_FLAGS ((GTK_BIN (combo)->child), GTK_CAN_FOCUS);
	if (swl->model != NULL)
		g_object_set (G_OBJECT (combo),
                      "model",		swl->model,
                      "text-column",	0,
                      NULL);

	g_signal_connect_object (G_OBJECT (swl), "model-changed",
		G_CALLBACK (cb_combo_model_changed), combo, 0);
	g_signal_connect_object (G_OBJECT (swl), "selection-changed",
		G_CALLBACK (cb_combo_selection_changed), combo, 0);
	g_signal_connect (G_OBJECT (combo), "changed",
		G_CALLBACK (cb_combo_changed), swl);

	return combo;
}

static void
sheet_widget_combo_class_init (SheetObjectWidgetClass *sow_class)
{
        sow_class->create_widget = &sheet_widget_combo_create_widget;
}

GSF_CLASS (SheetWidgetCombo, sheet_widget_combo,
	   &sheet_widget_combo_class_init, NULL,
	   SHEET_WIDGET_LIST_BASE_TYPE);
/**************************************************************************/

/**
 * sheet_widget_init_clases:
 * @void:
 *
 * Initilize the classes for the sheet-object-widgets. We need to initalize
 * them before we try loading a sheet that might contain sheet-object-widgets
 **/
void
sheet_object_widget_register (void)
{
	SHEET_WIDGET_FRAME_TYPE;
	SHEET_WIDGET_BUTTON_TYPE;
	SHEET_WIDGET_SCROLLBAR_TYPE;
	SHEET_WIDGET_CHECKBOX_TYPE;
	SHEET_WIDGET_RADIO_BUTTON_TYPE;
	SHEET_WIDGET_LIST_TYPE;
	SHEET_WIDGET_COMBO_TYPE;
	SHEET_WIDGET_SPINBUTTON_TYPE;
	SHEET_WIDGET_SLIDER_TYPE;
}
