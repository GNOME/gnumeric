/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * widget-editable-label.c: A label that can be used to edit its text on demand
 * 			and provides control over its colour.
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
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
#include "widget-editable-label.h"
#include <style-color.h>
#include <gnm-marshalers.h>

#include <gtk/gtkentry.h>
#include <gtk/gtkmain.h>
#include <gdk/gdkkeysyms.h>
#include <gsf/gsf-impl-utils.h>

#include <string.h>

#define EDITABLE_LABEL_CLASS(k) (G_TYPE_CHECK_CLASS_CAST (k), EDITABLE_LABEL_TYPE)
struct _EditableLabel {
	GtkEntry  entry;

	GdkColor  base, text;
	gboolean  base_set, text_set;
	char	 *unedited_text;
};

typedef struct {
	GtkEntryClass  entry;

	gboolean (* edit_finished) (EditableLabel *el, char const *newtext);
} EditableLabelClass;

#define MARGIN 1

/* Signals we emit */
enum {
	EDIT_FINISHED,
	LAST_SIGNAL
};

#define BASE_TYPE GTK_TYPE_ENTRY

static guint el_signals [LAST_SIGNAL] = { 0 };

static void
el_set_color_gdk (EditableLabel *el, GdkColor *base, GdkColor *text)
{
	GtkStyle *s = gtk_style_copy (gtk_widget_get_style (GTK_WIDGET (el)));

	GdkColor tmp = s->base [GTK_STATE_NORMAL];
	s->base [GTK_STATE_NORMAL] = *base;
	*base = tmp;

	tmp = s->text [GTK_STATE_NORMAL];
	s->text [GTK_STATE_NORMAL] = *text;
	*text = tmp;

	gtk_widget_set_style (GTK_WIDGET (el), s);
	gtk_style_unref (s);
}

static void
el_set_cursor (GtkEntry *entry, GdkCursorType cursor_type)
{
	GdkCursor *cursor = gdk_cursor_new (cursor_type);
	gdk_window_set_cursor (entry->text_area, cursor);
	gdk_cursor_unref (cursor);
}

static void
el_stop_editing (EditableLabel *el)
{
	if (el->unedited_text == NULL)
		return;

	g_free (el->unedited_text);
	el->unedited_text = NULL;

	el_set_color_gdk (el, &el->base, &el->text);
	el_set_cursor (GTK_ENTRY (el), GDK_HAND2);
	gtk_editable_set_editable (GTK_EDITABLE (el), FALSE);
	gtk_editable_select_region (GTK_EDITABLE (el), 0, 0);
	gtk_grab_remove (GTK_WIDGET (el));
}

static void
el_entry_activate (GtkEntry *entry, G_GNUC_UNUSED gpointer ignored)
{
	EditableLabel *el = EDITABLE_LABEL (entry);
	gboolean reject = FALSE;
	char const *text = gtk_entry_get_text (entry);

	if (el->unedited_text  == NULL)
		return;

	if (!strcmp (el->unedited_text, text))
		text = NULL;
	g_signal_emit (G_OBJECT (entry), el_signals [EDIT_FINISHED], 0,
		       text, &reject);
	if (reject)
		editable_label_set_text (el, el->unedited_text);
	el_stop_editing (el);
}

static void
el_destroy (GtkObject *object)
{
	EditableLabel *el = EDITABLE_LABEL (object);
	GtkObjectClass *base;

	el_stop_editing (el);

	base = g_type_class_peek (BASE_TYPE);
	base->destroy (object);
}

static gint
el_button_press_event (GtkWidget *widget, GdkEventButton *button)
{
	GtkWidgetClass *base;
	EditableLabel *el = EDITABLE_LABEL (widget);

	if (button->window != widget->window &&
	    button->window != el->entry.text_area) {
		/* Accept the name change */
		el_entry_activate (GTK_ENTRY (el), NULL);
		gdk_event_put ((GdkEvent *)button);
		return TRUE;
	}

	if (button->type == GDK_2BUTTON_PRESS) {
		editable_label_start_editing (el);
		return FALSE;
	}

	if (el->unedited_text == NULL)
		return FALSE;

	base = g_type_class_peek (BASE_TYPE);
	return base->button_press_event (widget, button);
}

/*
 * GtkWidget key_press method override
 *
 * If the label is being edited, we forward the event to the GtkEntry widget.
 */
static gint
el_key_press_event (GtkWidget *w, GdkEventKey *event)
{
	GtkWidgetClass *base;
	EditableLabel  *el = EDITABLE_LABEL (w);

	if (el->unedited_text == NULL)
		return FALSE;

	if (event->keyval == GDK_Escape) {
		gboolean dummy;
		el_stop_editing (el);
		g_signal_emit (G_OBJECT (el), el_signals [EDIT_FINISHED], 0,
			       NULL, &dummy);
		return TRUE;
	}
	base = g_type_class_peek (BASE_TYPE);
	return base->key_press_event (w, event);
}

