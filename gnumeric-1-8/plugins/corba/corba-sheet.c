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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric.h>

#include "corba-sheet.h"

#include <sheet.h>
#include <sheet-view.h>
#include <sheet-control-priv.h>

#include <gsf/gsf-impl-utils.h>
#include <bonobo/bonobo-object.h>

#define CORBA_SHEET_TYPE	(csheet_get_type ())
#define CORBA_SHEET(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CORBA_SHEET_TYPE, CorbaSheet))
#define IS_CORBA_SHEET(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), CORBA_SHEET_TYPE))

typedef struct _SheetControlCORBA SheetControlCORBA;
typedef struct {
	BonoboObject	   base;
	SheetControlCORBA *container;
} CorbaSheet;

typedef struct {
	BonoboObjectClass      parent_class;

	POA_GNOME_Gnumeric_Sheet__epv epv;
} CorbaSheetClass;

struct _SheetControlCORBA {
	SheetControl base;

	CorbaSheet *servant;
};
typedef SheetControlClass SheetControlCORBAClass;

static GType csheet_get_type   (void);

static Sheet *
servant_to_sheet (PortableServer_Servant servant)
{
	CorbaSheet *cs = CORBA_SHEET (bonobo_object (servant));
	return sc_sheet (SHEET_CONTROL (cs->container));
}

static CORBA_string
csheet_get_name (PortableServer_Servant servant,
		 CORBA_Environment *ev)
{
	Sheet *sheet = servant_to_sheet (servant);
	return CORBA_string_dup (sheet->name_unquoted);
}

static void
csheet_set_name (PortableServer_Servant servant, CORBA_char const * value,
		    CORBA_Environment *ev)
{
	/*
	Sheet *sheet = servant_to_sheet (servant);
	*/

	/* DO NOT CALL sheet_rename that is too low level */
}

static CORBA_short
csheet_get_index (PortableServer_Servant servant,
		  CORBA_Environment *ev)
{
	Sheet *sheet = servant_to_sheet (servant);
	return sheet->index_in_wb;
}

static void
csheet_set_index (PortableServer_Servant servant,
		  CORBA_short indx,
		  CORBA_Environment *ev)
{
	/*
	Sheet *sheet = servant_to_sheet (servant);
	*/
	/* FIXME: do something */
}

static void
csheet_dispose (GObject *obj)
{
	CorbaSheet *cs = CORBA_SHEET (obj);
	SheetControlCORBA *scc = cs->container;

	if (scc != NULL) {
		cs->container = NULL;
		scc->servant = NULL; /* break loop */
		g_object_unref (G_OBJECT(scc));
	}
}

static void
csheet_init (GObject *obg)
{
}

static void
csheet_class_init (GObjectClass *gobject_class)
{
	CorbaSheetClass *cs_class = (CorbaSheetClass *) gobject_class;

	gobject_class->dispose = csheet_dispose;

	/* populate CORBA epv */
	cs_class->epv._get_name  = csheet_get_name;
	cs_class->epv._set_name  = csheet_set_name;
	cs_class->epv._get_index = csheet_get_index;
	cs_class->epv._set_index = csheet_set_index;
}

BONOBO_TYPE_FUNC_FULL (CorbaSheet,
		       GNOME_Gnumeric_Sheet,
		       BONOBO_OBJECT_TYPE,
		       csheet);

/*************************************************************************/

static void
scc_finalize (GObject *obj)
{
	GObjectClass *parent_class;
	SheetControlCORBA *scc = SHEET_CONTROL_CORBA (obj);
	CorbaSheet *cs = scc->servant;

	if (cs != NULL) {
		scc->servant = NULL;
		cs->container = NULL; /* break loop */
		bonobo_object_unref (BONOBO_OBJECT (cs));
	}

	parent_class = g_type_class_peek (SHEET_CONTROL_TYPE);
	parent_class->finalize (obj);
}

static void
scc_class_init (GObjectClass *object_class)
{
	object_class->finalize	    = &scc_finalize;
}

static void
scc_init (SheetControlCORBA *scc)
{
}

GSF_CLASS (SheetControlCORBA, sheet_control_corba,
	   scc_class_init, scc_init, SHEET_CONTROL_TYPE);

SheetControl *
sheet_control_corba_new (SheetView *sv)
{
	SheetControlCORBA *scc =
		g_object_new (SHEET_CONTROL_CORBA_TYPE, NULL);
	scc->servant = g_object_new (CORBA_SHEET_TYPE, NULL);
	scc->servant->container = scc;
	sv_attach_control (sv, SHEET_CONTROL (scc));
	return SHEET_CONTROL (scc);
}

GNOME_Gnumeric_Sheet
sheet_control_corba_obj (SheetControl *sc)
{
	SheetControlCORBA *scc = SHEET_CONTROL_CORBA (sc);
	g_return_val_if_fail (scc != NULL, NULL);
	return bonobo_object_corba_objref (BONOBO_OBJECT (scc->servant));
}
