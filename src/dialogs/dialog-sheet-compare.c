/*
 * dialog-sheet-compare.c: Dialog to compare two sheets.
 *
 * (C) Copyright 2018 Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <sheet-diff.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gui-util.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <workbook.h>
#include <workbook-priv.h>
#include <sheet.h>
#include <ranges.h>
#include <cell.h>
#include <sheet-style.h>
#include <application.h>
#include <selection.h>
#include <sheet-view.h>
#include <widgets/gnm-sheet-sel.h>
#include <widgets/gnm-workbook-sel.h>

#define SHEET_COMPARE_KEY          "sheet-compare-dialog"

enum {
	ITEM_SECTION,
	ITEM_DIRECTION,
	ITEM_OLD_LOC,
	ITEM_NEW_LOC,
	ITEM_NO,
	ITEM_QCOLS,
	NUM_COLUMNS
};

enum {
	SEC_CELLS,
	SEC_STYLE,
	SEC_COLROW
};

enum {
	DIR_NA,
	DIR_ADDED,
	DIR_REMOVED,
	DIR_CHANGED,
	DIR_QUIET // Like CHANGED, but for always-changed context
};


typedef struct {
	WBCGtk  *wbcg;

	GtkBuilder *gui;
	GtkWidget *dialog;
	GtkWidget *notebook;

	GtkWidget *cancel_btn;
	GtkWidget *compare_btn;

	GtkWidget *sheet_sel_A;
	GtkWidget *sheet_sel_B;
	GtkWidget *wb_sel_A;
	GtkWidget *wb_sel_B;
	GtkWidget *results_window;

	GtkTreeView *results_view;
	GtkTreeStore *results;

	gboolean has_cell_section;
	GtkTreeIter cell_section_iter;

	gboolean has_style_section;
	GtkTreeIter style_section_iter;

	gboolean has_colrow_section;
	GtkTreeIter colrow_section_iter;

	Sheet *old_sheet;
	Sheet *new_sheet;
} SheetCompare;


static void
cb_sheet_compare_destroy (SheetCompare *state)
{
	Workbook *wb = wb_control_get_workbook (GNM_WBC (state->wbcg));

	g_object_unref (state->gui);
	g_object_set_data (G_OBJECT (wb), SHEET_COMPARE_KEY, NULL);
	state->gui = NULL;

	g_free (state);
}

static void
cb_cancel_clicked (G_GNUC_UNUSED GtkWidget *ignore,
		   SheetCompare *state)
{
	    gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static GtkWidget *
create_wb_selector (SheetCompare *state, GtkWidget *sheet_sel)
{
	GtkWidget *res = gnm_workbook_sel_new ();
	gnm_sheet_sel_link (GNM_SHEET_SEL (sheet_sel),
			    GNM_WORKBOOK_SEL (res));
	return res;
}

static void
select_default_sheets (SheetCompare *state)
{
	Workbook *wb = wb_control_get_workbook (GNM_WBC (state->wbcg));
	GList *wb_list = gnm_app_workbook_list ();

	if (g_list_length (wb_list) > 1) {
		// Multiple workbooks

		gnm_workbook_sel_set_workbook
			(GNM_WORKBOOK_SEL (state->wb_sel_A), wb);
		gnm_workbook_sel_set_workbook
			(GNM_WORKBOOK_SEL (state->wb_sel_B),
			 wb == wb_list->data ? wb_list->next->data : wb_list->data);
	} else if (workbook_sheet_count (wb) > 1) {
		// One workbook, multiple sheets
		gnm_sheet_sel_set_sheet (GNM_SHEET_SEL (state->sheet_sel_B),
					 workbook_sheet_by_index (wb, 1));
	} else {
		// One workbook, one sheet
	}
}

/* ------------------------------------------------------------------------- */

static void
setup_section (SheetCompare *state, gboolean *phas, GtkTreeIter *iter,
	       int section)
{
	if (!*phas) {
		gtk_tree_store_insert (state->results, iter, NULL, -1);
		gtk_tree_store_set (state->results, iter,
				    ITEM_SECTION, section,
				    ITEM_DIRECTION, DIR_NA,
				    -1);
		*phas = TRUE;
	}
}

static void
extract_range (GnmRangeRef const *rr, GnmRange *r, Sheet **psheet)
{
	*psheet = rr->a.sheet;
	r->start.col = rr->a.col;
	r->start.row = rr->a.row;
	r->end.col = rr->b.col;
	r->end.row = rr->b.row;
}

