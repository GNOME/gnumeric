/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_SELECTION_H_
# define _GNM_SELECTION_H_

#include "gnumeric.h"
#include <goffice/graph/goffice-graph.h>

G_BEGIN_DECLS

typedef enum {
	COL_ROW_NO_SELECTION,
	COL_ROW_PARTIAL_SELECTION,
	COL_ROW_FULL_SELECTION
} ColRowSelectionType;

/* Selection information */
GnmCellPos const *sv_is_singleton_selected (SheetView const *sv);
gboolean sv_is_pos_selected         (SheetView const *sv, int col, int row);
gboolean sv_is_range_selected       (SheetView const *sv, GnmRange const *r);
gboolean sv_is_full_range_selected  (SheetView const *sv, GnmRange const *r);
gboolean sv_is_colrow_selected	    (SheetView const *sv,
				    int colrow, gboolean is_col);
gboolean sv_is_full_colrow_selected (SheetView const *sv,
				     gboolean is_cols, int col);
ColRowSelectionType sv_selection_col_type (SheetView const *sv, int col);
ColRowSelectionType sv_selection_row_type (SheetView const *sv, int row);

char		*selection_to_string   (SheetView *sv,
					gboolean include_sheet_name_prefix);
GnmRange const	*selection_first_range (SheetView const *sv,
				       GOCmdContext *cc, char const *cmd_name);
GSList		*selection_get_ranges  (SheetView const *sv,
					gboolean allow_intersection);

void	 sv_selection_to_plot	   (SheetView *sv, GogPlot *plot);

/* Selection management */
void	 sv_selection_reset	   (SheetView *sv);
void	 sv_selection_add_pos	   (SheetView *sv, int col, int row);
void	 sv_selection_add_range	   (SheetView *sv, GnmRange const *range);
void	 sv_selection_add_full	   (SheetView *sv,
				    int edit_col, int edit_row,
				    int base_col, int base_row,
				    int move_col, int move_row);
void	sv_selection_set	   (SheetView *sv, GnmCellPos const *edit,
				    int base_col, int base_row,
				    int move_col, int move_row);
void	sv_selection_extend_to	   (SheetView *sv, int col, int row);
void	sv_selection_free	   (SheetView *sv);

void	sv_selection_walk_step	   (SheetView *sv,
				    gboolean forward,
				    gboolean horizontal);

/* Utilities for operating on a selection */
typedef void	 (*SelectionApplyFunc)	(SheetView *sv, GnmRange const *,
					 gpointer user_data);
typedef gboolean (*GnmSelectionFunc)	(SheetView *sv, GnmRange const *r,
					 gpointer user_data);

void	 sv_selection_apply	 (SheetView *sv, SelectionApplyFunc const func,
				  gboolean allow_intersection,
				  gpointer user_data);
gboolean sv_selection_foreach	 (SheetView *sv,
				  GnmSelectionFunc handler,
				  gpointer user_data);

G_END_DECLS

#endif /* _GNM_SELECTION_H_ */
