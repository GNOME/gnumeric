/*
 * A label that can be used to edit its text on demand.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *
 * FIXME: add support for drawing the selection.
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "widget-editable-label.h"
#include <style-color.h>
#include <gnm-marshalers.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkwindow.h>
#include <libgnomecanvas/gnome-canvas.h>
#include <libgnomecanvas/gnome-canvas-util.h>
#include <libgnomecanvas/gnome-canvas-line.h>
#include <libgnomecanvas/gnome-canvas-text.h>
#include <libgnomecanvas/gnome-canvas-rect-ellipse.h>
#include <gal/util/e-util.h>

#define EDITABLE_LABEL_CLASS(k) (G_TYPE_CHECK_CLASS_CAST (k), EDITABLE_LABEL_TYPE)
struct _EditableLabel {
	GnomeCanvas     canvas;
	GnomeCanvasItem *text_item;
	StyleColor	*color;

	char            *text;
	GtkWidget       *toplevel;
	GtkWidget       *entry;
	GnomeCanvasItem *cursor;
	GnomeCanvasItem *background;
};

typedef struct {
	GnomeCanvasClass parent_class;

	gboolean (* text_changed)    (EditableLabel *el, char const *newtext);
	void     (* editing_stopped) (EditableLabel *el);
} EditableLabelClass;

#define MARGIN 1

/* Signals we emit */
enum {
	TEXT_CHANGED,
	EDITING_STOPPED,
	LAST_SIGNAL
};

static guint el_signals [LAST_SIGNAL] = { 0 };

static GnomeCanvasClass *el_parent_class;

static void el_stop_editing (EditableLabel *el);
static void el_change_text (EditableLabel *el, const char *text);

static void
el_entry_activate (GtkWidget *entry, EditableLabel *el)
{
	gboolean accept = TRUE;
	char const *text = gtk_entry_get_text (GTK_ENTRY (el->entry));

	g_signal_emit (G_OBJECT (el), el_signals [TEXT_CHANGED], 0,
		       text, &accept);

	if (accept)
		editable_label_set_text (el, text);
	else
		editable_label_set_text (el, el->text);

	el_stop_editing (el);
}

static void
el_edit_sync (EditableLabel *el)
{
	GnomeCanvasGroup *root_group = GNOME_CANVAS_GROUP (GNOME_CANVAS (el)->root);
	GtkEntry *entry = GTK_ENTRY (el->entry);
	GtkWidget *widget = GTK_WIDGET (el);
	GnomeCanvasPoints *points;
	GdkFont *font;
	int cursor_pos;
	char const *text;

	text = gtk_entry_get_text (entry);
	font = gtk_style_get_font (widget->style);

	el_change_text (el, text);

	cursor_pos = gtk_editable_get_position (GTK_EDITABLE (entry));

	points = gnome_canvas_points_new (2);
	points->coords [0] = gdk_text_width (font, text, cursor_pos) + MARGIN;
	points->coords [1] = 1 + MARGIN;
	points->coords [2] = points->coords [0];
	points->coords [3] = font->ascent + font->descent - 1 - MARGIN;

	/* Draw the cursor */
	if (!el->cursor) {
		el->cursor = gnome_canvas_item_new (
			root_group, GNOME_TYPE_CANVAS_LINE,
			"points",         points,
			"fill_color_gdk", &widget->style->fg [GTK_STATE_NORMAL],
			NULL);
		gnome_canvas_item_set (GNOME_CANVAS_ITEM (el->text_item),
			"fill_color_gdk", &widget->style->fg [GTK_STATE_NORMAL],
			NULL);
	} else
		gnome_canvas_item_set (
			el->cursor,
			"points",         points,
			NULL);

	gnome_canvas_points_free (points);

	if (!el->background) {
		el->background = gnome_canvas_item_new (
			root_group, GNOME_TYPE_CANVAS_RECT,
			"x1",                (double) 0,
			"y1",                (double) 0,
			"x2",                (double) gdk_string_measure (font, text) + MARGIN * 2,
			"y2",                (double) font->ascent + font->descent + MARGIN * 2,
			"fill_color_gdk",    &widget->style->base [GTK_STATE_NORMAL],
			"outline_color_gdk", &widget->style->fg [GTK_STATE_NORMAL],
			NULL);

		/* Just a large enough number, to make sure it goes to the very back */
		gnome_canvas_item_lower (el->background, 10);
	} else
		gnome_canvas_item_set (
			el->background,
			"x1",             (double) 0,
			"y1",             (double) 0,
			"x2",             (double) gdk_string_measure (font, text) + MARGIN * 2,
			"y2",             (double) font->ascent + font->descent + MARGIN * 2,
			"fill_color_gdk", &widget->style->base [GTK_STATE_NORMAL],
			NULL);

}

static void
el_start_editing (EditableLabel *el, char const *text, gboolean select_text)
{
	gtk_widget_grab_focus (GTK_WIDGET (el));

	/* Create a GtkEntry to actually do the editing */
	el->entry = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (el->entry), text);
	g_signal_connect (G_OBJECT (el->entry), "activate",
			  GTK_SIGNAL_FUNC (el_entry_activate), el);
	el->toplevel = gtk_window_new (GTK_WINDOW_POPUP);
	gtk_container_add (GTK_CONTAINER (el->toplevel), el->entry);
	gtk_widget_set_uposition (el->toplevel, 20000, 20000);
	gtk_widget_show_all (el->toplevel);

	gtk_grab_add (GTK_WIDGET (el));

	if (select_text)
		gtk_editable_select_region (GTK_EDITABLE (el->entry), 0, -1);

	el_edit_sync (el);
}

static void
el_stop_editing (EditableLabel *el)
{
	if (el->toplevel) {
		gtk_object_destroy (GTK_OBJECT (el->toplevel));
		el->toplevel = NULL;
		el->entry = NULL;
	}

	if (el->cursor) {
		gtk_object_destroy (GTK_OBJECT (el->cursor));
		el->cursor = NULL;
	}

	if (el->background) {
		gtk_object_destroy (GTK_OBJECT (el->background));
		el->background = NULL;
	}

	editable_label_set_color (el, el->color);
	gtk_grab_remove (GTK_WIDGET (el));
}

static void
el_change_text (EditableLabel *el, const char *text)
{
	char *item_text;

	item_text = GNOME_CANVAS_TEXT (el->text_item)->text;

	if (strcmp (text, item_text) == 0)
		return;

	gnome_canvas_item_set (
		GNOME_CANVAS_ITEM (el->text_item),
		"text",  text,
		NULL);
	gtk_widget_queue_resize (GTK_WIDGET (el));
}

/*
 * GtkObject destroy method override
 */
static void
el_destroy (GtkObject *object)
{
	EditableLabel *el = EDITABLE_LABEL (object);

	el_stop_editing (el);
	if (el->text != NULL) {
		g_free (el->text);
		el->text = NULL;
	}

	editable_label_set_color (el, NULL);
	((GtkObjectClass *)el_parent_class)->destroy (object);
}

/*
 * GtkWidget size_request method override
 */
static void
el_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
	EditableLabel *el = EDITABLE_LABEL (widget);
	double width, height;

	if (!el->text_item || !GTK_WIDGET_REALIZED (widget)){
		requisition->width = 1;
		requisition->height = 1;
	}

	g_object_get (G_OBJECT (el->text_item),
		"text_width",  &width,
		"text_height", &height,
		NULL);

	requisition->width = width + MARGIN * 2;
	requisition->height = height + MARGIN * 2;
}

/*
 * GtkWidget button_press_event method override
 */
static gint
el_button_press_event (GtkWidget *widget, GdkEventButton *button)
{
	GtkWidgetClass *widget_class;
	EditableLabel *el = EDITABLE_LABEL (widget);

	if (!el->text)
		return FALSE;

	if (el->entry && button->window != widget->window){
		/* Accept the name change */
		el_entry_activate (el->entry, el);

		gdk_event_put ((GdkEvent *)button);
		return TRUE;
	}

	if (button->type == GDK_2BUTTON_PRESS) {
		el_start_editing (el,
			GNOME_CANVAS_TEXT (el->text_item)->text, TRUE);
		return FALSE;
	}

	widget_class = g_type_class_peek (GNOME_TYPE_CANVAS);
	if (widget_class && widget_class->button_press_event)
		return widget_class->button_press_event (widget, button);
	return FALSE;
}

/*
 * GtkWidget key_press method override
 *
 * If the label is being edited, we forward the event to the GtkEntry widget.
 */
static gint
el_key_press_event (GtkWidget *widget, GdkEventKey *event)
{
	EditableLabel *el = EDITABLE_LABEL (widget);

	if (!el->entry)
		return FALSE;

	if (event->keyval == GDK_Escape) {
		el_stop_editing (el);
		el_change_text (el, el->text);
		g_signal_emit (G_OBJECT (el), el_signals [EDITING_STOPPED], 0);

		return TRUE;
	}

	gtk_widget_event (GTK_WIDGET (el->entry), (GdkEvent *) event);

	/* The previous call could have killed the edition */
	if (el->entry)
		el_edit_sync (el);

	return TRUE;
}

