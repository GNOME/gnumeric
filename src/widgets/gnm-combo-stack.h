/* $Id$ */

#ifndef _GTK_COMBO_STACK_H
#define _GTK_COMBO_STACK_H

#include <gtk/gtk.h>
#include "gtk-combo-box.h"

#define GTK_COMBO_STACK(obj)	    GTK_CHECK_CAST (obj, gtk_combo_stack_get_type (), GtkComboStack)
#define GTK_COMBO_STACK_CLASS(klass) GTK_CHECK_CLASS_CAST (klass, gtk_combo_stack_get_type (), GtkComboTextClass)
#define GTK_IS_COMBO_STACK(obj)      GTK_CHECK_TYPE (obj, gtk_combo_stack_get_type ())

typedef struct _GtkComboStack	   GtkComboStack;
/* typedef struct _GtkComboTextPrivate GtkComboTextPrivate;*/
typedef struct _GtkComboStackClass   GtkComboStackClass;

struct _GtkComboStack {
	GtkComboBox parent;

	GtkWidget *button;
	GtkWidget *list;
	GtkWidget *scrolled_window;

	gint num_items;
};

struct _GtkComboStackClass {
	GtkComboBoxClass parent_class;
};


GtkType    gtk_combo_stack_get_type  (void);
GtkWidget *gtk_combo_stack_new       (const gchar *stock_name,
				      gboolean const is_scrolled);

void       gtk_combo_stack_push_item (GtkComboStack *combo_stack,
				      const gchar *item);

#endif
