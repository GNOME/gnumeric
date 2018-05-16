/*
 * gnm-notebook.c: Implements a button-only notebook.
 *
 * Copyright (c) 2008 Morten Welinder <terra@gnome.org>
 * Copyright notices for included gtknotebook.c, see below.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 **/

#include <gnumeric-config.h>
#include <widgets/gnm-notebook.h>
#include <goffice/goffice.h>
#include <gnm-i18n.h>
#include <gsf/gsf-impl-utils.h>

/* ------------------------------------------------------------------------- */

/**
 * gnm_notebook_get_nth_label: (skip)
 */

/**
 * gnm_notebook_get_current_label: (skip)
 */

/* ------------------------------------------------------------------------- */

struct GnmNotebookButton_ {
	/*
	 * We need to derive from GtkLabel mostly for theming reasons,
	 * but GtkLabel is also special for clipping.
	 */
	GtkLabel base;

	PangoLayout *layout;
	PangoLayout *layout_active;

	PangoRectangle logical, logical_active;
	int x_offset, x_offset_active;

	GdkRGBA *fg, *bg;
};

typedef struct {
	GtkLabelClass parent_class;
} GnmNotebookButtonClass;

static GObjectClass *gnm_notebook_button_parent_class;

enum {
	NBB_PROP_0,
	NBB_PROP_BACKGROUND_COLOR,
	NBB_PROP_TEXT_COLOR
};

static void
gnm_notebook_button_finalize (GObject *obj)
{
	GnmNotebookButton *nbb = GNM_NOTEBOOK_BUTTON (obj);
	g_clear_object (&nbb->layout);
	g_clear_object (&nbb->layout_active);
	gdk_rgba_free (nbb->fg);
	gdk_rgba_free (nbb->bg);
	gnm_notebook_button_parent_class->finalize (obj);
}

