/* vim: set sw=8: */

/*
 * sheet-view.c:
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
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

#include "sheet.h"
#include "sheet-view.h"
#include "sheet-control.h"
#include "sheet-control-priv.h"

#include <gal/util/e-util.h>

Sheet *
sv_sheet (SheetView const *sv)
{
	return sv->sheet;
}

WorkbookView *
sv_wbv (SheetView const *sv)
{
	return sv->wbv;
}

void
sv_attach_control (SheetView *sv, SheetControl *sc)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (IS_SHEET_CONTROL (sc));
	g_return_if_fail (sc->view == NULL);

	if (sv->controls == NULL)
		sv->controls = g_ptr_array_new ();
	g_ptr_array_add (sv->controls, sc);
	sc->view  = sv;
	sc->sheet = sv_sheet (sv); /* convenient */
}

void
sv_detach_control (SheetControl *sc)
{
	g_return_if_fail (IS_SHEET_CONTROL (sc));
	g_return_if_fail (IS_SHEET_VIEW (sc->view));

	g_ptr_array_remove (sc->view->controls, sc);
	if (sc->view->controls->len == 0) {
		g_ptr_array_free (sc->view->controls, TRUE);
		sc->view->controls = NULL;
	}
	sc->view = NULL;
}

static GObjectClass *parent_class;
static void
s_view_finalize (GObject *object)
{
	SheetView *sv = SHEET_VIEW (object);

	if (sv->controls != NULL) {
		SHEET_VIEW_FOREACH_CONTROL (sv, control, {
			sv_detach_control (control);
			g_object_unref (G_OBJECT (control));
		});
		if (sv->controls != NULL)
			g_warning ("Unexpected left over controls");
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
sheet_view_class_init (GObjectClass *klass)
{
	SheetViewClass *wbc_class = SHEET_VIEW_CLASS (klass);

	g_return_if_fail (wbc_class != NULL);

	parent_class = g_type_class_peek (G_TYPE_OBJECT);
	klass->finalize = s_view_finalize;
}

E_MAKE_TYPE (sheet_view, "SheetView", SheetView,
	     sheet_view_class_init, NULL, G_TYPE_OBJECT);

SheetView *
sheet_view_new (Sheet *sheet, WorkbookView *wbv)
{
	SheetView *sv = g_object_new (SHEET_VIEW_TYPE, NULL);
	sheet_attach_view (sheet, sv);
	sv->wbv = wbv;
	return sv;
}
