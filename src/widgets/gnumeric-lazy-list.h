/*
 * gnumeric-lazy-list.h
 *
 * Copyright (C) 2003 Morten Welinder
 *
 * based extensively on:
 *
 * GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * Modified by the GTK+ Team and others 1997-2000.  See the GTK AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA.
 */

#ifndef __GNUMERIC_LAZY_LIST_H__
#define __GNUMERIC_LAZY_LIST_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GNUMERIC_TYPE_LAZY_LIST              (gnumeric_lazy_list_get_type ())
#define GNUMERIC_LAZY_LIST(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNUMERIC_TYPE_LAZY_LIST, GnumericLazyList))
#define GNUMERIC_LAZY_LIST_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GNUMERIC_TYPE_LAZY_LIST, GnumericLazyListClass))
#define GNUMERIC_IS_LAZY_LIST(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNUMERIC_TYPE_LAZY_LIST))
#define GNUMERIC_IS_LAZY_LIST_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GNUMERIC_TYPE_LAZY_LIST))
#define GNUMERIC_LAZY_LIST_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GNUMERIC_TYPE_LAZY_LIST, GnumericLazyListClass))


typedef struct _GnumericLazyList       GnumericLazyList;
typedef struct _GnumericLazyListClass  GnumericLazyListClass;
typedef void (*GnumericLazyListValueGetFunc) (gint row, gint col, gpointer user, GValue *result);

struct _GnumericLazyList
{
	GObject parent;

	/*< private >*/
	gint stamp;
	int rows;
	int cols;
	GType *column_headers;

	GnumericLazyListValueGetFunc get_value;
	gpointer user_data;
};

struct _GnumericLazyListClass
{
  GObjectClass parent_class;
};


GType             gnumeric_lazy_list_get_type (void) G_GNUC_CONST;
GnumericLazyList *gnumeric_lazy_list_new (GnumericLazyListValueGetFunc get_value,
					  gpointer user_data,
					  gint n_rows,
					  gint n_columns,
					  ...);
void              gnumeric_lazy_list_add_column (GnumericLazyList *ll,
						 int count,
						 GType typ);

G_END_DECLS

#endif /* __GNUMERIC _LAZY_LIST_H__ */
