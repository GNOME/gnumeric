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
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "sheet-control-priv.h"

#include <gsf/gsf-impl-utils.h>

#define SC_CLASS(o) SHEET_CONTROL_CLASS (G_OBJECT_GET_CLASS (o))
#define SC_VIRTUAL_FULL(func, handle, arglist, call)		\
void sc_ ## func arglist				        \
{								\
	SheetControlClass *sc_class;			        \
								\
	g_return_if_fail (IS_SHEET_CONTROL (sc));		\
								\
	sc_class = SC_CLASS (sc);				\
	if (sc_class->handle != NULL)				\
		sc_class->handle call;				\
}
#define SC_VIRTUAL(func, arglist, call) SC_VIRTUAL_FULL(func, func, arglist, call)

/*****************************************************************************/

static GObjectClass *parent_class;

static void
sc_finalize (GObject *obj)
{
	/* Commented out until needed */
	/* SheetControl *sc = SHEET_CONTROL (obj); */
	G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
sc_class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_peek (G_TYPE_OBJECT);
	object_class->finalize = sc_finalize;
}

GSF_CLASS (SheetControl, sheet_control,
	   sc_class_init, NULL, G_TYPE_OBJECT);

WorkbookControl *
sc_wbc (SheetControl const *sc)
{
	g_return_val_if_fail (IS_SHEET_CONTROL (sc), NULL);
	return sc->wbc;
}

Sheet *
sc_sheet (SheetControl const *sc)
{
	g_return_val_if_fail (IS_SHEET_CONTROL (sc), NULL);
	return sc->sheet;
}

SheetView *
sc_view (SheetControl const *sc)
{
	g_return_val_if_fail (IS_SHEET_CONTROL (sc), NULL);
	return sc->view;
}

SC_VIRTUAL (resize, (SheetControl *sc, gboolean force_scroll), (sc, force_scroll))

SC_VIRTUAL (set_zoom_factor, (SheetControl *sc), (sc))

SC_VIRTUAL (redraw_all, (SheetControl *sc, gboolean headers), (sc, headers))
SC_VIRTUAL (redraw_range,
	    (SheetControl *sc, Range const *r),
	    (sc, r))
SC_VIRTUAL (redraw_headers,
	    (SheetControl *sc,
	     gboolean const col, gboolean const row, Range const * r),
	    (sc, col, row, r))

SC_VIRTUAL (ant, (SheetControl *sc), (sc))
SC_VIRTUAL (unant, (SheetControl *sc), (sc))

SC_VIRTUAL (adjust_preferences, (SheetControl *sc), (sc))

SC_VIRTUAL (scrollbar_config, (SheetControl const *sc), (sc));

SC_VIRTUAL (mode_edit, (SheetControl *sc), (sc));

SC_VIRTUAL (set_top_left,
	    (SheetControl *sc, int col, int row),
	    (sc, col, row))
SC_VIRTUAL (compute_visible_region,
	    (SheetControl *sc, gboolean full_recompute),
	    (sc, full_recompute))
SC_VIRTUAL (make_cell_visible,
	    (SheetControl *sc, int col, int row, gboolean couple_panes),
	    (sc, col, row, couple_panes));

SC_VIRTUAL (cursor_bound, (SheetControl *sc, Range const *r), (sc, r))
SC_VIRTUAL (set_panes, (SheetControl *sc), (sc))

SC_VIRTUAL (object_create_view,	(SheetControl *sc, SheetObject *so), (sc, so));
SC_VIRTUAL (object_destroy_view, (SheetControl *sc, SheetObject *so), (sc, so));

float
sc_colrow_distance_get (SheetControl const *sc, gboolean is_col,
			int start, int end)
{
	SheetControlClass *sc_class;

	g_return_val_if_fail (IS_SHEET_CONTROL (sc),1.);

	sc_class = SC_CLASS (sc);
	if (sc_class->colrow_distance_get != NULL)
		return sc_class->colrow_distance_get (sc, is_col,start, end);
	return 1.;
}
