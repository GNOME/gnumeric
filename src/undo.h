/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_UNDO_H_
#define _GNM_UNDO_H_

#include "gnumeric.h"
#include "sheet.h"
#include "colrow.h"
#include <goffice/goffice.h>
#include <glib-object.h>

G_BEGIN_DECLS

/* ------------------------------------------------------------------------- */

#define GNM_TYPE_UNDO_COLROW_RESTORE_STATE_GROUP  (gnm_undo_colrow_restore_state_group_get_type ())
#define GNM_UNDO_COLROW_RESTORE_STATE_GROUP(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_UNDO_COLROW_RESTORE_STATE_GROUP, GNMUndoColrowRestoreStateGroup))
#define GNM_IS_UNDO_COLROW_RESTORE_STATE_GROUP(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_UNDO_COLROW_RESTORE_STATE_GROUP))

GType gnm_undo_colrow_restore_state_group_get_type (void);

typedef struct _GNMUndoColrowRestoreStateGroup GNMUndoColrowRestoreStateGroup;
typedef struct _GNMUndoColrowRestoreStateGroupClass GNMUndoColrowRestoreStateGroupClass;

struct _GNMUndoColrowRestoreStateGroup {
	GOUndo base;
	Sheet *sheet;
	gboolean is_cols;
	ColRowIndexList *selection;
	ColRowStateGroup *saved_state;
};

struct _GNMUndoColrowRestoreStateGroupClass {
	GOUndoClass base;
};

GOUndo *gnm_undo_colrow_restore_state_group_new (Sheet *sheet, gboolean is_cols,
						  ColRowIndexList *selection,
						  ColRowStateGroup *saved_state);

/* ------------------------------------------------------------------------- */

#define GNM_TYPE_UNDO_COLROW_SET_SIZES  (gnm_undo_colrow_set_sizes_get_type ())
#define GNM_UNDO_COLROW_SET_SIZES(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_UNDO_COLROW_SET_SIZES, GNMUndoColrowSetSizes))
#define GNM_IS_UNDO_COLROW_SET_SIZES(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_UNDO_COLROW_SET_SIZES))

GType gnm_undo_colrow_set_sizes_get_type (void);

typedef struct _GNMUndoColrowSetSizes GNMUndoColrowSetSizes;
typedef struct _GNMUndoColrowSetSizesClass GNMUndoColrowSetSizesClass;

struct _GNMUndoColrowSetSizes {
	GOUndo base;
	Sheet *sheet;
	gboolean is_cols;
	ColRowIndexList *selection;
	int new_size;
};

struct _GNMUndoColrowSetSizesClass {
	GOUndoClass base;
};

GOUndo *gnm_undo_colrow_set_sizes_new (Sheet *sheet, gboolean is_cols,
				       ColRowIndexList *selection,
				       int new_size);

/* ------------------------------------------------------------------------- */


G_END_DECLS

#endif /* _GNM_UNDO_H_ */
