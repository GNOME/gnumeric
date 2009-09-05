/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnumeric-text-view.c: A textview extension handling formatting
 *
 * Copyright (C) 2009  Andreas J. Guelzow

 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 or 3 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
*/

#ifndef GNUMERIC_TEXT_VIEW_H
#define GNUMERIC_TEXT_VIEW_H

#include "gui-gnumeric.h"
#include <gtk/gtk.h>

#define GNM_TEXT_VIEW_TYPE	(gnm_text_view_get_type ())
#define GNM_TEXT_VIEW(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TEXT_VIEW_TYPE, GnmTextView))
#define IS_GNM_TEXT_VIEW(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TEXT_VIEW_TYPE))

typedef struct _GnmTextView GnmTextView;

GType gnm_text_view_get_type (void);
GnmTextView *gnm_text_view_new       (void);

#endif /* GNUMERIC_TEXT_VIEW_H */