static void
loc_from_range (GnmRangeRef *loc, Sheet *sheet, GnmRange const *r)
{
	gnm_cellref_init (&loc->a, sheet,
			  r->start.col, r->start.row,
			  FALSE);
	gnm_cellref_init (&loc->b, sheet,
			  r->end.col, r->end.row,
			  FALSE);
}

static const char *
get_mstyle_name (int e)
{
	switch (e) {
	case MSTYLE_COLOR_BACK: return _("Background color");
	case MSTYLE_COLOR_PATTERN: return _("Pattern color");

	case MSTYLE_BORDER_TOP: return _("Top border");
	case MSTYLE_BORDER_BOTTOM: return _("Bottom border");
	case MSTYLE_BORDER_LEFT: return _("Left border");
	case MSTYLE_BORDER_RIGHT: return _("Right border");
	case MSTYLE_BORDER_REV_DIAGONAL: return _("Reverse diagonal border");
	case MSTYLE_BORDER_DIAGONAL: return _("Diagonal border");
	case MSTYLE_PATTERN: return _("Pattern");

	case MSTYLE_FONT_COLOR: return _("Font color");
	case MSTYLE_FONT_NAME: return _("Font");
	case MSTYLE_FONT_BOLD: return _("Bold");
	case MSTYLE_FONT_ITALIC: return _("Italic");
	case MSTYLE_FONT_UNDERLINE: return _("Underline");
	case MSTYLE_FONT_STRIKETHROUGH: return _("Strikethrough");
	case MSTYLE_FONT_SCRIPT: return _("Script");
	case MSTYLE_FONT_SIZE: return _("Size");

	case MSTYLE_FORMAT: return _("Format");

	case MSTYLE_ALIGN_V: return _("Vertical alignment");
	case MSTYLE_ALIGN_H: return _("Horizontal alignment");
	case MSTYLE_INDENT: return _("Indentation");
	case MSTYLE_ROTATION: return _("Rotation");
	case MSTYLE_TEXT_DIR: return _("Direction");
	case MSTYLE_WRAP_TEXT: return _("Wrap");
	case MSTYLE_SHRINK_TO_FIT: return _("Shrink-to-fit");

	case MSTYLE_CONTENTS_LOCKED: return _("Locked");
	case MSTYLE_CONTENTS_HIDDEN: return _("Hidden");

	case MSTYLE_VALIDATION: return _("Validation");
	case MSTYLE_HLINK: return _("Hyperlink");
	case MSTYLE_INPUT_MSG: return _("Input message");
	case MSTYLE_CONDITIONS: return _("Conditional format");
	default: return "?";
	}
}


static void
section_renderer_func (GtkTreeViewColumn *tree_column,
		       GtkCellRenderer   *cell,
		       GtkTreeModel      *model,
		       GtkTreeIter       *iter,
		       gpointer           user_data)
{
	int section, dir;
	const char *text = "?";
	int e;

	gtk_tree_model_get (model, iter,
			    ITEM_SECTION, &section,
			    ITEM_DIRECTION, &dir,
			    ITEM_NO, &e,
			    -1);
	switch (dir) {
	case DIR_NA:
		switch (section) {
		case SEC_CELLS: text = _("Cells"); break;
		case SEC_STYLE: text = _("Formatting"); break;
		case SEC_COLROW: text = _("Columns/Rows"); break;
		}
		break;
	case DIR_QUIET:
		switch (section) {
		case SEC_STYLE:
			text = (e == -1) ? _("Various") : get_mstyle_name (e);
			break;
		case SEC_COLROW:
			text = _("Size");
			break;
		default:
			text = "";
		}
		break;
	case DIR_ADDED: text = _("Added"); break;
	case DIR_REMOVED: text = _("Removed"); break;
	case DIR_CHANGED: text = _("Changed"); break;
	}

	g_object_set (cell, "text", text, NULL);
}

