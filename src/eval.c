/* vim: set sw=8: */
/*
 * eval.c:  Manage calculation dependencies between objects
 *
 * Copyright (C) 2000,2001
 *  Jody Goldberg   (jody@gnome.org)
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
#include "eval.h"

#include "parse-util.h"
#include "ranges.h"
#include "value.h"
#include "main.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook-private.h"
#include "expr.h"
#include "expr-name.h"
#include "cell.h"
#include "sheet.h"

#define BUCKET_SIZE	128

#define UNLINK_DEP(dep)							\
  do {									\
	if (dep->sheet->deps->dependent_list == dep)			\
		dep->sheet->deps->dependent_list = dep->next_dep;	\
	if (dep->next_dep)						\
		dep->next_dep->prev_dep = dep->prev_dep;		\
	if (dep->prev_dep)						\
		dep->prev_dep->next_dep = dep->next_dep;		\
	/* Note, that ->prev_dep and ->next_dep are still valid.  */	\
  } while (0)

static GPtrArray *dep_classes = NULL;

void
dependent_types_init (void)
{
	g_return_if_fail (dep_classes == NULL);

	/* Init with a pair of NULL classes so we can access directly */
	dep_classes = g_ptr_array_new ();
	g_ptr_array_add	(dep_classes, NULL);
	g_ptr_array_add	(dep_classes, NULL);
}

void
dependent_types_shutdown (void)
{
	g_return_if_fail (dep_classes != NULL);
	g_ptr_array_free (dep_classes, TRUE);
}

/**
 * dependent_register_type :
 * @klass : A vtable
 *
 * Store the vtable and allocate an ID for a new class
 * of dependents.
 */
guint32
dependent_type_register (DependentClass const *klass)
{
	guint32 res;

	g_return_val_if_fail (dep_classes != NULL, 0);

	g_ptr_array_add	(dep_classes, (gpointer)klass);
	res = dep_classes->len-1;

	g_return_val_if_fail (res <= DEPENDENT_TYPE_MASK, res);

	return res;
}


/**
 * dependent_set_expr :
 * @dep : The dependent we are interested in.
 * @new_expr : new expression.
 *
 * When the expression associated with a dependent needs to change
 * this routine dispatches to the virtual handler.
 */
void
dependent_set_expr (Dependent *dep, ExprTree *new_expr)
{
	int const t = dependent_type (dep);

	if (t == DEPENDENT_CELL) {
		/*
		 * Explicitly do not check for array subdivision, we may be
		 * replacing the corner of an array.
		 */
		cell_set_expr_unsafe (DEP_TO_CELL (dep), new_expr, NULL);
	} else {
		DependentClass *klass = g_ptr_array_index (dep_classes, t);

		g_return_if_fail (klass);
		if (klass->set_expr != NULL)
			(*klass->set_expr) (dep, new_expr);
#if 0
		{
			ParsePos pos;
			char *str;

			parse_pos_init_dep (&pos, dep);
			dependent_debug_name (dep, stdout);

			str = expr_tree_as_string (new_expr, &pos);
			printf(" new = %s\n", str);
			g_free (str);

			str = expr_tree_as_string (dep->expression, &pos);
			printf("\told = %s\n", str);
			g_free (str);
		}
#endif

		if (new_expr != NULL)
			expr_tree_ref (new_expr);
		if (dependent_is_linked (dep))
			dependent_unlink (dep, NULL);
		if (dep->expression != NULL)
			expr_tree_unref (dep->expression);

		dep->expression = new_expr;
		if (new_expr != NULL)
			dependent_changed (dep, TRUE);
	}
}

/**
 * dependent_set_sheet
 * @dep :
 * @sheet :
 */
void
dependent_set_sheet (Dependent *dep, Sheet *sheet)
{
	g_return_if_fail (dep != NULL);
	g_return_if_fail (dep->sheet == NULL);
	g_return_if_fail (!dependent_is_linked (dep));

	dep->sheet = sheet;
	if (dep->expression != NULL)
		dependent_changed (dep, TRUE);
}

/*
 * dependent_queue_recalc:
 * @dep: the dependent that contains the expression needing recomputation.
 *
 * Marks @dep as needing recalculation
 * NOTE : it does NOT recursively dirty dependencies.
 */
#define dependent_queue_recalc(dep) \
  do { (dep)->flags |= DEPENDENT_NEEDS_RECALC; } while (0)

static void
cb_cell_list_deps (Dependent *dep, gpointer user)
{
	GSList **list = (GSList **)user;
	*list = g_slist_prepend (*list, dep);
}

static GSList *
cell_list_deps (const Cell *cell)
{
	GSList *deps = NULL;
	cell_foreach_dep (cell, cb_cell_list_deps, &deps);
	return deps;
}


/**
 * dependent_queue_recalc_list :
 * @list :
 *
 * Queues any elements of @list for recalc that are not already queued,
 * and marks all elements as needing a recalc.
 */
static void
dependent_queue_recalc_list (GSList *list)
{
	GSList *work = NULL;

	for (; list != NULL ; list = list->next) {
		Dependent *dep = list->data;
		if (!(dep->flags & DEPENDENT_NEEDS_RECALC)) {
			dependent_queue_recalc (dep);
			work = g_slist_prepend (work, dep);
		}
	}

	/*
	 * Work is now a list of marked cells whose dependencies need
	 * to be marked.  Marking early guarentees that we will not
	 * get duplicates.  (And it thus limits the length of the list.)
	 * We treat work as a stack.
	 */

	while (work) {
		Dependent *dep = work->data;

		/* Pop the top element.  */
		list = work;
		work = work->next;
		g_slist_free_1 (list);

		if (dependent_is_cell (dep)) {
			GSList *deps = cell_list_deps (DEP_TO_CELL (dep));
			GSList *waste = NULL;
			GSList *next;
			for (list = deps; list != NULL ; list = next) {
				Dependent *dep = list->data;
				next = list->next;
				if (dep->flags & DEPENDENT_NEEDS_RECALC) {
					list->next = waste;
					waste = list;
				} else {
					dependent_queue_recalc (dep);
					list->next = work;
					work = list;
				}
			}
			g_slist_free (waste);
		}
	}
}


