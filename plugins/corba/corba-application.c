/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * corba-workbook.c: A WorkbookControl for use by CORBA that implements the
 *			 Gnumeric::Workbook interface.
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
#include <gnumeric.h>

#include "corba-workbook.h"
#include "GNOME_Gnumeric.h"

#include <application.h>
#include <workbook.h>
#include <workbook-view.h>
#include <plugin-util.h>
#include <module-plugin-defs.h>
#include <command-context.h>
#include <command-context-stderr.h>
#include <gnumeric-i18n.h>

#include <bonobo.h>

BONOBO_TYPE_FUNC_FULL (CorbaApplication, 
		       GNOME_Gnumeric_Application,
		       BONOBO_OBJECT_TYPE,
		       capp);

static GNOME_Gnumeric_Workbook
capp_workbook_open (PortableServer_Servant ignore,
		    CORBA_char const      *file_name,
		    CORBA_boolean          shared_view,
		    CORBA_Environment     *ev)
{
	Workbook     *wb = application_workbook_get_by_name (file_name);
	WorkbookView *wbv;

	if (wb != NULL) {
		if (shared_view && wb->wb_views->len > 0)
			wbv = g_ptr_array_index (wb->wb_views, 0);
		else
			wbv = NULL;
	} else {
		CommandContextStderr *cc = command_context_stderr_new ();
		IOContext *io_context = gnumeric_io_context_new (COMMAND_CONTEXT (cc));
		wbv = wb_view_new_from_file (file_name, NULL, io_context);
		g_object_unref (G_OBJECT (io_context));
		g_object_unref (G_OBJECT (cc));
	}

	return workbook_control_corba_obj (workbook_control_corba_new (wbv, wb));
}

static GNOME_Gnumeric_Workbooks *
capp_workbooks (PortableServer_Servant ignore,
		CORBA_Environment     *ev)
{
	GList *workbooks = application_workbook_list ();
	int i, len = g_list_length (workbooks);
	GNOME_Gnumeric_Workbooks *res = GNOME_Gnumeric_Workbooks__alloc ();
	res->_length = res->_maximum = len;
	res->_buffer = GNOME_Gnumeric_Workbooks_allocbuf (len);
	res->_release = CORBA_TRUE;

	for (i = 0; i < len ; ++i)
		res->_buffer [i] = CORBA_OBJECT_NIL;

	return res;
}

static void
capp_instance_init (CorbaApplication *capp)
{
}

static void
capp_class_init (CorbaApplicationClass *capp)
{
	capp->epv.workbooks = capp_workbooks;
	capp->epv.workbook_open	= capp_workbook_open;
}

static CorbaApplication *capp = NULL;

void
plugin_init_general (ErrorInfo **ret_error)
{
	if (capp)
		return;

	if (!bonobo_is_initialized ()) {
		int argc = 1;
		char *argv[] = { (char *)"Gnumeric" };
		bonobo_init (&argc, argv);
	}

	capp = g_object_new (CORBA_TYPE_APPLICATION, NULL);

	bonobo_activation_active_server_register (
		"OAFIID:GNOME_Gnumeric_Application",
		BONOBO_OBJREF (capp));
	/* FIXME: this badly needs to check return values */
}

void
plugin_cleanup_general (ErrorInfo **ret_error)
{
	if (capp) {
		bonobo_activation_active_server_unregister (
			"OAFIID:GNOME_Gnumeric_Application",
			BONOBO_OBJREF (capp));
		bonobo_object_unref (capp);
		capp = NULL;
	}
}

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

