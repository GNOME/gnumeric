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
#include "ranges.h"
#include "selection.h"
#include "application.h"

#include <gal/util/e-util.h>
#include <libgnome/gnome-i18n.h>

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

static void
sv_weakref_notify (SheetView **ptr, GObject *sv)
{
	g_return_if_fail (ptr != NULL);
	g_return_if_fail (*ptr == (SheetView *)sv); /* remember sv is dead */
	*ptr = NULL;
}

void
sv_weak_ref (SheetView *sv, SheetView **ptr)
{
	g_return_if_fail (ptr != NULL);

	*ptr = sv;
	if (sv != NULL)
		g_object_weak_ref (G_OBJECT (sv),
			(GWeakNotify) sv_weakref_notify,
			ptr);
}

void
sv_weak_unref (SheetView **ptr)
{
	g_return_if_fail (ptr != NULL);

	if (*ptr != NULL) {
		g_object_weak_unref (G_OBJECT (*ptr),
			(GWeakNotify) sv_weakref_notify,
			ptr);
		*ptr = NULL;
	}
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

	sv_unant (sv);

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

void
sv_unant (SheetView *sv)
{
	GList *ptr;

	g_return_if_fail (IS_SHEET_VIEW (sv));

	if (sv->ants == NULL)
		return;
	for (ptr = sv->ants; ptr != NULL; ptr = ptr->next)
		g_free (ptr->data);
	g_list_free (sv->ants);
	sv->ants = NULL;

	SHEET_VIEW_FOREACH_CONTROL (sv, control,
		sc_unant (control););
}

void
sv_ant (SheetView *sv, GList *ranges)
{
	GList *ptr;

	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (ranges != NULL);

	if (sv->ants != NULL)
		sv_unant (sv);
	for (ptr = ranges; ptr != NULL; ptr = ptr->next)
		sv->ants = g_list_prepend (sv->ants, range_dup (ptr->data));
	sv->ants = g_list_reverse (sv->ants);

	SHEET_VIEW_FOREACH_CONTROL (sv, control,
		sc_ant (control););
}

gboolean
sv_selection_copy (SheetView *sv, WorkbookControl *wbc)
{
	Range const *sel;

	if (!(sel = selection_first_range (sv_sheet (sv), wbc, _("Copy"))))
		return FALSE;

	application_clipboard_cut_copy (wbc, FALSE, sv, sel, TRUE);

	return TRUE;
}

gboolean
sv_selection_cut (SheetView *sv, WorkbookControl *wbc)
{
	Range const *sel;

	/* 'cut' is a poor description of what we're
	 * doing here.  'move' would be a better
	 * approximation.  The key portion of this process is that
	 * the range being moved has all
	 * 	- references to it adjusted to the new site.
	 * 	- relative references from it adjusted.
	 *
	 * NOTE : This command DOES NOT MOVE ANYTHING !
	 *        We only store the src, paste does the move.
	 */
	g_return_val_if_fail (IS_SHEET_VIEW (sv), FALSE);

	if (!(sel = selection_first_range (sv_sheet (sv), wbc, _("Cut"))))
		return FALSE;

	if (sheet_range_splits_region (sv_sheet (sv), sel, NULL, wbc, _("Cut")))
		return FALSE;

	application_clipboard_cut_copy (wbc, TRUE, sv, sel, TRUE);

	return TRUE;
}