void
cb_dependent_queue_recalc (Dependent *dep, gpointer ignore)
{
	g_return_if_fail (dep != NULL);

	if (!(dep->flags & DEPENDENT_NEEDS_RECALC)) {
		GSList listrec;
		listrec.next = NULL;
		listrec.data = dep;
		dependent_queue_recalc_list (&listrec);
	}
}


/**************************************************************************/
#undef ENABLE_MICRO_HASH
#ifdef ENABLE_MICRO_HASH
typedef struct {
	gint     num_buckets;
	gint     num_elements;
	union {
		GSList **buckets;
		GSList *singleton;
	} u;
} MicroHash;

#define MICRO_HASH_MIN_SIZE 11
#define MICRO_HASH_MAX_SIZE 13845163
#define MICRO_HASH_RESIZE(hash_table)				\
G_STMT_START {							\
	if ((hash_table->num_buckets > MICRO_HASH_MIN_SIZE &&		\
	     hash_table->num_buckets >= 3 * hash_table->num_elements) || 	\
	    (hash_table->num_buckets < MICRO_HASH_MAX_SIZE &&		\
	     3 * hash_table->num_buckets <= hash_table->num_elements))	\
		micro_hash_resize (hash_table);			\
} G_STMT_END

/* The records are aligned so the bottom few bits don't hold much
 * entropy
 */
#define MICRO_HASH_hash(key)	((guint)((int) (key) >> 9))

static void
micro_hash_resize (MicroHash *hash_table)
{
	GSList **new_buckets, *node, *next;
	guint bucket;
	gint old_num_buckets = hash_table->num_buckets;
	gint new_num_buckets = CLAMP (g_spaced_primes_closest (hash_table->num_elements),
		MICRO_HASH_MIN_SIZE, MICRO_HASH_MAX_SIZE);

	if (old_num_buckets <= 1) {
		if (new_num_buckets == 1)
			return;
		new_buckets = g_new0 (GSList *, new_num_buckets);
		for (node = hash_table->u.singleton; node; node = next) {
			next = node->next;
			bucket =  MICRO_HASH_hash (node->data) % new_num_buckets;
			node->next = new_buckets [bucket];
			new_buckets [bucket] = node;
		}
		hash_table->u.buckets = new_buckets;
	} else if (new_num_buckets > 1) {
		new_buckets = g_new0 (GSList *, new_num_buckets);
		for (old_num_buckets = hash_table->num_buckets; old_num_buckets-- > 0 ; )
			for (node = hash_table->u.buckets [old_num_buckets]; node; node = next) {
				next = node->next;
				bucket =  MICRO_HASH_hash (node->data) % new_num_buckets;
				node->next = new_buckets [bucket];
				new_buckets [bucket] = node;
			}
		g_free (hash_table->u.buckets);
		hash_table->u.buckets = new_buckets;
	} else {
		GSList *singleton = NULL;
		while (old_num_buckets-- > 0)
			singleton = g_slist_concat (singleton, hash_table->u.buckets [old_num_buckets]);
		g_free (hash_table->u.buckets);
		hash_table->u.singleton = singleton;
	}

	hash_table->num_buckets = new_num_buckets;
}

static void
micro_hash_insert (MicroHash *hash_table, gpointer key)
{
	GSList **head;
	int const hash_size = hash_table->num_buckets;

	if (hash_size > 1) {
		guint const bucket = MICRO_HASH_hash (key) % hash_size;
		head = hash_table->u.buckets + bucket;
	} else
		head = & (hash_table->u.singleton);

	if (g_slist_find (*head, key) == NULL) {
		*head = g_slist_prepend (*head, key);
		hash_table->num_elements++;
		MICRO_HASH_RESIZE (hash_table);
	}
}

static void
micro_hash_remove (MicroHash *hash_table, gpointer key)
{
	GSList **head;
	int const hash_size = hash_table->num_buckets;

	if (hash_size > 1) {
		guint const bucket = MICRO_HASH_hash (key) % hash_size;
		head = hash_table->u.buckets + bucket;
	} else
		head = & (hash_table->u.singleton);

	for (; *head != NULL ; head = &((*head)->next))
		if ((*head)->data == key) {
			*head = (*head)->next;
			hash_table->num_elements--;
			MICRO_HASH_RESIZE (hash_table);
			return;
		}
}

static void
micro_hash_release (MicroHash *hash_table)
{
	guint i = hash_table->num_buckets;

	if (i > 1) {
		while (i-- > 0)
			g_slist_free (hash_table->u.buckets[i]);
		g_free (hash_table->u.buckets);
		hash_table->u.buckets = NULL;
	} else {
		g_slist_free (hash_table->u.singleton);
		hash_table->u.singleton = NULL;
	}
	hash_table->num_elements = 0;
	hash_table->num_buckets = 1;
}

static void
micro_hash_init (MicroHash *hash_table, gpointer key)
{
	hash_table->num_elements = 0;
	hash_table->num_buckets = 1;
	hash_table->u.singleton = g_slist_prepend (NULL, key);
}

/*************************************************************************/

