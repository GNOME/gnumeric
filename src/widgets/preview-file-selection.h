/* -*- mode: c; style: linux -*- */

/* preview-file-selection.h
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Written by Rachel Hestilow <hestilow@ximian.com> 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef __PREVIEW_FILE_SELECTION_H__
#define __PREVIEW_FILE_SELECTION_H__

#include <gtk/gtkfilesel.h>
#include <libgnomeui/gnome-file-entry.h>

G_BEGIN_DECLS

#define PREVIEW_FILE_SELECTION_TYPE          preview_file_selection_get_type ()	
#define PREVIEW_FILE_SELECTION(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, PREVIEW_FILE_SELECTION_TYPE, PreviewFileSelection)
#define PREVIEW_FILE_SELECTION_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, PREVIEW_FILE_SELECTION_TYPE, PreviewFileSelectionClass)
#define IS_PREVIEW_FILE_SELECTION(obj)        G_TYPE_CHECK_INSTANCE_TYPE (obj, PREVIEW_FILE_SELECTION_TYPE)

typedef struct _PreviewFileSelection PreviewFileSelection;
typedef struct _PreviewFileSelectionClass PreviewFileSelectionClass;
typedef struct _PreviewFileSelectionPrivate PreviewFileSelectionPrivate;

struct _PreviewFileSelection 
{
	GtkFileSelection parent;

	PreviewFileSelectionPrivate *priv;
};

struct _PreviewFileSelectionClass 
{
	GtkFileSelectionClass parent_class;
};

GType preview_file_selection_get_type (void);

GtkWidget *preview_file_selection_new (const gchar *title, gboolean do_preview);

G_END_DECLS

#endif /* __PREVIEW_FILE_SELECTION_H__ */
