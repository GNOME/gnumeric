/*
 * A label that can be used to edit its text on demand.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include "widget-editable-label.h"

#define MARGIN 1
/* 
 * Inside this file we define a number of aliases for the
 * EditableClass object, a short hand for it is EL.
 *
 * I do want to avoid typing.
 */ 
#define EL(x) EDITABLE_LABEL(x)
typedef EditableLabel El;
typedef EditableLabelClass ElClass;

/* Signals we emit */
enum {
	TEXT_CHANGED,
	LAST_SIGNAL
};

static guint el_signals [LAST_SIGNAL] = { 0 };

static GnomeCanvasClass *el_parent_class;

static void el_stop_editing (El *el);
static void el_change_text (El *el, const char *text);
static void el_realize (GtkWidget *widget);

static void
el_entry_activate (GtkWidget *entry, El *el)
{
	gboolean accept = TRUE;
	char *text = gtk_entry_get_text (GTK_ENTRY (el->entry));
	
	gtk_signal_emit (GTK_OBJECT (el), el_signals [TEXT_CHANGED], text, &accept);

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
	char *text;

	text = gtk_entry_get_text (entry);
	font = widget->style->font;
	
	el_change_text (el, text);

	cursor_pos = GTK_EDITABLE (entry)->current_pos;
	
	points = gnome_canvas_points_new (2);
	points->coords [0] = gdk_text_width (font, text, cursor_pos) + MARGIN;
	points->coords [1] = 1 + MARGIN;
	points->coords [2] = points->coords [0];
	points->coords [3] = font->ascent + font->descent - 1 - MARGIN;
	
	/* Draw the cursor */
	if (!el->cursor)
		el->cursor = gnome_canvas_item_new (
			root_group, gnome_canvas_line_get_type (),
			"points",         points,
			"fill_color_gdk", &widget->style->fg [GTK_STATE_NORMAL],
			NULL);
	else
		gnome_canvas_item_set (
			el->cursor,
			"points",         points,
			NULL);

	gnome_canvas_points_free (points);
	
	if (!el->background){
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
el_start_editing (El *el, const char *text)
{
	GtkWidget *toplevel;

	/*
	 * Temporarly be focusable
	 */
	GTK_WIDGET_SET_FLAGS (GTK_WIDGET (el), GTK_CAN_FOCUS);
	gtk_widget_grab_focus (GTK_WIDGET (el));

	/*
	 * Create a GtkEntry to actually do the editing.
	 */
	el->entry = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (el->entry), text);
	gtk_signal_connect (GTK_OBJECT (el->entry), "activate",
			    GTK_SIGNAL_FUNC (el_entry_activate), el);
	toplevel = gtk_window_new (GTK_WINDOW_POPUP);
	gtk_container_add (GTK_CONTAINER (toplevel), el->entry);
	gtk_widget_set_uposition (toplevel, 20000, 20000);
	gtk_widget_show_all (toplevel);

	gtk_grab_add (GTK_WIDGET (el));
	
/*	gtk_editable_select_region (GTK_EDITABLE (el->entry, 0, -1)); */

	/*
	 * Syncronize the GtkEntry with the label
	 */
	el_edit_sync (el);
	
}

static void
el_stop_editing (El *el)
{
	if (el->entry){
		GtkWidget *toplevel = gtk_widget_get_toplevel (el->entry);
		
		gtk_object_unref (GTK_OBJECT (toplevel));
		el->entry = NULL;
	}

	if (el->cursor){
		gtk_object_unref (GTK_OBJECT (el->cursor));
		el->cursor = NULL;
	}

	if (el->background){
		gtk_object_unref (GTK_OBJECT (el->background));
		el->background = NULL;
	}

	gtk_grab_remove (GTK_WIDGET (el));
	GTK_WIDGET_UNSET_FLAGS (GTK_WIDGET (el), GTK_CAN_FOCUS);
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
	if (el->text)
		g_free (el->text);
	
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
	font = widget->style->font;
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
		el_stop_editing (el);
		return FALSE;
	}
	
	if (button->type == GDK_2BUTTON_PRESS){
		char *text = GNOME_CANVAS_TEXT (el->text_item)->text;

		el_start_editing (el, text);
					    
		return FALSE;
	}

	gtk_widget_event (GTK_WIDGET (el)->parent, (GdkEvent *) button);
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
	El *el = EL (widget);

	if (!el->entry)
		return FALSE;

	if (event->keyval == GDK_Escape){
		el_stop_editing (el);
		el_change_text (el, el->text);
		
		return TRUE;
	}
	
	gtk_widget_event (GTK_WIDGET (el->entry), (GdkEvent *) event);

	/* The previous call could have killed the edition */
	if (el->entry)
		el_edit_sync (el);

	return TRUE;
}

static void
el_realize(GtkWidget *widget)
{
  if (GTK_WIDGET_CLASS (el_parent_class)->realize)
    (*GTK_WIDGET_CLASS (el_parent_class)->realize)(widget);

  gnome_canvas_item_set(GNOME_CANVAS_ITEM (EL (widget)->text_item),
                        "font_gdk",
                        widget->style->font, NULL);
}

static void
el_class_init (ElClass *class)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GnomeCanvasClass *canvas_class;

	object_class = (GtkObjectClass *) class;
	widget_class = (GtkWidgetClass *) class;
	canvas_class = (GnomeCanvasClass *) class;

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
			object_class->type,
			GTK_SIGNAL_OFFSET (EditableLabelClass, text_changed),
			gtk_marshal_BOOL__POINTER,
			GTK_TYPE_BOOL, 1, GTK_TYPE_POINTER);
	
	gtk_object_class_add_signals (object_class, el_signals, LAST_SIGNAL);
}

static void
el_init (El *el)
{
	gnome_canvas_set_scroll_region (GNOME_CANVAS (el), 0, 0, 32000, 32000);
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
			(GtkObjectInitFunc) el_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		el_type = gtk_type_unique (gnome_canvas_get_type (), &el_info);
	}

	return el_type;
}

void
editable_label_set_text (EditableLabel *el, const char *text)
{
	g_return_if_fail (el != NULL);
	g_return_if_fail (text != NULL);
	g_return_if_fail (IS_EDITABLE_LABEL (el));

	/* This code is usually invoked with el->text as the name */
	if (text != el->text){
		if (el->text)
			g_free (el->text);

		el->text = g_strdup (text);
	}

	if (!el->text_item){
		GnomeCanvasGroup *root_group;

		root_group = GNOME_CANVAS_GROUP (GNOME_CANVAS (el)->root);
		
		el->text_item = gnome_canvas_item_new (
			root_group, gnome_canvas_text_get_type (),
			"anchor",   GTK_ANCHOR_NORTH_WEST,
			"text",     text,
			"x",        (double) 1,
			"y",        (double) 1,
			NULL);
	} else {
		el_change_text (el, text);
	}
}

GtkWidget *
editable_label_new (const char *text)
{
	GtkWidget *el;

	el = gtk_type_new (editable_label_get_type ());

	if (text)
		editable_label_set_text (EDITABLE_LABEL (el), text);
	
	return el;
}

