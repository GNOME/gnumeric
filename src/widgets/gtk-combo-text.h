#ifndef _GTK_COMBO_TEXT_H
#define _GTK_COMBO_TEXT_H

#include "gtk-combo-box.h"

#define GTK_COMBO_TEXT(obj)	    GTK_CHECK_CAST (obj, gtk_combo_text_get_type (), GtkComboText)
#define GTK_COMBO_TEXT_CLASS(klass) GTK_CHECK_CLASS_CAST (klass, gtk_combo_text_get_type (), GtkComboTextClass)
#define GTK_IS_COMBO_TEXT(obj)      GTK_CHECK_TYPE (obj, gtk_combo_text_get_type ())

typedef struct _GtkComboText	   GtkComboText;
/* typedef struct _GtkComboTextPrivate GtkComboTextPrivate;*/
typedef struct _GtkComboBoxClass   GtkComboTextClass;

struct _GtkComboText {
	GtkComboBox parent;

	GtkWidget *entry;
	GtkWidget *list;
};

struct _GtkComboTextClass {
	GtkComboBoxClass parent_class;
};


GtkType    gtk_combo_text_get_type  (void);
void       gtk_combo_text_construct (GtkComboText *combo_text);
GtkWidget *gtk_combo_text_new();

void       gtk_combo_text_add_item  (GtkComboText *combo_text,
				     const gchar *item,
				     const gchar *value);

#endif
