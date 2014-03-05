/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dependent.c:  Manage calculation dependencies between objects
 *
 * Copyright (C) 2000-2006 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2006-2013 Morten Welinder (terra@gnome.org)
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
#include "gnumeric.h"
#include "dependent.h"

#include "workbook-priv.h"
#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "expr.h"
#include "expr-impl.h"
#include "expr-name.h"
#include "application.h"
#include "workbook-view.h"
#include "ranges.h"
#include "gutils.h"
#include "sheet-view.h"
#include "func.h"

#include <goffice/goffice.h>
#include <string.h>

static gboolean gnm_cell_eval_content (GnmCell *cell);
static void dependent_changed (GnmDependent *dep);
static void dependent_clear_dynamic_deps (GnmDependent *dep);

/* ------------------------------------------------------------------------- */

/*
 * Note: we unconditionally use pools for
 *   deps->range_pool
 *   deps->single_pool
 * since we need the ability to free en masse.
 */

#ifndef USE_POOLS
#define USE_POOLS 0
#endif

#if USE_POOLS
static GOMemChunk *micro_few_pool;
static GOMemChunk *cset_pool;
#define CHUNK_ALLOC(T,p) ((T*)go_mem_chunk_alloc (p))
#define CHUNK_FREE(p,v) go_mem_chunk_free ((p), (v))
#define MICRO_HASH_FEW 3 /* Odd and small. */
#define NEW_FEW CHUNK_ALLOC (gpointer, micro_few_pool)
#define FREE_FEW(p) CHUNK_FREE (micro_few_pool, p)
#else
#define CHUNK_ALLOC(T,c) g_slice_new (T)
#define CHUNK_FREE(p,v) g_slice_free1 (sizeof(*v),(v))
#define MICRO_HASH_FEW 4 /* Even and small. */
#define NEW_FEW (gpointer *)g_slice_alloc (MICRO_HASH_FEW * sizeof (gpointer))
#define FREE_FEW(p) g_slice_free1 (MICRO_HASH_FEW * sizeof (gpointer), p)
#endif

/* ------------------------------------------------------------------------- */
/* Maps between row numbers and bucket numbers.  */

#define BUCKET_SIZE	1024
#define BUCKET_OF_ROW(row) ((row) / BUCKET_SIZE)
#define BUCKET_START_ROW(b) ((b) * BUCKET_SIZE)
#define BUCKET_END_ROW(b) ((b) * BUCKET_SIZE + (BUCKET_SIZE - 1))

/* ------------------------------------------------------------------------- */

/* Keep this odd */
#define CSET_SEGMENT_SIZE 29

typedef struct _CSet CSet;
struct _CSet {
        int count;
        CSet *next;
        gpointer data[CSET_SEGMENT_SIZE];
        /* And one pointer for allocation overhead.  */
};

#if 0
static gboolean
cset_find (CSet *list, gpointer datum)
{
        while (list) {
                guint i = list->count;
                while (i-- > 0)
                        if (list->data[i] == datum)
                                return TRUE;
                list = list->next;
        }
        return FALSE;
}
#endif

static void
cset_free (CSet *list)
{
        while (list) {
                CSet *next = list->next;
                CHUNK_FREE (cset_pool, list);
                list = next;
        }
}

/* NOTE: takes reference.  */
static void
cset_insert (CSet **list, gpointer datum)
{
	CSet *cs = *list;
        if (cs == NULL || cs->count == CSET_SEGMENT_SIZE) {
                CSet *h = *list = CHUNK_ALLOC (CSet, cset_pool);
                h->next = cs;
                h->count = 1;
		h->data[0] = datum;
        } else
		cs->data[cs->count++] = datum;
}

/* NOTE: takes reference.  Returns TRUE if datum was already present.  */
static gboolean
cset_insert_checked (CSet **list, gpointer datum)
{
	CSet *cs = *list;
	CSet *nonfull = NULL;

        while (cs) {
                guint i = cs->count;
		if (i != CSET_SEGMENT_SIZE)
			nonfull = cs;
                while (i-- > 0)
                        if (cs->data[i] == datum)
                                return TRUE;
                cs = cs->next;
        }

	if (nonfull)
		nonfull->data[nonfull->count++] = datum;
	else
		cset_insert (list, datum);
        return FALSE;
}


/* NOTE: takes reference.  Returns TRUE if removed.  */
static gboolean
cset_remove (CSet **list, gpointer datum)
{
        CSet *l, *last = NULL;

        for (l = *list; l; l = l->next) {
                guint i;

                for (i = l->count; i-- > 0; )
                        if (l->data[i] == datum) {
                                l->count--;
                                if (l->count == 0) {
                                        if (last)
                                                last->next = l->next;
                                        else
                                                *list = l->next;
                                        CHUNK_FREE (cset_pool, l);
                                } else
					l->data[i] = l->data[l->count];
                                return TRUE;
                        }
                last = l;
        }
        return FALSE;
}

#define CSET_FOREACH(list,var,code)			\
  do {							\
        CSet *cs_;					\
        for (cs_ = (list); cs_; cs_ = cs_->next) {	\
		guint i_;				\
                for (i_ = cs_->count; i_-- > 0; ) {	\
                        var = cs_->data[i_];		\
                        code				\
                }					\
        }						\
  } while (0)


/* ------------------------------------------------------------------------- */

static void
gnm_dep_set_expr_undo_undo (GnmDependent *dep, GnmExprTop const *texpr)
{
	dependent_set_expr (dep, texpr);
	dependent_link (dep);
	dependent_changed (dep);
}

static GOUndo *
gnm_dep_set_expr_undo_new (GnmDependent *dep)
{
	gnm_expr_top_ref (dep->texpr);
	return go_undo_binary_new (dep, (gpointer)dep->texpr,
				   (GOUndoBinaryFunc)gnm_dep_set_expr_undo_undo,
				   NULL,
				   (GFreeFunc)gnm_expr_top_unref);
}

static GOUndo *
gnm_dep_unlink_undo_new (GSList *deps)
{
	return go_undo_unary_new (deps,
				  (GOUndoUnaryFunc)dependents_link,
				  (GFreeFunc)g_slist_free);
}

/* ------------------------------------------------------------------------- */

#undef DEBUG_EVALUATION

static void
dummy_dep_eval (G_GNUC_UNUSED GnmDependent *dep)
{
}

static void cell_dep_eval (GnmDependent *dep);
static void cell_dep_set_expr (GnmDependent *dep, GnmExprTop const *new_texpr);
static GSList *cell_dep_changed (GnmDependent *dep);
static GnmCellPos const *cell_dep_pos (GnmDependent const *dep);
static void cell_dep_debug_name (GnmDependent const *dep, GString *target);
static const GnmDependentClass cell_dep_class = {
	cell_dep_eval,
	cell_dep_set_expr,
	cell_dep_changed,
	cell_dep_pos,
	cell_dep_debug_name,
};

static GSList *dynamic_dep_changed (GnmDependent *dep);
static void dynamic_dep_debug_name (GnmDependent const *dep, GString *target);
static const GnmDependentClass dynamic_dep_class = {
	dummy_dep_eval,
	NULL,
	dynamic_dep_changed,
	NULL,
	dynamic_dep_debug_name,
};
typedef struct {
	GnmDependent base;
	GnmDependent *container;
	GSList *ranges;
	GSList *singles;
} DynamicDep;

static void name_dep_debug_name (GnmDependent const *dep, GString *target);
static const GnmDependentClass name_dep_class = {
	dummy_dep_eval,
	NULL,
	NULL,
	NULL,
	name_dep_debug_name,
};

static void managed_dep_debug_name (GnmDependent const *dep, GString *target);
static const GnmDependentClass managed_dep_class = {
	dummy_dep_eval,
	NULL,
	NULL,
	NULL,
	managed_dep_debug_name,
};

static void style_dep_eval (GnmDependent *dep);
static GSList *style_dep_changed (GnmDependent *dep);
static GnmCellPos const *style_dep_pos (GnmDependent const *dep);
static void style_dep_debug_name (GnmDependent const *dep, GString *target);
static const GnmDependentClass style_dep_class = {
	style_dep_eval,
	NULL,
	style_dep_changed,
	style_dep_pos,
	style_dep_debug_name,
};
typedef struct {
	GnmDependent base;
	GnmCellPos pos;
} GnmStyleDependent;


static GPtrArray *dep_classes = NULL;

void
dependent_types_init (void)
{
	g_return_if_fail (dep_classes == NULL);

	/* Init with a NULL class so we can access directly */
	dep_classes = g_ptr_array_new ();
	g_ptr_array_add	(dep_classes, NULL); /* bogus filler */
	g_ptr_array_add	(dep_classes, (gpointer)&cell_dep_class);
	g_ptr_array_add	(dep_classes, (gpointer)&dynamic_dep_class);
	g_ptr_array_add	(dep_classes, (gpointer)&name_dep_class);
	g_ptr_array_add	(dep_classes, (gpointer)&managed_dep_class);
	g_ptr_array_add	(dep_classes, (gpointer)&style_dep_class);

#if USE_POOLS
	micro_few_pool =
		go_mem_chunk_new ("micro few pool",
				  MICRO_HASH_FEW * sizeof (gpointer),
				  16 * 1024 - 128);
	cset_pool =
		go_mem_chunk_new ("cset pool",
				  sizeof (CSet),
				  16 * 1024 - 128);
#endif
}

void
dependent_types_shutdown (void)
{
	g_return_if_fail (dep_classes != NULL);
	g_ptr_array_free (dep_classes, TRUE);
	dep_classes = NULL;

#if USE_POOLS
	go_mem_chunk_destroy (micro_few_pool, FALSE);
	micro_few_pool = NULL;
	go_mem_chunk_destroy (cset_pool, FALSE);
	cset_pool = NULL;
#endif
}

/**
 * dependent_register_type :
 * @klass: A vtable
 *
 * Store the vtable and allocate an ID for a new class
 * of dependents.
 */
guint32
dependent_type_register (GnmDependentClass const *klass)
{
	guint32 res;

	g_return_val_if_fail (dep_classes != NULL, 0);

	g_ptr_array_add	(dep_classes, (gpointer)klass);
	res = dep_classes->len-1;

	g_return_val_if_fail (res <= DEPENDENT_TYPE_MASK, res);

	return res;
}

/*
 * dependent_flag_recalc:
 * @dep: the dependent that contains the expression needing recomputation.
 *
 * Marks @dep as needing recalculation
 * NOTE : it does NOT recursively dirty dependencies.
 */
#define dependent_flag_recalc(dep) \
  do { (dep)->flags |= DEPENDENT_NEEDS_RECALC; } while (0)

/**
 * dependent_changed:
 * @cell: the dependent that changed
 *
 * Queues a recalc.
 */
static void
dependent_changed (GnmDependent *dep)
{
	if (dep->sheet &&
	    dep->sheet->workbook->recursive_dirty_enabled)
		dependent_queue_recalc (dep);
	else
		dependent_flag_recalc (dep);
}

/**
 * dependent_set_expr:
 * @dep: The dependent we are interested in.
 * @new_texpr: new expression.
 *
 * When the expression associated with @dep needs to change this routine
 * dispatches to the virtual handler, unlinking @dep if necessary beforehand.
 * Adds a ref to @new_expr.
 *
 * NOTE : it does NOT relink dependents in case they are going to move later.
 **/
void
dependent_set_expr (GnmDependent *dep, GnmExprTop const *new_texpr)
{
	int const t = dependent_type (dep);
	GnmDependentClass *klass = g_ptr_array_index (dep_classes, t);

	if (dependent_is_linked (dep))
		dependent_unlink (dep);
	if (dep->flags & DEPENDENT_HAS_DYNAMIC_DEPS)
		dependent_clear_dynamic_deps (dep);

	if (klass->set_expr)
		klass->set_expr (dep, new_texpr);
	else {
		if (new_texpr)
			gnm_expr_top_ref (new_texpr);
		if (dep->texpr)
			gnm_expr_top_unref (dep->texpr);
		dep->texpr = new_texpr;
		if (new_texpr)
			dependent_changed (dep);
	}
}

