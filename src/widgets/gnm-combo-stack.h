/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gnm-combo-stack.h - A combo box for displaying stacks (useful for Undo lists)
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

#ifndef _GNM_COMBO_STACK_H
#define _GNM_COMBO_STACK_H

#include "gnm-combo-box.h"

G_BEGIN_DECLS

#define GNM_COMBO_STACK_TYPE    (gnm_combo_stack_get_type ())
#define GNM_COMBO_STACK(obj)	G_TYPE_CHECK_INSTANCE_CAST (obj, gnm_combo_stack_get_type (), GnmComboStack)
#define IS_GNM_COMBO_STACK(obj) G_TYPE_CHECK_INSTANCE_TYPE (obj, gnm_combo_stack_get_type ())

typedef struct _GnmComboStack	     GnmComboStack;

GtkType    gnm_combo_stack_get_type   (void);
GtkWidget *gnm_combo_stack_new        (char const *stock_name,
				       gboolean const is_scrolled);
void       gnm_combo_stack_push	      (GnmComboStack *combo_stack,
				       char const *item);
void       gnm_combo_stack_pop	      (GnmComboStack *combo, int n);
void       gnm_combo_stack_truncate   (GnmComboStack *combo, int n);
GtkWidget *gnm_combo_stack_get_button (GnmComboStack *combo);

G_END_DECLS

#endif /* _GNM_COMBO_STACK_H */
