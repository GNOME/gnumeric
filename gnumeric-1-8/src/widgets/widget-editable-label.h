#ifndef GNUMERIC_WIDGET_EDITABLE_LABEL_H
#define GNUMERIC_WIDGET_EDITABLE_LABEL_H

G_BEGIN_DECLS

#include <gtk/gtkwidget.h>

#define EDITABLE_LABEL_TYPE     (editable_label_get_type ())
#define EDITABLE_LABEL(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), EDITABLE_LABEL_TYPE, EditableLabel))
#define IS_EDITABLE_LABEL(obj)  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EDITABLE_LABEL_TYPE))

typedef struct _EditableLabel EditableLabel;

GType     editable_label_get_type (void);
GtkWidget  *editable_label_new       (char const *text, GdkColor *base_color,
				      GdkColor *text_color);
void        editable_label_set_text  (EditableLabel *el, char const *text);
char const *editable_label_get_text  (EditableLabel const *el);
void        editable_label_set_color (EditableLabel *el, GdkColor *base, GdkColor *text);
void        editable_label_start_editing (EditableLabel *el);
gboolean    editable_label_get_editable (EditableLabel *el);
void        editable_label_set_editable (EditableLabel *el, gboolean editable);

G_END_DECLS

#endif /* GNUMERIC_WIDGET_EDITABLE_LABEL_H */
