/*
 * sheet-conditions.c: storage mechanism for conditional styles
 *
 * Copyright (C) 2020 Morten Welinder (terra@gnome.org)
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

// The style condition system's dependency setup
//
//
//     GnmStyleCondDep #1 ------------+
//                                    |
//     GnmStyleCondDep #2 ------------+
//                                    +-------- CSGroupDep
//     GnmStyleCondDep #3 ------------+
//                                    |
//     GnmStyleCondDep #4 ------------+
//
// Each GnmStyleCondDep handles one GnmStyleCond with 0-2 condition expressions
// in it.  It has a position (see below) and is hooked into the dependency for
// just that single location.  The action of a GnmStyleCondDep being changed is
// simply to mark its CSGroupDep being changed.  (This is not the main dependency
// tracking mechanism.  However, when dynamic dependents are being created using
// condition evaluations, they will be hooked here.)
//
// The CSGroupDep is handling all ranges for which a certain GnmStyleConditions
// is in use (within a single sheet).  Its position and the position of all the
// GnmStyleCondDep below it is the top left corner of the first range.  It
// carries a single artificial expression consisting of ranges from all the
// expressions in the GnmStyleCondDep extended to account for all ranges.
// Example: style is for range C1:D9, so the position is C1.  Let the single
// expression be A1>0 (relative to C1).  The artificial expression will be
// A1:B9 and is the range that some cell in C1:C9 depends on.
//
// The dependency system will see A1 (irrelevant, for the GnmStyleCondDep) and
// A1:B9 (for the CSGroupDep) which is what really triggers changes.


#include <gnumeric-config.h>
#include <sheet-conditions.h>
#include <style-conditions.h>
#include <sheet.h>
#include <workbook-priv.h>
#include <ranges.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <value.h>
#include <func.h>
#include <mstyle.h>
#include <gutils.h>

// Do house keeping at the end of loading instead of repeatedly during.
// (Meant to be on; setting for debugging only.)
#define FAST_LOAD 1

// Short-circuit most work at exit time.
// (Meant to be on; setting for debugging only.)
#define FAST_EXIT 1

static gboolean debug_sheet_conds;

// ----------------------------------------------------------------------------

static guint csgd_get_dep_type (void);

typedef struct {
	GnmDependent base;
	GnmCellPos pos;
} CSGroupDep;

// A group is a collection of ranges that share conditional styling.  They
// need not share GnmStyle.
typedef struct {
	// Dependent that gets triggered by the dependents managing the
	// expressions inside the conditions.  (Must be first.)
	CSGroupDep dep;

	// The conditional styling
	GnmStyleConditions *conds;

	// The ranges
	GArray *ranges; // element-type: GnmRange
} CSGroup;

struct GnmSheetConditionsData_ {
	GHashTable *groups;
	gboolean needs_simplify;

	GHashTable *linked_conditions;

	gulong sig_being_loaded;
	gpointer sig_being_loaded_object;
};

static void update_group (CSGroup *g);

static void
cb_free_group (CSGroup *g)
{
	g_array_set_size (g->ranges, 0);
	update_group (g);

	g_array_free (g->ranges, TRUE);
	g_free (g);
}

static void
simplify_group (CSGroup *g)
{
	gnm_range_simplify (g->ranges);
	update_group (g);
}

static gboolean
sc_equal (GnmStyleConditions const *sca, GnmStyleConditions const *scb)
{
	return gnm_style_conditions_equal (sca, scb, FALSE);
}

static void
cb_being_loaded (Sheet *sheet)
{
	if (!sheet->workbook->being_loaded)
		sheet_conditions_simplify (sheet);
}

void
sheet_conditions_init (Sheet *sheet)
{
	GnmSheetConditionsData *cd;

	debug_sheet_conds = gnm_debug_flag ("sheet-conditions");

	cd = sheet->conditions = g_new0 (GnmSheetConditionsData, 1);
	cd->groups = g_hash_table_new_full
		(g_direct_hash, g_direct_equal,
		 NULL,
		 (GDestroyNotify)cb_free_group);

	cd->linked_conditions = g_hash_table_new
		((GHashFunc)gnm_style_conditions_hash,
		 (GCompareFunc)sc_equal);

	cd->sig_being_loaded_object = sheet->workbook;
	if (cd->sig_being_loaded_object) {
		cd->sig_being_loaded =
			g_signal_connect_swapped (G_OBJECT (cd->sig_being_loaded_object),
						  "notify::being-loaded",
						  G_CALLBACK (cb_being_loaded),
						  sheet);
		// We can't grab a ref to the workbook as that would introduce a ref loop.
		g_object_add_weak_pointer (cd->sig_being_loaded_object,
					   &cd->sig_being_loaded_object);
	}
}


void
sheet_conditions_uninit (Sheet *sheet)
{
	GnmSheetConditionsData *cd = sheet->conditions;

	if (cd->sig_being_loaded_object) {
		g_signal_handler_disconnect (cd->sig_being_loaded_object, cd->sig_being_loaded);
		g_object_remove_weak_pointer (cd->sig_being_loaded_object,
					      &cd->sig_being_loaded_object);
		cd->sig_being_loaded = 0;
		cd->sig_being_loaded_object = NULL;
	}

	if (g_hash_table_size (cd->groups) > 0)
		g_warning ("Left-over conditional styling.");

	g_hash_table_destroy (cd->groups);
	cd->groups = NULL;

	g_hash_table_destroy (cd->linked_conditions);
	cd->linked_conditions = NULL;

	g_free (cd);
	sheet->conditions = NULL;
}

// ----------------------------------------------------------------------------

/**
 * sheet_conditions_share_conditions_add:
 * @conds: (transfer none):
 *
 * Returns: (transfer none) (nullable): Conditions equivalent to @conds, or
 * %NULL if @conds had not been seen before.
 */
