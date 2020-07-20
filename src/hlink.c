/*
 * hlink.c: hyperlink support
 *
 * Copyright (C) 2000-2005 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <hlink.h>
#include <hlink-impl.h>
#include <command-context.h>
#include <workbook-control.h>
#include <workbook-view.h>
#include <selection.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-style.h>
#include <ranges.h>
#include <position.h>
#include <expr-name.h>
#include <expr.h>
#include <value.h>
#include <mstyle.h>

#include <goffice/goffice.h>
#include <gsf/gsf-impl-utils.h>
#include <gnm-i18n.h>

#define GET_CLASS(instance) G_TYPE_INSTANCE_GET_CLASS (instance, GNM_HLINK_TYPE, GnmHLinkClass)

static GObjectClass *gnm_hlink_parent_class;

enum {
	PROP_0,
	PROP_SHEET,
};

/*
 * WARNING WARNING WARNING
 *
 * The type names are used in the xml persistence DO NOT CHANGE THEM
 */
/**
 * gnm_hlink_activate:
 * @lnk: #GnmHLink
 * @wbcg: the wbcg that activated the link
 *
 * Returns: %TRUE if the link successfully activated.
 **/
gboolean
gnm_hlink_activate (GnmHLink *lnk, WBCGtk *wbcg)
{
	g_return_val_if_fail (GNM_IS_HLINK (lnk), FALSE);

	return GET_CLASS (lnk)->Activate (lnk, wbcg);
}

/**
 * gnm_sheet_hlink_find:
 * @sheet: #Sheet
 * @pos: #GcmCellPos
 *
 * Returns: (transfer none) (nullable): the found #GnmHLink.
 **/
GnmHLink *
gnm_sheet_hlink_find (Sheet const *sheet, GnmCellPos const *pos)
{
	GnmStyle const *style = sheet_style_get (sheet, pos->col, pos->row);
	return gnm_style_get_hlink (style);
}

static void
gnm_hlink_finalize (GObject *obj)
{
	GnmHLink *lnk = (GnmHLink *)obj;

	g_free (lnk->target);
	lnk->target = NULL;

	g_free (lnk->tip);
	lnk->tip = NULL;

	gnm_hlink_parent_class->finalize (obj);
}

static void
gnm_hlink_base_set_target (GnmHLink *lnk, gchar const *target)
{
	gchar *tmp = g_strdup (target);
	g_free (lnk->target);
	lnk->target = tmp;
}

static const char *
gnm_hlink_base_get_target (GnmHLink const *lnk)
{
	return lnk->target;
}