typedef MicroHash	DepCollection;
#define dep_collection_init(dc, dep)	\
	micro_hash_init (&(dc), dep)
#define dep_collection_insert(dc, dep)	\
	micro_hash_insert (&(dc), dep)
#define dep_collection_remove(dc, dep)	\
	micro_hash_remove (&(dc), dep)
#define dep_collection_is_empty(dc)				\
	(dc.num_elements == 0)
#define dep_collection_foreach_dep(dc, dep, code) do {		\
	GSList *l;						\
	int i = dc.num_buckets;					\
	if (i <= 1) { 						\
		for (l = dc.u.singleton; l ; l = l->next) {	\
			Dependent *dep = l->data;		\
			code					\
		}						\
	} else while (i-- > 0) {				\
		for (l = dc.u.buckets [i]; l ; l = l->next) {	\
			Dependent *dep = l->data;		\
			code					\
		}						\
	}							\
} while (0)
#define dep_collection_foreach_list(dc, list, code) do {	\
	GSList *list;						\
	int i = dc.num_buckets;					\
	if (i <= 1) { 						\
		list = dc.u.singleton;				\
		code						\
	} else while (i-- > 0) {				\
		list = dc.u.buckets[i];				\
		code						\
	}							\
} while (0)
#else
typedef GSList *	DepCollection;
#define dep_collection_init(dc, dep)	\
	dc = g_slist_prepend (NULL, dep)
#define dep_collection_insert(dc, dep) \
	if (!g_slist_find (dc, dep)) dc = g_slist_prepend (dc, dep)
#define dep_collection_remove(dc, dep)	\
	dc = g_slist_remove (dc, dep);
#define dep_collection_is_empty(dc)	\
	(dc == NULL)
#define dep_collection_foreach_dep(dc, dep, code) do {		\
	GSList *l;						\
	for (l = dc; l != NULL ; l = l->next) {			\
		Dependent *dep = l->data;			\
		code						\
	}							\
} while (0)
#define dep_collection_foreach_list(dc, list, code) do {	\
	GSList *list = dc;					\
	code							\
} while (0)
#endif

/**************************************************************************
 * Data structures for managing dependencies between objects.
 *
 * The DependencyRange hash needs to be improved.  It is a huge
 * performance hit when there are large numbers of range depends.
 */

/*
 * A DependencyRange defines a range of cells whose values
 * are used by another objects in the spreadsheet.
 *
 * A change in those cells will trigger a recomputation on the
 * cells listed in deps.
 */
typedef struct {
	DepCollection	deps;	/* Must be first */
	Range  range;
} DependencyRange;

/*
 *  A DependencySingle stores a list of dependents that rely
 * on the cell at @pos.
 * 
 * A change in this cell will trigger a recomputation on the
 * cells listed in deps.
 */
typedef struct {
	DepCollection	deps;	/* Must be first */
	CellPos pos;
} DependencySingle;

/* A utility type */
typedef struct {
	DepCollection	deps;	/* Must be first */
} DependencyAny;

typedef enum {
	REMOVE_DEPS = 0,
	ADD_DEPS = 1
} DepOperation;

static guint
deprange_hash_func (gconstpointer v)
{
	DependencyRange const *r = v;

	return ((((r->range.start.row << 8) + r->range.end.row) << 8) +
		(r->range.start.col << 8) + (r->range.end.col));
}
static gint
deprange_equal_func (gconstpointer v, gconstpointer v2)
{
	DependencyRange const *r1 = (DependencyRange const *) v;
	DependencyRange const *r2 = (DependencyRange const *) v2;

	if (r1->range.start.col != r2->range.start.col)
		return 0;
	if (r1->range.start.row != r2->range.start.row)
		return 0;
	if (r1->range.end.col != r2->range.end.col)
		return 0;
	if (r1->range.end.row != r2->range.end.row)
		return 0;

	return 1;
}


static guint
depsingle_hash (gconstpointer key)
{
	DependencySingle const *d = (DependencySingle const *) key;

	return (d->pos.row << 8) ^ d->pos.col;
}

static gint
depsingle_equal (gconstpointer ai, gconstpointer bi)
{
	DependencySingle const *a = (DependencySingle const *)ai;
	DependencySingle const *b = (DependencySingle const *)bi;

	return (a->pos.row == b->pos.row &&
		a->pos.col == b->pos.col);
}

static void
handle_cell_single_dep (Dependent *dep, CellPos const *pos,
			CellRef const *a, DepOperation operation)
{
	DependencyContainer *deps;
	DependencySingle *single;
	DependencySingle lookup;

	if (a->sheet == NULL)
		deps = dep->sheet->deps;
	else
		deps = a->sheet->deps;

	if (!deps)
		return;

	/* Convert to absolute cordinates */
	cellref_get_abs_pos (a, pos, &lookup.pos);

	single = g_hash_table_lookup (deps->single_hash, &lookup);

	if (operation == ADD_DEPS) {
		if (single) {
			/* Inserts if it is not already there */
			dep_collection_insert (single->deps, dep);
		} else {
			single  = g_new (DependencySingle, 1);
			*single = lookup;
			dep_collection_init (single->deps, dep);
			g_hash_table_insert (deps->single_hash, single, single);
		}
	} else { /* Remove */
		if (single) {
			dep_collection_remove (single->deps, dep);
			if (dep_collection_is_empty (single->deps)) {
				g_hash_table_remove (deps->single_hash, single);
				g_free (single);
			}
		} else
			;/* Referenced twice and list killed already */
	}
}

