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

#include "corba-workbook.h"
#include "corba-sheet.h"

#include <workbook.h>
#include <workbook-control-priv.h>
#include <sheet-view.h>
#include <sheet-control-priv.h>
#include <ranges.h>

#include <gsf/gsf-impl-utils.h>
#include <bonobo.h>

typedef struct {
	WorkbookControl wb_control;

	POA_GNOME_Gnumeric_Workbook	 servant;
	gboolean			 initialized, activated;
	CORBA_Object   			 corba_obj; /* local CORBA object */

	CORBA_Environment		*ev; /* exception from the caller */
} WorkbookControlCORBA;

typedef struct {
	WorkbookControlClass   wb_control_class;
} WorkbookControlCORBAClass;

static WorkbookControlCORBA *
wbcc_from_servant (PortableServer_Servant serv)
{
	WorkbookControlCORBA *wbcc = (WorkbookControlCORBA *)(((char *)serv) - G_STRUCT_OFFSET (WorkbookControlCORBA, servant));

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbcc), NULL);

	return wbcc;
}

static CORBA_string
cworkbook_get_name (PortableServer_Servant servant,
		    CORBA_Environment *ev)
{
        WorkbookControlCORBA *wbcc = wbcc_from_servant (servant);
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcc));
	return CORBA_string_dup (workbook_get_filename (wb));
}

static void
cworkbook_set_name (PortableServer_Servant servant, CORBA_char const *name,
		    CORBA_Environment *ev)
{
        WorkbookControlCORBA *wbcc = wbcc_from_servant (servant);
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcc));

	g_return_if_fail (wbcc != NULL);
	workbook_set_filename (wb, name);
}

static GNOME_Gnumeric_Sheet
cworkbook_sheet_by_index (PortableServer_Servant servant, CORBA_short i,
			  CORBA_Environment *ev)
{
        WorkbookControlCORBA *wbcc = wbcc_from_servant (servant);
	Sheet *sheet;

	g_return_val_if_fail (wbcc != NULL, CORBA_OBJECT_NIL);

	sheet = workbook_sheet_by_index (wb_control_workbook (WORKBOOK_CONTROL (wbcc)), i);
	if (sheet != NULL) {
		/* CORBA_exception_set (ev, CORBA_USER_EXCEPTION, ex_GNOME_Gnumeric_Workbook_NameExists, NULL); */
	}

	return CORBA_OBJECT_NIL;
}

static GNOME_Gnumeric_Sheet
cworkbook_sheet_by_name (PortableServer_Servant servant, CORBA_char const *name,
			 CORBA_Environment *ev)
{
        WorkbookControlCORBA *wbcc = wbcc_from_servant (servant);
	Sheet *sheet;

	g_return_val_if_fail (wbcc != NULL, CORBA_OBJECT_NIL);

	sheet = workbook_sheet_by_name (wb_control_workbook (WORKBOOK_CONTROL (wbcc)), name);
	if (sheet != NULL) {
	}

	return CORBA_OBJECT_NIL;
}

static GNOME_Gnumeric_Sheet
cworkbook_sheet_add (PortableServer_Servant servant, CORBA_char const *name, CORBA_short pos,
		     CORBA_Environment *ev)
{
        WorkbookControlCORBA *wbcc = wbcc_from_servant (servant);
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcc));
	Sheet *sheet = workbook_sheet_add (wb, NULL, TRUE);
}

static GNOME_Gnumeric_Sheets *
cworkbook_sheets (PortableServer_Servant servant,
		  CORBA_Environment *ev)
{
        WorkbookControlCORBA *wbcc = wbcc_from_servant (servant);
	GList *sheets =
		workbook_sheets (wb_control_workbook (WORKBOOK_CONTROL (wbcc)));
	int i, len = g_list_length (sheets);
	GNOME_Gnumeric_Workbooks *res = GNOME_Gnumeric_Sheets__alloc ();

	res->_length = res->_maximum = len;
	res->_buffer = GNOME_Gnumeric_Sheets_allocbuf (len);
	res->_release = CORBA_TRUE;

	for (i = 0; i < len ; ++i) {
		res->_buffer [i] = CORBA_OBJECT_NIL;
	}

	g_list_free (sheets);

	return res;
}

static void
wbcc_error (CommandContext *ctxt, GError *gerr)
{
	WorkbookControlCORBA *wbcc = WORKBOOK_CONTROL_CORBA (ctxt);

	if (gerr->domain == gnm_error_system ()) {
		GNOME_Gnumeric_ErrorSystem *err = GNOME_Gnumeric_ErrorSystem__alloc();
		err->msg = CORBA_string_dup (gerr->message);
		CORBA_exception_set (wbcc->ev, CORBA_USER_EXCEPTION,
			ex_GNOME_Gnumeric_ErrorSystem, err);
	} else if (gerr->domain == gnm_error_read ()) {
		GNOME_Gnumeric_ErrorRead *err = GNOME_Gnumeric_ErrorRead__alloc();
		err->msg = CORBA_string_dup (gerr->message);
		CORBA_exception_set (wbcc->ev, CORBA_USER_EXCEPTION,
			ex_GNOME_Gnumeric_ErrorRead, err);
	} else if (gerr->domain == gnm_error_write ()) {
		GNOME_Gnumeric_ErrorSave *err = GNOME_Gnumeric_ErrorSave__alloc();
		err->msg = CORBA_string_dup (gerr->message);
		CORBA_exception_set (wbcc->ev, CORBA_USER_EXCEPTION,
			ex_GNOME_Gnumeric_ErrorSave, err);
	} else if (gerr->domain == gnm_error_array ()) {
		GNOME_Gnumeric_ErrorSplitsArray *err = GNOME_Gnumeric_ErrorSplitsArray__alloc();
		err->msg = CORBA_string_dup (gerr->message);
		CORBA_exception_set (wbcc->ev, CORBA_USER_EXCEPTION,
			ex_GNOME_Gnumeric_ErrorSplitsArray, err);
	} else if (gerr->domain == gnm_error_invalid ()) {
		GNOME_Gnumeric_ErrorInvalid *err = GNOME_Gnumeric_ErrorInvalid__alloc();
		err->msg = CORBA_string_dup (gerr->message);
		CORBA_exception_set (wbcc->ev, CORBA_USER_EXCEPTION,
			ex_GNOME_Gnumeric_ErrorInvalid, err);
	}
}

