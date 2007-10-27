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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric.h>

#include "corba-workbook.h"
#include "GNOME_Gnumeric.h"

#include <application.h>
#include <workbook-priv.h>
#include <workbook-view.h>
#include <gnm-plugin.h>
#include <goffice/app/io-context.h>
#include <command-context.h>
#include <command-context-stderr.h>
#include <glib/gi18n-lib.h>

#include <bonobo/bonobo-object.h>
#include <bonobo/bonobo-main.h>

#undef DEBUG_CORBA

typedef struct {
	BonoboObject base;
	/* No need for any data */
} CorbaApplication;

typedef struct {
	BonoboObjectClass      parent_class;

	POA_GNOME_Gnumeric_Application__epv epv;
} CorbaApplicationClass;

static GNOME_Gnumeric_Workbook
capp_workbook_open (PortableServer_Servant ignore,
		    CORBA_char const      *file_name,
		    CORBA_boolean          shared_view,
		    CORBA_Environment     *ev)
{
	Workbook     *wb = gnm_app_workbook_get_by_name (file_name, NULL);
	WorkbookView *wbv;

	if (wb != NULL) {
		if (shared_view && wb->wb_views->len > 0)
			wbv = g_ptr_array_index (wb->wb_views, 0);
		else
			wbv = NULL;
	} else {
		GOCmdContext *cc = cmd_context_stderr_new ();
		IOContext *io_context = gnumeric_io_context_new (cc);
		wbv = wb_view_new_from_uri (file_name, NULL, io_context, NULL);
		g_object_unref (G_OBJECT (io_context));
		g_object_unref (G_OBJECT (cc));
	}

	return workbook_control_corba_obj (workbook_control_corba_new (wbv, wb));
}

static GNOME_Gnumeric_Workbooks *
capp_workbooks (PortableServer_Servant ignore,
		CORBA_Environment     *ev)
{
	GList *workbooks = gnm_app_workbook_list ();
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
capp_init (CorbaApplication *capp)
{
}

static void
capp_class_init (CorbaApplicationClass *capp)
{
	capp->epv.workbooks = capp_workbooks;
	capp->epv.workbook_open	= capp_workbook_open;
}

static BONOBO_TYPE_FUNC_FULL (CorbaApplication,
		       GNOME_Gnumeric_Application,
		       BONOBO_OBJECT_TYPE,
		       capp);

/***************************************************************/
static CorbaApplication *capp = NULL;

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *p, GOCmdContext *cc)
{
	if (capp)
		return;

	if (!bonobo_is_initialized ()) {
		int argc = 1;
		char *argv[] = { (char *)"Gnumeric" };
		bonobo_init (&argc, argv);
	}

	capp = g_object_new (capp_get_type(), NULL);

	if (bonobo_activation_register_active_server (
		    "OAFIID:GNOME_Gnumeric_Application",
		    BONOBO_OBJREF (capp), NULL)
	    != Bonobo_ACTIVATION_REG_SUCCESS) {
#ifdef DEBUG_CORBA
                        printf("Could not register as CORBA server\n");
#endif
                        return ;
	}
}

G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *p, GOCmdContext *cc)
{
	if (capp) {
		bonobo_activation_unregister_active_server (
			"OAFIID:GNOME_Gnumeric_Application",
			BONOBO_OBJREF (capp));
		bonobo_object_unref (capp);
		capp = NULL;
	}
}

GNM_PLUGIN_MODULE_HEADER;
