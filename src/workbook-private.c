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

#ifdef WITH_BONOBO
#ifdef GNOME2_CONVERSION_COMPLETE
#define WORKBOOK_PRIVATE_TYPE        (workbook_private_get_type ())
#define WORKBOOK_PRIVATE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), WORKBOOK_PRIVATE_TYPE, WorkbookPrivate))
#define WORKBOOK_PRIVATE_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), WORKBOOK_PRIVATE_TYPE, WorkbookPrivateClass))
#define IS_WORKBOOK_PRIVATE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), WORKBOOK_PRIVATE_TYPE))
#define IS_WORKBOOK_PRIVATE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), WORKBOOK_PRIVATE_TYPE))

typedef struct {
	BonoboObjectClass bonobo_parent_class;
} WorkbookPrivateClass;

GType workbook_private_get_type (void);

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

GType
workbook_private_get_type (void)
{
	static GType type = 0;

	if (!type) {
		GTypeInfo info = {
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

static int
workbook_persist_file_load (BonoboPersistFile *ps, const CORBA_char *filename,
			    CORBA_Environment *ev, void *closure)
{
	WorkbookView *wbv = closure;

	return wb_view_open (wbv, /* FIXME */ NULL, filename, FALSE) ? 0 : -1;
}

static int
workbook_persist_file_save (BonoboPersistFile *ps, const CORBA_char *filename,
			    CORBA_Environment *ev, void *closure)
{
	WorkbookView *wbv = closure;
	GnumFileSaver *fs;

	fs = get_file_saver_by_id ("Gnumeric_XmlIO:gnum_xml");
	return wb_view_save_as (wbv, fs, filename, NULL /* FIXME */) ? 0 : -1;
}

static void
workbook_bonobo_setup (WorkbookPrivate *wbp)
{
	BonoboPersistFile *persist_file;

	/* FIXME : This is totaly broken.
	 * 1) it does not belong here at the workbook level
	 * 2) which view use ?
	 * 3) it should not be in this file.
	 */
	persist_file = bonobo_persist_file_new (
		workbook_persist_file_load,
		workbook_persist_file_save,
		wbv);
	bonobo_object_add_interface (
		BONOBO_OBJECT (wbp),
		BONOBO_OBJECT (persist_file));
}
#endif
#endif

WorkbookPrivate *
workbook_private_new (void)
{
#if defined(GNOME2_CONVERSION_COMPLETE) && defined(WITH_BONOBO)
	WorkbookPrivate *wbp = g_object_new (workbook_private_get_type (), NULL);
	workbook_bonobo_setup (Workbook *wb)
#else
	WorkbookPrivate *wbp = g_new0 (WorkbookPrivate, 1);
#endif

	return wbp;
}

void
workbook_private_delete (WorkbookPrivate *wbp)
{
#if defined(GNOME2_CONVERSION_COMPLETE) && defined(WITH_BONOBO)
	bonobo_object_unref (BONOBO_OBJECT (wbp));
#else
	g_free (wbp);
#endif
}
