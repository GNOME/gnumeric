/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * hlink.c: hyperlink support
 *
 * Copyright (C) 2000-2002 Jody Goldberg (jody@gnome.org)
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
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "hlink.h"
#include "hlink-impl.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "selection.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-style.h"
#include "ranges.h"
#include "position.h"
#include "expr-name.h"
#include "value.h"
#include "mstyle.h"

#include <gsf/gsf-impl-utils.h>
#include <libgnome/gnome-url.h>

#define GET_CLASS(instance) G_TYPE_INSTANCE_GET_CLASS (instance, GNM_HLINK_TYPE, GnmHLinkClass)

/**
 * WARNING WARNING WARNING
 *
 * The type names are used in the xml persistence DO NOT CHANGE THEM
 **/
/**
 * gnm_hlink_activate :
 * @link :
 * @wbv : the view that activated the link
 *
 * Return TRUE if the link successfully activated.
 **/
gboolean
gnm_hlink_activate (GnmHLink *lnk, WorkbookControl *wbc)
{
	g_return_val_if_fail (GNM_IS_HLINK (lnk), FALSE);

	return GET_CLASS (lnk)->Activate (lnk, wbc);
}

GnmHLink *
sheet_hlink_find (Sheet const *sheet, CellPos const *pos)
{
	MStyle const *style = sheet_style_get (sheet, pos->col, pos->row);
	return mstyle_get_hlink (style);
}

static void
gnm_hlink_finalize (GObject *obj)
{
	GObjectClass *parent_class;
	GnmHLink *lnk = (GnmHLink *)obj;

	g_free (lnk->target);
	lnk->target = NULL;

	g_free (lnk->tip);
	lnk->tip = NULL;

	parent_class = g_type_class_peek (G_TYPE_OBJECT);
	if (parent_class && parent_class->finalize)
		parent_class->finalize (obj);
}

static void
gnm_hlink_class_init (GObjectClass *object_class)
{
	object_class->finalize = gnm_hlink_finalize;
}
static void
gnm_hlink_init (GObject *obj)
{
	GnmHLink *lnk = (GnmHLink * )obj;
	lnk->target = NULL;
	lnk->tip = NULL;
}
GSF_CLASS_ABSTRACT (GnmHLink, gnm_hlink,
		    gnm_hlink_class_init, gnm_hlink_init, G_TYPE_OBJECT)

guchar const *
gnm_hlink_get_target (GnmHLink const *lnk)
{
	g_return_val_if_fail (GNM_IS_HLINK (lnk), NULL);
	return lnk->target;
}

void
gnm_hlink_set_target (GnmHLink *lnk, guchar const *target)
{
	guchar *tmp;

	g_return_if_fail (GNM_IS_HLINK (lnk));

	tmp = g_strdup (target);
	g_free (lnk->target);
	lnk->target = tmp;
}

guchar const *
gnm_hlink_get_tip (GnmHLink const *l)
{
	g_return_val_if_fail (GNM_IS_HLINK (l), NULL);
	return l->tip;
}

void
gnm_hlink_set_tip (GnmHLink *l, guchar const *tip)
{
	guchar *tmp;

	g_return_if_fail (GNM_IS_HLINK (l));

	tmp = g_strdup (tip);
	g_free (l->tip);
	l->tip = tmp;
}

/***************************************************************************/
/* Link to named regions within the current workbook */
typedef struct { GnmHLinkClass hlink; } GnmHLinkCurWBClass;
typedef struct {
	GnmHLink hlink;
} GnmHLinkCurWB;
#define GNM_HLINK_CUR_WB(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), gnm_hlink_cur_wb_get_type (), GnmHLinkCurWB))