GnmStyleConditions *
sheet_conditions_share_conditions_add (GnmStyleConditions *conds)
{
	Sheet *sheet = gnm_style_conditions_get_sheet (conds);
	GnmSheetConditionsData *cd = sheet->conditions;
	int n = 0;
	GnmStyleConditions *res = NULL;
	gpointer key, value;

	if (g_hash_table_lookup_extended (cd->linked_conditions, conds, &key, &value)) {
		res = conds = key;
		n = GPOINTER_TO_INT (value);
	}

	g_hash_table_insert (cd->linked_conditions,
			     conds,
			     GINT_TO_POINTER (n + 1));
	return res;
}

/**
 * sheet_conditions_share_conditions_remove:
 * @conds: (transfer none):
 *
 * This notifies the sheet conditions manager that one use of the shared
 * conditions has gone away.
 */
void
sheet_conditions_share_conditions_remove (GnmStyleConditions *conds)
{
	Sheet *sheet = gnm_style_conditions_get_sheet (conds);
	GnmSheetConditionsData *cd = sheet->conditions;
	int n = GPOINTER_TO_INT (g_hash_table_lookup (cd->linked_conditions, conds));

	if (n > 1)
		g_hash_table_insert (cd->linked_conditions,
				     conds,
				     GINT_TO_POINTER (n - 1));
	else if (n == 1)
		g_hash_table_remove (cd->linked_conditions, conds);
	else
		g_warning ("We're confused with sheet condition usage (%d).", n);
}

// ----------------------------------------------------------------------------

void
sheet_conditions_simplify (Sheet *sheet)
{
	GHashTableIter hiter;
	gpointer value;
	GnmSheetConditionsData *cd = sheet->conditions;

	if (!cd->needs_simplify)
		return;

	if (debug_sheet_conds)
		g_printerr ("Optimizing sheet conditions for %s\n",
			    sheet->name_unquoted);

	g_hash_table_iter_init (&hiter, cd->groups);
	while (g_hash_table_iter_next (&hiter, NULL, &value)) {
		CSGroup *g = value;
		simplify_group (g);
	}
	cd->needs_simplify = FALSE;
}

