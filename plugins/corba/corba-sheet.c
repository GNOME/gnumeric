/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * corba-sheet.c: A SheetControl for use by CORBA that implements the
 *			 Gnumeric::Sheet interface.
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

#include "corba-sheet.h"
#include "GNOME_Gnumeric.h"

#include <sheet.h>
#include <sheet-control-priv.h>

#include <gsf/gsf-impl-utils.h>
#include <bonobo.h>

typedef struct {
	SheetControl base;

	POA_GNOME_Gnumeric_Sheet	 servant;
	gboolean			 initialized, activated;
	CORBA_Object   			 corba_obj; /* local CORBA object */

	CORBA_Environment		*ev; /* exception from the caller */
} SheetControlCORBA;

typedef struct {
	SheetControlClass   base;
} SheetControlCORBAClass;

static SheetControlCORBA *
scc_from_servant (PortableServer_Servant serv)
{
	SheetControlCORBA *scc = (SheetControlCORBA *)(((char *)serv) - G_STRUCT_OFFSET (SheetControlCORBA, servant));

	g_return_val_if_fail (IS_SHEET_CONTROL (scc), NULL);

	return scc;
}

static CORBA_string
csheet_get_name (PortableServer_Servant servant,
		 CORBA_Environment *ev)
{
        SheetControlCORBA *scc = scc_from_servant (servant);
	Sheet *sheet = sc_sheet	(SHEET_CONTROL (scc));
	return CORBA_string_dup (sheet->name_unquoted);
}

static void
csheet_set_name (PortableServer_Servant servant, CORBA_char const * value,
		    CORBA_Environment *ev)
{
        SheetControlCORBA *scc = scc_from_servant (servant);
	Sheet *sheet = sc_sheet	(SHEET_CONTROL (scc));

	/* DO NOT CALL sheet_rename that is too low level */
}

static CORBA_short
csheet_get_index (PortableServer_Servant servant,
		  CORBA_Environment *ev)
{
        SheetControlCORBA *scc = scc_from_servant (servant);
	Sheet *sheet = sc_sheet	(SHEET_CONTROL (scc));
	return sheet->index_in_wb;
}

static void
csheet_set_index (PortableServer_Servant servant, CORBA_short indx,
		  CORBA_Environment *ev)
{
        SheetControlCORBA *scc = scc_from_servant (servant);
	Sheet *sheet = sc_sheet	(SHEET_CONTROL (scc));
}

static POA_GNOME_Gnumeric_Sheet__vepv	sheet_vepv;
static POA_GNOME_Gnumeric_Sheet__epv	sheet_epv;

static void
scc_finalize (GObject *obj)
{
	GObjectClass *parent_class;
	CORBA_Environment ev;
	SheetControlCORBA *scc = SHEET_CONTROL_CORBA (obj);

	CORBA_exception_init (&ev);

	if (scc->activated) {
		PortableServer_POA poa = bonobo_poa ();
		PortableServer_ObjectId *oid = PortableServer_POA_servant_to_id (poa,
			&scc->servant, &ev);
		PortableServer_POA_deactivate_object (poa, oid, &ev);
		scc->activated = FALSE;
		CORBA_free (oid);

		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("unexpected exception while finalizing");
		}
	}

	if (scc->initialized) {
		POA_GNOME_Gnumeric_Sheet__fini (&scc->servant, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("unexpected exception while finalizing");
		}
		scc->initialized = FALSE;
	}
	CORBA_exception_free (&ev);

	parent_class = g_type_class_peek (SHEET_CONTROL_TYPE);
	if (parent_class->finalize)
		parent_class->finalize (obj);
}

static void
scc_class_init (GObjectClass *object_class)
{
	object_class->finalize	    = &scc_finalize;

	sheet_vepv.GNOME_Gnumeric_Sheet_epv = &sheet_epv;
	sheet_epv._get_name	= csheet_get_name;
	sheet_epv._set_name	= csheet_set_name;
	sheet_epv._get_index	= csheet_get_index;
	sheet_epv._set_index	= csheet_set_index;
}

static void
scc_init (SheetControlCORBA *scc)
{
	CORBA_Environment ev;

	scc->initialized = FALSE;
	scc->activated   = FALSE;

	CORBA_exception_init (&ev);
	scc->servant.vepv = &sheet_vepv;
	POA_GNOME_Gnumeric_Sheet__init (&scc->servant, &ev);

	if (ev._major == CORBA_NO_EXCEPTION) {
		PortableServer_ObjectId *oid;
		PortableServer_POA poa = bonobo_poa ();
		
		scc->initialized = TRUE;

		oid = PortableServer_POA_activate_object (poa,
			&scc->servant, &ev);
		scc->activated = (ev._major == CORBA_NO_EXCEPTION);

		scc->corba_obj = PortableServer_POA_servant_to_reference (poa,
			&scc->servant, &ev);
		CORBA_free (oid);
	} else {
		g_warning ("'%s' : while creating a corba control",
			   bonobo_exception_get_text (&ev));
	}
	CORBA_exception_free (&ev);
}

GSF_CLASS (SheetControlCORBA, sheet_control_corba,
	   scc_class_init, scc_init, SHEET_CONTROL_TYPE);

SheetControl *
sheet_control_corba_new (SheetView *sv)
{
	SheetControl *sc =
		g_object_new (sheet_control_corba_get_type (), NULL);
	sv_attach_control (sv, SHEET_CONTROL (sc));
	return sc;
}