static void
gnm_notebook_button_set_property (GObject      *obj,
				  guint         prop_id,
				  const GValue *value,
				  GParamSpec   *pspec)
{
	GnmNotebookButton *nbb = GNM_NOTEBOOK_BUTTON (obj);

	switch (prop_id) {
	case NBB_PROP_BACKGROUND_COLOR:
		gdk_rgba_free (nbb->bg);
		nbb->bg = g_value_dup_boxed (value);
		gtk_widget_queue_draw (GTK_WIDGET (obj));
		g_clear_object (&nbb->layout);
		g_clear_object (&nbb->layout_active);
		break;
	case NBB_PROP_TEXT_COLOR:
		gdk_rgba_free (nbb->fg);
		nbb->fg = g_value_dup_boxed (value);
		gtk_widget_queue_draw (GTK_WIDGET (obj));
		gtk_widget_override_color (GTK_WIDGET (obj),
					   GTK_STATE_FLAG_NORMAL,
					   nbb->fg);
		gtk_widget_override_color (GTK_WIDGET (obj),
					   GTK_STATE_FLAG_ACTIVE,
					   nbb->fg);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
gnm_notebook_button_ensure_layout (GnmNotebookButton *nbb)
{
	const char *text = gtk_label_get_text (GTK_LABEL (nbb));

	if (nbb->layout) {
		if (strcmp (text, pango_layout_get_text (nbb->layout)) == 0)
			return;
		pango_layout_set_text (nbb->layout, text, -1);
		pango_layout_set_text (nbb->layout_active, text, -1);
	} else {
		PangoAttrList *attrs, *attrs_active;
		PangoAttribute *attr;
		PangoFontDescription *desc;
		GtkWidget *widget = GTK_WIDGET (nbb);
		GtkStyleContext *context =
			gtk_widget_get_style_context (widget);

		nbb->layout = gtk_widget_create_pango_layout (widget, text);
		nbb->layout_active = gtk_widget_create_pango_layout (widget, text);

		/* Common */
		attrs = pango_attr_list_new ();
		if (nbb->bg) {
			attr = go_color_to_pango
				(go_color_from_gdk_rgba (nbb->bg, NULL),
				 FALSE);
			attr->start_index = 0;
			attr->end_index = -1;
			pango_attr_list_insert (attrs, attr);
		}
		attrs_active = pango_attr_list_copy (attrs);

		// As-of gtk+ 3.20 we have to set the context state to the state
		// we are querying for.  This ought to work before gtk+ 3.20
		// too.
		gtk_style_context_save (context);

		/* Normal */
		gtk_style_context_set_state (context, GTK_STATE_FLAG_NORMAL);
		gtk_style_context_get (context, GTK_STATE_FLAG_NORMAL,
				       "font", &desc, NULL);
		attr = pango_attr_font_desc_new (desc);
		attr->start_index = 0;
		attr->end_index = -1;
		pango_attr_list_insert (attrs, attr);
		pango_font_description_free (desc);
		pango_layout_set_attributes (nbb->layout, attrs);
		pango_attr_list_unref (attrs);

		/* Active */
		gtk_style_context_set_state (context, GTK_STATE_FLAG_ACTIVE);
		gtk_style_context_get (context, GTK_STATE_FLAG_ACTIVE,
				       "font", &desc, NULL);
		attr = pango_attr_font_desc_new (desc);
		attr->start_index = 0;
		attr->end_index = -1;
		pango_attr_list_insert (attrs_active, attr);
		pango_font_description_free (desc);
		pango_layout_set_attributes (nbb->layout_active, attrs_active);
		pango_attr_list_unref (attrs_active);

		gtk_style_context_restore (context);
	}

	pango_layout_get_extents (nbb->layout, NULL, &nbb->logical);
	pango_layout_get_extents (nbb->layout_active, NULL, &nbb->logical_active);
}

static void
gnm_notebook_button_screen_changed (GtkWidget *widget, G_GNUC_UNUSED GdkScreen *prev)
{
	GnmNotebookButton *nbb = GNM_NOTEBOOK_BUTTON (widget);
	g_clear_object (&nbb->layout);
	g_clear_object (&nbb->layout_active);
}

static gboolean
gnm_notebook_button_draw (GtkWidget *widget, cairo_t *cr)
{
	GnmNotebookButton *nbb = GNM_NOTEBOOK_BUTTON (widget);
	GnmNotebook *nb = GNM_NOTEBOOK (gtk_widget_get_parent (widget));
	GtkStyleContext *context = gtk_widget_get_style_context (widget);
	gboolean is_active = (widget == gnm_notebook_get_current_label (nb));
	GtkStateFlags state =
		is_active ? GTK_STATE_FLAG_ACTIVE : GTK_STATE_FLAG_NORMAL;
	GtkBorder padding;

	gtk_style_context_save (context);
	gtk_style_context_set_state (context, state);

	gtk_style_context_get_padding (context, state, &padding);

	gnm_notebook_button_ensure_layout (nbb);

	gtk_render_layout (context, cr,
			   padding.left + (is_active ? nbb->x_offset_active : nbb->x_offset),
			   0,
			   is_active ? nbb->layout_active : nbb->layout);

	gtk_style_context_restore (context);
	return FALSE;
}

static GtkSizeRequestMode
gnm_notebook_button_get_request_mode (GtkWidget *widget)
{
	return GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

static void
gnm_notebook_button_get_preferred_height (GtkWidget *widget,
					  gint      *minimum,
					  gint      *natural)
{
	GnmNotebookButton *nbb = GNM_NOTEBOOK_BUTTON (widget);
	GtkBorder padding;
	GtkStyleContext *ctxt = gtk_widget_get_style_context (widget);

	// As-of gtk+ 3.20 we have to set the context state to the state
	// we are querying for.  This ought to work before gtk+ 3.20 too.
	gtk_style_context_save (ctxt);
	gtk_style_context_set_state (ctxt, GTK_STATE_FLAG_NORMAL);
	gtk_style_context_get_padding (ctxt,
				       GTK_STATE_FLAG_NORMAL,
				       &padding);
	gtk_style_context_restore (ctxt);

	gnm_notebook_button_ensure_layout (nbb);

	*minimum = *natural =
		(padding.top +
		 PANGO_PIXELS_CEIL (MAX (nbb->logical.height,
					 nbb->logical_active.height)) +
		 padding.bottom);
}

static void
gnm_notebook_button_get_preferred_width (GtkWidget *widget,
					 gint      *minimum,
					 gint      *natural)
{
	GnmNotebookButton *nbb = GNM_NOTEBOOK_BUTTON (widget);
	GtkBorder padding;
	GtkStyleContext *ctxt = gtk_widget_get_style_context (widget);

	// As-of gtk+ 3.20 we have to set the context state to the state
	// we are querying for.  This ought to work before gtk+ 3.20 too.
	gtk_style_context_save (ctxt);
	gtk_style_context_set_state (ctxt, GTK_STATE_FLAG_NORMAL);
	gtk_style_context_get_padding (ctxt,
				       GTK_STATE_FLAG_NORMAL,
				       &padding);
	gtk_style_context_restore (ctxt);

	gnm_notebook_button_ensure_layout (nbb);

	*minimum = *natural =
		(padding.left +
		 PANGO_PIXELS_CEIL (MAX (nbb->logical.width,
					 nbb->logical_active.width)) +
		 padding.right);
}

static void
gnm_notebook_button_size_allocate (GtkWidget     *widget,
				   GtkAllocation *allocation)
{
	GnmNotebookButton *nbb = GNM_NOTEBOOK_BUTTON (widget);

	gnm_notebook_button_ensure_layout (nbb);
	nbb->x_offset =
		(allocation->width - PANGO_PIXELS (nbb->logical.width)) / 2;
	nbb->x_offset_active =
		(allocation->width - PANGO_PIXELS (nbb->logical_active.width)) / 2;

	GTK_WIDGET_CLASS(gnm_notebook_button_parent_class)
		->size_allocate (widget, allocation);
}

static void
gnm_notebook_button_class_init (GObjectClass *klass)
{
	GtkWidgetClass *wclass = (GtkWidgetClass *)klass;

	gnm_notebook_button_parent_class = g_type_class_peek_parent (klass);
	klass->finalize = gnm_notebook_button_finalize;
	klass->set_property = gnm_notebook_button_set_property;

	g_object_class_install_property
		(klass,
		 NBB_PROP_BACKGROUND_COLOR,
		 g_param_spec_boxed ("background-color",
				     P_("Background Color"),
				     P_("Override color to use for background"),
				     GDK_TYPE_RGBA,
				     G_PARAM_WRITABLE));
	g_object_class_install_property
		(klass,
		 NBB_PROP_TEXT_COLOR,
		 g_param_spec_boxed ("text-color",
				     P_("Text Color"),
				     P_("Override color to use for label"),
				     GDK_TYPE_RGBA,
				     G_PARAM_WRITABLE));

	wclass->draw = gnm_notebook_button_draw;
	wclass->screen_changed = gnm_notebook_button_screen_changed;
	wclass->get_request_mode = gnm_notebook_button_get_request_mode;
	wclass->get_preferred_width = gnm_notebook_button_get_preferred_width;
	wclass->get_preferred_height = gnm_notebook_button_get_preferred_height;
	wclass->size_allocate = gnm_notebook_button_size_allocate;
}

static void
gnm_notebook_button_init (GnmNotebookButton *nbb)
{
}

GSF_CLASS (GnmNotebookButton, gnm_notebook_button,
	   gnm_notebook_button_class_init,
	   gnm_notebook_button_init, GTK_TYPE_LABEL)
#if 0
	;
#endif

/* ------------------------------------------------------------------------- */

struct _GnmNotebook {
	GtkNotebook parent;

	/*
	 * This is the number of pixels from a regular notebook that
	 * we are not drawing.  It is caused by the empty widgets
	 * that we have to use.
	 */
	int dummy_height;
};

typedef struct {
	GtkNotebookClass parent_class;
} GnmNotebookClass;

static GtkNotebookClass *gnm_notebook_parent_class;

#define DUMMY_KEY "GNM-NOTEBOOK-DUMMY-WIDGET"

static void
gnm_notebook_size_allocate (GtkWidget     *widget,
			    GtkAllocation *allocation)
{
	int i, h = 0;
	GnmNotebook *gnb = (GnmNotebook *)widget;
	GtkAllocation alc = *allocation;

	for (i = 0; TRUE; i++) {
		GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (widget), i);
		GtkAllocation a;
		if (!page)
			break;
		if (!gtk_widget_get_visible (page))
			continue;
		gtk_widget_get_allocation (page, &a);
		h = MAX (h, a.height);
	}

	gnb->dummy_height = h;

	alc.y -= h;
	((GtkWidgetClass *)gnm_notebook_parent_class)->size_allocate
		(widget, &alc);
}

static gboolean
gnm_notebook_button_press (GtkWidget      *widget,
			   GdkEventButton *event)
{
	GnmNotebook *nb = GNM_NOTEBOOK (widget);
	unsigned ui;

	for (ui = 0; /* Nothing */; ui++) {
		GtkWidget *child = gnm_notebook_get_nth_label (nb, ui);
		GtkAllocation child_allocation;

		if (!child)
			break;

		if (!gtk_widget_get_child_visible (child))
			continue;

		gtk_widget_get_allocation (child, &child_allocation);

		if (event->x >= child_allocation.x &&
		    event->x <  child_allocation.x + child_allocation.width &&
		    event->y >= child_allocation.y &&
		    event->y <  child_allocation.y + child_allocation.height) {
			if (0)
				g_printerr ("Button %d pressed\n", ui);
			if (gtk_widget_event (child, (GdkEvent*)event))
				return TRUE;
			else
				break;
		}
	}

	return GTK_WIDGET_CLASS(gnm_notebook_parent_class)
		->button_press_event (widget, event);
}

static GType
gnm_notebook_child_type (G_GNUC_UNUSED GtkContainer *container)
{
	return GNM_NOTEBOOK_BUTTON_TYPE;
}

static void
gnm_notebook_class_init (GtkWidgetClass *klass)
{
	GtkWidgetClass *wclass = (GtkWidgetClass *)klass;
	GtkContainerClass *cclass = (GtkContainerClass *)klass;

	gnm_notebook_parent_class = g_type_class_peek (GTK_TYPE_NOTEBOOK);
	klass->size_allocate = gnm_notebook_size_allocate;

	cclass->child_type = gnm_notebook_child_type;

	wclass->button_press_event = gnm_notebook_button_press;
}

static void
gnm_notebook_init (GnmNotebook *notebook)
{
	gtk_notebook_set_scrollable (GTK_NOTEBOOK (notebook), TRUE);
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_BOTTOM);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (notebook), FALSE);
	gtk_notebook_set_group_name (GTK_NOTEBOOK (notebook), "Gnumeric");
}