void
sheet_conditions_dump (Sheet *sheet)
{
	GnmSheetConditionsData *cd = sheet->conditions;
	GHashTableIter hiter;
	gpointer value;
	int N = 0;

	g_printerr ("Conditional styling for sheet %s:\n", sheet->name_unquoted);
	g_hash_table_iter_init (&hiter, cd->groups);
	while (g_hash_table_iter_next (&hiter, NULL, &value)) {
		CSGroup *g = value;
		unsigned ui, ri;
		GPtrArray const *ga;
		GnmCellPos const *pos;
		char *s;
		GnmParsePos pp;

		if (N > 0)
			g_printerr ("\n");

		pos = gnm_style_conditions_get_pos (g->conds);
		g_printerr ("  Conditions at %s\n",
			    pos ? cellpos_as_string (pos) : "-");
		ga = gnm_style_conditions_details (g->conds);
		for (ui = 0; ui < (ga ? ga->len : 0u); ui++) {
			GnmStyleCond *sc = g_ptr_array_index (ga, ui);
			char *s = gnm_style_cond_as_string (sc);
			g_printerr ("    [%d] %s\n", ui, s);
			g_free (s);
		}

		g_printerr ("  Ranges:\n");
		for (ri = 0; ri < g->ranges->len; ri++) {
			GnmRange *r = &g_array_index (g->ranges, GnmRange, ri);
			g_printerr ("    [%d] %s\n", ri, range_as_string (r));
		}

		g_printerr ("  Dependent expression:\n");
		parse_pos_init_dep (&pp, &g->dep.base);
		s = gnm_expr_top_as_string (g->dep.base.texpr,
					    &pp,
					    sheet_get_conventions (sheet));
		g_printerr ("    %s\n", s);
		g_free (s);

		N++;
	}
}

// ----------------------------------------------------------------------------

static CSGroup *
find_group (GnmSheetConditionsData *cd, GnmStyle *style)
{
	GnmStyleConditions const *conds = gnm_style_get_conditions (style);
	return g_hash_table_lookup (cd->groups, conds);
}


void
sheet_conditions_add (Sheet *sheet, GnmRange const *r, GnmStyle *style)
{
	GnmSheetConditionsData *cd = sheet->conditions;
	CSGroup *g;

	if (FAST_EXIT && sheet->being_destructed)
		return;

	g = find_group (cd, style);
	if (!g) {
		g = g_new0 (CSGroup, 1);
		g->dep.base.flags = csgd_get_dep_type ();
		g->dep.base.sheet = sheet;
		g->conds = gnm_style_get_conditions (style);
		g->ranges = g_array_new (FALSE, FALSE, sizeof (GnmRange));
		g_hash_table_insert (cd->groups, g->conds, g);
	}

	g_array_append_val (g->ranges, *r);
	if (g->ranges->len > 1) {
		if (FAST_LOAD && sheet->workbook->being_loaded)
			cd->needs_simplify = TRUE;
		else
			simplify_group (g);
	} else
		update_group (g);
}

void
sheet_conditions_remove (Sheet *sheet, GnmRange const *r, GnmStyle *style)
{
	GnmSheetConditionsData *cd = sheet->conditions;
	CSGroup *g;
	unsigned ri;

	if (FAST_EXIT && sheet->being_destructed) {
		g_hash_table_remove_all (cd->groups);
		return;
	}

	if (!range_valid (r)) {
		// Inverted range, probably related to style in a tile that
		// extended past the sheet
		return;
	}

	//g_printerr ("Removing style %p from %s\n", style, range_as_string (r));
	g = find_group (cd, style);
	if (!g) {
		g_warning ("Removing conditional style we don't have?");
		return;
	}

	for (ri = 0; ri < g->ranges->len; ri++) {
		GnmRange *r2 = &g_array_index (g->ranges, GnmRange, ri);
		GnmRange rest[4];
		int n = 0;

		if (!range_overlap (r, r2))
			continue;

		if (r->start.col > r2->start.col) {
			// Keep a section to the left
			rest[n] = *r2;
			rest[n].end.col = r->start.col - 1;
			n++;
		}
		if (r->end.col < r2->end.col) {
			// Keep a section to the right
			rest[n] = *r2;
			rest[n].start.col = r->end.col + 1;
			n++;
		}
		if (r->start.row > r2->start.row) {
			// Keep a section above
			rest[n] = *r2;
			rest[n].end.row = r->start.row - 1;
			n++;
			}
		if (r->end.row < r2->end.row) {
			// Keep a section below
			rest[n] = *r2;
			rest[n].start.row = r->end.row + 1;
			n++;
		}

		if (n == 0) {
			g_array_remove_index (g->ranges, ri);
			ri--;  // Counter-act loop increment
			if (g->ranges->len == 0) {
				g_hash_table_remove (cd->groups, g->conds);
				g = NULL;
				break;
			}
		} else {
			*r2 = rest[0];
			g_array_append_vals (g->ranges, rest + 1, n - 1);
		}
	}

	if (FAST_LOAD && sheet->workbook->being_loaded)
		cd->needs_simplify = TRUE;
	else if (g)
		simplify_group (g);
}