static void
add_range_dep (DependencyContainer *deps, Dependent *dep,
	       DependencyRange const *r)
{
	int i = r->range.start.row / BUCKET_SIZE;
	int const end = r->range.end.row / BUCKET_SIZE;

	for ( ; i <= end; i++) {
		/* Look it up */
		DependencyRange *result;
		
		if (deps->range_hash [i] == NULL) {
			deps->range_hash [i] =
				g_hash_table_new (deprange_hash_func,
						  deprange_equal_func);
			result = NULL;
		} else {
			result = g_hash_table_lookup (deps->range_hash[i], r);

			if (result) {
				/* Inserts if it is not already there */
				dep_collection_insert (result->deps, dep);
				continue;
			}
		}

		/* Create a new DependencyRange structure */
		result = g_new (DependencyRange, 1);
		*result = *r;
		dep_collection_init (result->deps, dep);
		g_hash_table_insert (deps->range_hash[i], result, result);
	}
}

static void
drop_range_dep (DependencyContainer *deps, Dependent *dep,
		DependencyRange const *r)
{
	int i = r->range.start.row / BUCKET_SIZE;
	int const end = r->range.end.row / BUCKET_SIZE;
	DependencyRange *result;

	if (!deps)
		return;

	for ( ; i <= end; i++) {
		result = g_hash_table_lookup (deps->range_hash[i], r);
		if (result) {
			dep_collection_remove (result->deps, dep);
			if (dep_collection_is_empty (result->deps)) {
				g_hash_table_remove (deps->range_hash[i], result);
				g_free (result);
			}
		}
	}
}

/*
 * Add the dependency of Dependent dep on the range
 * enclose by CellRef a and CellRef b
 *
 * We compute the location from @pos
 */
static void
handle_cell_range_deps (Dependent *dep, CellPos const *pos,
			CellRef const *a, CellRef const *b, DepOperation operation)
{
	DependencyRange range;
	DependencyContainer *depsa, *depsb;

	cellref_get_abs_pos (a, pos, &range.range.start);
	cellref_get_abs_pos (b, pos, &range.range.end);
	range_normalize (&range.range);

	depsa = eval_sheet (a->sheet, dep->sheet)->deps;
	if (operation)
		add_range_dep  (depsa, dep, &range);
	else
		drop_range_dep (depsa, dep, &range);

	depsb = eval_sheet (b->sheet, dep->sheet)->deps;
	if (depsa != depsb) {
		/* FIXME: we need to iterate sheets between to be correct */
		if (operation)
			add_range_dep  (depsb, dep, &range);
		else
			drop_range_dep (depsb, dep, &range);
	}
}

/*
 * Adds the dependencies for a Value
 */
static void
handle_value_deps (Dependent *dep, CellPos const *pos,
		   Value const *value, DepOperation operation)
{
	switch (value->type) {
	case VALUE_EMPTY:
	case VALUE_STRING:
	case VALUE_INTEGER:
	case VALUE_FLOAT:
	case VALUE_BOOLEAN:
	case VALUE_ERROR:
		/* Constants have no dependencies */
		break;

		/* Check every element of the array */
	case VALUE_ARRAY:
	{
		int x, y;

		for (x = 0; x < value->v_array.x; x++)
			for (y = 0; y < value->v_array.y; y++)
				handle_value_deps (dep, pos,
						   value->v_array.vals [x] [y],
						   operation);
		break;
	}
	case VALUE_CELLRANGE:
		handle_cell_range_deps (dep, pos,
			&value->v_range.cell.a,
			&value->v_range.cell.b,
			operation);
		break;
	default:
		g_warning ("Unknown Value type, dependencies lost");
		break;
	}
}

/*
 * This routine walks the expression tree looking for cell references
 * and cell range references.
 */
static void
handle_tree_deps (Dependent *dep, CellPos const *pos,
		  ExprTree *tree, DepOperation operation)
{
	switch (tree->any.oper) {
	case OPER_ANY_BINARY:
		handle_tree_deps (dep, pos, tree->binary.value_a, operation);
		handle_tree_deps (dep, pos, tree->binary.value_b, operation);
		return;

	case OPER_ANY_UNARY:
		handle_tree_deps (dep, pos, tree->unary.value, operation);
		return;

	case OPER_VAR:
		handle_cell_single_dep (dep, pos, &tree->var.ref, operation);
		return;

	case OPER_CONSTANT:
		handle_value_deps (dep, pos, tree->constant.value, operation);
		return;

	/*
	 * FIXME: needs to be taught implicit intersection +
	 * more cunning handling of argument type matching.
	 */
	case OPER_FUNCALL: {
		ExprList *l;

		for (l = tree->func.arg_list; l; l = l->next)
			handle_tree_deps (dep, pos, l->data, operation);
		return;
	}

	case OPER_NAME:
		if (operation == ADD_DEPS)
			expr_name_add_dep (tree->name.name, dep);
		else
			expr_name_remove_dep (tree->name.name, dep);
		if (!tree->name.name->builtin && tree->name.name->active)
			handle_tree_deps (dep, pos, tree->name.name->t.expr_tree, operation);
		return;

	case OPER_ARRAY:
		if (tree->array.x != 0 || tree->array.y != 0) {
			/* Non-corner cells depend on the corner */
			CellRef a;

			/* We cannot support array expressions unless
			 * we have a position.
			 */
			g_return_if_fail (pos != NULL);

			a.col_relative = a.row_relative = FALSE;
			a.sheet = dep->sheet;
			a.col   = pos->col - tree->array.x;
			a.row   = pos->row - tree->array.y;

			handle_cell_single_dep (dep, pos, &a, operation);
		} else
			/* Corner cell depends on the contents of the expr */
			handle_tree_deps (dep, pos, tree->array.corner.expr, operation);
		return;

	case OPER_SET: {
		ExprList *l;

		for (l = tree->set.set; l; l = l->next)
			handle_tree_deps (dep, pos, l->data, operation);
		return;
	}

	default:
		g_warning ("Unknown Operation type, dependencies lost");
		break;
	}
}

