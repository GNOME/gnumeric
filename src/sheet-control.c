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

#include <gal/util/e-util.h>

#define SC_CLASS(o) SHEET_CONTROL_CLASS ((o)->object.klass)
#define SC_VIRTUAL_FULL(func, handle, arglist, call)		\
void sc_ ## func arglist				        \
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
	/* Commented out until needed */
	/* SheetControl *sc = SHEET_CONTROL (obj); */ 
	GTK_OBJECT_CLASS (parent_class)->destroy (obj);
}

static void
sc_class_init (GtkObjectClass *object_class)
{
	parent_class = gtk_type_class (gtk_object_get_type ());
	object_class->destroy = sc_destroy;
}

E_MAKE_TYPE (sheet_control, "SheetControl", SheetControl,
	     sc_class_init, NULL, GTK_TYPE_OBJECT);

Sheet *
sc_sheet (SheetControl *sc)
{
	g_return_val_if_fail (IS_SHEET_CONTROL (sc), NULL);
	return sc->sheet;
}

void
sc_set_sheet (SheetControl *sc, Sheet *sheet)
{
	g_return_if_fail (IS_SHEET_CONTROL (sc));
	sc->sheet = sheet;
}


SC_VIRTUAL (resize, (SheetControl *sc, gboolean force_scroll), (sc, force_scroll))

SC_VIRTUAL (set_zoom_factor, (SheetControl *sc), (sc))

SC_VIRTUAL (redraw_all, (SheetControl *sc), (sc))

SC_VIRTUAL (redraw_region,
	    (SheetControl *sc,
	     int start_col, int start_row, int end_col, int end_row),
	    (sc, start_col, start_row, end_col, end_row))

SC_VIRTUAL (redraw_headers,
	    (SheetControl *sc,
	     gboolean const col, gboolean const row, Range const * r),
	    (sc, col, row, r))

SC_VIRTUAL (ant, (SheetControl *sc), (sc))

SC_VIRTUAL (unant, (SheetControl *sc), (sc))

SC_VIRTUAL (adjust_preferences, (SheetControl *sc), (sc))

SC_VIRTUAL (update_cursor_pos, (SheetControl *sc), (sc))

SC_VIRTUAL (scrollbar_config, (SheetControl const *sc), (sc));

SC_VIRTUAL (mode_edit, (SheetControl *sc), (sc));

SC_VIRTUAL (compute_visible_region,
	    (SheetControl *sc, gboolean full_recompute),
	    (sc, full_recompute))

SC_VIRTUAL (make_cell_visible,
	    (SheetControl *sc, int col, int row, gboolean force_scroll, gboolean couple_panes),
	    (sc, col, row, force_scroll, couple_panes));

SC_VIRTUAL (cursor_bound, (SheetControl *sc, Range const *r), (sc, r))
SC_VIRTUAL (set_panes, (SheetControl *sc), (sc))