static void
gnm_hlink_base_set_property (GObject *object, guint property_id,
			     GValue const *value, GParamSpec *pspec)
{
	GnmHLink *hlink = (GnmHLink *)object;

	switch (property_id) {
	case PROP_SHEET:
		// Construction-time only
		hlink->sheet = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}


static void
gnm_hlink_class_init (GObjectClass *object_class)
{
	GnmHLinkClass *hlink_class = (GnmHLinkClass *)object_class;

	gnm_hlink_parent_class = g_type_class_peek_parent (object_class);

	object_class->finalize = gnm_hlink_finalize;
	object_class->set_property = gnm_hlink_base_set_property;
	g_object_class_install_property (object_class, PROP_SHEET,
		g_param_spec_object ("sheet",
				     P_("Parent sheet"),
				     P_("The sheet in which the link lives"),
				     GNM_SHEET_TYPE,
				     GSF_PARAM_STATIC |
				     G_PARAM_WRITABLE |
				     G_PARAM_CONSTRUCT_ONLY));

	hlink_class->set_target = gnm_hlink_base_set_target;
	hlink_class->get_target = gnm_hlink_base_get_target;
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

/**
 * gnm_hlink_get_target:
 * @lnk: #GnmHLink
 *
 * Returns: (transfer none): @lnk's target.
 */
const char *
gnm_hlink_get_target (GnmHLink const *lnk)
{
	g_return_val_if_fail (GNM_IS_HLINK (lnk), NULL);

	return GET_CLASS (lnk)->get_target (lnk);
}

void
gnm_hlink_set_target (GnmHLink *lnk, gchar const *target)
{
	g_return_if_fail (GNM_IS_HLINK (lnk));

	GET_CLASS (lnk)->set_target (lnk, target);
}

/**
 * gnm_hlink_get_tip:
 * @lnk: #GnmHLink
 *
 * Returns: (transfer none): @lnk's tooltip.
 */
const char *
gnm_hlink_get_tip (GnmHLink const *lnk)
{
	g_return_val_if_fail (GNM_IS_HLINK (lnk), NULL);
	return lnk->tip;
}

void
gnm_hlink_set_tip (GnmHLink *lnk, gchar const *tip)
{
	gchar *tmp;

	g_return_if_fail (GNM_IS_HLINK (lnk));

	tmp = g_strdup (tip);
	g_free (lnk->tip);
	lnk->tip = tmp;
}

/**
 * gnm_hlink_get_sheet:
 * @lnk: #GnmHLink
 *
 * Returns: (transfer none): the sheet
 */
Sheet *
gnm_hlink_get_sheet (GnmHLink *lnk)
{
	g_return_val_if_fail (GNM_IS_HLINK (lnk), NULL);
	return lnk->sheet;
}

GnmHLink *
gnm_hlink_new (GType typ, Sheet *sheet)
{
	g_return_val_if_fail (typ != 0, NULL);
	g_return_val_if_fail (g_type_is_a (typ, GNM_HLINK_TYPE), NULL);
	g_return_val_if_fail (!G_TYPE_IS_ABSTRACT (typ), NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	return g_object_new (typ, "sheet", sheet, NULL);
}

/**
 * gnm_hlink_dup_to:
 * @lnk: Existing link
 * @sheet: target sheet
 *
 * Returns: (transfer full): A duplicate link.
 */
GnmHLink *
gnm_hlink_dup_to (GnmHLink *lnk, Sheet *sheet)
{
	GnmHLink *new_lnk = gnm_hlink_new (G_OBJECT_TYPE (lnk), sheet);

	gnm_hlink_set_target (new_lnk, gnm_hlink_get_target (lnk));
	gnm_hlink_set_tip (new_lnk, lnk->tip);

	return new_lnk;
}

/**
 * gnm_hlink_equal:
 * @a: a #GnmHLink
 * @b: a #GnmHLink
 * @relax_sheet: if %TRUE, ignore differences solely caused by being linked into different sheets.
 *
 * Returns: %TRUE, if links are equal
 */
gboolean
gnm_hlink_equal (GnmHLink const *a, GnmHLink const *b, gboolean relax_sheet)
{
	g_return_val_if_fail (GNM_IS_HLINK (a), FALSE);
	g_return_val_if_fail (GNM_IS_HLINK (b), FALSE);

	if (a == b)
		return TRUE;

	if (!relax_sheet && a->sheet != b->sheet)
		return FALSE;

	return (g_strcmp0 (a->target, b->target) == 0 &&
		g_strcmp0 (a->tip, b->tip) == 0);
}

/***************************************************************************/
/* Link to named regions within the current workbook */
typedef struct { GnmHLinkClass hlink; } GnmHLinkCurWBClass;
typedef struct {
	GnmHLink hlink;

	GnmDepManaged dep;
} GnmHLinkCurWB;
#define GNM_HLINK_CUR_WB(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), gnm_hlink_cur_wb_get_type (), GnmHLinkCurWB))

#define GNM_IS_HLINK_CUR_WB(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), gnm_hlink_cur_wb_get_type ()))


static GObjectClass *gnm_hlink_cur_wb_parent_class;

static gboolean
gnm_hlink_cur_wb_activate (GnmHLink *lnk, WBCGtk *wbcg)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
	SheetView *sv;
	GnmSheetRange sr;

	if (!gnm_hlink_get_range_target (lnk, &sr)) {
		go_cmd_context_error_invalid
			(GO_CMD_CONTEXT (wbcg),
			 _("Link target"),
			 lnk->target ? lnk->target : "-");
		return FALSE;
	}

	sv = sheet_get_view (sr.sheet,  wb_control_view (wbc));
	sv_selection_set (sv, &sr.range.start,
			  sr.range.start.col, sr.range.start.row,
			  sr.range.end.col, sr.range.end.row);
	gnm_sheet_view_make_cell_visible (sv, sr.range.start.col, sr.range.start.row, FALSE);
	if (wbcg_cur_sheet (wbcg) != sr.sheet)
		wb_view_sheet_focus (wb_control_view (wbc), sr.sheet);

	return TRUE;
}

static void
gnm_hlink_cur_wb_set_target (GnmHLink *lnk, const char *target)
{
	GnmHLinkCurWB *hlcwb = (GnmHLinkCurWB *)lnk;
	GnmExprTop const *texpr = NULL;

	((GnmHLinkClass*)gnm_hlink_cur_wb_parent_class)
		->set_target (lnk, NULL);

	if (target && lnk->sheet) {
		GnmParsePos pp;
		GnmExprParseFlags flags = GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_INVALID;
		GnmConventions const *convs = lnk->sheet->convs;

		parse_pos_init_sheet (&pp, lnk->sheet);
		texpr = gnm_expr_parse_str (target, &pp, flags, convs, NULL);

		if (texpr == NULL || gnm_expr_top_is_err (texpr, GNM_ERROR_REF)) {
			// Nothing, error
		} else if (gnm_expr_get_name (texpr->expr)) {
			// Nothing, we're good
		} else {
			// Allow only ranges and normalize
			GnmValue *v = gnm_expr_top_get_range (texpr);
			gnm_expr_top_unref (texpr);
			texpr = v ? gnm_expr_top_new_constant (v) : NULL;
		}
	}

	dependent_managed_set_sheet (&hlcwb->dep, lnk->sheet);
	dependent_managed_set_expr (&hlcwb->dep, texpr);
	if (texpr)
		gnm_expr_top_unref (texpr);
}

static const char *
gnm_hlink_cur_wb_get_target (GnmHLink const *lnk)
{
	GnmHLinkCurWB *hlcwb = (GnmHLinkCurWB *)lnk;
	GnmExprTop const *texpr = dependent_managed_get_expr (&hlcwb->dep);
	char *tgt = NULL;
	Sheet *sheet = lnk->sheet;

	if (texpr && sheet) {
		GnmConventions const *convs = sheet_get_conventions (sheet);
		GnmParsePos pp;
		parse_pos_init_sheet (&pp, sheet);
		tgt = gnm_expr_top_as_string (texpr, &pp, convs);
	}

	// Use parent class for storage.  Ick!
	((GnmHLinkClass*)gnm_hlink_cur_wb_parent_class)
		->set_target ((GnmHLink *)lnk, tgt);
	g_free (tgt);

	return ((GnmHLinkClass*)gnm_hlink_cur_wb_parent_class)
		->get_target (lnk);
}

static void
gnm_hlink_cur_wb_init (GObject *obj)
{
	GnmHLinkCurWB *hlcwb = (GnmHLinkCurWB *)obj;
	dependent_managed_init (&hlcwb->dep, NULL);
}

static void
gnm_hlink_cur_wb_finalize (GObject *obj)
{
	GnmHLinkCurWB *hlcwb = (GnmHLinkCurWB *)obj;

	dependent_managed_set_expr (&hlcwb->dep, NULL);

	gnm_hlink_cur_wb_parent_class->finalize (obj);
}

static void
gnm_hlink_cur_wb_class_init (GObjectClass *object_class)
{
	GnmHLinkClass *hlink_class = (GnmHLinkClass *) object_class;

	gnm_hlink_cur_wb_parent_class = g_type_class_peek_parent (object_class);

	object_class->finalize = gnm_hlink_cur_wb_finalize;
	hlink_class->Activate = gnm_hlink_cur_wb_activate;
	hlink_class->set_target = gnm_hlink_cur_wb_set_target;
	hlink_class->get_target = gnm_hlink_cur_wb_get_target;
}

GSF_CLASS (GnmHLinkCurWB, gnm_hlink_cur_wb,
	   gnm_hlink_cur_wb_class_init, gnm_hlink_cur_wb_init,
	   GNM_HLINK_TYPE)
#if 0
;
#endif


/**
 * gnm_hlink_get_range_target:
 * @lnk: the hyperlink to query
 * @sr: (out): location to store link target range
 *
 * This function determines the location that a link points to.  It will
 * resolve names.
 *
 * Returns: %TRUE, if the link refers to a range.
 */
gboolean
gnm_hlink_get_range_target (GnmHLink const *lnk, GnmSheetRange *sr)
{
	GnmHLinkCurWB *hlcwb;
	GnmExprTop const *texpr;
	GnmValue *vr;
	GnmRangeRef const *r;
	GnmParsePos pp;
	Sheet *start_sheet, *end_sheet;

	memset (sr, 0, sizeof (*sr));

	g_return_val_if_fail (GNM_IS_HLINK (lnk), FALSE);

	if (!GNM_IS_HLINK_CUR_WB (lnk))
		return FALSE;

	hlcwb = (GnmHLinkCurWB *)lnk;
	texpr = dependent_managed_get_expr (&hlcwb->dep);
	if (!texpr)
		return FALSE;
	vr = gnm_expr_top_get_range (texpr);
	if (!vr)
		return FALSE;
	r = value_get_rangeref (vr);

	parse_pos_init_sheet (&pp, lnk->sheet);
	gnm_rangeref_normalize_pp (r, &pp, &start_sheet, &end_sheet,
				   &sr->range);
	sr->sheet = start_sheet;
	value_release (vr);

	return TRUE;
}


/**
 * gnm_hlink_get_target_expr:
 * @lnk: the hyperlink to query
 *
 * This function determines the location that a link points to.
 *
 * Returns: (transfer none) (nullable): A #GnmExprTop describing the target.
 */
GnmExprTop const *
gnm_hlink_get_target_expr (GnmHLink const *lnk)
{

	GnmHLinkCurWB *hlcwb;

	g_return_val_if_fail (GNM_IS_HLINK (lnk), NULL);

	if (!GNM_IS_HLINK_CUR_WB (lnk))
		return NULL;

	hlcwb = (GnmHLinkCurWB *)lnk;
	return dependent_managed_get_expr (&hlcwb->dep);
}



/***************************************************************************/
/* Link to arbitrary urls */
typedef struct { GnmHLinkClass hlink; } GnmHLinkURLClass;
typedef struct {
	GnmHLink hlink;
} GnmHLinkURL;

static gboolean
gnm_hlink_url_activate (GnmHLink *lnk, WBCGtk *wbcg)
{
	GError *err = NULL;
	GdkScreen *screen;

	if (lnk->target == NULL)
		return FALSE;

	screen = gtk_window_get_screen (wbcg_toplevel (wbcg));
	err = go_gtk_url_show (lnk->target, screen);

	if (err != NULL) {
		char *msg = g_strdup_printf (_("Unable to activate the url '%s'"), lnk->target);
		go_cmd_context_error_invalid (GO_CMD_CONTEXT (wbcg),
					      msg, err->message);
		g_free (msg);
		g_error_free (err);
	}

	return err == NULL;
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
gnm_hlink_external_activate (GnmHLink *lnk, WBCGtk *wbcg)
{
	GError *err = NULL;
	gboolean res = FALSE;
	char *cmd;
	GdkScreen *screen;

	if (lnk->target == NULL)
		return FALSE;

	cmd = go_shell_arg_to_uri (lnk->target);
	screen = gtk_window_get_screen (wbcg_toplevel (wbcg));
	err = go_gtk_url_show (cmd, screen);
	g_free (cmd);

	if (err != NULL) {
		char *msg = g_strdup_printf(_("Unable to open '%s'"), lnk->target);
		go_cmd_context_error_invalid (GO_CMD_CONTEXT (wbcg),
					      msg, err->message);
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

/**
 * gnm_hlink_init_: (skip)
 */
void
gnm_hlink_init_ (void)
{
	/* make sure that all hlink types are registered */
	gnm_hlink_cur_wb_get_type ();
	gnm_hlink_url_get_type ();
	gnm_hlink_email_get_type ();
	gnm_hlink_external_get_type ();
}
