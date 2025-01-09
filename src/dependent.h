#ifndef _GNM_DEPENDENT_H_
# define _GNM_DEPENDENT_H_

#include <gnumeric.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

struct _GnmDependent {
	guint	  flags;
	Sheet	 *sheet;
	GnmExprTop const *texpr;

	/* Double-linked list.  */
	GnmDependent *next_dep, *prev_dep;
};

typedef struct {
	// Evaluate the dep
	void (*eval)	   (GnmDependent *dep);

	// Change the expression held by the dep.
	void (*set_expr)   (GnmDependent *dep, GnmExprTop const *new_texpr);

	// Perform actions required when dep changes.  Returns a list of
	// further deps that should be considered changed.
	GSList* (*changed) (GnmDependent *dep);

	// In what position in the sheet does the dep sit?  (Optional.)
	GnmCellPos* (*pos) (GnmDependent const *dep);

	// Append a name for debugging purposes
	void (*debug_name) (GnmDependent const *dep, GString *target);

	// Is evaluation supposed to happen in array context?
	gboolean q_array_context;
} GnmDependentClass;

typedef enum {
	DEPENDENT_NO_FLAG	   = 0,

	/* Types */
	DEPENDENT_CELL		   = 0x00000001,	/* builtin type */
	DEPENDENT_DYNAMIC_DEP	   = 0x00000002,	/* builtin type */
	DEPENDENT_NAME		   = 0x00000003,	/* builtin pseudo type */
	DEPENDENT_MANAGED	   = 0x00000004,	/* builtin type */
	DEPENDENT_TYPE_MASK	   = 0x00000fff,

	/* Linked into the workbook wide expression list */
	DEPENDENT_IS_LINKED	   = 0x00001000,
	DEPENDENT_NEEDS_RECALC	   = 0x00002000,
	DEPENDENT_BEING_CALCULATED = 0x00004000,
	/* GnmDependent is in the midst of a cyclic calculation */
	DEPENDENT_BEING_ITERATED   = 0x00008000,

	DEPENDENT_GOES_INTERSHEET  = 0x00010000,
	DEPENDENT_GOES_INTERBOOK   = 0x00020000,
	DEPENDENT_USES_NAME	   = 0x00040000,
	DEPENDENT_HAS_3D	   = 0x00080000,
	DEPENDENT_HAS_DYNAMIC_DEPS = 0x00200000,
	DEPENDENT_IGNORE_ARGS	   = 0x00400000,
	DEPENDENT_LINK_FLAGS	   = 0x007ff000,

	/* An internal utility flag */
	DEPENDENT_FLAGGED	   = 0x01000000,
	DEPENDENT_CAN_RELOCATE	   = 0x02000000
} GnmDependentFlags;

#define dependent_type(dep)		((dep)->flags & DEPENDENT_TYPE_MASK)
#define dependent_is_cell(dep)		(dependent_type (dep) == DEPENDENT_CELL)
#define dependent_needs_recalc(dep)	((dep)->flags & DEPENDENT_NEEDS_RECALC)
#define dependent_is_linked(dep)	((dep)->flags & DEPENDENT_IS_LINKED)

struct _GnmDepContainer {
	GnmDependent *head, *tail;

	/* Large ranges hashed on 'range' to accelerate duplicate culling. This
	 * is traversed by g_hash_table_foreach mostly.
	 */
	int buckets;
	GHashTable **range_hash;
	GOMemChunk *range_pool;

	/* Single ranges, this maps an GnmEvalPos * to a GSList of its
	 * dependencies.
	 */
	GHashTable *single_hash;
	GOMemChunk *single_pool;

	/* All of the ExprNames that refer to this container */
	GHashTable *referencing_names;

	/* Dynamic Deps */
	GHashTable *dynamic_deps;
};

typedef void (*GnmDepFunc) (GnmDependent *dep, gpointer user);

guint32	 dependent_type_register   (GnmDependentClass const *klass);
void	 dependent_types_init	   (void);
void	 dependent_types_shutdown  (void);

void	 dependent_set_expr	   (GnmDependent *dep, GnmExprTop const *new_texpr);
void	 dependent_set_sheet	   (GnmDependent *dep, Sheet *sheet);
void	 dependent_link		   (GnmDependent *dep);
void	 dependent_unlink	   (GnmDependent *dep);
void	 dependent_queue_recalc	   (GnmDependent *dep);
void	 dependent_add_dynamic_dep (GnmDependent *dep, GnmRangeRef const *rr);

gboolean dependent_is_volatile     (GnmDependent *dep);

gboolean dependent_has_pos (GnmDependent const *dep);
GnmCellPos const *dependent_pos (GnmDependent const *dep);
void dependent_move (GnmDependent *dep, int dx, int dy);

GOUndo  *dependents_relocate	    (GnmExprRelocateInfo const *info);
void	 dependents_link	    (GSList *deps);

void	 gnm_cell_eval		    (GnmCell *cell);
void	 cell_queue_recalc	    (GnmCell *cell);
void	 cell_foreach_dep	    (GnmCell const *cell, GnmDepFunc func, gpointer user);

void sheet_region_queue_recalc	  (Sheet const *sheet, GnmRange const *range);
void dependents_invalidate_sheet  (Sheet *sheet, gboolean destroy);
void dependents_workbook_destroy  (Workbook *wb);
void dependents_revive_sheet      (Sheet *sheet);
void workbook_queue_all_recalc	  (Workbook *wb);
void workbook_queue_volatile_recalc (Workbook *wb);

GnmDepContainer *gnm_dep_container_new  (Sheet *sheet);
void		 gnm_dep_container_dump	(GnmDepContainer const *deps,
					 Sheet *sheet);
void dependents_dump (Workbook *wb);
void             gnm_dep_container_sanity_check (GnmDepContainer const *deps);
void             gnm_dep_container_resize (GnmDepContainer *deps, int rows);

// ----------------------------------------------------------------------------

typedef struct {
	GnmDependent base;
} GnmDepManaged;

void dependent_managed_init (GnmDepManaged *dep, Sheet *sheet);
void dependent_managed_set_expr (GnmDepManaged *dep, GnmExprTop const *texpr);
GnmExprTop const *dependent_managed_get_expr (GnmDepManaged const *dep);
void dependent_managed_set_sheet (GnmDepManaged *dep, Sheet *sheet);

// ----------------------------------------------------------------------------

#define DEPENDENT_CONTAINER_FOREACH_DEPENDENT(dc, dep, code)	\
  do {								\
	GnmDependent *dep = (dc)->head;				\
	while (dep) {						\
		GnmDependent *_next = dep->next_dep;		\
		code;						\
		dep = _next;					\
	}							\
  } while (0)

#define DEPENDENT_MAKE_TYPE(t,...)					\
guint									\
t ## _get_dep_type (void)						\
{									\
	static guint32 type = 0;					\
	if (type == 0) {						\
		static GnmDependentClass klass = { __VA_ARGS__ };	\
		klass.eval	 = &t ## _eval;				\
		klass.debug_name = &t ## _debug_name;			\
		type = dependent_type_register (&klass);		\
	}								\
	return type;							\
}

void dependent_debug_name (GnmDependent const *dep, GString *target);

G_END_DECLS

#endif /* _GNM_DEPENDENT_H_ */
