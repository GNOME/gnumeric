#ifndef GNUMERIC_WIDGET_EDITABLE_LABEL_H
#define GNUMERIC_WIDGET_EDITABLE_LABEL_H

#include <libgnome/gnome-defs.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkentry.h>
#include <libgnomeui/gnome-canvas.h>

BEGIN_GNOME_DECLS

#define EDITABLE_LABEL_TYPE     (editable_label_get_type ())
#define EDITABLE_LABEL(obj)     (GTK_CHECK_CAST((obj), EDITABLE_LABEL_TYPE, EditableLabel))
#define EDITABLE_LABEL_CLASS(k) (GTK_CHECK_CLASS_CAST(k), EDITABLE_LABEL_TYPE)
#define IS_EDITABLE_LABEL(obj)  (GTK_CHECK_TYPE((obj), EDITABLE_LABEL_TYPE))

typedef struct {
	GnomeCanvas     canvas;
	GnomeCanvasItem *text_item;

	char            *text;
	GtkWidget       *toplevel;
	GtkWidget       *entry;
	GnomeCanvasItem *cursor;
	GnomeCanvasItem *background;
} EditableLabel;

GtkType    editable_label_get_type (void);
GtkWidget *editable_label_new      (const char *text);
void       editable_label_set_text (EditableLabel *el, const char *text);

typedef struct {
	GnomeCanvasClass parent_class;

	/* Signals emited by this widget */
	gboolean (* text_changed)    (EditableLabel *el, const char *newtext);
	void     (* editing_stopped) (void);
} EditableLabelClass;

END_GNOME_DECLS

#endif /* GNUMERIC_WIDGET_EDITABLE_LABEL_H */