static gboolean
gnm_hlink_cur_wb_activate (GnmHLink *lnk, WorkbookControl *wbc)
{
	RangeRef const *r;
	CellPos tmp;
	Sheet	  *sheet = wb_control_cur_sheet      (wbc);
	SheetView *sv	 = wb_control_cur_sheet_view (wbc);
	Value *target = global_range_parse (sheet, lnk->target);

	/* not an address, is it a name ? */
	if (target == NULL) {
		ParsePos pp;
		GnmNamedExpr *nexpr = expr_name_lookup (
			parse_pos_init_sheet (&pp, sheet), lnk->target);

		if (nexpr != NULL)
			target = gnm_expr_get_range (nexpr->expr);
	}
	if (target == NULL) {
		gnumeric_error_invalid (COMMAND_CONTEXT (wbc),
					_("Link target"),
					lnk->target);
				return FALSE;
	}

	r = &target->v_range.cell;
	tmp.col = r->a.col;
	tmp.row = r->a.row;
	sv = sheet_get_view (r->a.sheet, wb_control_view (wbc));
	sv_selection_set (sv, &tmp, r->a.col, r->a.row, r->b.col, r->b.row);
	sv_make_cell_visible (sv, r->a.col, r->a.row, FALSE);
	if (sheet != r->a.sheet)
		wb_view_sheet_focus (wb_control_view (wbc), sheet);
	value_release (target);
	return TRUE;
}

static void
gnm_hlink_cur_wb_class_init (GObjectClass *object_class)
{
	GnmHLinkClass *hlink_class = (GnmHLinkClass *) object_class;

	hlink_class->Activate	   = gnm_hlink_cur_wb_activate;
}

GSF_CLASS (GnmHLinkCurWB, gnm_hlink_cur_wb,
	   gnm_hlink_cur_wb_class_init, NULL,
	   GNM_HLINK_TYPE)

/***************************************************************************/
/* Link to arbitrary urls */
typedef struct { GnmHLinkClass hlink; } GnmHLinkURLClass;
typedef struct {
	GnmHLink hlink;
} GnmHLinkURL;

static gboolean
gnm_hlink_url_activate (GnmHLink *lnk, WorkbookControl *wbc)
{
	GError *err = NULL;
	gboolean res;

	if (lnk->target == NULL)
		return FALSE;

	res = gnome_url_show (lnk->target, &err);

	if (err != NULL) {
		char *msg = g_strdup_printf(_("Unable to activate the url '%s'"), lnk->target);
		gnumeric_error_invalid (COMMAND_CONTEXT (wbc), msg, err->message);
		g_free (msg);
		g_error_free (err);
	}

	return res;
}

static void
gnm_hlink_url_class_init (GObjectClass *object_class)
{
	GnmHLinkClass *hlink_class = (GnmHLinkClass *) object_class;

	hlink_class->Activate  = gnm_hlink_url_activate;
}

GSF_CLASS (GnmHLinkURL, gnm_hlink_url,
	   gnm_hlink_url_class_init, NULL,
	   GNM_HLINK_TYPE)

/***************************************************************************/
/* email is just a url, but it is cleaner to stick it in a distinct type   */
typedef struct { GnmHLinkURLClass hlink; } GnmHLinkEMailClass;
typedef struct {
	GnmHLinkURL hlink;
} GnmHLinkEMail;

GSF_CLASS (GnmHLinkEMail, gnm_hlink_email,
	   NULL, NULL,
	   gnm_hlink_url_get_type ())

/***************************************************************************/
/* Link to arbitrary urls */
typedef struct { GnmHLinkClass hlink; } GnmHLinkExternalClass;
typedef struct {
	GnmHLink hlink;
} GnmHLinkExternal;

static gboolean
gnm_hlink_external_activate (GnmHLink *lnk, WorkbookControl *wbc)
{
	GError *err = NULL;
	gboolean res = FALSE;

	if (lnk->target == NULL)
		return FALSE;

#warning TODO
	if (err != NULL) {
		char *msg = g_strdup_printf(_("Unable to open '%s'"), lnk->target);
		gnumeric_error_invalid (COMMAND_CONTEXT (wbc), msg, err->message);
		g_free (msg);
		g_error_free (err);
	}

	return res;
}

static void
gnm_hlink_external_class_init (GObjectClass *object_class)
{
	GnmHLinkClass *hlink_class = (GnmHLinkClass *) object_class;

	hlink_class->Activate  = gnm_hlink_external_activate;
}

GSF_CLASS (GnmHLinkExternal, gnm_hlink_external,
	   gnm_hlink_external_class_init, NULL,
	   GNM_HLINK_TYPE)
