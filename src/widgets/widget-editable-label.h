#ifndef GNUMERIC_WIDGET_EDITABLE_LABEL_H
#define GNUMERIC_WIDGET_EDITABLE_LABEL_H

BEGIN_GNOME_DECLS

#include <gtk/gtkwidget.h>

#define EDITABLE_LABEL_TYPE     (editable_label_get_type ())
#define EDITABLE_LABEL(obj)     (GTK_CHECK_CAST((obj), EDITABLE_LABEL_TYPE, EditableLabel))
#define IS_EDITABLE_LABEL(obj)  (GTK_CHECK_TYPE((obj), EDITABLE_LABEL_TYPE))

typedef struct _EditableLabel EditableLabel;

GtkType     editable_label_get_type  (void);
GtkWidget  *editable_label_new       (char const *text, StyleColor *color);
void        editable_label_set_text  (EditableLabel *el, char const *text);
char const *editable_label_get_text  (EditableLabel const *el);
void        editable_label_set_color (EditableLabel *el, StyleColor *color);

END_GNOME_DECLS

#endif /* GNUMERIC_WIDGET_EDITABLE_LABEL_H */
