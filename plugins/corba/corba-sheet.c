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

BONOBO_TYPE_FUNC_FULL (SheetControl, 
		       GNOME_Gnumeric_Sheet,
		       BONOBO_OBJECT_TYPE,
		       csheet);

#define SERVANT_TO_SC(s) (SHEET_CONTROL (bonobo_object (s)))

static CORBA_string
csheet_get_name (PortableServer_Servant servant,
		 CORBA_Environment *ev)
{
	Sheet *sheet = sc_sheet	(SERVANT_TO_SC (servant));
	return CORBA_string_dup (sheet->name_unquoted);
}

static void
csheet_set_name (PortableServer_Servant servant, CORBA_char const * value,
		    CORBA_Environment *ev)
{
	Sheet *sheet = sc_sheet	(SERVANT_TO_SC (servant));

	/* DO NOT CALL sheet_rename that is too low level */
}

static CORBA_short
csheet_get_index (PortableServer_Servant servant,
		  CORBA_Environment *ev)
{
	Sheet *sheet = sc_sheet	(SERVANT_TO_SC (servant));
	return sheet->index_in_wb;
}

static void
csheet_set_index (PortableServer_Servant servant,
		  CORBA_short indx,
		  CORBA_Environment *ev)
{
	Sheet *sheet = sc_sheet	(SERVANT_TO_SC (servant));
	/* FIXME: do something */
}

static void
csheet_dispose (SheetControl *sc)
{
	if (sc->view) {
		SheetView *v = sc->view;
		sc->view = NULL;
		g_object_unref (v);
	}
}

static void
csheet_instance_init (SheetControl *sc)
{
}

static void
csheet_class_init (SheetControlClass *sc_class)
{
	GObjectClass *gobject_class = (GObjectClass *) sc_class;

	gobject_class->dispose = csheet_dispose;

	/* populate CORBA epv */
	sc_class->epv._get_name  = csheet_get_name;
	sc_class->epv._set_name  = csheet_set_name;
	sc_class->epv._get_index = csheet_get_index;
	sc_class->epv._set_index = csheet_set_index;
}

SheetControl *
sheet_control_corba_new (SheetView *sv)
{
	SheetControl *sc =
		g_object_new (sheet_control_corba_get_type (), NULL);
	sc->view = g_object_ref (sv);
	return sc;
}
