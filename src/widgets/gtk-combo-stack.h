/* File import from gal to gnumeric by import-gal.  Do not edit.  */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gtk-combo-stack.h - A combo box for displaying stacks (useful for Undo lists)
 *
 * Copyright (C) 2000 ÉRDI Gergõ <cactus@cactus.rulez.org>
 *
 * Authors:
 *   ÉRDI Gergõ <cactus@cactus.rulez.org>
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

#ifndef _GTK_COMBO_STACK_H
#define _GTK_COMBO_STACK_H

#include <gtk/gtkwidget.h>
#include "gtk-combo-box.h"

G_BEGIN_DECLS

#define GTK_COMBO_STACK_TYPE         (gtk_combo_stack_get_type ())
#define GTK_COMBO_STACK(obj)	     G_TYPE_CHECK_INSTANCE_CAST (obj, gtk_combo_stack_get_type (), GtkComboStack)
#define GTK_COMBO_STACK_CLASS(klass) G_TYPE_CHECK_CLASS_CAST (klass, gtk_combo_stack_get_type (), GtkComboTextClass)
#define GTK_IS_COMBO_STACK(obj)      G_TYPE_CHECK_INSTANCE_TYPE (obj, gtk_combo_stack_get_type ())

typedef struct _GtkComboStack	     GtkComboStack;
typedef struct _GtkComboStackPrivate GtkComboStackPrivate;
typedef struct _GtkComboStackClass   GtkComboStackClass;

struct _GtkComboStack {
	GtkComboBox parent;

	GtkComboStackPrivate *priv;
};

struct _GtkComboStackClass {
	GtkComboBoxClass parent_class;
};


GtkType    gtk_combo_stack_get_type  (void);
GtkWidget *gtk_combo_stack_new       (const gchar *stock_name,
				      gboolean const is_scrolled);

void       gtk_combo_stack_push_item (GtkComboStack *combo_stack,
				      const gchar *item);

void       gtk_combo_stack_remove_top (GtkComboStack *combo_stack,
				       gint num);
void       gtk_combo_stack_pop       (GtkComboStack *combo_stack,
				      gint num);
void       gtk_combo_stack_clear     (GtkComboStack *combo_stack);
void       gtk_combo_stack_truncate  (GtkComboStack *combo, int n);

G_END_DECLS

#endif