GnmCellPos const *
dependent_pos (GnmDependent const *dep)
{
	static GnmCellPos const dummy = { 0, 0 };
	int const t = dependent_type (dep);
	GnmDependentClass *klass = g_ptr_array_index (dep_classes, t);

	return klass->pos ? klass->pos (dep) : &dummy;
}


/**
 * dependent_set_sheet:
 * @dep:
 * @sheet:
 */
void
dependent_set_sheet (GnmDependent *dep, Sheet *sheet)
{
	g_return_if_fail (dep != NULL);
	g_return_if_fail (dep->sheet == NULL);
	g_return_if_fail (!dependent_is_linked (dep));

	dep->sheet = sheet;
	if (dep->texpr) {
		dependent_link (dep);
		dependent_changed (dep);
	}
}

static void
cb_cell_list_deps (GnmDependent *dep, gpointer user)
{
	GSList **list = (GSList **)user;
	*list = g_slist_prepend (*list, dep);
}

static GSList *
cell_list_deps (GnmCell const *cell)
{
	GSList *deps = NULL;
	cell_foreach_dep (cell, cb_cell_list_deps, &deps);
	return deps;
}

static void
dependent_queue_recalc_main (GSList *work)
{
	/*
	 * Work is now a list of marked cells whose dependencies need
	 * to be marked.  Marking early guarentees that we will not
	 * get duplicates.  (And it thus limits the length of the list.)
	 * We treat work as a stack.
	 */

	while (work) {
		GnmDependent *dep = work->data;
		int const t = dependent_type (dep);
		GnmDependentClass *klass = g_ptr_array_index (dep_classes, t);

		/* Pop the top element.  */
		work = g_slist_delete_link (work, work);

		if (klass->changed) {
			GSList *extra = klass->changed (dep);
			if (extra) {
				g_slist_last (extra)->next = work;
				work = extra;
			}
		}
	}
}


/**
 * dependent_queue_recalc_list :
 * @list:
 *
 * Queues any elements of @list for recalc that are not already queued,
 * and marks all elements as needing a recalc.
 */
static void
dependent_queue_recalc_list (GSList *list)
{
	GSList *work = NULL;

	for (; list != NULL ; list = list->next) {
		GnmDependent *dep = list->data;
		if (!dependent_needs_recalc (dep)) {
			dependent_flag_recalc (dep);
			work = g_slist_prepend (work, dep);
		}
	}

	dependent_queue_recalc_main (work);
}

void
dependent_queue_recalc (GnmDependent *dep)
{
	g_return_if_fail (dep != NULL);

#ifdef DEBUG_EVALUATION
	g_printerr ("/* QUEUE (%s) */\n", cell_name (GNM_DEP_TO_CELL (dep)));
#endif
	if (!dependent_needs_recalc (dep)) {
		GSList listrec;
		listrec.next = NULL;
		listrec.data = dep;
		dependent_queue_recalc_list (&listrec);
	}
}

/**************************************************************************/

typedef struct {
	gint num_buckets;
	gint num_elements;
	union {
		gpointer one;
		gpointer *few;
		CSet **many;
	} u;
} MicroHash;

#define MICRO_HASH_MIN_SIZE 11
#define MICRO_HASH_MAX_SIZE 13845163

#define MICRO_HASH_hash(key) ((guint)GPOINTER_TO_UINT(key))

static void
micro_hash_many_to_few (MicroHash *hash_table)
{
	CSet **buckets = hash_table->u.many;
	int nbuckets = hash_table->num_buckets;
	int i = 0;

	hash_table->u.few = NEW_FEW;

	while (nbuckets-- > 0 ) {
		gpointer datum;

		CSET_FOREACH (buckets[nbuckets], datum, {
			hash_table->u.few[i++] = datum;
		});
		cset_free (buckets[nbuckets]);
	}

	g_free (buckets);
}

static void
micro_hash_many_resize (MicroHash *hash_table, int new_nbuckets)
{
	CSet **buckets = hash_table->u.many;
	int nbuckets = hash_table->num_buckets;
	CSet **new_buckets = g_new0 (CSet *, new_nbuckets);

	hash_table->u.many = new_buckets;
	hash_table->num_buckets = new_nbuckets;

	while (nbuckets-- > 0 ) {
		gpointer datum;

		CSET_FOREACH (buckets[nbuckets], datum, {
			guint bucket = MICRO_HASH_hash (datum) % new_nbuckets;
			cset_insert (&(new_buckets[bucket]), datum);
		});
		cset_free (buckets[nbuckets]);
	}
	g_free (buckets);

#if 0
	{
		int nonzero = 0;
		int capacity = 0, totlen = 0;
		int i;

		for (i = 0; i < new_nbuckets; i++) {
			CSet *cs = new_buckets[i];
			if (cs) {
				nonzero++;
				while (cs) {
					totlen += cs->count;
					capacity += CSET_SEGMENT_SIZE;
					cs = cs->next;
				}
			}
		}

		g_printerr ("resize %p: %d [%d %.1f %.0f%%]\n",
			    hash_table,
			    new_nbuckets,
			    hash_table->num_elements,
			    (double)totlen / nonzero,
			    100.0 * totlen / capacity);
	}
#endif
}


static void
micro_hash_few_to_many (MicroHash *hash_table)
{
	int nbuckets = hash_table->num_buckets = MICRO_HASH_MIN_SIZE;
	CSet **buckets = g_new0 (CSet *, nbuckets);
	int i;

	for (i = 0; i < hash_table->num_elements; i++) {
		gpointer datum = hash_table->u.few[i];
		guint bucket = MICRO_HASH_hash (datum) % nbuckets;
		cset_insert (&(buckets[bucket]), datum);
	}
	FREE_FEW (hash_table->u.few);
	hash_table->u.many = buckets;
}



static void
micro_hash_insert (MicroHash *hash_table, gpointer key)
{
	int N = hash_table->num_elements;

	g_return_if_fail (key != NULL);

	if (N == 0) {
		hash_table->u.one = key;
	} else if (N == 1) {
		gpointer key0 = hash_table->u.one;
		if (key == key0)
			return;
		/* one --> few */
		hash_table->u.few = NEW_FEW;
		hash_table->u.few[0] = key0;
		hash_table->u.few[1] = key;
		memset (hash_table->u.few + 2, 0, (MICRO_HASH_FEW - 2) * sizeof (gpointer));
	} else if (N <= MICRO_HASH_FEW) {
		int i;

		for (i = 0; i < N; i++)
			if (hash_table->u.few[i] == key)
				return;

		if (N == MICRO_HASH_FEW) {
			guint bucket;

			micro_hash_few_to_many (hash_table);
			bucket = MICRO_HASH_hash (key) % hash_table->num_buckets;
			cset_insert (&(hash_table->u.many[bucket]), key);
		} else
			hash_table->u.few[N] = key;
	} else {
		int nbuckets = hash_table->num_buckets;
		guint bucket = MICRO_HASH_hash (key) % nbuckets;
		CSet **buckets = hash_table->u.many;

		if (cset_insert_checked (&(buckets[bucket]), key))
			return;

		if (N > CSET_SEGMENT_SIZE * nbuckets &&
		    nbuckets < MICRO_HASH_MAX_SIZE) {
			int new_nbuckets = g_spaced_primes_closest (N / (CSET_SEGMENT_SIZE / 2));
			if (new_nbuckets > MICRO_HASH_MAX_SIZE)
				new_nbuckets = MICRO_HASH_MAX_SIZE;
			micro_hash_many_resize (hash_table, new_nbuckets);
		}
	}

	hash_table->num_elements++;
}

static void
micro_hash_remove (MicroHash *hash_table, gpointer key)
{
	int N = hash_table->num_elements;
	guint bucket;

	if (N == 0)
		return;

	if (N == 1) {
		if (hash_table->u.one != key)
			return;
		hash_table->u.one = NULL;
		hash_table->num_elements--;
		return;
	}

	if (N <= MICRO_HASH_FEW) {
		int i;

		for (i = 0; i < N; i++)
			if (hash_table->u.few[i] == key) {
				hash_table->u.few[i] = hash_table->u.few[N - 1];
				hash_table->num_elements--;
				if (hash_table->num_elements > 1)
					return;
				/* few -> one */
				key = hash_table->u.few[0];
				FREE_FEW (hash_table->u.few);
				hash_table->u.one = key;
				return;
			}
		return;
	}

	bucket = MICRO_HASH_hash (key) % hash_table->num_buckets;
	if (cset_remove (&(hash_table->u.many[bucket]), key)) {
		hash_table->num_elements--;

		if (hash_table->num_elements <= MICRO_HASH_FEW)
			micro_hash_many_to_few (hash_table);
		else {
			/* Maybe resize? */
		}
		return;
	}
}


static void
micro_hash_release (MicroHash *hash_table)
{
	int N = hash_table->num_elements;

	if (N <= 1)
		; /* Nothing */
	else if (N <= MICRO_HASH_FEW)
		FREE_FEW (hash_table->u.few);
	else {
		guint i = hash_table->num_buckets;
		while (i-- > 0)
			cset_free (hash_table->u.many[i]);
		g_free (hash_table->u.many);
	}
	hash_table->num_elements = 0;
	hash_table->num_buckets = 1;
	hash_table->u.one = NULL;
}

static void
micro_hash_init (MicroHash *hash_table, gpointer key)
{
	hash_table->num_elements = 1;
	hash_table->u.one = key;
}

static inline gboolean
micro_hash_is_empty (MicroHash const *hash_table)
{
	return hash_table->num_elements == 0;
}

/*************************************************************************/

#define micro_hash_foreach_dep(dc, dep, code) do {			\
        guint i_ = dc.num_elements;					\
	if (i_ <= MICRO_HASH_FEW) {					\
		const gpointer *e_ = (i_ == 1) ? &dc.u.one : dc.u.few;	\
		while (i_-- > 0) {					\
			GnmDependent *dep = e_[i_];			\
			code						\
		}							\
	} else {							\
		GnmDependent *dep;					\
		guint b_ = dc.num_buckets;				\
		while (b_-- > 0)					\
			CSET_FOREACH (dc.u.many[b_], dep, code);	\
	}								\
} while (0)

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
	MicroHash	deps;	/* Must be first */
	GnmRange  range;
} DependencyRange;

/*
 *  A DependencySingle stores a list of dependents that rely
 * on the cell at @pos.
 *
 * A change in this cell will trigger a recomputation on the
 * cells listed in deps.
 */
typedef struct {
	MicroHash	deps;	/* Must be first */
	GnmCellPos pos;
} DependencySingle;

/* A utility type */
typedef struct {
	MicroHash	deps;	/* Must be first */
} DependencyAny;

static guint
deprange_hash (DependencyRange const *r)
{
	guint a = r->range.start.row;
	guint b = r->range.end.row;
	guint c = r->range.start.col;
	guint d = r->range.end.col;

	return (((((a << 8) + b) << 8) + c) << 8) + d;
}

static gint
deprange_equal (DependencyRange const *r1, DependencyRange const *r2)
{
	return range_equal (&(r1->range), &(r2->range));
}

static guint
depsingle_hash (DependencySingle const *depsingle)
{
	return (depsingle->pos.row << 8) ^ depsingle->pos.col;
}

static gint
depsingle_equal (DependencySingle const *a, DependencySingle const *b)
{
	return (a->pos.row == b->pos.row && a->pos.col == b->pos.col);
}

static GnmDependentFlags
link_single_dep (GnmDependent *dep, GnmCellPos const *pos, GnmCellRef const *ref)
{
	DependencySingle lookup;
	DependencySingle *single;
	GnmDependentFlags flag = DEPENDENT_NO_FLAG;
	Sheet const *sheet = eval_sheet (ref->sheet, dep->sheet);
	GnmDepContainer *deps = sheet->deps;

	if (sheet != dep->sheet)
		flag = (sheet->workbook != dep->sheet->workbook)
			? DEPENDENT_GOES_INTERBOOK
			: DEPENDENT_GOES_INTERSHEET;

	/* Inserts if it is not already there */
	gnm_cellpos_init_cellref (&lookup.pos, ref, pos, sheet);
	single = g_hash_table_lookup (deps->single_hash, &lookup);
	if (single == NULL) {
		single = go_mem_chunk_alloc (deps->single_pool);
		*single = lookup;
		micro_hash_init (&single->deps, dep);
		g_hash_table_insert (deps->single_hash, single, single);
	} else
		micro_hash_insert (&single->deps, dep);

	return flag;
}