static void
location_renderer_func (GtkTreeViewColumn *tree_column,
			GtkCellRenderer   *cell,
			GtkTreeModel      *model,
			GtkTreeIter       *iter,
			gpointer           user_data)
{
	GnmRangeRef *loc_old = NULL;
	GnmRangeRef *loc_new = NULL;
	GnmRangeRef *loc;

	gtk_tree_model_get (model, iter,
			    ITEM_OLD_LOC, &loc_new,
			    ITEM_NEW_LOC, &loc_old,
			    -1);

	loc = loc_old ? loc_old : loc_new;
	if (loc) {
		GnmRange r;
		Sheet *sheet;
		char *str = NULL;
		const char *text;

		extract_range (loc, &r, &sheet);

		if (range_is_full (&r, sheet, TRUE) &&
		    r.start.row == r.end.row)
			text = str = g_strdup_printf
				(_("Row %s"), row_name (r.start.row));
		else if (range_is_full (&r, sheet, FALSE) &&
			 r.start.col == r.end.col)
			text = str = g_strdup_printf
				(_("Column %s"), col_name (r.start.col));
		else
			text = range_as_string (&r);

		g_object_set (cell, "text", text, NULL);
		g_free (str);
	} else
		g_object_set (cell, "text", "", NULL);

	g_free (loc_old);
	g_free (loc_new);
}

static char *
do_color (GnmColor const *gcolor)
{
	GOColor color = gcolor->go_color;
	unsigned r, g, b, a;
	char buf[16];
	const char *coltxt = NULL;
	int n;
	GONamedColor nc;

	GO_COLOR_TO_RGBA (color, &r, &g, &b, &a);
	if (a == 0xff)
		snprintf (buf, sizeof (buf), "#%02X%02X%02X", r, g, b);
	else
		snprintf (buf, sizeof (buf), "#%02X%02X%02X%02X", r, g, b, a);

	for (n = 0; go_color_palette_query (n, &nc); n++) {
		if (nc.color == color) {
			coltxt = nc.name;
			break;
		}
	}

	return g_strdup_printf
		("%s%s (<span bgcolor=\"%s\">   </span>)",
		 gcolor->is_auto ? "Auto " : "",
		 coltxt ? coltxt : buf,
		 buf);
}

static char *
do_bool (gboolean b)
{
	return g_strdup (b ? _("Yes") : _("No"));
}

static char *
do_int (int i)
{
	return g_strdup_printf ("%d", i);
}

static char *
do_double (double d)
{
	return g_strdup_printf ("%g", d);
}

static char *
do_halign (GnmHAlign h)
{
	switch (h) {
	case GNM_HALIGN_GENERAL: return g_strdup (_("General"));
	case GNM_HALIGN_LEFT: return g_strdup (_("Left"));
	case GNM_HALIGN_RIGHT: return g_strdup (_("Right"));
	case GNM_HALIGN_CENTER: return g_strdup (_("Center"));
	case GNM_HALIGN_FILL: return g_strdup (_("Fill"));
	case GNM_HALIGN_JUSTIFY: return g_strdup (_("Justify"));
	case GNM_HALIGN_CENTER_ACROSS_SELECTION: return g_strdup (_("Center across selection"));
	case GNM_HALIGN_DISTRIBUTED: return g_strdup (_("Distributed"));
	default: return g_strdup ("?");
	}
}

static char *
do_valign (GnmVAlign v)
{
	switch (v) {
	case GNM_VALIGN_TOP: return g_strdup (_("Top"));
	case GNM_VALIGN_BOTTOM: return g_strdup (_("Bottom"));
	case GNM_VALIGN_CENTER: return g_strdup (_("Center"));
	case GNM_VALIGN_JUSTIFY: return g_strdup (_("Justify"));
	case GNM_VALIGN_DISTRIBUTED: return g_strdup (_("Distributed"));
	default: return g_strdup ("?");
	}
}

static const char *underlines[] = {
	N_("None"), N_("Single"), N_("Double"),
	N_("Single low"), N_("Double low"), NULL
};

static const char *textdirs[] = {
	N_("Right-to-left"), N_("Auto"), N_("Left-to-right"), NULL
};

static char *
do_enum (int i, const char *const choices[])
{
	if (i < 0 || i >= (int)g_strv_length ((gchar **)choices))
		return g_strdup ("?");
	return g_strdup (_(choices[i]));
}