GSF_CLASS (GnmNotebook, gnm_notebook,
	   gnm_notebook_class_init, gnm_notebook_init, GTK_TYPE_NOTEBOOK)

int
gnm_notebook_get_n_visible (GnmNotebook *nb)
{
	int count = 0;
	GList *l, *children = gtk_container_get_children (GTK_CONTAINER (nb));

	for (l = children; l; l = l->next) {
		GtkWidget *child = l->data;
		if (gtk_widget_get_visible (child))
			count++;
	}

	g_list_free (children);

	return count;
}

GtkWidget *
gnm_notebook_get_nth_label (GnmNotebook *nb, int n)
{
	GtkWidget *page;

	g_return_val_if_fail (GNM_IS_NOTEBOOK (nb), NULL);

	page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (nb), n);
	if (!page)
		return NULL;

	return gtk_notebook_get_tab_label (GTK_NOTEBOOK (nb), page);
}

GtkWidget *
gnm_notebook_get_current_label (GnmNotebook *nb)
{
	int i;

	g_return_val_if_fail (GNM_IS_NOTEBOOK (nb), NULL);

	i = gtk_notebook_get_current_page (GTK_NOTEBOOK (nb));
	return i == -1 ? NULL : gnm_notebook_get_nth_label (nb, i);
}