/**
 * dependent_link:
 * @dep : the dependent that changed
 * @pos: The optionally NULL position of the dependent.
 *
 * Adds the dependent to the workbook wide list of dependents.
 */
void
dependent_link (Dependent *dep, CellPos const *pos)
{
	Sheet *sheet;

	g_return_if_fail (dep != NULL);
	g_return_if_fail (dep->expression != NULL);
	g_return_if_fail (!(dep->flags & DEPENDENT_IN_EXPR_LIST));
	g_return_if_fail (IS_SHEET (dep->sheet));
	g_return_if_fail (dep->sheet->deps != NULL);

	sheet = dep->sheet;

	/* Make this the new head of the dependent list.  */
	dep->prev_dep = NULL;
	dep->next_dep = sheet->deps->dependent_list;
	if (dep->next_dep)
		dep->next_dep->prev_dep = dep;
	sheet->deps->dependent_list = dep;
	dep->flags |= DEPENDENT_IN_EXPR_LIST;

	handle_tree_deps (dep, pos, dep->expression, ADD_DEPS);
}

/**
 * dependent_unlink:
 * @dep : the dependent that changed
 * @pos: The optionally NULL position of the dependent.
 *
 * Removes the dependent from the workbook wide list of dependents.
 */
void
dependent_unlink (Dependent *dep, CellPos const *pos)
{
	static CellPos const dummy = { 0, 0 };
	g_return_if_fail (dep != NULL);

	if (pos == NULL)
		pos = &dummy;

	if (dep->sheet != NULL) {
		g_return_if_fail (dep->expression != NULL);
		g_return_if_fail (dep->flags & DEPENDENT_IN_EXPR_LIST);
		g_return_if_fail (IS_SHEET (dep->sheet));

		/* see note in do_deps_destroy */
		/* NOTE That notion is wrong it is only viable for removing
		 * dependents within the sheet or book being destroyed.
		 * If we are deleting a sheet with references outside that sheet
		 * we need to drop those dependencies.  A good idea would be to
		 * flag dependents with links outside the current sheet and
		 * book (both) and use that to save time later.
		 */

		if (dep->sheet->deps != NULL) {
			handle_tree_deps (dep, pos, dep->expression, REMOVE_DEPS);
			UNLINK_DEP (dep);
/* FIXME FIXME FIXME : Massive hack !  rework dependent linkage cycle after release */
		} else if (!dependent_is_cell (dep))
			handle_tree_deps (dep, pos, dep->expression, REMOVE_DEPS);

		dep->flags &= ~(DEPENDENT_IN_EXPR_LIST | DEPENDENT_NEEDS_RECALC);
	}
}

/**
 * dependent_unlink_sheet :
 * @sheet :
 *
 * An internal routine to remove all expressions associated with a given sheet
 * from the workbook wide expression list.  WARNING : This is a dangerous
 * internal function.  it leaves the dependents in an invalid state.  It is
 * intended for use by sheet_destroy_contents.
 */
void
dependent_unlink_sheet (Sheet *sheet)
{
	g_return_if_fail (IS_SHEET (sheet));

	SHEET_FOREACH_DEPENDENT (sheet, dep, {
		 dep->flags &= ~DEPENDENT_IN_EXPR_LIST;
		 UNLINK_DEP (dep);
	 });
}

/**
 * dependent_changed:
 * @cell : the dependent that changed
 * @queue_recalc: also queue a recalc for the dependent.
 *
 * Registers the expression with the sheet and optionally queues a recalc
 * of the dependent.
 */
void
dependent_changed (Dependent *dep, gboolean queue_recalc)
{
	static CellPos const pos = { 0, 0 };

	g_return_if_fail (dep != NULL);

	/* A pos should not be necessary, but supply one just in case.  If a
	 * user specifies a relative reference this is probably what they want.
	 */
	dependent_link (dep, &pos);

	if (queue_recalc) {
		if (dep->sheet->workbook->priv->recursive_dirty_enabled)
			cb_dependent_queue_recalc (dep,  NULL);
		else
			dependent_queue_recalc (dep);
	}
}

/**
 * cell_add_dependencies:
 * @cell:
 *
 * This registers the dependencies for this cell
 * by scanning all of the references made in the
 * parsed expression.
 **/
void
cell_add_dependencies (Cell *cell)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->base.sheet != NULL);
	g_return_if_fail (cell->base.sheet->deps != NULL);

	if (cell_has_expr (cell))
		handle_tree_deps (CELL_TO_DEP (cell), &cell->pos,
				  cell->base.expression, ADD_DEPS);
}

/**
 * cell_drop_dependencies:
 * @cell:
 *
 *
 **/
void
cell_drop_dependencies (Cell *cell)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->base.sheet != NULL);

	if (cell_has_expr (cell))
		handle_tree_deps (CELL_TO_DEP (cell), &cell->pos,
				  cell->base.expression, REMOVE_DEPS);
}

void
cell_eval (Cell *cell)
{
	g_return_if_fail (cell != NULL);

	if (cell_needs_recalc (cell)) {
		gboolean finished = cell_eval_content (cell);

		/* This should always be the top of the stack */
		g_return_if_fail (finished);

		cell->base.flags &= ~DEPENDENT_NEEDS_RECALC;
	}
}

