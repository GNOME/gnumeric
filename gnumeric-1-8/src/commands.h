/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_COMMANDS_H_
# define _GNM_COMMANDS_H_

#include "gnumeric.h"
#include "tools/tools.h"

G_BEGIN_DECLS

void command_undo   (WorkbookControl *wbc);
void command_redo   (WorkbookControl *wbc);
void command_repeat (WorkbookControl *wbc);
void command_setup_combos	(WorkbookControl *wbc);
void command_list_release	(GSList *cmds);

gboolean cmd_set_text		(WorkbookControl *wbc, Sheet *sheet,
				 GnmCellPos const *pos, char const *new_text,
				 PangoAttrList *markup);

gboolean cmd_area_set_text	(WorkbookControl *wbc, SheetView *sv,
				 char const *text, gboolean as_array);
gboolean cmd_create_data_table	(WorkbookControl *wbc,
				 Sheet *sheet, GnmRange const *r,
				 char const *col_input, char const *row_input);

gboolean cmd_insert_cols	(WorkbookControl *wbc, Sheet *sheet,
				 int start_col, int count);
gboolean cmd_insert_rows	(WorkbookControl *wbc, Sheet *sheet,
				 int start_row, int count);
gboolean cmd_delete_cols	(WorkbookControl *wbc, Sheet *sheet,
				 int start_col, int count);
gboolean cmd_delete_rows	(WorkbookControl *wbc, Sheet *sheet,
				 int start_row, int count);

gboolean cmd_resize_colrow	(WorkbookControl *wbc, Sheet *sheet,
				 gboolean is_col, ColRowIndexList *selection,
				 int new_size);

gboolean cmd_paste_cut		(WorkbookControl *wbc,
				 GnmExprRelocateInfo const *info,
				 gboolean move_selection,
				 char *cmd_descriptor);
gboolean cmd_paste_copy		(WorkbookControl *wbc,
				 GnmPasteTarget const *pt, GnmCellRegion *content);

gboolean cmd_sort		(WorkbookControl *wbc, GnmSortData *data);

gboolean cmd_autofill		(WorkbookControl *wbc, Sheet *sheet,
				 gboolean default_increment,
				 int base_col, int base_row,
				 int w, int h, int end_col, int end_row,
				 gboolean inverse_autofill);

gboolean cmd_copyrel		(WorkbookControl *wbc,
				 int dx, int dy,
				 char const *name);

/* currently these operate on the current sheet, and it calling control's
 * selection.  In the future we should pass in a virtualized selection.
 */
gboolean cmd_selection_format		(WorkbookControl *wbc,
					 GnmStyle *style, GnmBorder **borders,
					 char const *opt_translated_name);
gboolean cmd_selection_clear		(WorkbookControl *wbc, int clear_flags);
gboolean cmd_selection_colrow_hide	(WorkbookControl *wbc,
					 gboolean is_cols, gboolean visible);
gboolean cmd_selection_outline_change	(WorkbookControl *wbc,
					 gboolean is_cols, int index, int depth);
gboolean cmd_selection_group		(WorkbookControl *wbc,
					 gboolean is_cols, gboolean group);
gboolean cmd_selection_autoformat	(WorkbookControl *wbc, GnmFormatTemplate *ft);

/******************************************************************************************/

gboolean cmd_global_outline_change	(WorkbookControl *wbc, gboolean is_cols, int depth);

gboolean cmd_merge_cells	(WorkbookControl *wbc, Sheet *sheet,
				 GSList const *selection, gboolean center);
gboolean cmd_unmerge_cells	(WorkbookControl *wbc, Sheet *sheet,
				 GSList const *selection);

gboolean cmd_search_replace     (WorkbookControl *wbc, GnmSearchReplace *sr);

gboolean cmd_colrow_std_size    (WorkbookControl *wbc, Sheet *sheet,
				 gboolean is_cols, double new_default);

gboolean cmd_zoom               (WorkbookControl *wbc, GSList *sheets, double factor);

gboolean cmd_objects_delete	(WorkbookControl *wbc, GSList *objects,
				 char const *name);
gboolean cmd_objects_move	(WorkbookControl *wbc,
				 GSList *objects, GSList *anchors,
				 gboolean objects_created, char const *name);
gboolean cmd_object_format	(WorkbookControl *wbc, SheetObject *so,
				 gpointer orig_style);

gboolean cmd_reorganize_sheets  (WorkbookControl *wbc,
				 WorkbookSheetState *old_state,
				 Sheet *undo_sheet);

gboolean cmd_rename_sheet	(WorkbookControl *wbc, Sheet *sheet,
				 char const *new_name);

gboolean cmd_set_comment	(WorkbookControl *wbc, Sheet *sheet,
				 GnmCellPos const *pos, char const *new_text);

gboolean cmd_analysis_tool	(WorkbookControl *wbc, Sheet *sheet,
				 data_analysis_output_t *dao, gpointer specs,
				 analysis_tool_engine engine);

gboolean cmd_merge_data		(WorkbookControl *wbc, Sheet *sheet,
				 GnmValue *merge_zone, GSList *merge_fields, GSList *merge_data);

gboolean cmd_change_meta_data	(WorkbookControl *wbc, GSList *changes, GSList *removed);
gboolean cmd_print_setup	(WorkbookControl *wbc, Sheet *sheet, PrintInformation const *pi);
gboolean cmd_define_name	(WorkbookControl *wbc, char const *name,
				 GnmParsePos const *pp,
				 GnmExprTop const *texpr,
				 char const *descriptor);
gboolean cmd_remove_name        (WorkbookControl *wbc, GnmNamedExpr *nexpr);

typedef enum  {
	cmd_object_pull_to_front,
	cmd_object_pull_forward ,
	cmd_object_push_backward,
	cmd_object_push_to_back
} CmdObjectRaiseSelector;
gboolean cmd_object_raise (WorkbookControl *wbc, SheetObject *so, CmdObjectRaiseSelector dir);

/* FIXME: figure out how to resolve these better.  */
struct _scenario_t;
struct _scenario_cmd_t;
gboolean cmd_scenario_add (WorkbookControl *wbc, struct _scenario_t *s, Sheet *sheet);
gboolean cmd_scenario_mngr (WorkbookControl *wbc, struct _scenario_cmd_t *sc, Sheet *sheet);

/* FIXME: figure out how to resolve this better.  */
struct _data_shuffling_t;
gboolean cmd_data_shuffle (WorkbookControl *wbc, struct _data_shuffling_t *sc, Sheet *sheet);


gboolean cmd_text_to_columns (WorkbookControl *wbc,
			      GnmRange const *src, Sheet *src_sheet,
			      GnmRange const *target, Sheet *target_sheet,
			      GnmCellRegion *content);

gboolean cmd_solver (WorkbookControl *wbc,
		     GSList *cells, GSList *ov, GSList *nv);

gboolean cmd_goal_seek (WorkbookControl *wbc,
			GnmCell *cell, GnmValue *ov, GnmValue *nv);

gboolean cmd_tabulate (WorkbookControl *wbc, gpointer data);

gboolean cmd_so_graph_config (WorkbookControl *wbc, SheetObject *sog,
                              GObject *n_graph, GObject *o_graph);
gboolean cmd_toggle_rtl (WorkbookControl *wbc, Sheet *sheet);

gboolean cmd_so_set_value (WorkbookControl *wbc,
			   const char *text,
			   const GnmCellRef *pref,
			   GnmValue *new_val);

G_END_DECLS

#endif /* _GNM_COMMANDS_H_ */