static GnmDependentFlags
unlink_single_dep (GnmDependent *dep, GnmCellPos const *pos, GnmCellRef const *a)
{
	DependencySingle lookup;
	DependencySingle *single;
	GnmDependentFlags flag = DEPENDENT_NO_FLAG;
	Sheet const *sheet = eval_sheet (a->sheet, dep->sheet);
	GnmDepContainer *deps = sheet->deps;

	if (sheet != dep->sheet)
		flag = (sheet->workbook != dep->sheet->workbook)
			? DEPENDENT_GOES_INTERBOOK
			: DEPENDENT_GOES_INTERSHEET;
	if (!deps)
		return flag;

	gnm_cellpos_init_cellref (&lookup.pos, a, pos, sheet);
	single = g_hash_table_lookup (deps->single_hash, &lookup);
	if (single != NULL) {
		micro_hash_remove (&single->deps, dep);
		if (micro_hash_is_empty (&single->deps)) {
			g_hash_table_remove (deps->single_hash, single);
			micro_hash_release (&single->deps);
			go_mem_chunk_free (deps->single_pool, single);
		}
	}

	return flag;
}

static inline GnmDependentFlags
link_unlink_single_dep (GnmDependent *dep, GnmCellPos const *pos,
			GnmCellRef const *a, gboolean qlink)
{
	return qlink
		? link_single_dep (dep, pos, a)
		: unlink_single_dep (dep, pos, a);
}


static void
link_range_dep (GnmDepContainer *deps, GnmDependent *dep,
		DependencyRange const *r)
{
	int i = BUCKET_OF_ROW (r->range.start.row);
	int end = BUCKET_OF_ROW (r->range.end.row);
	DependencyRange r2 = *r;

	/*
	 * It is possible to see ranges bigger than the sheet when
	 * operating with 3D ranges.  See bug #704109.
	 */
	end = MIN (end, deps->buckets - 1);

	for ( ; i <= end; i++) {
		DependencyRange *result;

		/* Restrict range to bucket.  */
		r2.range.start.row = MAX (r->range.start.row, BUCKET_START_ROW (i));
		r2.range.end.row = MIN (r->range.end.row, BUCKET_END_ROW (i));

		if (deps->range_hash[i] == NULL)
			deps->range_hash[i] = g_hash_table_new (
				(GHashFunc)  deprange_hash,
				(GEqualFunc) deprange_equal);
		else {
			result = g_hash_table_lookup (deps->range_hash[i], &r2);
			if (result) {
				/* Inserts if it is not already there */
				micro_hash_insert (&result->deps, dep);
				continue;
			}
		}

		/* Create a new DependencyRange structure */
		result = go_mem_chunk_alloc (deps->range_pool);
		*result = r2;
		micro_hash_init (&result->deps, dep);
		g_hash_table_insert (deps->range_hash[i], result, result);
	}
}

static void
unlink_range_dep (GnmDepContainer *deps, GnmDependent *dep,
		  DependencyRange const *r)
{
	int i = BUCKET_OF_ROW (r->range.start.row);
	int end = BUCKET_OF_ROW (r->range.end.row);
	DependencyRange r2 = *r;

	if (!deps)
		return;

	end = MIN (end, deps->buckets - 1);

	for ( ; i <= end; i++) {
		DependencyRange *result;

		/* Restrict range to bucket.  */
		r2.range.start.row = MAX (r->range.start.row, BUCKET_START_ROW (i));
		r2.range.end.row = MIN (r->range.end.row, BUCKET_END_ROW (i));

		result = g_hash_table_lookup (deps->range_hash[i], &r2);
		if (result) {
			micro_hash_remove (&result->deps, dep);
			if (micro_hash_is_empty (&result->deps)) {
				g_hash_table_remove (deps->range_hash[i], result);
				micro_hash_release (&result->deps);
				go_mem_chunk_free (deps->range_pool, result);
			}
		}
	}
}

static inline void
link_unlink_range_dep (GnmDepContainer *deps, GnmDependent *dep,
		       DependencyRange const *r, gboolean qlink)
{
	if (qlink)
		link_range_dep (deps, dep, r);
	else
		unlink_range_dep (deps, dep, r);
}

static GnmDependentFlags
link_unlink_cellrange_dep (GnmDependent *dep, GnmCellPos const *pos,
			   GnmCellRef const *a, GnmCellRef const *b,
			   gboolean qlink)
{
	DependencyRange range;
	GnmDependentFlags flag = DEPENDENT_NO_FLAG;

	gnm_cellpos_init_cellref (&range.range.start, a, pos, dep->sheet);
	gnm_cellpos_init_cellref (&range.range.end, b, pos, dep->sheet);
	range_normalize (&range.range);

	if (a->sheet != NULL) {
		if (a->sheet != dep->sheet)
			flag = (a->sheet->workbook != dep->sheet->workbook)
				? DEPENDENT_GOES_INTERBOOK : DEPENDENT_GOES_INTERSHEET;

		if (b->sheet != NULL && a->sheet != b->sheet) {
			Workbook const *wb = a->sheet->workbook;
			int i = a->sheet->index_in_wb;
			int stop = b->sheet->index_in_wb;
			if (i > stop) { int tmp = i; i = stop ; stop = tmp; }

			g_return_val_if_fail (b->sheet->workbook == wb, flag);

			while (i <= stop) {
				Sheet *sheet = g_ptr_array_index (wb->sheets, i);
				i++;
				link_unlink_range_dep (sheet->deps, dep, &range,
						       qlink);
			}
			flag |= DEPENDENT_HAS_3D;
		} else
			link_unlink_range_dep (a->sheet->deps, dep, &range,
					       qlink);
	} else
		link_unlink_range_dep (dep->sheet->deps, dep, &range, qlink);

	return flag;
}

static GnmDependentFlags
link_unlink_expr_dep (GnmEvalPos *ep, GnmExpr const *tree, gboolean qlink)
{
	g_return_val_if_fail (tree != NULL, DEPENDENT_NO_FLAG);

	switch (GNM_EXPR_GET_OPER (tree)) {
	case GNM_EXPR_OP_RANGE_CTOR:  /* See #562363 */
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		return link_unlink_expr_dep (ep, tree->binary.value_a, qlink) |
		       link_unlink_expr_dep (ep, tree->binary.value_b, qlink);
	case GNM_EXPR_OP_ANY_UNARY:
		return link_unlink_expr_dep (ep, tree->unary.value, qlink);
	case GNM_EXPR_OP_CELLREF:
		return link_unlink_single_dep (ep->dep, dependent_pos (ep->dep), &tree->cellref.ref, qlink);

	case GNM_EXPR_OP_CONSTANT:
		/* TODO: pass in eval flags so that we can use implicit
		 * intersection
		 */
		if (VALUE_CELLRANGE == tree->constant.value->type)
			return link_unlink_cellrange_dep
				(ep->dep, dependent_pos (ep->dep),
				 &tree->constant.value->v_range.cell.a,
				 &tree->constant.value->v_range.cell.b,
				 qlink);
		return DEPENDENT_NO_FLAG;

	/*
	 * FIXME: needs to be taught implicit intersection +
	 * more cunning handling of argument type matching.
	 */
	case GNM_EXPR_OP_FUNCALL: {
		int i;
		GnmDependentFlags flag = DEPENDENT_NO_FLAG;
		if (tree->func.func->fn_type == GNM_FUNC_TYPE_STUB)
			gnm_func_load_stub (tree->func.func);
		if (tree->func.func->linker) {
			GnmFuncEvalInfo fei;
			fei.pos = ep;
			fei.func_call = &tree->func;
			flag = tree->func.func->linker (&fei, qlink);
		}
		if (!(flag & DEPENDENT_IGNORE_ARGS))
			for (i = 0; i < tree->func.argc; i++)
				flag |= link_unlink_expr_dep (ep, tree->func.argv[i], qlink);
		return flag;
	}

	case GNM_EXPR_OP_NAME:
		if (qlink)
			expr_name_add_dep (tree->name.name, ep->dep);
		else
			expr_name_remove_dep (tree->name.name, ep->dep);
		if (expr_name_is_active (tree->name.name))
			return link_unlink_expr_dep (ep, tree->name.name->texpr->expr, qlink) | DEPENDENT_USES_NAME;
		return DEPENDENT_USES_NAME;

	case GNM_EXPR_OP_ARRAY_ELEM: {
		/* Non-corner cells depend on the corner */
		GnmCellRef a;
		GnmCellPos const *pos = dependent_pos (ep->dep);
		/* We cannot support array expressions unless
		 * we have a position.
		 */
		g_return_val_if_fail (pos != NULL, DEPENDENT_NO_FLAG);

		a.col_relative = a.row_relative = FALSE;
		a.sheet = ep->dep->sheet;
		a.col   = pos->col - tree->array_elem.x;
		a.row   = pos->row - tree->array_elem.y;

		return link_unlink_single_dep (ep->dep, pos, &a, qlink);
	}

	case GNM_EXPR_OP_ARRAY_CORNER: {
		GnmEvalPos pos = *ep;
		pos.array = &tree->array_corner;
		/* Corner cell depends on the contents of the expr */
		return link_unlink_expr_dep (&pos, tree->array_corner.expr, qlink);
	}

	case GNM_EXPR_OP_SET: {
		int i;
		GnmDependentFlags res = DEPENDENT_NO_FLAG;

		for (i = 0; i < tree->set.argc; i++)
			res |= link_unlink_expr_dep (ep, tree->set.argv[i], qlink);
		return res;
	}
#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
#endif
	}
	return DEPENDENT_NO_FLAG;
}

static void
workbook_link_3d_dep (GnmDependent *dep)
{
	Workbook *wb = dep->sheet->workbook;

	if (wb->being_reordered)
		return;
	if (wb->sheet_order_dependents == NULL)
		wb->sheet_order_dependents =
			g_hash_table_new (g_direct_hash, g_direct_equal);
	g_hash_table_insert (wb->sheet_order_dependents, dep, dep);
}

static void
workbook_unlink_3d_dep (GnmDependent *dep)
{
	Workbook *wb = dep->sheet->workbook;

	/* during destruction */
	if (wb->sheet_order_dependents == NULL)
		return;

	if (wb->being_reordered)
		return;

	g_hash_table_remove (wb->sheet_order_dependents, dep);
}

/**
 * gnm_dep_style_dependency:
 * @sheet:
 * @texpr:
 * @r:
 * @accum: (inout) (transfer full) (element-type GnmDependent):
 **/
void
gnm_dep_style_dependency (Sheet *sheet,
			  GnmExprTop const *texpr,
			  GnmRange const *r,
			  GPtrArray *accum)
{
	int row, col;

	/*
	 * FIXME: Maybe do better for an expression that is just an
	 * absolute ref.
	 */

	for (row = r->start.row; row <= r->end.row; row++) {
		for (col = r->start.col; col <= r->end.col; col++) {
			GnmStyleDependent *sd = g_new0 (GnmStyleDependent, 1);
			GnmDependent *dep = &sd->base;

			dep->sheet = sheet;
			dep->flags = DEPENDENT_STYLE;
			dep->texpr = NULL;
			sd->pos.col = col;
			sd->pos.row = row;

			dependent_set_expr (dep, texpr);
			dependent_link (dep);
			g_ptr_array_add (accum, dep);
		}
	}
}

/*****************************************************************************/

static void
cell_dep_eval (GnmDependent *dep)
{
	gboolean finished = gnm_cell_eval_content (GNM_DEP_TO_CELL (dep));
	(void)finished; /* We don't currently care */
	dep->flags &= ~GNM_CELL_HAS_NEW_EXPR;
}

static void
cell_dep_set_expr (GnmDependent *dep, GnmExprTop const *new_texpr)
{
	/*
	 * Explicitly do not check for array subdivision, we may be
	 * replacing the corner of an array.
	 */
	gnm_cell_set_expr_unsafe (GNM_DEP_TO_CELL (dep), new_texpr);
}