/**
 * cell_queue_recalc :
 * @cell :
 *
 * Queue the cell and everything that depends on it for recalculation.
 * If a dependency is already queued ignore it.
 */
void
cell_queue_recalc (Cell const *cell)
{
	g_return_if_fail (cell != NULL);

	if (!cell_needs_recalc (cell)) {
		GSList *deps;

		if (cell_has_expr (cell))
			dependent_queue_recalc (CELL_TO_DEP (cell));

		deps = cell_list_deps (cell);
		dependent_queue_recalc_list (deps);
		g_slist_free (deps);
	}
}

typedef struct {
	int      col, row;
	DepFunc	 func;
	gpointer user;
} search_rangedeps_closure_t;

static void
cb_search_rangedeps (gpointer key, gpointer value, gpointer closure)
{
	search_rangedeps_closure_t const *c = closure;
	DependencyRange const *deprange = key;
	Range const *range = &(deprange->range);

#if 0
	/* When things get slow this is a good test to enable */
	static int counter = 0;
	if ((++counter % 100000) == 0)
	    printf ("%d\n", counter / 100000);
#endif

	/* No intersection is the common case */
	if (range_contains (range, c->col, c->row)) {
		DepFunc	 func = c->func;
		dep_collection_foreach_dep (deprange->deps, dep, 
			(func) (dep, c->user););
	}
}

static void
cell_foreach_range_dep (Cell const *cell, DepFunc func, gpointer user)
{
	search_rangedeps_closure_t closure;
	GHashTable *bucket =
		cell->base.sheet->deps->range_hash [cell->pos.row /BUCKET_SIZE];

	if (bucket != NULL) {
		closure.col   = cell->pos.col;
		closure.row   = cell->pos.row;
		closure.func  = func;
		closure.user  = user;

		/* FIXME FIXME FIXME :
		 * This call decimates performance
		 * If this list contains lots of ranges we are toast.  Consider
		 * subdividing the master list.  A simple fixed bucket scheme is
		 * probably sufficient (say 64x64) but we could go to something
		 * adaptive or a simple quad tree.
		 */
		g_hash_table_foreach (bucket,
			&cb_search_rangedeps, &closure);
	}
}

static void
cell_foreach_single_dep (Sheet const *sheet, int col, int row,
			 DepFunc func, gpointer user)
{
	DependencySingle lookup, *single;
	DependencyContainer *deps = sheet->deps;

	lookup.pos.col = col;
	lookup.pos.row = row;

	single = g_hash_table_lookup (deps->single_hash, &lookup);
	if (single != NULL)
		dep_collection_foreach_dep (single->deps, dep,
			(*func) (dep, user););
}

void
cell_foreach_dep (Cell const *cell, DepFunc func, gpointer user)
{
	g_return_if_fail (cell != NULL);

	/* accelerate exit */
	if (!cell->base.sheet->deps)
		return;

	cell_foreach_range_dep (cell, func, user);
	cell_foreach_single_dep (cell->base.sheet, cell->pos.col, cell->pos.row,
				 func, user);
}

static void
cb_recalc_all_depends (gpointer key, gpointer value, gpointer ignore)
{
	DependencyAny const *depany = key;
	dep_collection_foreach_list (depany->deps, list,
		dependent_queue_recalc_list (list););
}

static void
cb_range_contained_depend (gpointer key, gpointer value, gpointer user)
{
	DependencyRange const *deprange  = key;
	Range const *range = &deprange->range;
	Range const *target = user;

	if (range_overlap (target, range))
		dep_collection_foreach_list (deprange->deps, list,
			dependent_queue_recalc_list (list););
}

static void
cb_single_contained_depend (gpointer key, gpointer value, gpointer user)
{
	DependencySingle const *depsingle  = key;
	Range const *target = user;

	if (range_contains (target, depsingle->pos.col, depsingle->pos.row))
		dep_collection_foreach_list (depsingle->deps, list,
			dependent_queue_recalc_list (list););
}

/**
 * sheet_region_queue_recalc :
 *
 * @sheet : The sheet.
 * @range : Optionally NULL range.
 *
 * Queues things that depend on @sheet!@range for recalc.
 *
 * If @range is NULL the entire sheet is used.
 */
void
sheet_region_queue_recalc (Sheet const *sheet, Range const *r)
{
	int i;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->deps != NULL);

	if (r == NULL) {
		/* mark the contained depends dirty non recursively */
		SHEET_FOREACH_DEPENDENT (sheet, dep, {
			dependent_queue_recalc (dep);
		});

		/* look for things that depend on the sheet */
		for (i = (SHEET_MAX_ROWS-1)/BUCKET_SIZE; i >= 0 ; i--) {
			GHashTable *hash = sheet->deps->range_hash[i];
			if (hash != NULL)
				g_hash_table_foreach (hash,
					&cb_recalc_all_depends, NULL);
		}
		g_hash_table_foreach (sheet->deps->single_hash,
			&cb_recalc_all_depends, NULL);
	} else {
		int const first = r->start.row / BUCKET_SIZE;

		/* mark the contained depends dirty non recursively */
		SHEET_FOREACH_DEPENDENT (sheet, dep, {
			Cell *cell = DEP_TO_CELL (dep);
			if (dependent_is_cell (dep) &&
			    range_contains (r, cell->pos.col, cell->pos.row))
				dependent_queue_recalc (dep);
		});

		/* look for things that depend on target region */
		for (i = (r->end.row)/BUCKET_SIZE; i >= first ; i--) {
			GHashTable *hash = sheet->deps->range_hash[i];
			if (hash != NULL)
			g_hash_table_foreach (hash,
				&cb_range_contained_depend, (gpointer)r);
		}
		g_hash_table_foreach (sheet->deps->single_hash,
			&cb_single_contained_depend, (gpointer)r);
	}
}

