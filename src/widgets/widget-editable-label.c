/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * widget-editable-label.c: A label that can be used to edit its text on demand.
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
#include <gal/util/e-util.h>

#define EDITABLE_LABEL_CLASS(k) (G_TYPE_CHECK_CLASS_CAST (k), EDITABLE_LABEL_TYPE)
struct _EditableLabel {
	GtkEntry    entry;
	StyleColor *color;
	char	   *unedited_text;
};

typedef struct {
	GtkEntryClass  entry;

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

static void
el_stop_editing (EditableLabel *el)
{
	if (el->unedited_text == NULL)
		return;

	g_free (el->unedited_text);
	el->unedited_text = NULL;
	editable_label_set_color (el, el->color);
	gtk_editable_set_editable (GTK_EDITABLE (el), FALSE);
}

static void
el_entry_activate (GtkEntry *entry, gpointer ignored)
{
	EditableLabel *el = EDITABLE_LABEL (entry);
	gboolean accept = TRUE;
	char const *text = gtk_entry_get_text (entry);

	if (el->unedited_text  == NULL ||
	    !strcmp (el->unedited_text, text))
		return;

	g_signal_emit (G_OBJECT (entry), el_signals [TEXT_CHANGED], 0,
		       text, &accept);
	if (!accept)
		editable_label_set_text (el, el->unedited_text);
	el_stop_editing (el);
}

static void
el_start_editing (EditableLabel *el)
{
	if (el->unedited_text != NULL)
		return;

	el->unedited_text = g_strdup (gtk_entry_get_text (GTK_ENTRY (el)));
	g_signal_connect (G_OBJECT (el),
		"activate",
		G_CALLBACK (el_entry_activate), NULL);
	gtk_widget_grab_focus (GTK_WIDGET (el));
	gtk_editable_select_region (GTK_EDITABLE (el), 0, -1);

	gtk_editable_set_editable (GTK_EDITABLE (el), TRUE);
}

static void
el_destroy (GtkObject *object)
{
	EditableLabel *el = EDITABLE_LABEL (object);
	GtkObjectClass *parent;

	el_stop_editing (el);
	editable_label_set_color (el, NULL);

	parent = g_type_class_peek (GTK_TYPE_ENTRY);
	if (parent != NULL && parent->destroy != NULL)
		parent->destroy (object);
}

static gint
el_button_press_event (GtkWidget *widget, GdkEventButton *button)
{
	GtkWidgetClass *parent;
	EditableLabel *el = EDITABLE_LABEL (widget);

	if (button->window != widget->window &&
	    button->window != el->entry.text_area) {
		/* Accept the name change */
		el_entry_activate (GTK_ENTRY (el), NULL);
		gdk_event_put ((GdkEvent *)button);
		return TRUE;
	}

	if (button->type == GDK_2BUTTON_PRESS) {
		el_start_editing (el);
		return FALSE;
	}

	parent = g_type_class_peek (GTK_TYPE_ENTRY);
	if (parent && parent->button_press_event)
		return parent->button_press_event (widget, button);
	return FALSE;
}

/*
 * GtkWidget key_press method override
 *
 * If the label is being edited, we forward the event to the GtkEntry widget.
 */
static gint
el_key_press_event (GtkWidget *el, GdkEventKey *event)
{
	GtkWidgetClass *parent;

	if (event->keyval == GDK_Escape) {
		el_stop_editing (EDITABLE_LABEL (el));
		g_signal_emit (G_OBJECT (el), el_signals [EDITING_STOPPED], 0);
		gtk_editable_select_region (GTK_EDITABLE (el), 0, 0);
		return TRUE;
	}
	parent = g_type_class_peek (GTK_TYPE_ENTRY);
	if (parent && parent->key_press_event)
		return parent->key_press_event (el, event);
	return FALSE;
}

static void
el_size_request (GtkWidget      *el,
		 GtkRequisition *requisition)
{
	PangoRectangle	 logical_rect;
	PangoLayoutLine *line;
	PangoLayout	*layout;
	GtkWidgetClass	*parent = g_type_class_peek (GTK_TYPE_ENTRY);

	parent->size_request (el, requisition);
	layout = gtk_entry_get_layout (GTK_ENTRY (el));
	line = pango_layout_get_lines (layout)->data;
	pango_layout_line_get_extents (line, NULL, &logical_rect);

	requisition->width = logical_rect.width / PANGO_SCALE + 2*2;
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

static void
cb_el_changed (GtkWidget *w, gpointer ignored)
{
	gtk_widget_queue_resize	(w);
}

static void
el_init (GObject *obj)
{
	g_signal_connect (obj, "changed", G_CALLBACK (cb_el_changed), NULL);
}

E_MAKE_TYPE (editable_label, "EditableLabel", EditableLabel,
	     el_class_init, el_init, GTK_TYPE_ENTRY)

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
		style->fg [GTK_STATE_NORMAL] = (contrast >= 0x18000) ? gs_black : gs_white,
		gtk_widget_set_style (GTK_WIDGET (el), style);
		gtk_style_unref (style);
	}
}

GtkWidget *
editable_label_new (char const *text, StyleColor *color)
{
	EditableLabel *el = g_object_new (EDITABLE_LABEL_TYPE,
		"has_frame",		FALSE,
		"editable",		FALSE,
		NULL);

	if (text != NULL)
		editable_label_set_text (el, text);
	if (color != NULL)
		editable_label_set_color (el, color);

	return GTK_WIDGET (el);
}