static GSList *
cell_dep_changed (GnmDependent *dep)
{
	/* When a cell changes, so do its dependents.  */

	GSList *deps = cell_list_deps (GNM_DEP_TO_CELL (dep));
	GSList *waste = NULL, *work = NULL;
	GSList *next, *list;
	for (list = deps; list != NULL ; list = next) {
		GnmDependent *dep = list->data;
		next = list->next;
		if (dependent_needs_recalc (dep)) {
			list->next = waste;
			waste = list;
		} else {
			dependent_flag_recalc (dep);
			list->next = work;
			work = list;
		}
	}
	g_slist_free (waste);
	return work;
}

static GnmCellPos const *
cell_dep_pos (GnmDependent const *dep)
{
	return &GNM_DEP_TO_CELL (dep)->pos;
}

static void
cell_dep_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append (target, cell_name (GNM_DEP_TO_CELL (dep)));
}

/*****************************************************************************/
/*
 * "Managed" dependent handling.
 *
 * A managed dependent is simply an expression set up to be maintained by
 * the dependency system so it follows row/column insert/delete nicely.
 *
 * The expression being managed is typically a cell range, but can be any
 * expression.  Everything is interpreted relative to A1 in the sheet.
 */

void
dependent_managed_init (GnmDependent *dep, Sheet *sheet)
{
	memset (dep, 0, sizeof (*dep));
	dep->flags = DEPENDENT_MANAGED;
	dep->sheet = sheet;
}

void
dependent_managed_set_expr (GnmDependent *dep, GnmExprTop const *texpr)
{
	g_return_if_fail (dep != NULL);
#if 0
	/* We need some kind of IS_A */
	g_return_if_fail (dependent_type (dep) == DEPENDENT_MANAGED);
#endif

	dependent_set_expr (dep, texpr);
	if (texpr && dep->sheet)
		dependent_link (dep);
}

void
dependent_managed_set_sheet (GnmDependent *dep, Sheet *sheet)
{
	GnmExprTop const *texpr;

	g_return_if_fail (dep != NULL);
#if 0
	/* We need some kind of IS_A */
	g_return_if_fail (dependent_type (dep) == DEPENDENT_MANAGED);
#endif

	if (dep->sheet == sheet)
		return;

	texpr = dep->texpr;
	if (texpr) gnm_expr_top_ref (texpr);
	dependent_set_expr (dep, NULL);
	/* We're now unlinked from everything. */
	dep->sheet = sheet;
	dependent_managed_set_expr (dep, texpr);
	if (texpr) gnm_expr_top_unref (texpr);
}

static void
managed_dep_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "Managed%p", (void *)dep);
}

/*****************************************************************************/

static gboolean
debug_style_deps (void)
{
	static int debug = -1;
	if (debug < 0)
		debug = gnm_debug_flag ("style-deps");
	return debug;
}

static void
style_dep_unrender (GnmDependent *dep, const char *what)
{
	GnmCellPos const *pos = dependent_pos (dep);
	GnmCell *cell;
	Sheet *sheet = dep->sheet;

	if (debug_style_deps ())
		g_printerr ("StyleDep %p at %s %s\n",
			    dep, cellpos_as_string (pos), what);

	/*
	 * If the cell exists, unrender it so format changes can take
	 * effect.
	 */
	cell = sheet_cell_get (sheet, pos->col, pos->row);
	if (cell)
		gnm_cell_unrender (cell);

	sheet_redraw_region (sheet,
			     pos->col, pos->row,
			     pos->col, pos->row);


}

static void
style_dep_eval (GnmDependent *dep)
{
	/*
	 * It is possible that the cell has been rendered between we ::changed
	 * was called.
	 */
	style_dep_unrender (dep, "being evaluated");
}

static GSList *
style_dep_changed (GnmDependent *dep)
{
	style_dep_unrender (dep, "changed");
	return NULL;
}

static GnmCellPos const *
style_dep_pos (GnmDependent const *dep)
{
	return &((GnmStyleDependent*)dep)->pos;
}

static void
style_dep_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "StyleDep%p", (void *)dep);
}

/*****************************************************************************/

static void
name_dep_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "Name%p", (void *)dep);
}

/*****************************************************************************/

static GSList *
dynamic_dep_changed (GnmDependent *dep)
{
	DynamicDep const *dyn = (DynamicDep *)dep;

	/* When a dynamic dependent changes, we mark its container.  */

	if (dependent_needs_recalc (dyn->container))
		return NULL;

	dependent_flag_recalc (dyn->container);
	return g_slist_prepend (NULL, dyn->container);
}

static void
dynamic_dep_debug_name (GnmDependent const *dep, GString *target)
{
	g_string_append_printf (target, "DynamicDep%p", (void *)dep);
}

void
dependent_add_dynamic_dep (GnmDependent *dep, GnmRangeRef const *rr)
{
	GnmDependentFlags    flags;
	DynamicDep	 *dyn;
	GnmCellPos const *pos;
	DependencyRange   range;

	g_return_if_fail (dep != NULL);

	pos = dependent_pos (dep);

	if (dep->flags & DEPENDENT_HAS_DYNAMIC_DEPS)
		dyn = g_hash_table_lookup (dep->sheet->deps->dynamic_deps, dep);
	else {
		dep->flags |= DEPENDENT_HAS_DYNAMIC_DEPS;
		dyn = g_new (DynamicDep, 1);
		dyn->base.flags		= DEPENDENT_DYNAMIC_DEP;
		dyn->base.sheet		= dep->sheet;
		dyn->base.texpr		= NULL;
		dyn->container		= dep;
		dyn->ranges		= NULL;
		dyn->singles		= NULL;
		g_hash_table_insert (dep->sheet->deps->dynamic_deps, dep, dyn);
	}

	gnm_cellpos_init_cellref (&range.range.start, &rr->a, pos, dep->sheet);
	gnm_cellpos_init_cellref (&range.range.end, &rr->b, pos, dep->sheet);
	if (range_is_singleton (&range.range)) {
		flags = link_single_dep (&dyn->base, pos, &rr->a);
		dyn->singles = g_slist_prepend (dyn->singles, gnm_rangeref_dup (rr));
	} else {
		flags = link_unlink_cellrange_dep (&dyn->base, pos, &rr->a, &rr->b, TRUE);
		dyn->ranges = g_slist_prepend (dyn->ranges, gnm_rangeref_dup (rr));
	}
	if (flags & DEPENDENT_HAS_3D)
		workbook_link_3d_dep (dep);
}

static void
dependent_clear_dynamic_deps (GnmDependent *dep)
{
	g_hash_table_remove (dep->sheet->deps->dynamic_deps, dep);
}

/*****************************************************************************/

/**
 * dependent_link:
 * @dep: the dependent that changed
 *
 * Adds the dependent to the workbook wide list of dependents.
 */
void
dependent_link (GnmDependent *dep)
{
	Sheet	   *sheet;
	GnmEvalPos  ep;

	g_return_if_fail (dep != NULL);
	g_return_if_fail (dep->texpr != NULL);
	g_return_if_fail (!(dep->flags & DEPENDENT_IS_LINKED));
	g_return_if_fail (IS_SHEET (dep->sheet));
	g_return_if_fail (dep->sheet->deps != NULL);

	sheet = dep->sheet;

	/* Make this the new tail of the dependent list.  */
	dep->prev_dep = sheet->deps->tail;
	dep->next_dep = NULL;
	if (dep->prev_dep)
		dep->prev_dep->next_dep = dep;
	else
		sheet->deps->head = dep; /* first element */
	sheet->deps->tail = dep;
	dep->flags |= DEPENDENT_IS_LINKED |
		link_unlink_expr_dep (eval_pos_init_dep (&ep, dep),
				      dep->texpr->expr, TRUE);

	if (dep->flags & DEPENDENT_HAS_3D)
		workbook_link_3d_dep (dep);
}

/**
 * dependent_unlink:
 * @dep: the dependent that changed
 *
 * Removes the dependent from its container's set of dependents and always
 * removes the linkages to what it depends on.
 */
void
dependent_unlink (GnmDependent *dep)
{
	GnmDepContainer *contain;
	GnmEvalPos ep;

	g_return_if_fail (dep != NULL);
	g_return_if_fail (dependent_is_linked (dep));
	g_return_if_fail (dep->texpr != NULL);
	g_return_if_fail (IS_SHEET (dep->sheet));

	link_unlink_expr_dep (eval_pos_init_dep (&ep, dep),
			      dep->texpr->expr, FALSE);
	contain = dep->sheet->deps;
	if (contain != NULL) {
		if (contain->head == dep)
			contain->head = dep->next_dep;
		if (contain->tail == dep)
			contain->tail = dep->prev_dep;
		if (dep->next_dep)
			dep->next_dep->prev_dep = dep->prev_dep;
		if (dep->prev_dep)
			dep->prev_dep->next_dep = dep->next_dep;

		if (dep->flags & DEPENDENT_HAS_DYNAMIC_DEPS)
			dependent_clear_dynamic_deps (dep);
	}

	if (dep->flags & DEPENDENT_HAS_3D)
		workbook_unlink_3d_dep (dep);
	dep->flags &= ~DEPENDENT_LINK_FLAGS;
}

/**
 * gnm_cell_eval_content:
 * @cell: the cell to evaluate.
 *
 * This function evaluates the contents of the cell,
 * it should not be used by anyone. It is an internal
 * function.
 **/