/*******************************************************************/

inline static void
invalidate_refs (Dependent *dep, ExprRewriteInfo const *rwinfo)
{
	ExprTree *newtree = expr_rewrite (dep->expression, rwinfo);

	/* We are told this dependent depends on this region, hence if newtree
	 * is null then either 
	 * 1) we did not depend on it ( ie. serious breakage )
	 * 2j we had a duplicate reference and we have already removed it.
	 * 3) We depended on things via a name which will be invalidated elsewhere
	 */
	if (newtree != NULL)
		dependent_set_expr (dep, newtree);
}

/*
 * WARNING : Hash is pointing to freed memory once this is complete
 * This is tightly coupled with do_deps_destroy.
 */
static void
cb_dep_hash_invalidate (gpointer key, gpointer value, gpointer closure)
{
	ExprRewriteInfo const *rwinfo = closure;
	DependencyAny *depany = value;
#ifndef ENABLE_MICRO_HASH
	GSList *deps = depany->deps;
	GSList *ptr = deps;
	Dependent *dependent;

	depany->deps = NULL;	/* poison it */
	if (rwinfo->type == EXPR_REWRITE_SHEET) {
		Sheet const *target = rwinfo->u.sheet;
		for (; ptr != NULL; ptr = ptr->next) {
			dependent = ptr->data;
			if (dependent->sheet != target)
				invalidate_refs (dependent, rwinfo);
		}
	} else if (rwinfo->type == EXPR_REWRITE_WORKBOOK) {
		Workbook const *target = rwinfo->u.workbook;
		for (; ptr != NULL; ptr = ptr->next) {
			dependent = ptr->data;
			if (dependent->sheet->workbook != target)
				invalidate_refs (dependent, rwinfo);
		}
	} else {
		g_assert_not_reached ();
	}

	g_slist_free (deps);
#else
	if (rwinfo->type == EXPR_REWRITE_SHEET) {
		Sheet const *target = rwinfo->u.sheet;
		dep_collection_foreach_dep (depany->deps, dep,
			if (dep->sheet != target)
				invalidate_refs (dep, rwinfo););
	} else if (rwinfo->type == EXPR_REWRITE_WORKBOOK) {
		Workbook const *target = rwinfo->u.workbook;
		dep_collection_foreach_dep(depany->deps, dep,
			if (dep->sheet->workbook != target)
				invalidate_refs (dep, rwinfo););
	} else {
		g_assert_not_reached ();
	}

	micro_hash_release (&depany->deps);
#endif
	g_free (depany);
}

static void
cb_name_invalidate_sheet (gpointer key, gpointer value, gpointer rwinfo)
{
	NamedExpression *nexpr = key;
	ExprTree *new_expr = expr_rewrite (nexpr->t.expr_tree, rwinfo);
	g_return_if_fail (new_expr != NULL);
	expr_name_set_expr (nexpr, new_expr);
}

/*
 * do_deps_destroy :
 * Invalidate references of all kinds to the target region described by
 * @rwinfo.
 */
static void
do_deps_destroy (Sheet *sheet, ExprRewriteInfo const *rwinfo)
{
	DependencyContainer *deps;

	g_return_if_fail (IS_SHEET (sheet));

	deps = sheet->deps;
	if (deps == NULL)
		return;

	/* We are destroying all the dependencies, there is no need to
	 * delicately remove individual items from the lists.  The only purpose
	 * that serves is to validate the state of our data structures.  If
	 * required this optimization can be disabled for debugging.
	 */
	sheet->deps = NULL;

	if (deps->range_hash) {
		int i;
		for (i = (SHEET_MAX_ROWS-1)/BUCKET_SIZE; i >= 0 ; i--) {
			GHashTable *hash = deps->range_hash[i];
			if (hash != NULL) {
				g_hash_table_foreach (hash,
					&cb_dep_hash_invalidate, (gpointer)rwinfo);
				g_hash_table_destroy (hash);
			}
		}
		g_free (deps->range_hash);
		deps->range_hash = NULL;
	}

	if (deps->single_hash) {
		g_hash_table_foreach (deps->single_hash,
			&cb_dep_hash_invalidate, (gpointer)rwinfo);
		g_hash_table_destroy (deps->single_hash);
		deps->single_hash = NULL;
	}

	if (deps->names) {
		g_hash_table_foreach (deps->names, 
			cb_name_invalidate_sheet, (gpointer)rwinfo);
		g_hash_table_destroy (deps->names);
		deps->names = NULL;
	}

	g_free (deps);
}

void
sheet_deps_destroy (Sheet *sheet)
{
	ExprRewriteInfo rwinfo;

	g_return_if_fail (sheet != NULL);

	rwinfo.type = EXPR_REWRITE_SHEET;
	rwinfo.u.sheet = sheet;

	do_deps_destroy (sheet, &rwinfo);
}

void
workbook_deps_destroy (Workbook *wb)
{
	ExprRewriteInfo rwinfo;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (wb->sheets != NULL);

	rwinfo.type = EXPR_REWRITE_WORKBOOK;
	rwinfo.u.workbook = wb;

	WORKBOOK_FOREACH_SHEET (wb, sheet, {
		do_deps_destroy (sheet, &rwinfo);
	});
}

void
workbook_queue_all_recalc (Workbook *wb)
{
	/* FIXME : warning what about dependents in other workbooks */

	WORKBOOK_FOREACH_DEPENDENT
		(wb, dep, { dependent_queue_recalc (dep); });
}