static void
el_size_request (GtkWidget *el, GtkRequisition *req)
{
	PangoRectangle	 logical_rect;
	PangoLayoutLine *line;
	PangoLayout	*layout;
	GtkWidgetClass	*base = g_type_class_peek (BASE_TYPE);

	base->size_request (el, req);
	layout = gtk_entry_get_layout (GTK_ENTRY (el));
	line = pango_layout_get_lines (layout)->data;
	pango_layout_line_get_extents (line, NULL, &logical_rect);

	req->width = logical_rect.width / PANGO_SCALE + 2*2;
}

static void
el_entry_realize (GtkWidget *widget)
{
	GtkWidgetClass	*base   = g_type_class_peek (BASE_TYPE);
	base->realize (widget);
	el_set_cursor (GTK_ENTRY (widget), GDK_HAND2);
}

static void
el_class_init (GtkObjectClass *object_class)
{
	GtkWidgetClass *widget_class;

	object_class->destroy = el_destroy;

	widget_class = (GtkWidgetClass *) object_class;
	widget_class->button_press_event = el_button_press_event;
	widget_class->key_press_event	 = el_key_press_event;
	widget_class->size_request	 = el_size_request;
	widget_class->realize		 = el_entry_realize;

	el_signals [EDIT_FINISHED] = g_signal_new ("edit_finished",
		EDITABLE_LABEL_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EditableLabelClass, edit_finished),
		(GSignalAccumulator) NULL, NULL,
		gnm__BOOLEAN__POINTER,
		G_TYPE_BOOLEAN, 1, G_TYPE_POINTER);
}

static void
cb_el_changed (GtkWidget *w, G_GNUC_UNUSED gpointer ignored)
{
	gtk_widget_queue_resize	(w);
}

static void
cb_el_parent_set (GtkWidget *w, G_GNUC_UNUSED gpointer ignored,
		  G_GNUC_UNUSED gpointer ignored_2)
{
	EditableLabel *el = EDITABLE_LABEL (w);
	GtkStyle *s = gtk_widget_get_style (w);
	if (!el->base_set)
		el->base = s->bg [GTK_STATE_NORMAL];
	if (!el->text_set)
		el->text = s->fg [GTK_STATE_NORMAL];
	editable_label_set_color (el, NULL, NULL);
}


static void
el_init (GObject *obj)
{
	g_signal_connect (obj, "changed", G_CALLBACK (cb_el_changed), NULL);
	g_signal_connect (obj, "parent_set", G_CALLBACK (cb_el_parent_set), NULL);
}

GSF_CLASS (EditableLabel, editable_label,
	   el_class_init, el_init, BASE_TYPE)

void
editable_label_set_text (EditableLabel *el, char const *text)
{
	gtk_entry_set_text (GTK_ENTRY (el), text);
}

char const *
editable_label_get_text  (EditableLabel const *el)
{
	g_return_val_if_fail (IS_EDITABLE_LABEL (el), "");
	return (el->unedited_text != NULL)
		? el->unedited_text
		: gtk_entry_get_text (GTK_ENTRY (el));
}

/**
 * editable_label_set_color :
 * @el :
 * @base_color : optionally NULL.
 * @text_color : optionally NULL.
 *
 * assign the specified colours.  If we are editing just store them for later use.
 */
void
editable_label_set_color (EditableLabel *el, GdkColor *base_color, GdkColor *text_color)
{
	g_return_if_fail (IS_EDITABLE_LABEL (el));

	if (base_color != NULL) {
		el->base_set = TRUE; 
		el->base = *base_color;
	}
	if (text_color != NULL) {
		el->text_set = TRUE; 
		el->text  = *text_color;
	}

	if (el->unedited_text == NULL) {
		GdkColor base, text;

		if (base_color == NULL)
			base_color = &el->base;
		if (text_color == NULL)
			text_color = &el->text;

		base = *base_color;
		text = *text_color;

		/* ignore the current colors */
		el_set_color_gdk (el, &base, &text);
	}
}

GtkWidget *
editable_label_new (char const *text, GdkColor *base_color, 
				      GdkColor *text_color)
{
	EditableLabel *el = g_object_new (EDITABLE_LABEL_TYPE,
		"has_frame",		FALSE,
		"editable",		FALSE,
		NULL);

	GtkStyle *s = gtk_widget_get_default_style ();
	el->base = s->bg [GTK_STATE_NORMAL];
	el->text = s->fg [GTK_STATE_NORMAL];

	/* assign the fg/bg and store base/text */
	el_set_color_gdk (el, &el->base, &el->text);

	editable_label_set_color (el, base_color, text_color);

        if (text != NULL)
                editable_label_set_text (el, text);

	return GTK_WIDGET (el);
}

void
editable_label_start_editing (EditableLabel *el)
{
	if (el->unedited_text != NULL)
		return;

	el->unedited_text = g_strdup (gtk_entry_get_text (GTK_ENTRY (el)));
	g_signal_connect (G_OBJECT (el),
		"activate",
		G_CALLBACK (el_entry_activate), NULL);
	gtk_editable_select_region (GTK_EDITABLE (el), 0, -1);
	gtk_editable_set_editable (GTK_EDITABLE (el), TRUE);
	el_set_color_gdk (el, &el->base, &el->text);
	el_set_cursor (GTK_ENTRY (el), GDK_XTERM);
	gtk_widget_grab_focus (GTK_WIDGET (el));
	gtk_grab_add (GTK_WIDGET (el));
}