static gboolean
gnm_cell_eval_content (GnmCell *cell)
{
	static GnmCell *iterating = NULL;
	GnmValue   *v;
	GnmEvalPos	 pos;
	int	 max_iteration;

	if (!gnm_cell_has_expr (cell) ||	/* plain cells without expr */
	    !dependent_is_linked (&cell->base)) /* special case within TABLE */
		return TRUE;

#ifdef DEBUG_EVALUATION
	{
		GnmParsePos pp;
		char *str = gnm_expr_top_as_string
			(cell->base.texpr,
			 parse_pos_init_cell (&pp, cell),
			 NULL);
		g_printerr ("{\nEvaluating %s!%s: %s;\n",
			    cell->base.sheet->name_quoted, cell_name (cell),
			    str);
		g_free (str);
	}
#endif

	/* This is the bottom of a cycle */
	if (cell->base.flags & DEPENDENT_BEING_CALCULATED) {
		if (!cell->base.sheet->workbook->iteration.enabled)
			return TRUE;

		/* but not the first bottom */
		if (cell->base.flags & DEPENDENT_BEING_ITERATED) {
#ifdef DEBUG_EVALUATION
			g_printerr ("}; /* already-iterate (%d) */\n", iterating == NULL);
#endif
			return iterating == NULL;
		}

		/* if we are still marked as iterating then make this the last
		 * time through.
		 */
		if (iterating == cell) {
#ifdef DEBUG_EVALUATION
			puts ("}; /* NO-iterate (1) */");
#endif
			return TRUE;
		} else if (iterating == NULL) {
			cell->base.flags |= DEPENDENT_BEING_ITERATED;
			iterating = cell;
#ifdef DEBUG_EVALUATION
			puts ("}; /* START iterate = TRUE (0) */");
#endif
			return FALSE;
		} else {
#ifdef DEBUG_EVALUATION
			puts ("}; /* other-iterate (0) */");
#endif
			return FALSE;
		}
	}

	/* Prepare to calculate */
	eval_pos_init_cell (&pos, cell);
	cell->base.flags |= DEPENDENT_BEING_CALCULATED;

	/*
	 * I'm somewhat afraid of thinking about the semantics of iteration
	 * if different workbooks have different settings.
	 */
	max_iteration = cell->base.sheet->workbook->iteration.max_number;

iterate :
	v = gnm_expr_top_eval (cell->base.texpr, &pos,
			       GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	if (v == NULL)
		v = value_new_error (&pos, "Internal error");

#ifdef DEBUG_EVALUATION
	{
		char *valtxt = v
			? value_get_as_string (v)
			: g_strdup ("NULL");
		g_printerr ("Evaluation(%d) %s := %s\n",
			    max_iteration, cell_name (cell), valtxt);
		g_free (valtxt);
	}
#endif

	/* The top of a cycle */
	if (cell->base.flags & DEPENDENT_BEING_ITERATED) {
		cell->base.flags &= ~DEPENDENT_BEING_ITERATED;

		/* We just completed the last iteration, don't change things */
		if (iterating && max_iteration-- > 0) {
			/* If we are within bounds make this the last round */
			if (value_diff (cell->value, v) < cell->base.sheet->workbook->iteration.tolerance)
				max_iteration = 0;
			else {
#ifdef DEBUG_EVALUATION
				puts ("/* iterate == NULL */");
#endif
				iterating = NULL;
			}
			value_release (cell->value);
			cell->value = v;

			gnm_cell_unrender (cell);
#ifdef DEBUG_EVALUATION
			puts ("/* LOOP */");
#endif
			goto iterate;
		}
		g_return_val_if_fail (iterating, TRUE);
		iterating = NULL;
	} else {
		gboolean had_value = (cell->value != NULL);
		if (had_value && value_equal (v, cell->value)) {
			/* Value didn't change.  */
			value_release (v);
		} else {
			gboolean was_string = had_value && (VALUE_IS_STRING (cell->value) || VALUE_IS_ERROR (cell->value));
			gboolean is_string = VALUE_IS_STRING (v) || VALUE_IS_ERROR (v);

			if ((was_string || is_string))
				sheet_cell_queue_respan (cell);

			if (had_value)
				value_release (cell->value);
			cell->value = v;

			gnm_cell_unrender (cell);
		}
	}

	if (iterating == cell)
		iterating = NULL;

#ifdef DEBUG_EVALUATION
	g_printerr ("} (%d)\n", iterating == NULL);
#endif
	cell->base.flags &= ~DEPENDENT_BEING_CALCULATED;
	return iterating == NULL;
}

/**
 * dependent_eval :
 * @dep:
 */
static void
dependent_eval (GnmDependent *dep)
{
	int const t = dependent_type (dep);
	GnmDependentClass *klass = g_ptr_array_index (dep_classes, t);

	if (dep->flags & DEPENDENT_HAS_DYNAMIC_DEPS) {
		dependent_clear_dynamic_deps (dep);
		dep->flags &= ~DEPENDENT_HAS_DYNAMIC_DEPS;
	}

	/*
	 * Problem: this really should be a tail call.
	 */
	klass->eval (dep);

	/* Don't clear flag until after in case we iterate */
	dep->flags &= ~DEPENDENT_NEEDS_RECALC;
}

void
gnm_cell_eval (GnmCell *cell)
{
	if (gnm_cell_needs_recalc (cell)) {
		/*
		 * This should be a tail call so the stack frame can be
		 * eliminated before the call.
		 */
		dependent_eval (GNM_CELL_TO_DEP (cell));
	}
}


/**
 * cell_queue_recalc :
 * @cell:
 *
 * Queue the cell and everything that depends on it for recalculation.
 * If a dependency is already queued ignore it.
 */
void
cell_queue_recalc (GnmCell *cell)
{
	g_return_if_fail (cell != NULL);

	if (!gnm_cell_needs_recalc (cell)) {
		GSList *deps;

		if (gnm_cell_has_expr (cell))
			dependent_flag_recalc (GNM_CELL_TO_DEP (cell));

		deps = cell_list_deps (cell);
		dependent_queue_recalc_list (deps);
		g_slist_free (deps);
	}
}

typedef struct {
	int col, row;
	GnmDepFunc func;
	gpointer user;
} search_rangedeps_closure_t;

static void
cb_search_rangedeps (gpointer key, G_GNUC_UNUSED gpointer value,
		     gpointer closure)
{
	search_rangedeps_closure_t const *c = closure;
	DependencyRange const *deprange = key;
	GnmRange const *range = &(deprange->range);

#if 0
	/* When things get slow this is a good test to enable */
	static int counter = 0;
	if ((++counter % 100000) == 0)
		g_printerr ("%d\n", counter / 100000);
#endif

	if (range_contains (range, c->col, c->row)) {
		GnmDepFunc func = c->func;
		micro_hash_foreach_dep (deprange->deps, dep,
			(func) (dep, c->user););
	}
}

static void
cell_foreach_range_dep (GnmCell const *cell, GnmDepFunc func, gpointer user)
{
	search_rangedeps_closure_t closure;
	GHashTable *bucket =
		cell->base.sheet->deps->range_hash[BUCKET_OF_ROW (cell->pos.row)];

	if (bucket != NULL) {
		closure.col = cell->pos.col;
		closure.row = cell->pos.row;
		closure.func = func;
		closure.user = user;
		g_hash_table_foreach (bucket, &cb_search_rangedeps, &closure);
	}
}

static void
cell_foreach_single_dep (Sheet const *sheet, int col, int row,
			 GnmDepFunc func, gpointer user)
{
	DependencySingle lookup, *single;
	GnmDepContainer *deps = sheet->deps;

	lookup.pos.col = col;
	lookup.pos.row = row;

	single = g_hash_table_lookup (deps->single_hash, &lookup);
	if (single != NULL)
		micro_hash_foreach_dep (single->deps, dep,
			(*func) (dep, user););
}

/**
 * cell_foreach_dep:
 * @cell: #GnmCell
 * @func: (scope call):
 * @user: user data.
 *
 **/
void
cell_foreach_dep (GnmCell const *cell, GnmDepFunc func, gpointer user)
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
cb_recalc_all_depends (gpointer key, G_GNUC_UNUSED gpointer value,
		       G_GNUC_UNUSED gpointer ignore)
{
	DependencyAny const *depany = key;
	GSList *work = NULL;
	micro_hash_foreach_dep (depany->deps, dep, {
		if (!dependent_needs_recalc (dep)) {
			dependent_flag_recalc (dep);
			work = g_slist_prepend (work, dep);
		}
	});
	dependent_queue_recalc_main (work);
}



static void
cb_range_contained_depend (gpointer key, G_GNUC_UNUSED gpointer value,
			   gpointer user)
{
	DependencyRange const *deprange  = key;
	GnmRange const *range = &deprange->range;
	GnmRange const *target = user;

	if (range_overlap (target, range)) {
		GSList *work = NULL;
		micro_hash_foreach_dep (deprange->deps, dep, {
			if (!dependent_needs_recalc (dep)) {
				dependent_flag_recalc (dep);
				work = g_slist_prepend (work, dep);
			}
		});
		dependent_queue_recalc_main (work);
	}
}

static void
cb_single_contained_depend (gpointer key,
			    G_GNUC_UNUSED gpointer value,
			    gpointer user)
{
	DependencySingle const *depsingle  = key;
	GnmRange const *target = user;

	if (range_contains (target, depsingle->pos.col, depsingle->pos.row)) {
		GSList *work = NULL;
		micro_hash_foreach_dep (depsingle->deps, dep, {
			if (!dependent_needs_recalc (dep)) {
				dependent_flag_recalc (dep);
				work = g_slist_prepend (work, dep);
			}
		});
		dependent_queue_recalc_main (work);
	}
}

/**
 * sheet_region_queue_recalc:
 * @sheet: The sheet.
 * @range: Optionally NULL range.
 *
 * Queues things that depend on @sheet!@range for recalc.
 *
 * If @range is NULL the entire sheet is used.
 */
void
sheet_region_queue_recalc (Sheet const *sheet, GnmRange const *r)
{
	int i;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->deps != NULL);

	if (r == NULL) {
		/* mark the contained depends dirty non recursively */
		SHEET_FOREACH_DEPENDENT (sheet, dep,
			dependent_flag_recalc (dep););

		/* look for things that depend on the sheet */
		for (i = sheet->deps->buckets - 1; i >= 0 ; i--) {
			GHashTable *hash = sheet->deps->range_hash[i];
			if (hash != NULL)
				g_hash_table_foreach (hash,
					&cb_recalc_all_depends, NULL);
		}
		g_hash_table_foreach (sheet->deps->single_hash,
			&cb_recalc_all_depends, NULL);
	} else {
		int const first = BUCKET_OF_ROW (r->start.row);

		/* mark the contained depends dirty non recursively */
		SHEET_FOREACH_DEPENDENT (sheet, dep, {
			GnmCell *cell = GNM_DEP_TO_CELL (dep);
			if (dependent_is_cell (dep) &&
			    range_contains (r, cell->pos.col, cell->pos.row))
				dependent_flag_recalc (dep);
		});

		/* look for things that depend on target region */
		for (i = BUCKET_OF_ROW (r->end.row); i >= first ; i--) {
			GHashTable *hash = sheet->deps->range_hash[i];
			if (hash != NULL)
				g_hash_table_foreach (hash,
					&cb_range_contained_depend, (gpointer)r);
		}
		g_hash_table_foreach (sheet->deps->single_hash,
			&cb_single_contained_depend, (gpointer)r);
	}
}

typedef struct
{
	int dep_type;
	union {
		GnmParsePos   pos;
		GnmDependent *dep;
	} u;
	GnmExprTop const *oldtree;
} ExprRelocateStorage;

/**
 * dependents_unrelocate_free :
 * @info:
 *
 * Free the undo info associated with a dependent relocation.
 */
static void
dependents_unrelocate_free (GSList *info)
{
	GSList *ptr = info;
	for (; ptr != NULL ; ptr = ptr->next) {
		ExprRelocateStorage *tmp = ptr->data;
		gnm_expr_top_unref (tmp->oldtree);
		g_free (tmp);
	}
	g_slist_free (info);
}

/**
 * dependents_unrelocate :
 * @info:
 *
 * Apply the undo info associated with a dependent relocation.
 */
static void
dependents_unrelocate (GSList *info)
{
	GSList *ptr = info;
	for (; ptr != NULL ; ptr = ptr->next) {
		ExprRelocateStorage *tmp = ptr->data;

		if (tmp->dep_type == DEPENDENT_CELL) {
			if (!IS_SHEET (tmp->u.pos.sheet)) {
				/* FIXME : happens when undoing a move from a deleted
				 * sheet.  Do we want to do something here */
			} else {
				GnmCell *cell = sheet_cell_get
					(tmp->u.pos.sheet,
					 tmp->u.pos.eval.col, tmp->u.pos.eval.row);

				/* It is possible to have a NULL if the relocation info
				 * is not really relevant.  eg when undoing a pasted
				 * cut that was relocated but also saved to a buffer */
				if (cell != NULL) {
					GnmExprArrayCorner const *corner =
						gnm_expr_top_get_array_corner (tmp->oldtree);
					if (corner) {
						gnm_cell_set_array_formula (tmp->u.pos.sheet,
									    tmp->u.pos.eval.col,
									    tmp->u.pos.eval.row,
									    tmp->u.pos.eval.col + corner->cols - 1,
									    tmp->u.pos.eval.row + corner->rows - 1,
									    gnm_expr_top_new (gnm_expr_copy (corner->expr)));
						cell_queue_recalc (cell);
						sheet_flag_status_update_cell (cell);
					} else
						sheet_cell_set_expr (cell, tmp->oldtree);
				}
			}
		} else if (tmp->dep_type == DEPENDENT_NAME) {
		} else {
			dependent_set_expr (tmp->u.dep, tmp->oldtree);
			dependent_flag_recalc (tmp->u.dep);
			dependent_link (tmp->u.dep);
		}
	}
}

/**
 * dependents_link:
 * @deps: (element-type GnmDependent): An slist of dependents.
 *
 * link a list of dependents.  (The list used to get freed, but is not
 * freed anymore.)
 */
void
dependents_link (GSList *deps)
{
	GSList *ptr = deps;

	/* put them back */
	for (; ptr != NULL ; ptr = ptr->next) {
		GnmDependent *dep = ptr->data;
		if (dep->sheet->being_invalidated)
			continue;
		if (dep->sheet->deps != NULL && !dependent_is_linked (dep)) {
			dependent_link (dep);
			dependent_queue_recalc (dep);
		}
	}
}

typedef struct {
	GnmRange const *target;
	GSList *list;
} CollectClosure;

static void
cb_range_contained_collect (DependencyRange const *deprange,
			    G_GNUC_UNUSED gpointer ignored,
			    CollectClosure *user)
{
	GnmRange const *range = &deprange->range;

	if (range_overlap (user->target, range))
		micro_hash_foreach_dep (deprange->deps, dep, {
			if (!(dep->flags & (DEPENDENT_FLAGGED | DEPENDENT_CAN_RELOCATE)) &&
			    dependent_type (dep) != DEPENDENT_DYNAMIC_DEP) {
				dep->flags |= DEPENDENT_FLAGGED;
				user->list = g_slist_prepend (user->list, dep);
			}});
}

