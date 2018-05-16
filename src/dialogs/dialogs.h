#ifndef GNUMERIC_DIALOGS_H
#define GNUMERIC_DIALOGS_H

#include <gnumeric.h>
#include <consolidate.h>
#include <goffice/goffice.h>

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

void	 dialog_formula_guru	(WBCGtk *wbcg, GnmFunc *func);
void	 dialog_plugin_manager	(WBCGtk *wbcg);
void	 dialog_goto_cell	(WBCGtk *wbcg);
void	 dialog_cell_format	(WBCGtk *wbcg,
				 FormatDialogPosition_t pageno,
				 int pages);
GtkDialog *dialog_cell_format_select_style (WBCGtk *wbcg,
					    gint pages, GtkWindow *w,
					    GnmStyle *style,
					    gpointer closure);
void	 dialog_cell_format_cond (WBCGtk *wbcg);
void     dialog_cell_format_style_added (gpointer closure, GnmStyle *style);
void	 dialog_paste_special	(WBCGtk *wbcg);
void	 dialog_insert_cells	(WBCGtk *wbcg);
void	 dialog_delete_cells	(WBCGtk *wbcg);
void	 dialog_zoom		(WBCGtk *wbcg, Sheet *sheet);
void	 dialog_about		(WBCGtk *wbcg);
void	 dialog_define_names	(WBCGtk *wbcg);
void	 dialog_paste_names	(WBCGtk *wbcg);
void	 dialog_cell_comment	(WBCGtk *wbcg,
				 Sheet *sheet, GnmCellPos const *pos);
void	 dialog_cell_sort	(WBCGtk *wbcg);
void	 dialog_workbook_attr	(WBCGtk *wbcg);
void	 dialog_goal_seek	(WBCGtk *wbcg, Sheet *sheet);
void	 dialog_solver		(WBCGtk *wbcg, Sheet *sheet);
void     dialog_simulation	(WBCGtk *wbcg, Sheet *sheet);
void	 dialog_printer_setup	(WBCGtk *wbcg, Sheet *sheet);
void	 dialog_advanced_filter	(WBCGtk *wbcg);
void	 dialog_shuffle	        (WBCGtk *wbcg);
void     dialog_scenario_add    (WBCGtk *wbcg);
void     dialog_scenarios       (WBCGtk *wbcg);
void	 dialog_data_slicer	(WBCGtk *wbcg, gboolean create);
void     dialog_data_table	(WBCGtk *wbcg);
void	 dialog_auto_filter	(WBCGtk *wbcg, GnmFilter *f, int i,
				 gboolean is_expr, GnmFilterCondition *cur);
void	 dialog_autosave	(WBCGtk *wbcg);
gboolean dialog_autosave_prompt	(WBCGtk *wbcg);
void     dialog_autoformat	(WBCGtk *wbcg);
void     dialog_consolidate	(WBCGtk *wbcg);
void     dialog_sheet_compare   (WBCGtk *wbcg);
void     dialog_sheet_order	(WBCGtk *wbcg);
void     dialog_sheet_resize    (WBCGtk *wbcg);
void     dialog_sheet_rename    (WBCGtk *wbcg, Sheet *sheet);
void     dialog_row_height	(WBCGtk *wbcg, gboolean set_default);
void	 dialog_fill_series     (WBCGtk *wbcg);
void     dialog_col_width	(WBCGtk *wbcg, gboolean set_default);
void     dialog_hyperlink	(WBCGtk *wbcg, SheetControl *sc);

typedef void (* ColRowCallback_t) (WBCGtk *wbcg, gboolean is_cols, gpointer data);
void	 dialog_col_row        (WBCGtk *wbcg, char const *operation,
				ColRowCallback_t callback, gpointer data);

typedef gboolean (*SearchDialogCallback) (WBCGtk *wbcg, GnmSearchReplace *sr);
void dialog_search		 (WBCGtk *wbcg);
void dialog_search_replace	 (WBCGtk *wbcg, SearchDialogCallback cb);
int  dialog_search_replace_query (WBCGtk *wbcg,
				  GnmSearchReplace *sr,
				  char const *location,
				  char const *old_text,
				  char const *new_text);