static void
cb_label_destroyed (G_GNUC_UNUSED GtkWidget *label, GtkWidget *dummy)
{
	gtk_widget_destroy (dummy);
}

static void
cb_label_visibility (GtkWidget *label,
		     G_GNUC_UNUSED GParamSpec *pspec,
		     GtkWidget *dummy)
{
	gtk_widget_set_visible (dummy, gtk_widget_get_visible (label));
}

void
gnm_notebook_insert_tab (GnmNotebook *nb, GtkWidget *label, int pos)
{
	GtkWidget *dummy_page = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_size_request (dummy_page, 1, 1);

	g_object_set_data (G_OBJECT (label), DUMMY_KEY, dummy_page);

	g_signal_connect_object (G_OBJECT (label), "destroy",
				 G_CALLBACK (cb_label_destroyed), dummy_page,
				 0);

	cb_label_visibility (label, NULL, dummy_page);
	g_signal_connect_object (G_OBJECT (label), "notify::visible",
				 G_CALLBACK (cb_label_visibility), dummy_page,
				 0);

	gtk_notebook_insert_page (GTK_NOTEBOOK (nb), dummy_page, label, pos);

	gtk_notebook_set_tab_reorderable (GTK_NOTEBOOK (nb), dummy_page,
					  TRUE);
}

void
gnm_notebook_move_tab (GnmNotebook *nb, GtkWidget *label, int newpos)
{
	GtkWidget *child = g_object_get_data (G_OBJECT (label), DUMMY_KEY);
	gtk_notebook_reorder_child (GTK_NOTEBOOK (nb), child, newpos);
}

void
gnm_notebook_set_current_page (GnmNotebook *nb, int page)
{
	gtk_notebook_set_current_page (GTK_NOTEBOOK (nb), page);
}

void
gnm_notebook_prev_page (GnmNotebook *nb)
{
	gtk_notebook_prev_page (GTK_NOTEBOOK (nb));
}

void
gnm_notebook_next_page (GnmNotebook *nb)
{
	gtk_notebook_next_page (GTK_NOTEBOOK (nb));
}