/**
 * dependent_eval :
 * @dep :
 */
gboolean
dependent_eval (Dependent *dep)
{
	if (dep->flags & DEPENDENT_NEEDS_RECALC) {
		int const t = dependent_type (dep);

		if (t != DEPENDENT_CELL) {
			DependentClass *klass = g_ptr_array_index (dep_classes, t);

			g_return_val_if_fail (klass, FALSE);
			(*klass->eval) (dep);
		} else {
			gboolean finished = cell_eval_content (DEP_TO_CELL (dep));

			/* This should always be the top of the stack */
			g_return_val_if_fail (finished, FALSE);
		}

		/* Don't clear flag until after in case we iterate */
		dep->flags &= ~DEPENDENT_NEEDS_RECALC;
		return TRUE;
	}
	return FALSE;
}

/**
 * workbook_recalc :
 * @wb : 
 *
 * Computes all dependents in @wb that have been flags as requiring
 * recomputation.
 *
 * NOTE! This does not recalc dependents in other workbooks.
 */
void
workbook_recalc (Workbook *wb)
{
	gboolean redraw = FALSE;

	g_return_if_fail (IS_WORKBOOK (wb));

	WORKBOOK_FOREACH_DEPENDENT (wb, dep, redraw |= dependent_eval (dep););
	if (redraw) {
		WORKBOOK_FOREACH_SHEET (wb, sheet, sheet_redraw_all (sheet, FALSE););
	}
}

/**
 * workbook_recalc_all :
 * @wb :
 *
 * Queues all dependents for recalc and marks them all as dirty.
 */
void
workbook_recalc_all (Workbook *wb)
{
	workbook_queue_all_recalc (wb);
	workbook_recalc (wb);
	WORKBOOK_FOREACH_VIEW (wb, view,
		sheet_update (wb_view_cur_sheet (view)););
}

DependencyContainer *
dependency_data_new (void)
{
	DependencyContainer *deps = g_new (DependencyContainer, 1);

	deps->dependent_list = NULL;

	deps->range_hash  = g_new0 (GHashTable *,
				    (SHEET_MAX_ROWS-1)/BUCKET_SIZE + 1);
	deps->single_hash = g_hash_table_new (depsingle_hash,
					      depsingle_equal);
	deps->names       = g_hash_table_new (g_direct_hash,
					      g_direct_equal);

	return deps;
}

/****************************************************************************
 * Debug utils
 */
static void
dump_dependent_list (GSList *l)
{
	printf ("(");
	for (; l; l = l->next) {
		Dependent *dep = l->data;
		dependent_debug_name (dep, stdout);
		printf (", ");
	}
	printf (")\n");
}

static void
dump_range_dep (gpointer key, gpointer value, gpointer closure)
{
	DependencyRange const *deprange = key;
	Range const *range = &(deprange->range);

	/* 2 calls to col_name and row_name.  It uses a static buffer */
	printf ("\t%s:", cell_pos_name (&range->start));
	printf ("%s -> ", cell_pos_name (&range->end));

	dep_collection_foreach_list (deprange->deps, list,
		dump_dependent_list (list););
}

static void
dump_single_dep (gpointer key, gpointer value, gpointer closure)
{
	DependencySingle *depsingle = key;

	printf ("\t%s -> ", cell_pos_name (&depsingle->pos));

	dep_collection_foreach_list (depsingle->deps, list,
		dump_dependent_list (list););
}

/**
 * sheet_dump_dependencies :
 * @sheet :
 *
 * A useful utility for checking the state of the dependency data structures.
 */
void
sheet_dump_dependencies (Sheet const *sheet)
{
	DependencyContainer *deps;

	g_return_if_fail (sheet != NULL);

	deps = sheet->deps;

	if (deps) {
		int i;
		printf ("For %s:%s\n",
			sheet->workbook->filename
			?  sheet->workbook->filename
			: "(no name)",
			sheet->name_unquoted);

		for (i = (SHEET_MAX_ROWS-1)/BUCKET_SIZE; i >= 0 ; i--) {
			GHashTable *hash = sheet->deps->range_hash[i];
			if (hash != NULL && g_hash_table_size (hash) > 0) {
				printf ("Bucket %d (%d-%d): Range hash size %d: range over which cells in list depend\n",
					i, i * BUCKET_SIZE, (i + 1) * BUCKET_SIZE - 1,
					g_hash_table_size (hash));
				g_hash_table_foreach (hash,
						      dump_range_dep, NULL);
			}
		}

		if (g_hash_table_size (deps->single_hash) > 0) {
			printf ("Single hash size %d: cell on which list of cells depend\n",
				g_hash_table_size (deps->single_hash));
			g_hash_table_foreach (deps->single_hash,
					      dump_single_dep, NULL);
		}
	}
}

/**
 * dependent_debug_name :
 * @dep : The dependent we are interested in.
 * @file : FILE * to print to.
 *
 * A useful little debugging utility.
 */
void
dependent_debug_name (Dependent const *dep, FILE *out)
{
	int t;

	g_return_if_fail (dep != NULL);
	g_return_if_fail (out != NULL);
	g_return_if_fail (dep_classes);

	if (dep->sheet != NULL)
		fprintf (out, "%s!", dep->sheet->name_quoted);
	else
		g_warning ("Invalid dep, missing sheet");

	t = dependent_type (dep);
	if (t != DEPENDENT_CELL) {
		DependentClass *klass = g_ptr_array_index (dep_classes, t);

		g_return_if_fail (klass);
		(*klass->debug_name) (dep, out);
	} else
		fprintf (out, "%s", cell_name (DEP_TO_CELL (dep)));
}