static char *
wbcc_get_password (CommandContext *cc, char const* msg)
{
	return NULL;
}

static void
wbcc_set_sensitive (CommandContext *cc, gboolean sensitive)
{
}

static void
wbcc_sheet_add (WorkbookControl *wbc, SheetView *sv)
{
	SheetControl *sc = sheet_control_corba_new (sv);
	sc->wbc = wbc;
}

static void
wbcc_sheet_remove (WorkbookControl *wbc, Sheet *sheet)
{
}

static void
wbcc_sheet_remove_all (WorkbookControl *wbc)
{
}

static POA_GNOME_Gnumeric_Workbook__vepv	workbook_vepv;
static POA_GNOME_Gnumeric_Workbook__epv		workbook_epv;

static void
wbcc_finalize (GObject *obj)
{
	GObjectClass *parent_class;
	CORBA_Environment ev;
	WorkbookControlCORBA *wbcc = WORKBOOK_CONTROL_CORBA (obj);

	CORBA_exception_init (&ev);

	if (wbcc->activated) {
		PortableServer_POA poa = bonobo_poa ();
		PortableServer_ObjectId *oid = PortableServer_POA_servant_to_id (poa,
			&wbcc->servant, &ev);
		PortableServer_POA_deactivate_object (poa, oid, &ev);
		wbcc->activated = FALSE;
		CORBA_free (oid);

		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("unexpected exception while finalizing");
		}
	}

	if (wbcc->initialized) {
		POA_GNOME_Gnumeric_Workbook__fini (&wbcc->servant, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("unexpected exception while finalizing");
		}
		wbcc->initialized = FALSE;
	}
	CORBA_exception_free (&ev);

	parent_class = g_type_class_peek (WORKBOOK_CONTROL_TYPE);
	if (parent_class->finalize)
		parent_class->finalize (obj);
}

static void
wbcc_class_init (GObjectClass *object_class)
{
	CommandContextClass *cc_class = COMMAND_CONTEXT_CLASS (object_class);
	WorkbookControlClass *wbc_class = WORKBOOK_CONTROL_CLASS (object_class);

	object_class->finalize	    = &wbcc_finalize;

	g_return_if_fail (cc_class != NULL);
	cc_class->get_password	     = wbcc_get_password;
	cc_class->set_sensitive	     = wbcc_set_sensitive;
	cc_class->error.error        = wbcc_error;

	wbc_class->sheet.add        = wbcc_sheet_add;
	wbc_class->sheet.remove	    = wbcc_sheet_remove;
	wbc_class->sheet.remove_all = wbcc_sheet_remove_all;

	workbook_vepv.GNOME_Gnumeric_Workbook_epv = &workbook_epv;
	workbook_epv._get_name	    = cworkbook_get_name;
	workbook_epv._set_name	    = cworkbook_set_name;
	workbook_epv.sheet_by_index = cworkbook_sheet_by_index;
	workbook_epv.sheet_by_name  = cworkbook_sheet_by_name;
	workbook_epv.sheet_add	    = cworkbook_sheet_add;
	workbook_epv.sheets	    = cworkbook_sheets;
}

static void
wbcc_init (WorkbookControlCORBA *wbcc)
{
	CORBA_Environment ev;

	wbcc->initialized = FALSE;
	wbcc->activated   = FALSE;

	CORBA_exception_init (&ev);
	wbcc->servant.vepv = &workbook_vepv;
	POA_GNOME_Gnumeric_Workbook__init (&wbcc->servant, &ev);

	if (ev._major == CORBA_NO_EXCEPTION) {
		PortableServer_ObjectId *oid;
		PortableServer_POA poa = bonobo_poa ();
		
		wbcc->initialized = TRUE;

		oid = PortableServer_POA_activate_object (poa,
			&wbcc->servant, &ev);
		wbcc->activated = (ev._major == CORBA_NO_EXCEPTION);

		wbcc->corba_obj = PortableServer_POA_servant_to_reference (poa,
			&wbcc->servant, &ev);
		CORBA_free (oid);
	} else {
		g_warning ("'%s' : while creating a corba control",
			   bonobo_exception_get_text (&ev));
	}
	CORBA_exception_free (&ev);
}

GSF_CLASS (WorkbookControlCORBA, workbook_control_corba,
	   wbcc_class_init, wbcc_init, WORKBOOK_CONTROL_TYPE);

WorkbookControl *
workbook_control_corba_new (WorkbookView *optional_view,
			    Workbook	 *optional_wb)
{
	WorkbookControl *wbc =
		g_object_new (workbook_control_corba_get_type (), NULL);
	workbook_control_set_view (wbc, optional_view, optional_wb);
	workbook_control_init_state (wbc);
	return wbc;
}

GNOME_Gnumeric_Workbook
workbook_control_corba_obj (WorkbookControl *wbc)
{
	WorkbookControlCORBA *wbcc = WORKBOOK_CONTROL_CORBA (wbc);
	g_return_val_if_fail (wbcc != NULL, NULL);
	return wbcc->corba_obj;
}
