#ifndef _GNM_COMBO_TEXT_H
#define _GNM_COMBO_TEXT_H

#include <gal/widgets/gtk-combo-box.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GNM_COMBO_TEXT(obj)	    GTK_CHECK_CAST (obj, gnm_combo_text_get_type (), GnmComboText)
#define GNM_COMBO_TEXT_CLASS(klass) GTK_CHECK_CLASS_CAST (klass, gnm_combo_text_get_type (), GnmComboTextClass)
#define GTK_IS_COMBO_TEXT(obj)      GTK_CHECK_TYPE (obj, gnm_combo_text_get_type ())

typedef struct _GnmComboText	   GnmComboText;
/* typedef struct _GnmComboTextPrivate GnmComboTextPrivate;*/
typedef struct _GtkComboBoxClass   GnmComboTextClass;

struct _GnmComboText {
	GtkComboBox parent;

	GtkWidget *entry;
	GtkWidget *list;
	GtkWidget *scrolled_window;
	GtkStateType cache_mouse_state;
	GtkWidget *cached_entry;
	gboolean case_sensitive;
	GHashTable*elements;
};

struct _GnmComboTextClass {
	GtkComboBoxClass parent_class;
};


GtkType    gnm_combo_text_get_type  (void);
GtkWidget *gnm_combo_text_new       (gboolean const is_scrolled);
void       gnm_combo_text_construct (GnmComboText *ct, gboolean const is_scrolled);

gint       gnm_combo_text_set_case_sensitive (GnmComboText *combo_text,
					      gboolean val);
void       gnm_combo_text_select_item (GnmComboText *combo_text,
				       int elem);
void       gnm_combo_text_set_text (GnmComboText *combo_text,
				       const gchar *text);
void       gnm_combo_text_add_item    (GnmComboText *combo_text,
				       const gchar *item,
				       const gchar *value);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif
