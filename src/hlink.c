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
gnm_hlink_activate (GnmHLink *link, WorkbookControl *wbv)
{
	g_return_val_if_fail (GNM_IS_HLINK (link), FALSE);

	return GET_CLASS (link)->Activate (link, wbv);
}

GnmHLink *
sheet_hlink_find (Sheet const *sheet, CellPos const *pos)
{
	MStyle const *style = sheet_style_get (sheet, pos->col, pos->row);
	return mstyle_is_element_set (style, MSTYLE_HLINK)
		?  mstyle_get_hlink (style) : NULL;
}

static void
gnm_hlink_finalize (GObject *obj)
{
	GObjectClass *parent_class;
	GnmHLink *link = (GnmHLink *)obj;

	g_free (link->tip);
	link->tip = NULL;

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
	GnmHLink *link = (GnmHLink * )obj;
	link->tip = NULL;
}
GSF_CLASS_ABSTRACT (GnmHLink, gnm_hlink,
		    gnm_hlink_class_init, gnm_hlink_init, G_TYPE_OBJECT)

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
	/* just parse it as necessary, no worries about maintaining link */
	char *target;
} GnmHLinkCurWB;
#define GNM_HLINK_CUR_WB(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), gnm_hlink_cur_wb_get_type (), GnmHLinkCurWB))

static gboolean
gnm_hlink_cur_wb_activate (GnmHLink *link, WorkbookControl *wbc)
{
	GnmHLinkCurWB *cur_wb = (GnmHLinkCurWB *)link;
	RangeRef const *r;
	CellPos tmp;
	Sheet	  *sheet = wb_control_cur_sheet      (wbc);
	SheetView *sv	 = wb_control_cur_sheet_view (wbc);
	Value *target = global_range_parse (sheet, cur_wb->target);

	/* not an address, is it a name ? */
	if (target == NULL) {
		ParsePos pp;
		GnmNamedExpr *nexpr = expr_name_lookup (
			parse_pos_init (&pp, NULL, sheet, 0, 0), cur_wb->target);

		if (nexpr != NULL) {
			if (!nexpr->builtin)
				target = gnm_expr_get_range (nexpr->t.expr_tree);
			if (target == NULL) {
				gnumeric_error_invalid (COMMAND_CONTEXT (wbc), _("Link target"),
							cur_wb->target);
				return FALSE;
			}
		}
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
gnm_hlink_cur_wb_finalize (GObject *obj)
{
	GObjectClass *parent_class;
	GnmHLinkCurWB *link = (GnmHLinkCurWB *)obj;

	g_free (link->target);
	link->target = NULL;

	parent_class = g_type_class_peek (GNM_HLINK_TYPE);
	if (parent_class && parent_class->finalize)
		parent_class->finalize (obj);
}

static void
gnm_hlink_cur_wb_class_init (GObjectClass *object_class)
{
	GnmHLinkClass *hlink_class = (GnmHLinkClass *) object_class;

	hlink_class->Activate	   = gnm_hlink_cur_wb_activate;
	object_class->finalize	   = gnm_hlink_cur_wb_finalize;
}
static void
gnm_hlink_cur_wb_init (GObject *obj)
{
	GnmHLinkCurWB *link = (GnmHLinkCurWB* )obj;
	link->target = NULL;
}

GSF_CLASS (GnmHLinkCurWB, gnm_hlink_cur_wb,
	   gnm_hlink_cur_wb_class_init, gnm_hlink_cur_wb_init,
	   GNM_HLINK_TYPE)

guchar const *
gnm_hlink_cur_wb_get_target (GnmHLink const *link)
{
	GnmHLinkCurWB const *cur_wb = GNM_HLINK_CUR_WB (link);

	g_return_val_if_fail (cur_wb != NULL, NULL);
	return cur_wb->target;
}

void
gnm_hlink_cur_wb_set_target (GnmHLink *link, guchar const *target)
{
	GnmHLinkCurWB *cur_wb = GNM_HLINK_CUR_WB (link);
	guchar *tmp;

	g_return_if_fail (cur_wb != NULL);

	tmp = g_strdup (target);
	g_free (cur_wb->target);
	cur_wb->target = tmp;
}

/***************************************************************************/
/* Link to arbitrary urls */
typedef struct { GnmHLinkClass hlink; } GnmHLinkURLClass;
typedef struct {
	GnmHLink hlink;
	char *url;
} GnmHLinkURL;
#define GNM_HLINK_URL(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), gnm_hlink_url_get_type (), GnmHLinkURL))

static gboolean
gnm_hlink_url_activate (GnmHLink *link, WorkbookControl *wbc)
{
	GnmHLinkURL *url = (GnmHLinkURL *)link;
	GError *err = NULL;
	gboolean res;
	
	if (url->url == NULL)
		return FALSE;

	res = gnome_url_show (url->url, &err);

	if (err != NULL) {
		char *msg = g_strdup_printf(_("Unable to activate the url '%s'"), url->url);
		gnumeric_error_invalid (COMMAND_CONTEXT (wbc), msg, err->message);
		g_free (msg);
		g_error_free (err);
	}

	return res;
}
static void
gnm_hlink_url_finalize (GObject *obj)
{
	GObjectClass *parent_class;
	GnmHLinkURL *link = (GnmHLinkURL *)obj;

	g_free (link->url);
	link->url = NULL;

	parent_class = g_type_class_peek (GNM_HLINK_TYPE);
	if (parent_class && parent_class->finalize)
		parent_class->finalize (obj);
}
static void
gnm_hlink_url_class_init (GObjectClass *object_class)
{
	GnmHLinkClass *hlink_class = (GnmHLinkClass *) object_class;

	hlink_class->Activate  = gnm_hlink_url_activate;
	object_class->finalize = gnm_hlink_url_finalize;
}
static void
gnm_hlink_url_init (GObject *obj)
{
	GnmHLinkURL *link = (GnmHLinkURL* )obj;
	link->url = NULL;
}

GSF_CLASS (GnmHLinkURL, gnm_hlink_url,
	   gnm_hlink_url_class_init, gnm_hlink_url_init,
	   GNM_HLINK_TYPE)

guchar const *
gnm_hlink_url_get_url (GnmHLink const *link)
{
	GnmHLinkURL const *url = GNM_HLINK_URL (link);

	g_return_val_if_fail (url != NULL, NULL);
	return url->url;
}

void
gnm_hlink_url_set_target (GnmHLink *link, guchar const *target)
{
	GnmHLinkURL *url = GNM_HLINK_URL (link);
	guchar *tmp;

	g_return_if_fail (url != NULL);

	tmp = g_strdup (target);
	g_free (url->url);
	url->url = tmp;
}