static void
cb_single_contained_collect (DependencySingle const *depsingle,
			     G_GNUC_UNUSED gpointer ignored,
			     CollectClosure *user)
{
	if (range_contains (user->target, depsingle->pos.col, depsingle->pos.row))
		micro_hash_foreach_dep (depsingle->deps, dep, {
			if (!(dep->flags & (DEPENDENT_FLAGGED | DEPENDENT_CAN_RELOCATE)) &&
			    dependent_type (dep) != DEPENDENT_DYNAMIC_DEP) {
				dep->flags |= DEPENDENT_FLAGGED;
				user->list = g_slist_prepend (user->list, dep);
			}});
}

static void
cb_collect_names (GnmNamedExpr *nexpr,
		  G_GNUC_UNUSED gpointer value,
		  GSList **l)
{
	*l = g_slist_prepend (*l, nexpr);
}

struct cb_remote_names {
	GSList *names;
	Workbook *wb;
};

static void
cb_remote_names1 (G_GNUC_UNUSED gconstpointer key,
		  GnmNamedExpr *nexpr,
		  struct cb_remote_names *data)
{
	data->names = g_slist_prepend (data->names, nexpr);
}

static void
cb_remote_names2 (GnmNamedExpr *nexpr,
		  G_GNUC_UNUSED gpointer value,
		  struct cb_remote_names *data)
{
	Workbook *wb =
		nexpr->pos.sheet ? nexpr->pos.sheet->workbook : nexpr->pos.wb;
	if (wb != data->wb)
		data->names = g_slist_prepend (data->names, nexpr);
}

/*
 * Get a list of all names that (may) reference data in a given sheet.
 * This is approximated as all names in the sheet, all global names in
 * its workbook, and all external references that actually mention the
 * sheet explicitly.
 */
static GSList *
names_referencing_sheet (Sheet *sheet)
{
	struct cb_remote_names data;

	data.names = NULL;
	data.wb = sheet->workbook;

	workbook_foreach_name (sheet->workbook, TRUE,
			       (GHFunc)cb_remote_names1,
			       &data);
	gnm_sheet_foreach_name (sheet, (GHFunc)cb_remote_names1, &data);

	if (sheet->deps->referencing_names)
		g_hash_table_foreach (sheet->deps->referencing_names,
				      (GHFunc)cb_remote_names2,
				      &data);

	return data.names;
}


/**
 * dependents_relocate:
 * @info: the descriptor record for what is being moved where.
 *
 * Fixes references to or from a region that is going to be moved.
 * Returns: (transfer full): a list of the locations and expressions that were changed outside of
 * the region.
 **/
GOUndo *
dependents_relocate (GnmExprRelocateInfo const *rinfo)
{
	GnmExprRelocateInfo local_rinfo;
	GSList    *l, *dependents = NULL, *undo_info = NULL;
	Sheet	  *sheet;
	GnmRange const   *r;
	int i;
	CollectClosure collect;
	GOUndo *u_exprs, *u_names;

	g_return_val_if_fail (rinfo != NULL, NULL);

	/* short circuit if nothing would move */
	if (rinfo->col_offset == 0 && rinfo->row_offset == 0 &&
	    rinfo->origin_sheet == rinfo->target_sheet)
		return NULL;

	sheet = rinfo->origin_sheet;
	r     = &rinfo->origin;

	/* collect contained cells with expressions */
	SHEET_FOREACH_DEPENDENT (rinfo->origin_sheet, dep, {
		GnmCell *cell = GNM_DEP_TO_CELL (dep);
		if (dependent_is_cell (dep) &&
		    range_contains (r, cell->pos.col, cell->pos.row)) {
			dependents = g_slist_prepend (dependents, dep);
			dep->flags |= DEPENDENT_FLAGGED;
		}
	});

	/* collect the things that depend on source region */
	collect.target = r;
	collect.list = dependents;
	g_hash_table_foreach (sheet->deps->single_hash,
		(GHFunc) &cb_single_contained_collect,
		(gpointer)&collect);
	{
		int const first = BUCKET_OF_ROW (r->start.row);
		GHashTable *hash;
		for (i = BUCKET_OF_ROW (r->end.row); i >= first ; i--) {
			hash = sheet->deps->range_hash[i];
			if (hash != NULL)
				g_hash_table_foreach (hash,
					(GHFunc) &cb_range_contained_collect,
					(gpointer)&collect);
		}
	}
	dependents = collect.list;
	local_rinfo = *rinfo;
	for (l = dependents; l; l = l->next) {
		GnmExprTop const *newtree;
		GnmDependent *dep = l->data;

		dep->flags &= ~DEPENDENT_FLAGGED;
		sheet_flag_status_update_range (dep->sheet, NULL);

		parse_pos_init_dep (&local_rinfo.pos, dep);

		/* it is possible nothing changed for contained deps
		 * using absolute references */
		newtree = gnm_expr_top_relocate (dep->texpr, &local_rinfo, FALSE);
		if (newtree != NULL) {
			int const t = dependent_type (dep);
			ExprRelocateStorage *tmp =
				g_new (ExprRelocateStorage, 1);

			tmp->dep_type = t;
			if (t == DEPENDENT_NAME) {
#warning "What should we do here and why do we leak tmp?"
			} else {
				if (t == DEPENDENT_CELL)
					tmp->u.pos = local_rinfo.pos;
				else
					tmp->u.dep = dep;
				tmp->oldtree = dep->texpr;
				gnm_expr_top_ref (tmp->oldtree);
				undo_info = g_slist_prepend (undo_info, tmp);

				dependent_set_expr (dep, newtree); /* unlinks */
				gnm_expr_top_unref (newtree);

				/* queue the things that depend on the changed dep
				 * even if it is going to move.
				 */
				dependent_queue_recalc (dep);

				/* relink if it is not going to move, if it is moving
				 * then the caller is responsible for relinking.
				 * This avoids a link/unlink/link tuple
				 */
				if (t == DEPENDENT_CELL) {
					GnmCellPos const *pos = &GNM_DEP_TO_CELL (dep)->pos;
					if (dep->sheet != sheet ||
					    !range_contains (r, pos->col, pos->row))
						dependent_link (dep);
				} else
					dependent_link (dep);
			}
		} else {
			/*
			 * The expression may not be changing, but it depends
			 * on something that is.  Not-corner array cells go here
			 * too
			 */
			dependent_queue_recalc (dep);
		}

		/* Not the most efficient, but probably not too bad.  It is
		 * definitely cheaper than finding the set of effected sheets. */
		sheet_flag_status_update_range (dep->sheet, NULL);
	}
	g_slist_free (dependents);

	u_exprs = go_undo_unary_new (undo_info,
				     (GOUndoUnaryFunc)dependents_unrelocate,
				     (GFreeFunc)dependents_unrelocate_free);

	u_names = NULL;

	switch (rinfo->reloc_type) {
	case GNM_EXPR_RELOCATE_INVALIDATE_SHEET:
	case GNM_EXPR_RELOCATE_MOVE_RANGE:
		break;

	case GNM_EXPR_RELOCATE_COLS:
	case GNM_EXPR_RELOCATE_ROWS: {
		GSList *l, *names = names_referencing_sheet (sheet);
		GnmExprRelocateInfo rinfo2 = *rinfo;

		for (l = names; l; l = l->next) {
			GnmNamedExpr *nexpr = l->data;
			GnmExprTop const *newtree;
			rinfo2.pos = nexpr->pos;
			newtree = gnm_expr_top_relocate (nexpr->texpr,
							 &rinfo2, TRUE);
			if (newtree) {
				GOUndo *u = expr_name_set_expr_undo_new (nexpr);
				u_names = go_undo_combine (u_names, u);
				expr_name_set_expr (nexpr, newtree);
			}
		}
		g_slist_free (names);
		break;

	default:
		g_assert_not_reached ();
	}
	}

	return go_undo_combine (u_exprs, u_names);
}

/*******************************************************************/

static gboolean
cb_collect_range (G_GNUC_UNUSED gpointer key,
		  DependencyAny *depany,
		  GSList **collector)
{
	*collector = g_slist_prepend (*collector, depany);
	return TRUE;
}

static void
dep_hash_destroy (GHashTable *hash, GSList **dyn_deps, Sheet *sheet)
{
	GSList *deps = NULL, *l;
	GnmExprRelocateInfo rinfo;
	GSList *deplist = NULL;
	gboolean destroy = (sheet->revive == NULL);

	/* We collect first because we will be changing the hash.  */
	if (destroy) {
		g_hash_table_foreach_remove (hash,
					     (GHRFunc)cb_collect_range,
					     &deps);
		g_hash_table_destroy (hash);
	} else {
		g_hash_table_foreach (hash, (GHFunc)cb_collect_range, &deps);
	}

	for (l = deps; l; l = l->next) {
		DependencyAny *depany = l->data;

		micro_hash_foreach_dep (depany->deps, dep, {
			if (dependent_type (dep) == DEPENDENT_DYNAMIC_DEP) {
				GnmDependent *c = ((DynamicDep *)dep)->container;
				if (!c->sheet->being_invalidated)
					*dyn_deps =
						g_slist_prepend (*dyn_deps, c);
			} else if (!dep->sheet->being_invalidated) {
				/*
				 * We collect here instead of doing right away as
				 * the dependent_set_expr below can change the
				 * container we are looping over.
				 */
				deplist = g_slist_prepend (deplist, dep);
			}
		});

		if (destroy)
			micro_hash_release (&depany->deps);
	}
	g_slist_free (deps);

	/*
	 * We do this after the above loop as this loop will
	 * invalidate some of the DependencyAny pointers from
	 * above.  The testcase for that is 314207, deleting
	 * all but the first sheet in one go.
	 */
	rinfo.reloc_type = GNM_EXPR_RELOCATE_INVALIDATE_SHEET;
	for (l = deplist; l; l = l->next) {
		GnmDependent *dep = l->data;
		GnmExprTop const *te = dep->texpr;
		/* We are told this dependent depends on this region, hence if
		 * newtree is null then either
		 * 1) we did not depend on it (ie., serious breakage )
		 * 2) we had a duplicate reference and we have already removed it.
		 * 3) We depended on things via a name which will be
		 *    invalidated elsewhere */
		GnmExprTop const *newtree = gnm_expr_top_relocate (te, &rinfo, FALSE);
		if (newtree != NULL) {
			if (sheet->revive)
				go_undo_group_add
					(sheet->revive,
					 gnm_dep_set_expr_undo_new (dep));
			dependent_set_expr (dep, newtree);
			gnm_expr_top_unref (newtree);
			dependent_link (dep);
		}
	}
	g_slist_free (deplist);
}

static void
cb_collect_deps_of_name (GnmDependent *dep, G_GNUC_UNUSED gpointer value,
			 GSList **accum)
{
	/* grab unflagged linked depends */
	if ((dep->flags & (DEPENDENT_FLAGGED|DEPENDENT_IS_LINKED)) == DEPENDENT_IS_LINKED) {
		dep->flags |= DEPENDENT_FLAGGED;
		*accum = g_slist_prepend (*accum, dep);
	}
}

struct cb_collect_deps_of_names {
	GSList *names;
	GSList *deps;
};

static void
cb_collect_deps_of_names (GnmNamedExpr *nexpr,
			  G_GNUC_UNUSED gpointer value,
			  struct cb_collect_deps_of_names *accum)
{
	accum->names = g_slist_prepend (accum->names, nexpr);
	if (nexpr->dependents)
		g_hash_table_foreach (nexpr->dependents,
				      (GHFunc)cb_collect_deps_of_name,
				      &accum->deps);
}