// ----------------------------------------------------------------------------

static void
lu1 (GnmDependent *dep, gboolean qlink)
{
	if (dep == NULL || dep->texpr == NULL ||
	    qlink == !!dependent_is_linked (dep))
		return;

	if (qlink)
		dependent_link (dep);
	else
		dependent_unlink (dep);
}

void
sheet_conditions_link_unlink_dependents (Sheet *sheet,
					 GnmRange const *r,
					 gboolean qlink)
{
	GnmSheetConditionsData *cd = sheet->conditions;
	GHashTableIter hiter;
	gpointer value;

	g_hash_table_iter_init (&hiter, cd->groups);
	while (g_hash_table_iter_next (&hiter, NULL, &value)) {
		CSGroup *g = value;
		unsigned ui, ri;
		gboolean overlap = (r == NULL);
		GPtrArray const *ga;

		for (ri = 0; !overlap && ri < g->ranges->len; ri++) {
			GnmRange const *r1 = &g_array_index (g->ranges, GnmRange, ri);
			if (range_overlap (r, r1))
				overlap = TRUE;
		}

		if (!overlap)
			continue;

		lu1 (&g->dep.base, qlink);

		ga = gnm_style_conditions_details (g->conds);
		for (ui = 0; ui < (ga ? ga->len : 0u); ui++) {
			GnmStyleCond *cond = g_ptr_array_index (ga, ui);
			unsigned ix;
			for (ix = 0; ix < G_N_ELEMENTS (cond->deps); ix++)
				lu1 (&cond->deps[ix].base, qlink);
		}
	}
}

// ----------------------------------------------------------------------------

static void
set_group_pos_and_expr (CSGroup *g, const GnmCellPos *pos, GnmExprTop const *texpr)
{
	GnmDependent *dep = &g->dep.base;

	if (dependent_is_linked (dep))
		dependent_unlink (dep);
	if (texpr != dep->texpr)
		dependent_set_expr (dep, texpr);
	g->dep.pos = *pos;
	if (texpr)
		dependent_link (dep);
}

typedef struct {
	GnmEvalPos epos;
	GnmExprList *deps;
	GnmRange const *r;
	Sheet *sheet;
} CollectGroupDepsState;

typedef enum {
	CGD_NO_FLAGS = 0,
	CGD_NON_SCALAR = 1,
} CollectGroupDefsFlags;


