/*
 * gnm-text-view.h: A textview extension handling formatting
 *
 * Copyright (C) 2009  Andreas J. Guelzow

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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

#ifndef GNM_TEXT_VIEW_H
#define GNM_TEXT_VIEW_H

#include <gnumeric-fwd.h>

#define GNM_TEXT_VIEW_TYPE	(gnm_text_view_get_type ())
#define GNM_TEXT_VIEW(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TEXT_VIEW_TYPE, GnmTextView))
#define GNM_IS_TEXT_VIEW(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TEXT_VIEW_TYPE))

typedef struct _GnmTextView GnmTextView;

GType gnm_text_view_get_type (void);
GnmTextView *gnm_text_view_new       (void);

#endif
