#ifndef GNUMERIC_DIALOGS_H
#define GNUMERIC_DIALOGS_H

#include "consolidate.h"
#include "gnumeric.h"
#include "summary.h"
#include "workbook-control-gui.h"

typedef enum {
	FD_CURRENT = -1,
	FD_NUMBER,
	FD_ALIGNMENT,
	FD_FONT,
	FD_BORDER,
	FD_BACKGROUND,
	FD_PROTECTION,
	FD_VALIDATION,
	FD_INPUT_MSG,
	FD_LAST = FD_INPUT_MSG
} FormatDialogPosition_t;

#ifdef ENABLE_BONOBO
void     dialog_graph_guru	(WorkbookControlGUI *wbcg, GnmGraph *graph, int page);
#endif
void	   dialog_formula_guru	(WorkbookControlGUI *wbcg, FunctionDefinition const *);
void	   dialog_plugin_manager  (WorkbookControlGUI *wbcg);
void	   dialog_goto_cell       (WorkbookControlGUI *wbcg);
GtkWidget* dialog_cell_number_fmt (WorkbookControlGUI *wbcg, Value *sample_val);
void	   dialog_cell_format     (WorkbookControlGUI *wbcg, Sheet *sheet,
				   FormatDialogPosition_t pageno);
int	   dialog_paste_special   (WorkbookControlGUI *wbcg);
void	   dialog_insert_cells    (WorkbookControlGUI *wbcg, Sheet *sheet);
void	   dialog_delete_cells    (WorkbookControlGUI *wbcg, Sheet *sheet);
void	   dialog_zoom            (WorkbookControlGUI *wbcg, Sheet *sheet);
void   	 dialog_about           (WorkbookControlGUI *wbcg);
void   	 dialog_define_names    (WorkbookControlGUI *wbcg);
void   	 dialog_cell_comment    (WorkbookControlGUI *wbcg,
				 Sheet *sheet, CellPos const *pos);
void   	 dialog_cell_sort       (WorkbookControlGUI *wbcg, Sheet *sheet);
void   	 dialog_workbook_attr   (WorkbookControlGUI *wbcg);
void   	 dialog_goal_seek       (WorkbookControlGUI *wbcg, Sheet *sheet);
void   	 dialog_solver          (WorkbookControlGUI *wbcg, Sheet *sheet);
void   	 dialog_printer_setup   (WorkbookControlGUI *wbcg, Sheet *sheet);
void     dialog_autocorrect     (WorkbookControlGUI *wbcg);
void   	 dialog_advanced_filter (WorkbookControlGUI *wbcg);
void   	 dialog_autosave        (WorkbookControlGUI *wbcg);
gboolean dialog_autosave_prompt (WorkbookControlGUI *wbcg);
void     dialog_autoformat      (WorkbookControlGUI *wbcg);
void     dialog_consolidate     (WorkbookControlGUI *wbcg, Sheet *sheet);
void     dialog_sheet_order     (WorkbookControlGUI *wbcg);
void     dialog_row_height      (WorkbookControlGUI *wbcg, gboolean set_default);
void     dialog_col_width       (WorkbookControlGUI *wbcg, gboolean set_default);
gboolean dialog_get_number      (WorkbookControlGUI *wbcg,
				 const char *glade_file,
				 double *init_and_return_value);
gboolean dialog_choose_cols_vs_rows (WorkbookControlGUI *wbcg, const char *title,
				     gboolean *is_cols);

typedef gboolean (*SearchDialogCallback) (WorkbookControlGUI *wbcg, SearchReplace *sr);
void dialog_search_replace (WorkbookControlGUI *wbcg, SearchDialogCallback cb);
int dialog_search_replace_query (WorkbookControlGUI *wbcg,
				 SearchReplace *sr,
				 const char *location,
				 const char *old_text,
				 const char *new_text);

void dialog_search (WorkbookControlGUI *wbcg);

void dialog_tabulate (WorkbookControlGUI *wbcg, Sheet *sheet);

void dialog_merge (WorkbookControlGUI *wbcg);

void dialog_function_select (WorkbookControlGUI *wbcg, char const *key);

int dialog_correlation_tool (WorkbookControlGUI *wbcg, Sheet *sheet);
int dialog_covariance_tool (WorkbookControlGUI *wbcg, Sheet *sheet);
int dialog_descriptive_stat_tool (WorkbookControlGUI *wbcg, Sheet *sheet);
int dialog_sampling_tool  (WorkbookControlGUI *wbcg, Sheet *sheet);
int dialog_ftest_tool     (WorkbookControlGUI *wbcg, Sheet *sheet);
int dialog_regression_tool (WorkbookControlGUI *wbcg, Sheet *sheet);
int dialog_random_tool    (WorkbookControlGUI *wbcg, Sheet *sheet);
int dialog_average_tool   (WorkbookControlGUI *wbcg, Sheet *sheet);
int dialog_exp_smoothing_tool (WorkbookControlGUI *wbcg, Sheet *sheet);
int dialog_fourier_tool   (WorkbookControlGUI *wbcg, Sheet *sheet);
int dialog_ranking_tool   (WorkbookControlGUI *wbcg, Sheet *sheet);
int dialog_anova_single_factor_tool (WorkbookControlGUI *wbcg, Sheet *sheet);
int dialog_anova_two_factor_tool (WorkbookControlGUI *wbcg, Sheet *sheet);
int dialog_histogram_tool (WorkbookControlGUI *wbcg, Sheet *sheet);

typedef enum {
	TTEST_PAIRED = 1,
	TTEST_UNPAIRED_EQUALVARIANCES = 2,
	TTEST_UNPAIRED_UNEQUALVARIANCES = 3,
	TTEST_ZTEST = 4
} ttest_type;

int dialog_ttest_tool    (WorkbookControlGUI *wbcg, Sheet *sheet, ttest_type test);

/* Modeless dialogs */
void   	 dialog_summary_update  (WorkbookControlGUI *wbcg, gboolean open_dialogs);
void     dialog_preferences (gint page);


#endif /* GNUMERIC_DIALOGS_H */