static void
collect_group_deps_rr (GnmRangeRef const *rr, CollectGroupDepsState *state,
		       CollectGroupDefsFlags flags)
{
	GnmRangeRef rr1, rr2;
	Sheet *a_sheet = eval_sheet (rr->a.sheet, state->sheet);
	Sheet *b_sheet = eval_sheet (rr->b.sheet, a_sheet);
	GnmRange r;
	int col, row;
	gboolean found = FALSE;
	int W = range_width (state->r);
	int H = range_height (state->r);

	if (a_sheet == state->sheet &&
	    rr->a.col_relative && rr->a.col == 0 &&
	    rr->a.row_relative && rr->a.row == 0 &&
	    b_sheet == state->sheet &&
	    rr->b.col_relative && rr->b.col == 0 &&
	    rr->b.row_relative && rr->b.row == 0) {
		// Ignore references to the cell itself -- the recalc
		// dependency is enough to update everything.
		if (debug_sheet_conds)
			g_printerr ("Self reference\n");
		return;
	}

	// Inspiration from value_intersection:
	gnm_rangeref_normalize (rr, &state->epos, &a_sheet, &b_sheet, &r);

	if (flags & CGD_NON_SCALAR)
		goto everything;

	if (eval_pos_is_array_context (&state->epos))
		goto everything; // Potential implicit iteration -- bail

	if (!(a_sheet == b_sheet || b_sheet == NULL))
		return; // An error

	col = state->epos.eval.col;
	row = state->epos.eval.row;

	if (range_is_singleton (&r)) {
		col = r.start.col;
		row = r.start.row;
		found = TRUE;
	} else if (r.start.row == r.end.row &&
		   r.start.col <= col && col + (W - 1) <= r.end.col) {
		row = r.start.row;
		found = TRUE;
	} else if (r.start.col == r.end.col &&
		   r.start.row <= row && row + (H - 1) <= r.end.row) {
		col = r.start.col;
		found = TRUE;
	}
	if (!found)
		goto everything;

	// Intersection.  The range is equivalent to a single cell
	gnm_cellref_init (&rr1.a, a_sheet, col, row, FALSE);
	rr1.b = rr1.a;
	rr = &rr1;

everything:
	if (!(a_sheet == b_sheet || b_sheet == NULL)) {
		if (debug_sheet_conds)
			g_printerr ("Ignoring 3d reference for conditional style.\n");
		return;
	}

	// Ignore wrapping for now.
	rr2 = *rr;
	if (rr->b.col_relative)
		rr2.b.col += W - 1;
	if (rr->b.row_relative)
		rr2.b.row += H - 1;

	// Don't include sheet if we don't have to.  This makes
	// debugging output easier to read.
	if (a_sheet == state->sheet && b_sheet == state->sheet)
		rr2.a.sheet = rr2.b.sheet = NULL;

	state->deps = gnm_expr_list_prepend
		(state->deps,
		 gnm_expr_new_constant
		 (value_new_cellrange_unsafe (&rr2.a, &rr2.b)));
}

static void
collect_group_deps (GnmExpr const *expr, CollectGroupDepsState *state,
		    CollectGroupDefsFlags flags)
{
	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_CONSTANT: {
		GnmValue const *cst = gnm_expr_get_constant (expr);
		if (VALUE_IS_CELLRANGE (cst))
			collect_group_deps_rr (value_get_rangeref (cst),
					       state, flags);
		return;
	}

	case GNM_EXPR_OP_NAME: {
		GnmNamedExpr const *nexpr = gnm_expr_get_name (expr);
		state->deps = gnm_expr_list_prepend
			(state->deps,
			 gnm_expr_copy (expr));
		if (expr_name_is_active (nexpr))
			collect_group_deps (nexpr->texpr->expr, state, flags);
		return;
	}

	case GNM_EXPR_OP_CELLREF: {
		GnmCellRef const *cr = gnm_expr_get_cellref (expr);
		GnmRangeRef rr;
		rr.a = *cr;
		rr.b = *cr;
		rr.b.sheet = NULL;
		collect_group_deps_rr (&rr, state, flags);
		return;
	}
	}

	// Otherwise... descend into subexpressions
	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
		collect_group_deps (expr->binary.value_a, state, flags);
		collect_group_deps (expr->binary.value_b, state, flags);
		return;

	case GNM_EXPR_OP_ANY_BINARY:
		if (!eval_pos_is_array_context (&state->epos))
			flags &= ~CGD_NON_SCALAR;
		collect_group_deps (expr->binary.value_a, state, flags);
		collect_group_deps (expr->binary.value_b, state, flags);
		return;

	case GNM_EXPR_OP_ANY_UNARY:
		if (!eval_pos_is_array_context (&state->epos))
			flags &= ~CGD_NON_SCALAR;
		collect_group_deps (expr->unary.value, state, flags);
		return;

	case GNM_EXPR_OP_FUNCALL: {
		GnmExprFunction const *call = &expr->func;
		GnmFunc const *func = call->func;
		int i, argc = call->argc;
		CollectGroupDefsFlags pass = flags & CGD_NO_FLAGS;

		if (gnm_func_get_flags (call->func) & GNM_FUNC_IS_PLACEHOLDER)
			break;

		for (i = 0; i < argc; i++) {
			char t = gnm_func_get_arg_type (func, i);
			CollectGroupDefsFlags extra =
				(t == 'A' || t == 'r' || t == '?')
				? CGD_NON_SCALAR
				: CGD_NO_FLAGS;
			collect_group_deps (expr->func.argv[i], state,
					    pass | extra);
		}
		return;
	}

	case GNM_EXPR_OP_SET: {
		int i, argc = expr->set.argc;
		for (i = 0; i < argc; i++)
			collect_group_deps (expr->set.argv[i], state, flags);
		return;
	}

	case GNM_EXPR_OP_ARRAY_CORNER:
		collect_group_deps (expr->array_corner.expr, state,
				    flags | CGD_NON_SCALAR);
		return;

	default:
		return;
	}
}



