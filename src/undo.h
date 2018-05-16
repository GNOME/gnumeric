#ifndef _GNM_UNDO_H_
#define _GNM_UNDO_H_

#include <gnumeric.h>
#include <sheet.h>
#include <colrow.h>
#include <sheet-filter.h>
#include <goffice/goffice.h>
#include <glib-object.h>

G_BEGIN_DECLS

/* ------------------------------------------------------------------------- */

#define GNM_TYPE_UNDO_COLROW_RESTORE_STATE_GROUP  (gnm_undo_colrow_restore_state_group_get_type ())
#define GNM_UNDO_COLROW_RESTORE_STATE_GROUP(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_UNDO_COLROW_RESTORE_STATE_GROUP, GnmUndoColrowRestoreStateGroup))
#define GNM_IS_UNDO_COLROW_RESTORE_STATE_GROUP(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_UNDO_COLROW_RESTORE_STATE_GROUP))

GType gnm_undo_colrow_restore_state_group_get_type (void);

typedef struct _GnmUndoColrowRestoreStateGroup GnmUndoColrowRestoreStateGroup;
typedef struct _GnmUndoColrowRestoreStateGroupClass GnmUndoColrowRestoreStateGroupClass;

struct _GnmUndoColrowRestoreStateGroup {
	GOUndo base;
	Sheet *sheet;
	gboolean is_cols;
	ColRowIndexList *selection;
	ColRowStateGroup *saved_state;
};

struct _GnmUndoColrowRestoreStateGroupClass {
	GOUndoClass base;
};

GOUndo *gnm_undo_colrow_restore_state_group_new (Sheet *sheet, gboolean is_cols,
						  ColRowIndexList *selection,
						  ColRowStateGroup *saved_state);

/* ------------------------------------------------------------------------- */

#define GNM_TYPE_UNDO_COLROW_SET_SIZES  (gnm_undo_colrow_set_sizes_get_type ())
#define GNM_UNDO_COLROW_SET_SIZES(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_UNDO_COLROW_SET_SIZES, GnmUndoColrowSetSizes))
#define GNM_IS_UNDO_COLROW_SET_SIZES(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_UNDO_COLROW_SET_SIZES))

GType gnm_undo_colrow_set_sizes_get_type (void);

typedef struct _GnmUndoColrowSetSizes GnmUndoColrowSetSizes;
typedef struct _GnmUndoColrowSetSizesClass GnmUndoColrowSetSizesClass;

struct _GnmUndoColrowSetSizes {
	GOUndo base;
	Sheet *sheet;
	gboolean is_cols;
	ColRowIndexList *selection;
	int new_size, from, to;
};

struct _GnmUndoColrowSetSizesClass {
	GOUndoClass base;
};

GOUndo *gnm_undo_colrow_set_sizes_new (Sheet *sheet, gboolean is_cols,
				       ColRowIndexList *selection,
				       int new_size, GnmRange const *r);

/* ------------------------------------------------------------------------- */

#define GNM_TYPE_UNDO_FILTER_SET_CONDITION  (gnm_undo_filter_set_condition_get_type ())
#define GNM_UNDO_FILTER_SET_CONDITION(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_UNDO_FILTER_SET_CONDITION, GnmUndoFilterSetCondition))
#define GNM_IS_UNDO_FILTER_SET_CONDITION(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_UNDO_FILTER_SET_CONDITION))

GType gnm_undo_filter_set_condition_get_type (void);

typedef struct _GnmUndoFilterSetCondition GnmUndoFilterSetCondition;
typedef struct _GnmUndoFilterSetConditionClass GnmUndoFilterSetConditionClass;

struct _GnmUndoFilterSetCondition {
	GOUndo base;
	GnmFilter *filter;
	unsigned i;
	GnmFilterCondition *cond;
};

struct _GnmUndoFilterSetConditionClass {
	GOUndoClass base;
};

GOUndo *gnm_undo_filter_set_condition_new (GnmFilter *filter, unsigned i,
					   GnmFilterCondition *cond,
					   gboolean retrieve_from_filter);

/* ------------------------------------------------------------------------- */


G_END_DECLS

#endif /* _GNM_UNDO_H_ */
