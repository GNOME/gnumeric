/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_COMBO_TEXT_H
#define _GNM_COMBO_TEXT_H

#include <gal/widgets/gtk-combo-box.h>

G_BEGIN_DECLS

#define GNM_TYPE_COMBO_TEXT	(gnm_combo_text_get_type ())
#define GNM_COMBO_TEXT(obj)	(G_TYPE_CHECK_INSTANCE_CAST (obj, GNM_TYPE_COMBO_TEXT, GnmComboText))
#define GNM_IS_COMBO_TEXT(obj)	(G_TYPE_CHECK_INSTANCE_TYPE (obj, GNM_TYPE_COMBO_TEXT))

typedef struct _GnmComboText	   GnmComboText;

struct _GnmComboText {
	GtkComboBox parent;

	GCompareFunc cmp_func;

	GtkWidget *entry;
	GtkWidget *list;
	GtkWidget *scroll;
	GtkWidget *cached_entry;
	GtkStateType cache_mouse_state;
};

typedef enum {		/* begin the search from : */
	GNM_COMBO_TEXT_FROM_TOP,	/* the top of the list */
	GNM_COMBO_TEXT_CURRENT,		/* the current selection */
	GNM_COMBO_TEXT_NEXT		/* the next element after current */
} GnmComboTextSearch;

GType      gnm_combo_text_get_type	(void);
GtkWidget *gnm_combo_text_new		 (GCompareFunc cmp_func);
GtkWidget *gnm_combo_text_glade_new	 (void);

gboolean   gnm_combo_text_set_text	 (GnmComboText *ct, const gchar *label,
					  GnmComboTextSearch start);
GtkWidget *gnm_combo_text_add_item	 (GnmComboText *ct, const gchar *label);

void       gnm_combo_text_clear	 	 (GnmComboText *ct);

G_END_DECLS

#endif /* _GNM_COMBO_TEXT_H */