static void
el_realize (GtkWidget *widget)
{
	EditableLabel *el = EDITABLE_LABEL (widget);

	if (GTK_WIDGET_CLASS (el_parent_class)->realize)
		(*GTK_WIDGET_CLASS (el_parent_class)->realize) (widget);

	gnome_canvas_set_scroll_region (GNOME_CANVAS (el), 0, 0, 32000, 32000);
	editable_label_set_color (el, el->color);
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (EDITABLE_LABEL (widget)->text_item),
		"font_desc", widget->style->font_desc,
		NULL);
}

static void
el_class_init (EditableLabelClass *klass)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GnomeCanvasClass *canvas_class;

	object_class = (GtkObjectClass *) klass;
	widget_class = (GtkWidgetClass *) klass;
	canvas_class = (GnomeCanvasClass *) klass;

	el_parent_class = gtk_type_class (gnome_canvas_get_type ());

	object_class->destroy = el_destroy;
	widget_class->size_request = el_size_request;
	widget_class->button_press_event = el_button_press_event;
	widget_class->key_press_event = el_key_press_event;
	widget_class->realize = el_realize;

	el_signals [TEXT_CHANGED] = g_signal_new ("text_changed",
		EDITABLE_LABEL_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EditableLabelClass, text_changed),
		(GSignalAccumulator) NULL, NULL,
		gnm__BOOLEAN__POINTER,
		G_TYPE_BOOLEAN, 1, G_TYPE_POINTER);

	el_signals [EDITING_STOPPED] = g_signal_new ( "editing_stopped",
		EDITABLE_LABEL_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EditableLabelClass, editing_stopped),
		(GSignalAccumulator) NULL, NULL,
		gnm__VOID__VOID,
		GTK_TYPE_NONE, 0);
}

E_MAKE_TYPE (editable_label, "EditableLabel", EditableLabel,
	     el_class_init, NULL, GNOME_TYPE_CANVAS)

void
editable_label_set_text (EditableLabel *el, char const *text)
{
	g_return_if_fail (el != NULL);
	g_return_if_fail (text != NULL);
	g_return_if_fail (IS_EDITABLE_LABEL (el));

	/* This code is usually invoked with el->text as the name */
	if (text != el->text) {
		if (el->text)
			g_free (el->text);

		el->text = g_strdup (text);
	}

	if (el->text_item == NULL) {
		GnomeCanvasGroup *root_group;
		GtkWidget* text_color_widget;

		text_color_widget = gtk_button_new ();
		gtk_widget_ensure_style (text_color_widget);

		root_group = GNOME_CANVAS_GROUP (GNOME_CANVAS (el)->root);

		el->text_item = gnome_canvas_item_new (
			root_group, gnome_canvas_text_get_type (),
			"anchor",   GTK_ANCHOR_NORTH_WEST,
			"text",     text,
			"x",        (double) MARGIN,
			"y",        (double) MARGIN,
			"fill_color_gdk",
				&text_color_widget->style->text[GTK_STATE_NORMAL],
			NULL);
		gtk_widget_destroy (text_color_widget);
	} else
		el_change_text (el, text);
}
char const *
editable_label_get_text  (EditableLabel const *el)
{
	g_return_val_if_fail (IS_EDITABLE_LABEL (el), "");
	return el->text;
}

void
editable_label_set_color (EditableLabel *el, StyleColor *color)
{
	g_return_if_fail (IS_EDITABLE_LABEL (el));

	if (color != NULL)
		style_color_ref (color);
	if (el->color != NULL)
		style_color_unref (el->color);

	el->color = color;
	if (el->color != NULL) {
		int contrast = el->color->color.red + el->color->color.green + el->color->color.blue;
		GtkStyle *style = gtk_style_copy (GTK_WIDGET (el)->style);
		style->bg [GTK_STATE_NORMAL] = el->color->color;
		gtk_widget_set_style (GTK_WIDGET (el), style);
		gtk_style_unref (style);

		gnome_canvas_item_set (el->text_item,
			"fill_color_gdk", (contrast >= 0x18000) ? &gs_black : &gs_white,
			NULL);
	}
}

GtkWidget *
editable_label_new (char const *text, StyleColor *color)
{
	GtkWidget *el;

	el = g_object_new (EDITABLE_LABEL_TYPE, NULL);

	if (text != NULL)
		editable_label_set_text (EDITABLE_LABEL (el), text);
	if (color != NULL)
		editable_label_set_color (EDITABLE_LABEL (el), color);

	return el;
}