static void
oldnew_renderer_func (GtkTreeViewColumn *tree_column,
		      GtkCellRenderer   *cell,
		      GtkTreeModel      *model,
		      GtkTreeIter       *iter,
		      gpointer           user_data)
{
	gboolean qnew = GPOINTER_TO_UINT (user_data);
	GnmRangeRef *loc = NULL;
	int section, dir, e;
	gboolean is_cols;
	char *text = NULL;
	gboolean qmarkup = FALSE;

	gtk_tree_model_get (model, iter,
			    ITEM_SECTION, &section,
			    ITEM_DIRECTION, &dir,
			    (qnew ? ITEM_NEW_LOC : ITEM_OLD_LOC), &loc,
			    ITEM_NO, &e,
			    ITEM_QCOLS, &is_cols,
			    -1);
	if (dir == DIR_NA || !loc || !loc->a.sheet)
		goto done;

	if (section == SEC_CELLS) {
		GnmCell const *cell =
			sheet_cell_get (loc->a.sheet, loc->a.col, loc->a.row);
		if (!cell)
			goto error;
		text = gnm_cell_get_entered_text (cell);
	} else if (section == SEC_STYLE) {
		GnmStyle const *style;
		if (e == -1)
			goto done;

		style = sheet_style_get (loc->a.sheet, loc->a.col, loc->a.row);

		switch (e) {
		case MSTYLE_COLOR_BACK:
			text = do_color (gnm_style_get_back_color (style));
			qmarkup = TRUE;
			break;
		case MSTYLE_COLOR_PATTERN:
			text = do_color (gnm_style_get_pattern_color (style));
			qmarkup = TRUE;
			break;
		case MSTYLE_FONT_COLOR:
			text = do_color (gnm_style_get_font_color (style));
			qmarkup = TRUE;
			break;
		case MSTYLE_PATTERN:
			// TODO: Add api to get pattern name from goffice
			text = do_int (gnm_style_get_pattern (style));
			break;

		case MSTYLE_FONT_NAME:
			text = g_strdup (gnm_style_get_font_name (style));
			break;
		case MSTYLE_FONT_BOLD:
			text = do_bool (gnm_style_get_font_bold (style));
			break;
		case MSTYLE_FONT_ITALIC:
			text = do_bool (gnm_style_get_font_italic (style));
			break;
		case MSTYLE_FONT_UNDERLINE:
			text = do_enum (gnm_style_get_font_uline (style),
					underlines);
			break;
		case MSTYLE_FONT_STRIKETHROUGH:
			text = do_bool (gnm_style_get_font_strike (style));
			break;
		case MSTYLE_FONT_SCRIPT:
			text = do_int (gnm_style_get_font_script (style));
			break;
		case MSTYLE_FONT_SIZE:
			text = do_double (gnm_style_get_font_size (style));
			break;
		case MSTYLE_ROTATION:
			text = do_int (gnm_style_get_rotation (style));
			break;
		case MSTYLE_INDENT:
			text = do_int (gnm_style_get_indent (style));
			break;

		case MSTYLE_FORMAT:
			text = g_strdup (go_format_as_XL (gnm_style_get_format (style)));
			break;

		case MSTYLE_TEXT_DIR:
			text = do_enum (1 + gnm_style_get_text_dir (style),
					textdirs);
			break;
		case MSTYLE_ALIGN_H:
			text = do_halign (gnm_style_get_align_h (style));
			break;
		case MSTYLE_ALIGN_V:
			text = do_valign (gnm_style_get_align_v (style));
			break;
		case MSTYLE_CONTENTS_LOCKED:
			text = do_bool (gnm_style_get_contents_locked (style));
			break;
		case MSTYLE_CONTENTS_HIDDEN:
			text = do_bool (gnm_style_get_contents_hidden (style));
			break;

		default:
			text = g_strdup (_("Unavailable"));
		}
	} else if (section == SEC_COLROW) {
		ColRowInfo const *cr =
			sheet_colrow_get_info (loc->a.sheet, e, is_cols);
		text = g_strdup_printf (ngettext ("%d pixel", "%d pixels", cr->size_pixels), cr->size_pixels);
	}

done:
	g_object_set (cell,
		      (qmarkup ? "markup" : "text"),
		      (text ? text : ""), NULL);
	g_free (text);

	g_free (loc);
	return;

error:
	text = g_strdup ("?");
	goto done;
}

static void
dsc_sheet_start (gpointer user, Sheet const *os, Sheet const *ns)
{
	SheetCompare *state = user;
	state->old_sheet = (Sheet *)os;
	state->new_sheet = (Sheet *)ns;
}