static void
invalidate_name (GnmNamedExpr *nexpr, Sheet *sheet)
{
	GnmExprTop const *old_expr = nexpr->texpr;
	GnmExprTop const *new_expr = NULL;
	gboolean scope_being_killed =
		nexpr->pos.sheet
		? nexpr->pos.sheet->being_invalidated
		: nexpr->pos.wb->during_destruction;

	if (!scope_being_killed) {
		GnmExprRelocateInfo rinfo;
		rinfo.reloc_type = GNM_EXPR_RELOCATE_INVALIDATE_SHEET;
		new_expr = gnm_expr_top_relocate (old_expr, &rinfo, FALSE);
		g_return_if_fail (new_expr != NULL);
	}

	if (nexpr->dependents && g_hash_table_size (nexpr->dependents))
		g_warning ("Left-over name dependencies\n");

	if (sheet->revive)
		go_undo_group_add (sheet->revive,
				   expr_name_set_expr_undo_new (nexpr));

	expr_name_set_expr (nexpr, new_expr);
}

static void
handle_dynamic_deps (GSList *dyn_deps)
{
	GSList *ptr;

	for (ptr = dyn_deps; ptr != NULL ; ptr = ptr->next) {
		GnmDependent *dep = ptr->data;
		if (dep->flags & DEPENDENT_HAS_DYNAMIC_DEPS) {
			dependent_clear_dynamic_deps (dep);
			dep->flags &= ~DEPENDENT_HAS_DYNAMIC_DEPS;
		}
	}
	dependent_queue_recalc_list (dyn_deps);
	g_slist_free (dyn_deps);
}

static void
handle_referencing_names (GnmDepContainer *deps, Sheet *sheet)
{
	GSList *ptr;
	GHashTable *names = deps->referencing_names;
	struct cb_collect_deps_of_names accum;
	gboolean destroy = (sheet->revive == NULL);

	if (!names)
		return;

	if (destroy)
		deps->referencing_names = NULL;

	accum.deps = NULL;
	accum.names = NULL;
	g_hash_table_foreach (names,
			      (GHFunc)cb_collect_deps_of_names,
			      (gpointer)&accum);

	for (ptr = accum.deps; ptr; ptr = ptr->next) {
		GnmDependent *dep = ptr->data;
		dep->flags &= ~DEPENDENT_FLAGGED;
		dependent_unlink (dep);
	}

	/* now that all of the dependents of these names are unlinked.
	 * change the references in the names to avoid this sheet */
	for (ptr = accum.names; ptr; ptr = ptr->next) {
		GnmNamedExpr *nexpr = ptr->data;
		invalidate_name (nexpr, sheet);
	}
	g_slist_free (accum.names);

	/* then relink things en-mass in case one of the deps outside
	 * this sheet used multiple names that referenced us */
	dependents_link (accum.deps);

	if (destroy) {
		g_slist_free (accum.deps);
		g_hash_table_destroy (names);
	} else {
		go_undo_group_add (sheet->revive,
				   gnm_dep_unlink_undo_new (accum.deps));
	}
}

static void
handle_outgoing_references (GnmDepContainer *deps, Sheet *sheet)
{
	GnmDependentFlags what = DEPENDENT_USES_NAME;
	GSList *accum = NULL;

	what |= (sheet->workbook && sheet->workbook->during_destruction)
		? DEPENDENT_GOES_INTERBOOK
		: DEPENDENT_GOES_INTERSHEET;
	DEPENDENT_CONTAINER_FOREACH_DEPENDENT (deps, dep, {
		if (dependent_is_linked (dep) && (dep->flags & what)) {
			dependent_unlink (dep);
			if (sheet->revive)
				accum = g_slist_prepend (accum, dep);
		}
	});

	if (accum)
		go_undo_group_add (sheet->revive,
				   gnm_dep_unlink_undo_new (accum));
}

/*
 * do_deps_destroy:
 * Invalidate references of all kinds to the sheet.
 *
 * Also destroy the dependency container.
 */
static void
do_deps_destroy (Sheet *sheet)
{
	GnmDepContainer *deps;
	GSList *dyn_deps = NULL;
	int i;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->being_invalidated);

	/* The GnmDepContainer (i.e., sheet->deps) contains the names that
	 * reference this, not the names it contains.  Remove the locally
	 * defined names here.
	 *
	 * NOTE : they may continue to exist inactively for a bit.  Be
	 * careful to remove them _before_ destroying the deps.  This is
	 * a bit wasteful in that we unlink and relink a few things that
	 * are going to be deleted.  However, it is necessary to catch
	 * all the different life cycles.
	 */
	gnm_named_expr_collection_free (sheet->names);
	sheet->names = NULL;

	deps = sheet->deps;
	if (deps == NULL)
		return;

	/* Destroy the records of what depends on this sheet.  There is no need
	 * to delicately remove individual items from the lists.  The only
	 * purpose that serves is to validate the state of our data structures.
	 * If required this optimization can be disabled for debugging.
	 */
	sheet->deps = NULL;
	if (sheet->revive) {
		g_object_unref (sheet->revive);
		sheet->revive = NULL;
	}

	for (i = deps->buckets - 1; i >= 0 ; i--) {
		GHashTable *hash = deps->range_hash[i];
		if (hash != NULL)
			dep_hash_destroy (hash, &dyn_deps, sheet);
	}
	dep_hash_destroy (deps->single_hash, &dyn_deps, sheet);

	g_free (deps->range_hash);
	deps->range_hash = NULL;
	/*
	 * Note: we have not freed the elements in the pool.  This call
	 * frees everything in one go.
	 */
	go_mem_chunk_destroy (deps->range_pool, TRUE);
	deps->range_pool = NULL;

	deps->single_hash = NULL;
	/*
	 * Note: we have not freed the elements in the pool.  This call
	 * frees everything in one go.
	 */
	go_mem_chunk_destroy (deps->single_pool, TRUE);
	deps->single_pool = NULL;

	/* Now that we have tossed all deps to this sheet we can queue the
	 * external dyn deps for recalc and free them */
	handle_dynamic_deps (dyn_deps);

	g_hash_table_destroy (deps->dynamic_deps);
	deps->dynamic_deps = NULL;

	handle_referencing_names (deps, sheet);

	/* Now we remove any links from dependents in this sheet to
	 * to other containers.  If the entire workbook is going away
	 * just look for inter-book links.
	 */
	handle_outgoing_references (deps, sheet);

	g_free (deps);
}

/*
 * do_deps_invalidate:
 * Invalidate references of all kinds to the sheet.
 */
static void
do_deps_invalidate (Sheet *sheet)
{
	GnmDepContainer *deps;
	GSList *dyn_deps = NULL;
	int i;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->being_invalidated);
	g_return_if_fail (sheet->revive == NULL);

	sheet->revive = go_undo_group_new ();

	gnm_named_expr_collection_unlink (sheet->names);

	deps = sheet->deps;

	for (i = deps->buckets - 1; i >= 0 ; i--) {
		GHashTable *hash = deps->range_hash[i];
		if (hash != NULL)
			dep_hash_destroy (hash, &dyn_deps, sheet);
	}
	dep_hash_destroy (deps->single_hash, &dyn_deps, sheet);

	/* Now that we have tossed all deps to this sheet we can queue the
	 * external dyn deps for recalc and free them */
	handle_dynamic_deps (dyn_deps);

	handle_referencing_names (deps, sheet);

	/* Now we remove any links from dependents in this sheet to
	 * to other containers.  If the entire workbook is going away
	 * just look for inter-book links.
	 */
	handle_outgoing_references (deps, sheet);
}

static void
cb_tweak_3d (GnmDependent *dep, G_GNUC_UNUSED gpointer value, GSList **deps)
{
	*deps = g_slist_prepend (*deps, dep);
}

static void
tweak_3d (Sheet *sheet)
{
	Workbook *wb = sheet->workbook;
	GSList *deps = NULL, *l;
	GnmExprRelocateInfo rinfo;

	if (!wb->sheet_order_dependents)
		return;

	g_hash_table_foreach (wb->sheet_order_dependents,
			      (GHFunc)cb_tweak_3d,
			      &deps);

	rinfo.reloc_type = GNM_EXPR_RELOCATE_INVALIDATE_SHEET;
	for (l = deps; l; l = l->next) {
		GnmDependent *dep = l->data;
		GnmExprTop const *te = dep->texpr;
		GnmExprTop const *newtree = gnm_expr_top_relocate (te, &rinfo, FALSE);

		if (newtree != NULL) {
			if (sheet->revive)
				go_undo_group_add
					(sheet->revive,
					 gnm_dep_set_expr_undo_new (dep));
			dependent_set_expr (dep, newtree);
			gnm_expr_top_unref (newtree);
			dependent_link (dep);
			dependent_changed (dep);
		}
	}
	g_slist_free (deps);
}

static void
dependents_invalidate_sheets (GSList *sheets, gboolean destroy)
{
	GSList *tmp;
	Workbook *last_wb;

	/* Mark all first.  */
	for (tmp = sheets; tmp; tmp = tmp->next) {
		Sheet *sheet = tmp->data;
		sheet->being_invalidated = TRUE;
	}

	/*
	 * Fixup 3d refs that start or end on one of these sheets.
	 * Ideally we do this one per workbook, but that is not critical
	 * so we are not going to outright sort the sheet list.
	 */
	last_wb = NULL;
	for (tmp = sheets; tmp; tmp = tmp->next) {
		Sheet *sheet = tmp->data;
		Workbook *wb = sheet->workbook;

		if (wb != last_wb)
			tweak_3d (sheet);
		last_wb = wb;
	}

	/* Now invalidate.  */
	for (tmp = sheets; tmp; tmp = tmp->next) {
		Sheet *sheet = tmp->data;
		if (destroy)
			do_deps_destroy (sheet);
		else
			do_deps_invalidate (sheet);
	}

	/* Unmark.  */
	for (tmp = sheets; tmp; tmp = tmp->next) {
		Sheet *sheet = tmp->data;
		sheet->being_invalidated = FALSE;
	}
}

void
dependents_invalidate_sheet (Sheet *sheet, gboolean destroy)
{
	GSList l;

	g_return_if_fail (IS_SHEET (sheet));

	l.next = NULL;
	l.data = sheet;
	dependents_invalidate_sheets (&l, destroy);
}

void
dependents_workbook_destroy (Workbook *wb)
{
	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (wb->during_destruction);
	g_return_if_fail (wb->sheets != NULL);

	/* Mark all first.  */
	WORKBOOK_FOREACH_SHEET (wb, sheet, sheet->being_invalidated = TRUE;);

	/* Kill 3d deps and workbook-level names, if needed.  */
	if (wb->sheet_order_dependents) {
		g_hash_table_destroy (wb->sheet_order_dependents);
		wb->sheet_order_dependents = NULL;
	}
	gnm_named_expr_collection_free (wb->names);
	wb->names = NULL;

	WORKBOOK_FOREACH_SHEET (wb, sheet, do_deps_destroy (sheet););

	/* Unmark.  */
	WORKBOOK_FOREACH_SHEET (wb, sheet, sheet->being_invalidated = FALSE;);
}

/*
 * dependents_revive_sheet:
 * Undo the effects of dependents_invalidate_sheet (sheet, FALSE).
 */
void
dependents_revive_sheet (Sheet *sheet)
{
	go_undo_undo (GO_UNDO (sheet->revive));
	g_object_unref (sheet->revive);
	sheet->revive = NULL;

	/* Re-link local names.  */
	gnm_named_expr_collection_relink (sheet->names);

	gnm_dep_container_sanity_check (sheet->deps);
}

void
workbook_queue_all_recalc (Workbook *wb)
{
	/* FIXME: what about dependents in other workbooks?  */
	WORKBOOK_FOREACH_DEPENDENT (wb, dep, dependent_flag_recalc (dep););
}

void
workbook_queue_volatile_recalc (Workbook *wb)
{
	WORKBOOK_FOREACH_DEPENDENT (wb, dep, {
		if (dependent_is_volatile (dep))	
			dependent_flag_recalc (dep);
	});
}


/**
 * workbook_recalc :
 * @wb:
 *
 * Computes all dependents in @wb that have been flaged as requiring
 * recomputation.
 *
 * NOTE! This does not recalc dependents in other workbooks.
 */
