/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gtk-combo-box.h - a customizable combobox
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Miguel de Icaza <miguel@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _GNM_COMBO_BOX_H_
#define _GNM_COMBO_BOX_H_

#include <gtk/gtkhbox.h>

G_BEGIN_DECLS

#define GNM_COMBO_BOX_TYPE          (gnm_combo_box_get_type())
#define GNM_COMBO_BOX(obj)	    G_TYPE_CHECK_INSTANCE_CAST (obj, gnm_combo_box_get_type (), GnmComboBox)
#define GNM_COMBO_BOX_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, gnm_combo_box_get_type (), GnmComboBoxClass)
#define IS_GNM_COMBO_BOX(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, gnm_combo_box_get_type ())

typedef struct _GnmComboBox	   GnmComboBox;
typedef struct _GnmComboBoxPrivate GnmComboBoxPrivate;
typedef struct _GnmComboBoxClass   GnmComboBoxClass;

struct _GnmComboBox {
	GtkHBox hbox;
	GnmComboBoxPrivate *priv;
};

struct _GnmComboBoxClass {
	GtkHBoxClass parent_class;

	GtkWidget *(*pop_down_widget) (GnmComboBox *cbox);

	/*
	 * invoked when the popup has been hidden, if the signal
	 * returns TRUE, it means it should be killed from the
	 */
	gboolean  *(*pop_down_done)   (GnmComboBox *cbox, GtkWidget *);

	/*
	 * Notification signals.
	 */
	void      (*pre_pop_down)     (GnmComboBox *cbox);
	void      (*post_pop_hide)    (GnmComboBox *cbox);
};

/* public */
GtkType    gnm_combo_box_get_type    (void);
void       gnm_combo_box_construct   (GnmComboBox *combo_box,
				      GtkWidget *display_widget,
				      GtkWidget *popdown_container,
				      GtkWidget	*popdown_focus);
void       gnm_combo_box_set_title   (GnmComboBox *combo,
				      const gchar *title);
void       gnm_combo_box_set_tearable        (GnmComboBox *combo,
					      gboolean tearable);
GtkWidget *gnm_combo_box_get_arrow	     (GnmComboBox *combo);

/* protected */
void       gnm_combo_box_get_pos     (GnmComboBox *combo_box, int *x, int *y);

void       gnm_combo_box_popup_hide  (GnmComboBox *combo_box);

void       gnm_combo_box_popup_display (GnmComboBox *combo_box);

void       gnm_combo_box_set_display (GnmComboBox *combo_box,
				      GtkWidget *display_widget);

gboolean   _gnm_combo_is_updating (GnmComboBox const *combo_box);

G_END_DECLS

#endif /* _GNM_COMBO_BOX_H_ */