static void
dsc_sheet_end (gpointer user)
{
	SheetCompare *state = user;
	state->old_sheet = NULL;
	state->new_sheet = NULL;
}

static void
dsc_cell_changed (gpointer user, GnmCell const *oc, GnmCell const *nc)
{
	SheetCompare *state = user;
	GtkTreeIter iter;
	int dir;

	setup_section (state,
		       &state->has_cell_section,
		       &state->cell_section_iter,
		       SEC_CELLS);

	dir = (oc ? (nc ? DIR_CHANGED : DIR_REMOVED) : DIR_ADDED);

	gtk_tree_store_insert (state->results, &iter,
			       &state->cell_section_iter,
			       -1);
	gtk_tree_store_set (state->results, &iter,
			    ITEM_SECTION, SEC_CELLS,
			    ITEM_DIRECTION, dir,
			    -1);

	if (oc) {
		GnmRangeRef loc;
		gnm_cellref_init (&loc.a, oc->base.sheet,
				  oc->pos.col, oc->pos.row,
				  FALSE);
		loc.b = loc.a;
		gtk_tree_store_set (state->results, &iter,
				    ITEM_OLD_LOC, &loc,
				    -1);
	}

	if (nc) {
		GnmRangeRef loc;
		gnm_cellref_init (&loc.a, nc->base.sheet,
				  nc->pos.col, nc->pos.row,
				  FALSE);
		loc.b = loc.a;
		gtk_tree_store_set (state->results, &iter,
				    ITEM_NEW_LOC, &loc,
				    -1);
	}
}

static void
dsc_style_changed (gpointer user, GnmRange const *r,
		   GnmStyle const *os, GnmStyle const *ns)
{
	SheetCompare *state = user;
	GtkTreeIter iter, piter;
	GnmRangeRef loc_old, loc_new;
	unsigned int conflicts;
	int e, estart;

	conflicts = gnm_style_find_differences (os, ns, TRUE);

	setup_section (state,
		       &state->has_style_section,
		       &state->style_section_iter,
		       SEC_STYLE);

	loc_from_range (&loc_old, state->old_sheet, r);
	loc_from_range (&loc_new, state->new_sheet, r);

	piter = state->style_section_iter;
	estart = ((conflicts & (conflicts - 1)) == 0 ? 0 : -1);
	for (e = estart; e < MSTYLE_ELEMENT_MAX; e++) {
		gboolean qhead = (e == -1);
		if (!qhead && (conflicts & (1u << e)) == 0)
			continue;

		gtk_tree_store_insert (state->results, &iter, &piter, -1);
		if (qhead) piter = iter;

		gtk_tree_store_set (state->results, &iter,
				    ITEM_SECTION, SEC_STYLE,
				    ITEM_DIRECTION, DIR_QUIET,
				    ITEM_OLD_LOC, &loc_old,
				    ITEM_NEW_LOC, &loc_new,
				    ITEM_NO, e,
				    -1);
	}
}

static void
dsc_colrow_changed (gpointer user, ColRowInfo const *oc, ColRowInfo const *nc,
		    gboolean is_cols, int i)
{
	SheetCompare *state = user;
	GtkTreeIter iter;
	GnmRangeRef loc_old, loc_new;
	GnmRange rold, rnew;

	(is_cols ? range_init_cols : range_init_rows)
		(&rold, state->old_sheet, i, i);
	loc_from_range (&loc_old, state->old_sheet, &rold);

	(is_cols ? range_init_cols : range_init_rows)
		(&rnew, state->new_sheet, i, i);
	loc_from_range (&loc_new, state->new_sheet, &rnew);

	setup_section (state,
		       &state->has_colrow_section,
		       &state->colrow_section_iter,
		       SEC_COLROW);

	gtk_tree_store_insert (state->results,
			       &iter, &state->colrow_section_iter, -1);

	gtk_tree_store_set (state->results, &iter,
			    ITEM_SECTION, SEC_COLROW,
			    ITEM_DIRECTION, DIR_QUIET,
			    ITEM_OLD_LOC, &loc_old,
			    ITEM_NEW_LOC, &loc_new,
			    ITEM_NO, i,
			    ITEM_QCOLS, is_cols,
			    -1);
}

static const GnmDiffActions dsc_actions = {
	.sheet_start = dsc_sheet_start,
	.sheet_end = dsc_sheet_end,
	.cell_changed = dsc_cell_changed,
	.style_changed = dsc_style_changed,
	.colrow_changed = dsc_colrow_changed,
};

