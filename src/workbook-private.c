/* vim: set sw=8: */

/*
 * workbook-private.c: support routines for the workbook private structure.
 *     This is a transitional object to help slip the control specific aspects
 *     of workbook into less visible areas.
 *
 * Copyright (C) 2000 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "workbook-private.h"

#ifdef ENABLE_BONOBO
#define WORKBOOK_PRIVATE_TYPE        (workbook_private_get_type ())
#define WORKBOOK_PRIVATE(o)          (GTK_CHECK_CAST ((o), WORKBOOK_PRIVATE_TYPE, WorkbookPrivate))
#define WORKBOOK_PRIVATE_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), WORKBOOK_PRIVATE_TYPE, WorkbookPrivateClass))
#define IS_WORKBOOK_PRIVATE(o)       (GTK_CHECK_TYPE ((o), WORKBOOK_PRIVATE_TYPE))
#define IS_WORKBOOK_PRIVATE_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), WORKBOOK_PRIVATE_TYPE))

typedef struct {
	BonoboObjectClass bonobo_parent_class;
} WorkbookPrivateClass;

GtkType workbook_private_get_type (void);

static void
workbook_private_init (GtkObject *object)
{
	WorkbookPrivate *p = WORKBOOK_PRIVATE (object);
	p->recursive_dirty_enabled = TRUE;
}

static void
workbook_private_class_init (GtkObjectClass *object_class)
{
}

GtkType
workbook_private_get_type (void)
{
	static GtkType type = 0;

	if (!type) {
		GtkTypeInfo info = {
			"WorkbookPrivate",
			sizeof (WorkbookPrivate),
			sizeof (WorkbookPrivateClass),
			(GtkClassInitFunc) workbook_private_class_init,
			(GtkObjectInitFunc) workbook_private_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (bonobo_object_get_type (), &info);
	}

	return type;
}
#endif

WorkbookPrivate *
workbook_private_new (void)
{
#ifdef ENABLE_BONOBO
	WorkbookPrivate *wbp = gtk_type_new (workbook_private_get_type ());
#else
	WorkbookPrivate *wbp = g_new0 (WorkbookPrivate, 1);
#endif

	return wbp;
}

void
workbook_private_delete (WorkbookPrivate *wbp)
{
#ifdef ENABLE_BONOBO
	bonobo_object_unref (BONOBO_OBJECT (wbp));
#else
	g_free (wbp);
#endif
}
