#ifndef GNUMERIC_DIALOGS_H
#define GNUMERIC_DIALOGS_H

#include "gnumeric.h"
#include "workbook-control-gui.h"
#include "consolidate.h"
#include <goffice/graph/gog-style.h>

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

void	 dialog_formula_guru	(WorkbookControlGUI *wbcg, GnmFunc const *);
void	 dialog_plugin_manager	(WorkbookControlGUI *wbcg);
void	 dialog_goto_cell	(WorkbookControlGUI *wbcg);
void	 dialog_cell_format	(WorkbookControlGUI *wbcg,
				 FormatDialogPosition_t pageno);
void	 dialog_conditional_fmt	(WorkbookControlGUI *wbcg);
void	 dialog_paste_special	(WorkbookControlGUI *wbcg);
void	 dialog_insert_cells	(WorkbookControlGUI *wbcg);
void	 dialog_delete_cells	(WorkbookControlGUI *wbcg);
void	 dialog_zoom		(WorkbookControlGUI *wbcg, Sheet *sheet);
void	 dialog_about		(WorkbookControlGUI *wbcg);
void	 dialog_define_names	(WorkbookControlGUI *wbcg);
void	 dialog_paste_names	(WorkbookControlGUI *wbcg);
void	 dialog_cell_comment	(WorkbookControlGUI *wbcg,
				 Sheet *sheet, GnmCellPos const *pos);
void	 dialog_cell_sort	(WorkbookControlGUI *wbcg);
void	 dialog_workbook_attr	(WorkbookControlGUI *wbcg);
void	 dialog_goal_seek	(WorkbookControlGUI *wbcg, Sheet *sheet);
void	 dialog_solver		(WorkbookControlGUI *wbcg, Sheet *sheet);
void     dialog_simulation	(WorkbookControlGUI *wbcg, Sheet *sheet);
void	 dialog_printer_setup	(WorkbookControlGUI *wbcg, Sheet *sheet);
void     dialog_autocorrect	(WorkbookControlGUI *wbcg);
void	 dialog_advanced_filter	(WorkbookControlGUI *wbcg);
void	 dialog_shuffle	        (WorkbookControlGUI *wbcg);
void     dialog_scenario_add    (WorkbookControlGUI *wbcg);
void     dialog_scenarios       (WorkbookControlGUI *wbcg);
void     dialog_data_table	(WorkbookControlGUI *wbcg);
void	 dialog_auto_filter	(WorkbookControlGUI *wbcg, GnmFilter *f, int i,
				 gboolean is_expr, GnmFilterCondition *cur);
void	 dialog_autosave	(WorkbookControlGUI *wbcg);
gboolean dialog_autosave_prompt	(WorkbookControlGUI *wbcg);
void     dialog_autoformat	(WorkbookControlGUI *wbcg);
void     dialog_consolidate	(WorkbookControlGUI *wbcg);
void     dialog_pivottable	(WorkbookControlGUI *wbcg);
void     dialog_sheet_order	(WorkbookControlGUI *wbcg);
void     dialog_row_height	(WorkbookControlGUI *wbcg, gboolean set_default);
void	 dialog_fill_series     (WorkbookControlGUI *wbcg);
void     dialog_col_width	(WorkbookControlGUI *wbcg, gboolean set_default);
void     dialog_hyperlink	(WorkbookControlGUI *wbcg, SheetControl *sc);

typedef void (* ColRowCallback_t) (WorkbookControlGUI *wbcg, gboolean is_cols, gpointer data);
GtkWidget *dialog_col_row       (WorkbookControlGUI *wbcg, char const *operation,
				 ColRowCallback_t callback,
				 gpointer data);

typedef gboolean (*SearchDialogCallback) (WorkbookControlGUI *wbcg, GnmSearchReplace *sr);
void dialog_search_replace (WorkbookControlGUI *wbcg, SearchDialogCallback cb);
int dialog_search_replace_query (WorkbookControlGUI *wbcg,
				 GnmSearchReplace *sr,
				 char const *location,
				 char const *old_text,
				 char const *new_text);

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
char *dialog_get_password (GtkWindow *parent, char const *filename);

/* Modeless dialogs */
void	dialog_edit_meta_data  (WorkbookControlGUI *wbcg, gboolean open_dialogs);
void	dialog_preferences (WorkbookControlGUI *wbcg, gint page);
void    dialog_recent_used (WorkbookControlGUI *wbcg);

void	dialog_new_view (WorkbookControlGUI *wbcg);
void	dialog_so_styled (WorkbookControlGUI *wbcg, GObject *so,
			  GogStyle *orig, GogStyle *default_style,
			  char const *title);
void	dialog_so_list	 (WorkbookControlGUI *wbcg, GObject *so);

void	dialog_doc_metadata_new  (WorkbookControlGUI *wbcg);

void    dialog_quit (WorkbookControlGUI *wbcg);

#endif /* GNUMERIC_DIALOGS_H */