static void
cb_compare_clicked (G_GNUC_UNUSED GtkWidget *ignore,
		    SheetCompare *state)
{
	GtkTreeView *tv = state->results_view;
	GtkTreeStore *ts = gtk_tree_store_new
		(NUM_COLUMNS,
		 G_TYPE_INT, // Enum, really
		 G_TYPE_INT, // Enum, really
		 gnm_rangeref_get_type (),
		 gnm_rangeref_get_type (),
		 G_TYPE_INT,
		 G_TYPE_BOOLEAN);
	Sheet *sheet_A, *sheet_B;

	if (gtk_tree_view_get_n_columns (tv) == 0) {
		GtkTreeViewColumn *tvc;
		GtkCellRenderer *cr;

		tvc = gtk_tree_view_column_new ();
		cr = gtk_cell_renderer_text_new ();
		gtk_tree_view_column_set_title (tvc, _("Description"));
		gtk_tree_view_column_set_cell_data_func
			(tvc, cr, section_renderer_func, NULL, NULL);
		gtk_tree_view_column_pack_start (tvc, cr, TRUE);
		gtk_tree_view_append_column (tv, tvc);

		tvc = gtk_tree_view_column_new ();
		cr = gtk_cell_renderer_text_new ();
		gtk_tree_view_column_set_title (tvc, _("Location"));
		gtk_tree_view_column_set_cell_data_func
			(tvc, cr, location_renderer_func, NULL, NULL);
		gtk_tree_view_column_pack_start (tvc, cr, TRUE);
		gtk_tree_view_append_column (tv, tvc);

		tvc = gtk_tree_view_column_new ();
		cr = gtk_cell_renderer_text_new ();
		g_object_set (G_OBJECT (cr), "max-width-chars", 30, NULL);
		gtk_tree_view_column_set_title (tvc, _("Old"));
		gtk_tree_view_column_set_cell_data_func
			(tvc, cr, oldnew_renderer_func,
			 GUINT_TO_POINTER (FALSE), NULL);
		gtk_tree_view_column_pack_start (tvc, cr, TRUE);
		gtk_tree_view_append_column (tv, tvc);

		tvc = gtk_tree_view_column_new ();
		cr = gtk_cell_renderer_text_new ();
		g_object_set (G_OBJECT (cr), "max-width-chars", 30, NULL);
		gtk_tree_view_column_set_title (tvc, _("New"));
		gtk_tree_view_column_set_cell_data_func
			(tvc, cr, oldnew_renderer_func,
			 GUINT_TO_POINTER (TRUE), NULL);
		gtk_tree_view_column_pack_start (tvc, cr, TRUE);
		gtk_tree_view_append_column (tv, tvc);
	}

	state->has_cell_section = FALSE;
	state->has_style_section = FALSE;
	state->has_colrow_section = FALSE;

	sheet_A = gnm_sheet_sel_get_sheet (GNM_SHEET_SEL (state->sheet_sel_A));
	sheet_B = gnm_sheet_sel_get_sheet (GNM_SHEET_SEL (state->sheet_sel_B));

	if (sheet_A && sheet_B) {
		state->results = ts;
		gnm_diff_sheets (&dsc_actions, state, sheet_A, sheet_B);
		state->results = NULL;
	}

	gtk_tree_view_set_model (tv, GTK_TREE_MODEL (ts));
	g_object_unref (ts);

	gtk_notebook_set_current_page (GTK_NOTEBOOK (state->notebook), 1);
}

static SheetView *
find_and_focus (GnmRangeRef const *loc, SheetView *avoid)
{
	Workbook *wb;
	GnmRange r;
	Sheet *loc_sheet;

	if (!loc)
		return NULL;
	extract_range (loc, &r, &loc_sheet);
	wb = loc_sheet->workbook;

	WORKBOOK_FOREACH_VIEW(wb, view, {
		SheetView *sv;
		int col = r.start.col;
		int row = r.start.row;

		sv = wb_view_cur_sheet_view (view);

		if (sv == avoid)
			continue;
		if (wb_view_cur_sheet (view) != loc_sheet)
			continue;

		gnm_sheet_view_set_edit_pos (sv, &r.start);
		sv_selection_set (sv, &r.start, col, row, col, row);
		gnm_sheet_view_make_cell_visible (sv, col, row, FALSE);
		gnm_sheet_view_update (sv);
		return sv;
		});
	return NULL;
}