static void
update_group (CSGroup *g)
{
	GnmCellPos const *pos;
	GnmExprTop const *texpr;
	GPtrArray const *ga;
	CollectGroupDepsState state;
	unsigned ui;

	if (g->ranges->len == 0) {
		dependent_set_expr (&g->dep.base, NULL);
		return;
	}

	// If we are finalizing the sheet, just get out.
	if (g->dep.base.sheet->deps == NULL)
		return;

	pos = &g_array_index (g->ranges, GnmRange, 0).start;
	gnm_style_conditions_set_pos (g->conds, pos);

	state.deps = NULL;
	state.sheet = g->dep.base.sheet;
	ga = gnm_style_conditions_details (g->conds);
	for (ui = 0; ui < (ga ? ga->len : 0u); ui++) {
		GnmStyleCond *cond = g_ptr_array_index (ga, ui);
		unsigned ix;
		for (ix = 0; ix < G_N_ELEMENTS (cond->deps); ix++) {
			GnmExprTop const *te = gnm_style_cond_get_expr (cond, ix);
			unsigned ri;
			if (!te)
				continue;

			eval_pos_init_dep (&state.epos, &cond->deps[ix].base);
			for (ri = 0; ri < g->ranges->len; ri++) {
				state.r = &g_array_index (g->ranges, GnmRange, ri);
				state.epos.eval = state.r->start;
				collect_group_deps (te->expr, &state, CGD_NO_FLAGS);
			}
		}
	}

	if (state.deps == NULL)
		texpr = gnm_expr_top_new_constant (value_new_error_REF (NULL));
	else {
		GnmFunc *f = gnm_func_lookup ("SUM", NULL);
		texpr = gnm_expr_top_new (gnm_expr_new_funcall (f, state.deps));
	}
	set_group_pos_and_expr (g, pos, texpr);
	gnm_expr_top_unref (texpr);
}

// ----------------------------------------------------------------------------

static void
csgd_eval (GnmDependent *dep)
{
	// Nothing yet
}

static GSList *
csgd_changed (GnmDependent *dep)
{
	CSGroupDep *gd = (CSGroupDep *)dep;
	CSGroup *g = (CSGroup *)gd; // Since the dep is first
	Sheet *sheet = dep->sheet;
	unsigned ri;

	if (debug_sheet_conds) {
		g_printerr ("Changed CSGroup/%p\n", (void *)dep);
	}

	for (ri = 0; ri < g->ranges->len; ri++) {
		GnmRange *r = &g_array_index (g->ranges, GnmRange, ri);
		sheet_range_unrender (sheet, r);
		// FIXME:
		// sheet_range_calc_spans ???
		sheet_queue_redraw_range (sheet, r);
	}

	return NULL;
}

static GnmCellPos *
csgd_pos (GnmDependent const *dep)
{
	return &((CSGroupDep *)dep)->pos;
}

static void
csgd_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "CSGroup/%p", (void *)dep);
}


static DEPENDENT_MAKE_TYPE(csgd, .eval = csgd_eval, .changed = csgd_changed, .pos = csgd_pos, .debug_name =  csgd_debug_name)