void dialog_tabulate		 (WBCGtk *wbcg, Sheet *sheet);
void dialog_merge		 (WBCGtk *wbcg);

void dialog_function_select	 (WBCGtk *wbcg, char const *key);
void dialog_function_select_help	 (WBCGtk *wbcg);
void dialog_function_select_paste	 (WBCGtk *wbcg, gint from, gint to);

int dialog_correlation_tool	 (WBCGtk *wbcg, Sheet *sheet);
int dialog_covariance_tool	 (WBCGtk *wbcg, Sheet *sheet);
int dialog_descriptive_stat_tool (WBCGtk *wbcg, Sheet *sheet);
int dialog_sampling_tool	 (WBCGtk *wbcg, Sheet *sheet);
int dialog_ftest_tool		 (WBCGtk *wbcg, Sheet *sheet);
int dialog_regression_tool	 (WBCGtk *wbcg, Sheet *sheet);
int dialog_random_tool		 (WBCGtk *wbcg, Sheet *sheet);
int dialog_random_cor_tool	 (WBCGtk *wbcg, Sheet *sheet);
int dialog_average_tool		 (WBCGtk *wbcg, Sheet *sheet);
int dialog_exp_smoothing_tool	 (WBCGtk *wbcg, Sheet *sheet);
int dialog_fourier_tool		 (WBCGtk *wbcg, Sheet *sheet);
int dialog_ranking_tool		 (WBCGtk *wbcg, Sheet *sheet);
int dialog_anova_single_factor_tool (WBCGtk *wbcg, Sheet *sheet);
int dialog_anova_two_factor_tool (WBCGtk *wbcg, Sheet *sheet);
int dialog_histogram_tool	 (WBCGtk *wbcg, Sheet *sheet);
int dialog_frequency_tool	 (WBCGtk *wbcg, Sheet *sheet);
int dialog_kaplan_meier_tool	 (WBCGtk *wbcg, Sheet *sheet);
int dialog_normality_tool	 (WBCGtk *wbcg, Sheet *sheet);
int dialog_one_mean_test_tool    (WBCGtk *wbcg, Sheet *sheet);
int dialog_chi_square_tool       (WBCGtk *wbcg, Sheet *sheet, gboolean independence);
int dialog_principal_components_tool	 (WBCGtk *wbcg, Sheet *sheet);
int dialog_wilcoxon_m_w_tool	 (WBCGtk *wbcg, Sheet *sheet);

typedef enum {
	SIGNTEST,
	SIGNTEST_WILCOXON
} signtest_type;

int dialog_sign_test_tool        (WBCGtk *wbcg, Sheet *sheet,
				  signtest_type type);
int dialog_sign_test_two_tool    (WBCGtk *wbcg, Sheet *sheet,
				  signtest_type type);

typedef enum {
	TTEST_PAIRED = 1,
	TTEST_UNPAIRED_EQUALVARIANCES = 2,
	TTEST_UNPAIRED_UNEQUALVARIANCES = 3,
	TTEST_ZTEST = 4
} ttest_type;

int dialog_ttest_tool    (WBCGtk *wbcg, Sheet *sheet, ttest_type test);
char *dialog_get_password (GtkWindow *parent, char const *filename);

/* Modeless dialogs */
void	dialog_preferences (WBCGtk *wbcg, gchar const *page);
void    dialog_recent_used (WBCGtk *wbcg);

void	dialog_new_view (WBCGtk *wbcg);

typedef enum {
	SO_STYLED_STYLE_ONLY = 0,
	SO_STYLED_LINE = 1,
	SO_STYLED_TEXT = 2,
} so_styled_t;

void	dialog_so_styled (WBCGtk *wbcg, GObject *so, GOStyle *default_style,
			  char const *title, so_styled_t extent);
void	dialog_so_list	 (WBCGtk *wbcg, GObject *so);

void	dialog_doc_metadata_new  (WBCGtk *wbcg, int page);

void    dialog_quit (WBCGtk *wbcg);

void	dialog_so_size	 (WBCGtk *wbcg, GObject *so);


#endif /* GNUMERIC_DIALOGS_H */