static void
cb_cursor_changed (GtkTreeView *tree_view, SheetCompare *state)
{
	GtkTreePath *path;
	gboolean ok;
	GnmRangeRef *loc_old = NULL;
	GnmRangeRef *loc_new = NULL;
	GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
	SheetView *sv;
	GtkTreeIter iter;

	gtk_tree_view_get_cursor (tree_view, &path, NULL);
	if (!path)
		return;

	ok = gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_path_free (path);
	if (!ok)
		return;

	gtk_tree_model_get (model, &iter,
			    ITEM_OLD_LOC, &loc_new,
			    ITEM_NEW_LOC, &loc_old,
			    -1);

	sv = find_and_focus (loc_old, NULL);
	(void)find_and_focus (loc_new, sv);

	g_free (loc_old);
	g_free (loc_new);
}

/* ------------------------------------------------------------------------- */

void
dialog_sheet_compare (WBCGtk *wbcg)
{
	SheetCompare *state;
	GtkBuilder *gui;
	Workbook *wb;
	PangoLayout *layout;
	int height, width;

	g_return_if_fail (wbcg != NULL);

	wb = wb_control_get_workbook (GNM_WBC (wbcg));

	gui = gnm_gtk_builder_load ("res:ui/sheet-compare.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, SHEET_COMPARE_KEY))
		return;

	layout = gtk_widget_create_pango_layout (GTK_WIDGET (wbcg_toplevel (wbcg)), "Mg19");
	pango_layout_get_pixel_size (layout, &width, &height);
	g_object_unref (layout);

	g_object_set_data (G_OBJECT (wb), SHEET_COMPARE_KEY, (gpointer) gui);
	state = g_new0 (SheetCompare, 1);
	state->gui = gui;
	state->wbcg = wbcg;
	state->dialog = go_gtk_builder_get_widget (gui, "sheet-compare-dialog");
	state->notebook = go_gtk_builder_get_widget (gui, "notebook");
	state->cancel_btn = go_gtk_builder_get_widget (gui, "cancel_button");
	state->compare_btn = go_gtk_builder_get_widget (gui, "compare_button");
	state->results_window = go_gtk_builder_get_widget (gui, "results_window");
	state->results_view = GTK_TREE_VIEW (go_gtk_builder_get_widget (gui, "results_treeview"));

	gtk_widget_set_size_request (state->results_window,
				     width / 4 * 40,
				     height * 10);

	state->sheet_sel_A = gnm_sheet_sel_new ();
	state->wb_sel_A = create_wb_selector (state, state->sheet_sel_A);
	go_gtk_widget_replace (go_gtk_builder_get_widget (gui, "sheet_selector_A"),
			       state->sheet_sel_A);
	go_gtk_widget_replace (go_gtk_builder_get_widget (gui, "wb_selector_A"),
			       state->wb_sel_A);

	state->sheet_sel_B = gnm_sheet_sel_new ();
	state->wb_sel_B = create_wb_selector (state, state->sheet_sel_B);
	go_gtk_widget_replace (go_gtk_builder_get_widget (gui, "sheet_selector_B"),
			       state->sheet_sel_B);
	go_gtk_widget_replace (go_gtk_builder_get_widget (gui, "wb_selector_B"),
			       state->wb_sel_B);

	select_default_sheets (state);

#define CONNECT(o,s,c) g_signal_connect(G_OBJECT(o),s,G_CALLBACK(c),state)
	CONNECT (state->cancel_btn, "clicked", cb_cancel_clicked);
	CONNECT (state->compare_btn, "clicked", cb_compare_clicked);
	CONNECT (state->results_view, "cursor-changed", cb_cursor_changed);
#undef CONNECT

	/* a candidate for merging into attach guru */
	wbc_gtk_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	g_object_set_data_full (G_OBJECT (state->dialog),
				"state", state,
				(GDestroyNotify) cb_sheet_compare_destroy);

	gnm_restore_window_geometry (GTK_WINDOW (state->dialog),
				     SHEET_COMPARE_KEY);

	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				GTK_WINDOW (state->dialog));
	gtk_widget_show_all (GTK_WIDGET (state->dialog));
}
