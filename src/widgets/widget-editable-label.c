/*
 * A label that can be used to edit its text on demand.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *
 * FIXME: add support for drawing the selection.
 *
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "widget-editable-label.h"
#include <style-color.h>

#include <gtk/gtkentry.h>
#include <libgnomecanvas/gnome-canvas.h>
#include <libgnomeui/libgnomeui.h>
#include <gdk/gdkkeysyms.h>

#define EDITABLE_LABEL_CLASS(k) (GTK_CHECK_CLASS_CAST (k), EDITABLE_LABEL_TYPE)
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

	/* Signals emited by this widget */
	gboolean (* text_changed)    (EditableLabel *el, char const *newtext);
	void     (* editing_stopped) (void);
} EditableLabelClass;

#define MARGIN 1
/*
 * Inside this file we define a number of aliases for the
 * EditableClass object, a short hand for it is EL.
 *
 * I do want to avoid typing.
 */
#define EL(x) EDITABLE_LABEL (x)
typedef EditableLabel El;
typedef EditableLabelClass ElClass;

/* Signals we emit */
enum {
	TEXT_CHANGED,
	EDITING_STOPPED,
	LAST_SIGNAL
};

static guint el_signals [LAST_SIGNAL] = { 0 };

static GnomeCanvasClass *el_parent_class;

static void el_stop_editing (El *el);
static void el_change_text (El *el, const char *text);

static void
el_entry_activate (GtkWidget *entry, El *el)
{
	gboolean accept = TRUE;
	char const *text = gtk_entry_get_text (GTK_ENTRY (el->entry));

	gtk_signal_emit (GTK_OBJECT (el), el_signals [TEXT_CHANGED], text,
			 &accept);

	if (accept)
		editable_label_set_text (el, text);
	else
		editable_label_set_text (el, el->text);

	el_stop_editing (el);
}

static void
el_edit_sync (El *el)
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
			root_group, gnome_canvas_line_get_type (),
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
			root_group, gnome_canvas_rect_get_type (),
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
el_start_editing (El *el, const char *text, gboolean select_text)
{
	gtk_widget_grab_focus (GTK_WIDGET (el));

	/*
	 * Create a GtkEntry to actually do the editing.
	 */
	el->entry = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (el->entry), text);
	gtk_signal_connect (GTK_OBJECT (el->entry), "activate",
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
el_stop_editing (El *el)
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
el_change_text (El *el, const char *text)
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
	El *el = EL (object);

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
	El *el = EL (widget);
	GdkFont *font;
	char *text;

	if (!el->text_item || !GTK_WIDGET_REALIZED (widget)){
		requisition->width = 1;
		requisition->height = 1;
	}

	/* The widget is realized, and we have a text item inside */
	font = gtk_style_get_font (widget->style);
	text = GNOME_CANVAS_TEXT (el->text_item)->text;

	requisition->width = gdk_string_measure (font, text) + MARGIN * 2;
	requisition->height = font->ascent + font->descent + MARGIN * 2;
}

/*
 * GtkWidget button_press_event method override
 */
static gint
el_button_press_event (GtkWidget *widget, GdkEventButton *button)
{
	El *el = EL (widget);

	if (!el->text)
		return FALSE;

	if (el->entry && button->window != widget->window){
		/* Accept the name change */
		el_entry_activate (el->entry, el);

		gdk_event_put ((GdkEvent *)button);
		return TRUE;
	}

	if (button->type == GDK_2BUTTON_PRESS){
		char *text = GNOME_CANVAS_TEXT (el->text_item)->text;

		el_start_editing (el, text, TRUE);

		return FALSE;
	}

	return gtk_widget_event (GTK_WIDGET (el)->parent, (GdkEvent *) button);
}

/*
 * GtkWidget key_press method override
 *
 * If the label is being edited, we forward the event to the GtkEntry widget.
 */
static gint
el_key_press_event (GtkWidget *widget, GdkEventKey *event)
{
	El *el = EL (widget);

	if (!el->entry)
		return FALSE;

	if (event->keyval == GDK_Escape) {
		el_stop_editing (el);
		el_change_text (el, el->text);
		gtk_signal_emit (GTK_OBJECT (el), el_signals [EDITING_STOPPED]);

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
	EditableLabel *el = EL (widget);

	if (GTK_WIDGET_CLASS (el_parent_class)->realize)
		(*GTK_WIDGET_CLASS (el_parent_class)->realize) (widget);

	gnome_canvas_set_scroll_region (GNOME_CANVAS (el), 0, 0, 32000, 32000);
	editable_label_set_color (el, el->color);
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (EL (widget)->text_item),
		"font_desc", widget->style->font_desc,
		NULL);
}

static void
el_class_init (ElClass *klass)
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

	/* The signals */
	el_signals [TEXT_CHANGED] =
		gtk_signal_new (
			"text_changed",
			GTK_RUN_LAST,
			GTK_CLASS_TYPE (klass),
			GTK_SIGNAL_OFFSET (EditableLabelClass, text_changed),
			gtk_marshal_BOOL__POINTER,
			GTK_TYPE_BOOL, 1, GTK_TYPE_POINTER);
	el_signals [EDITING_STOPPED] =
		gtk_signal_new (
			"editing_stopped",
			GTK_RUN_LAST,
			GTK_CLASS_TYPE (klass),
			GTK_SIGNAL_OFFSET (EditableLabelClass, editing_stopped),
			gtk_marshal_NONE__NONE,
			GTK_TYPE_NONE, 0);
}

GtkType
editable_label_get_type (void)
{
	static GtkType el_type = 0;

	if (!el_type){
		GtkTypeInfo el_info = {
			"EditableLabel",
			sizeof (EditableLabel),
			sizeof (EditableLabelClass),
			(GtkClassInitFunc) el_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		el_type = gtk_type_unique (gnome_canvas_get_type (), &el_info);
	}

	return el_type;
}

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

	if (!el->text_item) {
		GnomeCanvasGroup *root_group;
		GtkWidget* text_color_widget;

		text_color_widget = gtk_button_new ();
		gtk_widget_ensure_style (text_color_widget);

		root_group = GNOME_CANVAS_GROUP (GNOME_CANVAS (el)->root);

		el->text_item = gnome_canvas_item_new (
			root_group, gnome_canvas_text_get_type (),
			"anchor",   GTK_ANCHOR_NORTH_WEST,
			"text",     text,
			"x",        (double) 1,
			"y",        (double) 1,
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
		GtkStyle  *style = gtk_style_copy (GTK_WIDGET (el)->style);
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

	el = gtk_type_new (editable_label_get_type ());

	if (text != NULL)
		editable_label_set_text (EDITABLE_LABEL (el), text);
	if (color != NULL)
		editable_label_set_color (EDITABLE_LABEL (el), color);

	return el;
}

