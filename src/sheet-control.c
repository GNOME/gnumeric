/* vim: set sw=8: */

/*
 * sheet-control.c:
 *
 * Copyright (C) 2001 Jon K Hellan (hellan@acm.org)
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
#include <config.h>
#include "sheet-control-priv.h"
#include "sheet-control.h"
#include "gnumeric-type-util.h"

#define SC_CLASS(o) SHEET_CONTROL_CLASS ((o)->object.klass)
#define SC_VIRTUAL_FULL(func, handle, arglist, call)		\
void sc_control_ ## func arglist				\
{								\
	SheetControlClass *sc_class;			        \
								\
	g_return_if_fail (IS_SHEET_CONTROL (sc));		\
								\
	sc_class = SC_CLASS (sc);				\
	if (sc_class != NULL && sc_class->handle != NULL)	\
		sc_class->handle call;				\
}
#define SC_VIRTUAL(func, arglist, call) SC_VIRTUAL_FULL(func, func, arglist, call)

/*****************************************************************************/

static GtkObjectClass *parent_class;

static void
sc_destroy (GtkObject *obj)
{
	SheetControl *sc = SHEET_CONTROL (obj);
	GTK_OBJECT_CLASS (parent_class)->destroy (obj);
}

static void
sheet_control_ctor_class (GtkObjectClass *object_class)
{
	parent_class = gtk_type_class (gtk_object_get_type ());
	object_class->destroy = sc_destroy;
}

GNUMERIC_MAKE_TYPE(sheet_control, "SheetControl", SheetControl,
		   sheet_control_ctor_class, NULL, gtk_object_get_type ())

void
sheet_control_init_state (SheetControl *sc)
{
	SheetControlClass *sc_class;

	g_return_if_fail (IS_SHEET_CONTROL (sc));

	sc_class = SC_CLASS (sc);
	if (sc_class != NULL && sc_class->init_state != NULL)
		sc_class->init_state (sc);
}