void
workbook_recalc (Workbook *wb)
{
	gboolean redraw = FALSE;

	g_return_if_fail (IS_WORKBOOK (wb));

	gnm_app_recalc_start ();

	WORKBOOK_FOREACH_DEPENDENT (wb, dep, {
		if (dependent_needs_recalc (dep)) {
			redraw = TRUE;
			dependent_eval (dep);
		}
	});

	gnm_app_recalc_finish ();

	/*
	 * This is a bit of a band-aid.  If anything is recalculated, we
	 * force a full redraw.  The alternative is to ask for updates
	 * of every cell that is changed and that is probably more
	 * expensive.
	 */
	if (redraw) {
		WORKBOOK_FOREACH_SHEET (wb, sheet, {
			SHEET_FOREACH_VIEW (sheet, sv, sv_flag_selection_change (sv););
			sheet_redraw_all (sheet, FALSE);});
	}
}

/**
 * workbook_recalc_all :
 * @wb:
 *
 * Queues all dependents for recalc and recalculates.
 */
void
workbook_recalc_all (Workbook *wb)
{
	workbook_queue_all_recalc (wb);

	/* Recalculate this workbook unconditionally.  */
	workbook_recalc (wb);

	/* Recalculate other workbooks when needed.  */
	gnm_app_recalc ();

	WORKBOOK_FOREACH_VIEW (wb, view,
		sheet_update (wb_view_cur_sheet (view)););
}

static void
dynamic_dep_free (DynamicDep *dyn)
{
	GnmDependent *dep = dyn->container;
	GnmCellPos const *pos = dependent_pos (dep);
	GnmRangeRef *rr;
	GSList *ptr;

	for (ptr = dyn->singles ; ptr != NULL ; ptr = ptr->next) {
		rr = ptr->data;
		unlink_single_dep (&dyn->base, pos, &rr->a);
		g_free (rr);
	}
	g_slist_free (dyn->singles);
	dyn->singles = NULL;

	for (ptr = dyn->ranges ; ptr != NULL ; ptr = ptr->next) {
		rr = ptr->data;
		link_unlink_cellrange_dep (&dyn->base, pos,
					   &rr->a, &rr->b, FALSE);
		g_free (rr);
	}
	g_slist_free (dyn->ranges);
	dyn->ranges = NULL;

	if (dyn->base.flags & DEPENDENT_HAS_3D)
		workbook_unlink_3d_dep (&dyn->base);
	g_free (dyn);
}


gboolean
dependent_is_volatile (GnmDependent *dep)
{
	/* This might need to be a virtual call. */
	return dep->texpr && gnm_expr_top_is_volatile (dep->texpr);
}

/**
 * gnm_dep_container_new: (skip)
 * @sheet: #Sheet
 *
 * Returns: (transfer full):
 **/
GnmDepContainer *
gnm_dep_container_new (Sheet *sheet)
{
	GnmDepContainer *deps = g_new (GnmDepContainer, 1);

	deps->head = deps->tail = NULL;

	deps->buckets = 1 + BUCKET_OF_ROW (gnm_sheet_get_last_row (sheet));
	deps->range_hash  = g_new0 (GHashTable *, deps->buckets);
	deps->range_pool  = go_mem_chunk_new ("range pool",
					       sizeof (DependencyRange),
					       16 * 1024 - 100);
	deps->single_hash = g_hash_table_new ((GHashFunc) depsingle_hash,
					      (GEqualFunc) depsingle_equal);
	deps->single_pool = go_mem_chunk_new ("single pool",
					       sizeof (DependencySingle),
					       16 * 1024 - 100);
	deps->referencing_names = g_hash_table_new (g_direct_hash,
						    g_direct_equal);

	deps->dynamic_deps = g_hash_table_new_full (g_direct_hash, g_direct_equal,
		NULL, (GDestroyNotify) dynamic_dep_free);

	return deps;
}

void
gnm_dep_container_resize (GnmDepContainer *deps, int rows)
{
	int i, buckets = 1 + BUCKET_OF_ROW (rows - 1);

	for (i = buckets; i < deps->buckets; i++) {
		GHashTable *hash = deps->range_hash[i];
		if (hash != NULL) {
			int s = g_hash_table_size (hash);
			if (s)
				g_printerr ("Hash table size: %d\n", s);
			g_hash_table_destroy (hash);
			deps->range_hash[i] = NULL;
		}
	}

	deps->range_hash = g_renew (GHashTable *, deps->range_hash, buckets);

	for (i = deps->buckets; i < buckets; i++)
		deps->range_hash[i] = NULL;

	deps->buckets = buckets;
}

/****************************************************************************
 * Debug utils
 */

static void
dependent_debug_name_for_sheet (GnmDependent const *dep, Sheet *sheet,
				GString *target)
{
	int t;
	GnmDependentClass *klass;

	g_return_if_fail (dep != NULL);
	g_return_if_fail (dep_classes);

	if (!dep->sheet)
		g_warning ("Invalid dep, missing sheet");

	if (sheet == dep->sheet) {
		/* Nothing */
	} else {
		g_string_append (target,
				 dep->sheet ? dep->sheet->name_quoted : "?");
		g_string_append_c (target, '!');
	}

	t = dependent_type (dep);
	klass = g_ptr_array_index (dep_classes, t);
	klass->debug_name (dep, target);
}


/**
 * dependent_debug_name :
 * @dep: The dependent we are interested in.
 *
 * A useful little debugging utility.
 */
void
dependent_debug_name (GnmDependent const *dep, GString *target)
{
	dependent_debug_name_for_sheet (dep, NULL, target);
}


static void
dump_range_dep (gpointer key, G_GNUC_UNUSED gpointer value, gpointer sheet_)
{
	DependencyRange const *deprange = key;
	GnmRange const *range = &(deprange->range);
	Sheet *sheet = sheet_;
	GString *target = g_string_sized_new (10000);
	gboolean first = TRUE;

	g_string_append (target, "    ");
	g_string_append (target, range_as_string (range));
	g_string_append (target, " -> (");

	micro_hash_foreach_dep (deprange->deps, dep, {
		if (first)
			first = FALSE;
		else
			g_string_append (target, ", ");
		dependent_debug_name_for_sheet (dep, sheet, target);
	});
	g_string_append_c (target, ')');

	g_printerr ("%s\n", target->str);
	g_string_free (target, TRUE);
}

static void
dump_single_dep (gpointer key, G_GNUC_UNUSED gpointer value, gpointer sheet_)
{
	DependencySingle *depsingle = key;
	Sheet *sheet = sheet_;
	GString *target = g_string_sized_new (10000);
	gboolean first = TRUE;

	g_string_append (target, "    ");
	g_string_append (target, cellpos_as_string (&depsingle->pos));
	g_string_append (target, " -> ");

	micro_hash_foreach_dep (depsingle->deps, dep, {
		if (first)
			first = FALSE;
		else
			g_string_append (target, ", ");
		dependent_debug_name_for_sheet (dep, sheet, target);
	});

	g_printerr ("%s\n", target->str);
	g_string_free (target, TRUE);
}

static void
dump_dynamic_dep (gpointer key, G_GNUC_UNUSED gpointer value,
		  G_GNUC_UNUSED gpointer closure)
{
	GnmDependent *dep = key;
	DynamicDep *dyn = value;
	GSList *l;
	GnmParsePos pp;
	GnmConventionsOut out;

	out.accum = g_string_new (NULL);
	out.pp    = &pp;
	out.convs = gnm_conventions_default;

	pp.wb    = dep->sheet->workbook;
	pp.sheet = dep->sheet;
	pp.eval  = *dependent_pos (dyn->container);

	g_string_append (out.accum, "    ");
	dependent_debug_name (dep, out.accum);
	g_string_append (out.accum, " -> ");
	dependent_debug_name (&dyn->base, out.accum);
	g_string_append (out.accum, " { c=");
	dependent_debug_name (dyn->container, out.accum);

	g_string_append (out.accum, ", s=[");
	for (l = dyn->singles; l; l = l->next) {
		rangeref_as_string (&out, l->data);
		if (l->next)
			g_string_append (out.accum, ", ");
	}

	g_string_append (out.accum, "], r=[");
	for (l = dyn->ranges; l; l = l->next) {
		rangeref_as_string (&out, l->data);
		if (l->next)
			g_string_append (out.accum, ", ");
	}

	g_string_append (out.accum, "] }");
	g_printerr ("%s\n", out.accum->str);
	g_string_free (out.accum, TRUE);
}

/**
 * gnm_dep_container_dump :
 * @deps:
 *
 * A useful utility for checking the state of the dependency data structures.
 */
void
gnm_dep_container_dump (GnmDepContainer const *deps,
			Sheet *sheet)
{
	int i;

	g_return_if_fail (deps != NULL);

	gnm_dep_container_sanity_check (deps);

	for (i = deps->buckets - 1; i >= 0 ; i--) {
		GHashTable *hash = deps->range_hash[i];
		if (hash != NULL && g_hash_table_size (hash) > 0) {
			g_printerr ("  Bucket %d (rows %d-%d): Range hash size %d: range over which cells in list depend\n",
				    i,
				    BUCKET_START_ROW (i) + 1,
				    BUCKET_END_ROW (i) + 1,
				    g_hash_table_size (hash));
			g_hash_table_foreach (hash,
					      dump_range_dep,
					      sheet);
		}
	}

	if (deps->single_hash && g_hash_table_size (deps->single_hash) > 0) {
		g_printerr ("  Single hash size %d: cell on which list of cells depend\n",
			    g_hash_table_size (deps->single_hash));
		g_hash_table_foreach (deps->single_hash,
				      dump_single_dep,
				      sheet);
	}

	if (deps->dynamic_deps && g_hash_table_size (deps->dynamic_deps) > 0) {
		g_printerr ("  Dynamic hash size %d: cells that depend on dynamic dependencies\n",
			    g_hash_table_size (deps->dynamic_deps));
		g_hash_table_foreach (deps->dynamic_deps,
				      dump_dynamic_dep, NULL);
	}

	if (deps->referencing_names && g_hash_table_size (deps->referencing_names) > 0) {
		GSList *l, *names = NULL;

		g_hash_table_foreach (deps->referencing_names,
				      (GHFunc)cb_collect_names,
				      &names);

		g_printerr ("  Names whose expressions explicitly reference this sheet\n    ");
		for (l = names; l; l = l->next) {
			GnmNamedExpr *nexpr = l->data;
			g_printerr ("%s%s",
				    expr_name_name (nexpr),
				    l->next ? ", " : "\n");
		}
		g_slist_free (names);
	}
}

void
dependents_dump (Workbook *wb)
{
	WORKBOOK_FOREACH_SHEET
		(wb, sheet,
		 {
			 g_printerr ("Dependencies for %s:\n", sheet->name_unquoted);
			 gnm_dep_container_dump (sheet->deps, sheet);
		 });
}

void
gnm_dep_container_sanity_check (GnmDepContainer const *deps)
{
	GnmDependent const *dep;
	GHashTable *seenb4;

	if (deps->head && !deps->tail)
		g_warning ("Dependency container %p has head, but no tail.", (void *)deps);
	if (deps->tail && !deps->head)
		g_warning ("Dependency container %p has tail, but no head.", (void *)deps);
	if (deps->head && deps->head->prev_dep)
		g_warning ("Dependency container %p has head, but not at the beginning.", (void *)deps);
	if (deps->tail && deps->tail->next_dep)
		g_warning ("Dependency container %p has tail, but not at the end.", (void *)deps);

	seenb4 = g_hash_table_new (g_direct_hash, g_direct_equal);
	for (dep = deps->head; dep; dep = dep->next_dep) {
		if (dep->prev_dep && (dep->prev_dep->next_dep != dep))
			g_warning ("Dependency container %p has left double-link failure at %p.", (void *)deps, (void *)dep);
		if (dep->next_dep && (dep->next_dep->prev_dep != dep))
			g_warning ("Dependency container %p has right double-link failure at %p.", (void *)deps, (void *)dep);
		if (!dep->next_dep && dep != deps->tail)
			g_warning ("Dependency container %p ends before its tail.", (void *)deps);
		if (!dependent_is_linked (dep))
			g_warning ("Dependency container %p contains unlinked dependency %p.", (void *)deps, (void *)dep);
		if (g_hash_table_lookup (seenb4, dep)) {
			g_warning ("Dependency container %p is circular.", (void *)deps);
			break;
		}
		g_hash_table_insert (seenb4, (gpointer)dep, (gpointer)dep);
	}
	g_hash_table_destroy (seenb4);
}

/* ------------------------------------------------------------------------- */
