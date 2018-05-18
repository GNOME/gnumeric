#ifndef _GNM_GCONF_H_
# define _GNM_GCONF_H_

#include <gnumeric.h>
#include <gsf/gsf-output-csv.h>
#include <stf-export.h>

G_BEGIN_DECLS

void     gnm_conf_init (void);
void     gnm_conf_shutdown (void);
void     gnm_conf_set_persistence (gboolean persist);

GOConfNode *gnm_conf_get_root (void);

char const *gnm_conf_get_short_desc (GOConfNode *node);
char const *gnm_conf_get_long_desc (GOConfNode *node);

/* Convenience APIs */

GtkPageSetup *gnm_conf_get_page_setup (void);
void gnm_conf_set_page_setup (GtkPageSetup *setup);

GtkPrintSettings *gnm_conf_get_print_settings (void);
void gnm_conf_set_print_settings (GtkPrintSettings *settings);

GnmStyle *gnm_conf_get_printer_decoration_font (void);

gboolean gnm_conf_get_toolbar_visible (const char *name);
void gnm_conf_set_toolbar_visible (const char *name, gboolean x);

GtkPositionType gnm_conf_get_toolbar_position (const char *name);
void gnm_conf_set_toolbar_position (const char *name, GtkPositionType x);

gboolean gnm_conf_get_detachable_toolbars (void);

/* ----------- AUTOMATICALLY GENERATED CODE BELOW -- DO NOT EDIT ----------- */

GOConfNode *gnm_conf_get_autocorrect_first_letter_node (void);
gboolean gnm_conf_get_autocorrect_first_letter (void);
void gnm_conf_set_autocorrect_first_letter (gboolean x);

GOConfNode *gnm_conf_get_autocorrect_first_letter_list_node (void);
GSList *gnm_conf_get_autocorrect_first_letter_list (void);
void gnm_conf_set_autocorrect_first_letter_list (GSList *x);

GOConfNode *gnm_conf_get_autocorrect_init_caps_node (void);
gboolean gnm_conf_get_autocorrect_init_caps (void);
void gnm_conf_set_autocorrect_init_caps (gboolean x);

GOConfNode *gnm_conf_get_autocorrect_init_caps_list_node (void);
GSList *gnm_conf_get_autocorrect_init_caps_list (void);
void gnm_conf_set_autocorrect_init_caps_list (GSList *x);

GOConfNode *gnm_conf_get_autocorrect_names_of_days_node (void);
gboolean gnm_conf_get_autocorrect_names_of_days (void);
void gnm_conf_set_autocorrect_names_of_days (gboolean x);

GOConfNode *gnm_conf_get_autocorrect_replace_node (void);
gboolean gnm_conf_get_autocorrect_replace (void);
void gnm_conf_set_autocorrect_replace (gboolean x);

GOConfNode *gnm_conf_get_autoformat_extra_dirs_node (void);
GSList *gnm_conf_get_autoformat_extra_dirs (void);
void gnm_conf_set_autoformat_extra_dirs (GSList *x);

GOConfNode *gnm_conf_get_autoformat_sys_dir_node (void);
const char *gnm_conf_get_autoformat_sys_dir (void);
void gnm_conf_set_autoformat_sys_dir (const char *x);

GOConfNode *gnm_conf_get_autoformat_usr_dir_node (void);
const char *gnm_conf_get_autoformat_usr_dir (void);
void gnm_conf_set_autoformat_usr_dir (const char *x);

GOConfNode *gnm_conf_get_core_defaultfont_bold_node (void);
gboolean gnm_conf_get_core_defaultfont_bold (void);
void gnm_conf_set_core_defaultfont_bold (gboolean x);

GOConfNode *gnm_conf_get_core_defaultfont_italic_node (void);
gboolean gnm_conf_get_core_defaultfont_italic (void);
void gnm_conf_set_core_defaultfont_italic (gboolean x);

GOConfNode *gnm_conf_get_core_defaultfont_name_node (void);
const char *gnm_conf_get_core_defaultfont_name (void);
void gnm_conf_set_core_defaultfont_name (const char *x);

GOConfNode *gnm_conf_get_core_defaultfont_size_node (void);
double gnm_conf_get_core_defaultfont_size (void);
void gnm_conf_set_core_defaultfont_size (double x);

GOConfNode *gnm_conf_get_core_file_save_def_overwrite_node (void);
gboolean gnm_conf_get_core_file_save_def_overwrite (void);
void gnm_conf_set_core_file_save_def_overwrite (gboolean x);

GOConfNode *gnm_conf_get_core_file_save_extension_check_disabled_node (void);
GSList *gnm_conf_get_core_file_save_extension_check_disabled (void);
void gnm_conf_set_core_file_save_extension_check_disabled (GSList *x);

GOConfNode *gnm_conf_get_core_file_save_single_sheet_node (void);
gboolean gnm_conf_get_core_file_save_single_sheet (void);
void gnm_conf_set_core_file_save_single_sheet (gboolean x);

GOConfNode *gnm_conf_get_core_gui_cells_extension_markers_node (void);
gboolean gnm_conf_get_core_gui_cells_extension_markers (void);
void gnm_conf_set_core_gui_cells_extension_markers (gboolean x);

GOConfNode *gnm_conf_get_core_gui_cells_function_markers_node (void);
gboolean gnm_conf_get_core_gui_cells_function_markers (void);
void gnm_conf_set_core_gui_cells_function_markers (gboolean x);

GOConfNode *gnm_conf_get_core_gui_editing_autocomplete_node (void);
gboolean gnm_conf_get_core_gui_editing_autocomplete (void);
void gnm_conf_set_core_gui_editing_autocomplete (gboolean x);

GOConfNode *gnm_conf_get_core_gui_editing_autocomplete_min_chars_node (void);
int gnm_conf_get_core_gui_editing_autocomplete_min_chars (void);
void gnm_conf_set_core_gui_editing_autocomplete_min_chars (int x);

GOConfNode *gnm_conf_get_core_gui_editing_enter_moves_dir_node (void);
GODirection gnm_conf_get_core_gui_editing_enter_moves_dir (void);
void gnm_conf_set_core_gui_editing_enter_moves_dir (GODirection x);

GOConfNode *gnm_conf_get_core_gui_editing_function_argument_tooltips_node (void);
gboolean gnm_conf_get_core_gui_editing_function_argument_tooltips (void);
void gnm_conf_set_core_gui_editing_function_argument_tooltips (gboolean x);

GOConfNode *gnm_conf_get_core_gui_editing_function_name_tooltips_node (void);
gboolean gnm_conf_get_core_gui_editing_function_name_tooltips (void);
void gnm_conf_set_core_gui_editing_function_name_tooltips (gboolean x);

GOConfNode *gnm_conf_get_core_gui_editing_recalclag_node (void);
int gnm_conf_get_core_gui_editing_recalclag (void);
void gnm_conf_set_core_gui_editing_recalclag (int x);

GOConfNode *gnm_conf_get_core_gui_editing_transitionkeys_node (void);
gboolean gnm_conf_get_core_gui_editing_transitionkeys (void);
void gnm_conf_set_core_gui_editing_transitionkeys (gboolean x);

GOConfNode *gnm_conf_get_core_gui_screen_horizontaldpi_node (void);
double gnm_conf_get_core_gui_screen_horizontaldpi (void);
void gnm_conf_set_core_gui_screen_horizontaldpi (double x);

GOConfNode *gnm_conf_get_core_gui_screen_verticaldpi_node (void);
double gnm_conf_get_core_gui_screen_verticaldpi (void);
void gnm_conf_set_core_gui_screen_verticaldpi (double x);

GOConfNode *gnm_conf_get_core_gui_toolbars_format_position_node (void);
GtkPositionType gnm_conf_get_core_gui_toolbars_format_position (void);
void gnm_conf_set_core_gui_toolbars_format_position (GtkPositionType x);

GOConfNode *gnm_conf_get_core_gui_toolbars_format_visible_node (void);
gboolean gnm_conf_get_core_gui_toolbars_format_visible (void);
void gnm_conf_set_core_gui_toolbars_format_visible (gboolean x);

GOConfNode *gnm_conf_get_core_gui_toolbars_object_position_node (void);
GtkPositionType gnm_conf_get_core_gui_toolbars_object_position (void);
void gnm_conf_set_core_gui_toolbars_object_position (GtkPositionType x);

GOConfNode *gnm_conf_get_core_gui_toolbars_object_visible_node (void);
gboolean gnm_conf_get_core_gui_toolbars_object_visible (void);
void gnm_conf_set_core_gui_toolbars_object_visible (gboolean x);

GOConfNode *gnm_conf_get_core_gui_toolbars_standard_position_node (void);
GtkPositionType gnm_conf_get_core_gui_toolbars_standard_position (void);
void gnm_conf_set_core_gui_toolbars_standard_position (GtkPositionType x);

GOConfNode *gnm_conf_get_core_gui_toolbars_standard_visible_node (void);
gboolean gnm_conf_get_core_gui_toolbars_standard_visible (void);
void gnm_conf_set_core_gui_toolbars_standard_visible (gboolean x);

GOConfNode *gnm_conf_get_core_gui_window_x_node (void);
double gnm_conf_get_core_gui_window_x (void);
void gnm_conf_set_core_gui_window_x (double x);

GOConfNode *gnm_conf_get_core_gui_window_y_node (void);
double gnm_conf_get_core_gui_window_y (void);
void gnm_conf_set_core_gui_window_y (double x);

GOConfNode *gnm_conf_get_core_gui_window_zoom_node (void);
double gnm_conf_get_core_gui_window_zoom (void);
void gnm_conf_set_core_gui_window_zoom (double x);

GOConfNode *gnm_conf_get_core_sort_default_ascending_node (void);
gboolean gnm_conf_get_core_sort_default_ascending (void);
void gnm_conf_set_core_sort_default_ascending (gboolean x);

GOConfNode *gnm_conf_get_core_sort_default_by_case_node (void);
gboolean gnm_conf_get_core_sort_default_by_case (void);
void gnm_conf_set_core_sort_default_by_case (gboolean x);

GOConfNode *gnm_conf_get_core_sort_default_retain_formats_node (void);
gboolean gnm_conf_get_core_sort_default_retain_formats (void);
void gnm_conf_set_core_sort_default_retain_formats (gboolean x);

GOConfNode *gnm_conf_get_core_sort_dialog_max_initial_clauses_node (void);
int gnm_conf_get_core_sort_dialog_max_initial_clauses (void);
void gnm_conf_set_core_sort_dialog_max_initial_clauses (int x);

GOConfNode *gnm_conf_get_core_workbook_autosave_time_node (void);
int gnm_conf_get_core_workbook_autosave_time (void);
void gnm_conf_set_core_workbook_autosave_time (int x);

GOConfNode *gnm_conf_get_core_workbook_n_cols_node (void);
int gnm_conf_get_core_workbook_n_cols (void);
void gnm_conf_set_core_workbook_n_cols (int x);

GOConfNode *gnm_conf_get_core_workbook_n_rows_node (void);
int gnm_conf_get_core_workbook_n_rows (void);
void gnm_conf_set_core_workbook_n_rows (int x);

GOConfNode *gnm_conf_get_core_workbook_n_sheet_node (void);
int gnm_conf_get_core_workbook_n_sheet (void);
void gnm_conf_set_core_workbook_n_sheet (int x);

GOConfNode *gnm_conf_get_core_xml_compression_level_node (void);
int gnm_conf_get_core_xml_compression_level (void);
void gnm_conf_set_core_xml_compression_level (int x);

GOConfNode *gnm_conf_get_cut_and_paste_prefer_clipboard_node (void);
gboolean gnm_conf_get_cut_and_paste_prefer_clipboard (void);
void gnm_conf_set_cut_and_paste_prefer_clipboard (gboolean x);

GOConfNode *gnm_conf_get_dialogs_rs_unfocused_node (void);
gboolean gnm_conf_get_dialogs_rs_unfocused (void);
void gnm_conf_set_dialogs_rs_unfocused (gboolean x);

GOConfNode *gnm_conf_get_functionselector_num_of_recent_node (void);
int gnm_conf_get_functionselector_num_of_recent (void);
void gnm_conf_set_functionselector_num_of_recent (int x);

GOConfNode *gnm_conf_get_functionselector_recentfunctions_node (void);
GSList *gnm_conf_get_functionselector_recentfunctions (void);
void gnm_conf_set_functionselector_recentfunctions (GSList *x);

GOConfNode *gnm_conf_get_plugin_glpk_glpsol_path_node (void);
const char *gnm_conf_get_plugin_glpk_glpsol_path (void);
void gnm_conf_set_plugin_glpk_glpsol_path (const char *x);

GOConfNode *gnm_conf_get_plugin_latex_use_utf8_node (void);
gboolean gnm_conf_get_plugin_latex_use_utf8 (void);
void gnm_conf_set_plugin_latex_use_utf8 (gboolean x);

GOConfNode *gnm_conf_get_plugin_lpsolve_lpsolve_path_node (void);
const char *gnm_conf_get_plugin_lpsolve_lpsolve_path (void);
void gnm_conf_set_plugin_lpsolve_lpsolve_path (const char *x);

GOConfNode *gnm_conf_get_plugins_activate_newplugins_node (void);
gboolean gnm_conf_get_plugins_activate_newplugins (void);
void gnm_conf_set_plugins_activate_newplugins (gboolean x);

GOConfNode *gnm_conf_get_plugins_active_node (void);
GSList *gnm_conf_get_plugins_active (void);
void gnm_conf_set_plugins_active (GSList *x);

GOConfNode *gnm_conf_get_plugins_extra_dirs_node (void);
GSList *gnm_conf_get_plugins_extra_dirs (void);
void gnm_conf_set_plugins_extra_dirs (GSList *x);

GOConfNode *gnm_conf_get_plugins_file_states_node (void);
GSList *gnm_conf_get_plugins_file_states (void);
void gnm_conf_set_plugins_file_states (GSList *x);

GOConfNode *gnm_conf_get_plugins_known_node (void);
GSList *gnm_conf_get_plugins_known (void);
void gnm_conf_set_plugins_known (GSList *x);

GOConfNode *gnm_conf_get_printsetup_across_then_down_node (void);
gboolean gnm_conf_get_printsetup_across_then_down (void);
void gnm_conf_set_printsetup_across_then_down (gboolean x);

GOConfNode *gnm_conf_get_printsetup_all_sheets_node (void);
gboolean gnm_conf_get_printsetup_all_sheets (void);
void gnm_conf_set_printsetup_all_sheets (gboolean x);

GOConfNode *gnm_conf_get_printsetup_center_horizontally_node (void);
gboolean gnm_conf_get_printsetup_center_horizontally (void);
void gnm_conf_set_printsetup_center_horizontally (gboolean x);

GOConfNode *gnm_conf_get_printsetup_center_vertically_node (void);
gboolean gnm_conf_get_printsetup_center_vertically (void);
void gnm_conf_set_printsetup_center_vertically (gboolean x);

GOConfNode *gnm_conf_get_printsetup_footer_node (void);
GSList *gnm_conf_get_printsetup_footer (void);
void gnm_conf_set_printsetup_footer (GSList *x);

GOConfNode *gnm_conf_get_printsetup_gtk_setting_node (void);
GSList *gnm_conf_get_printsetup_gtk_setting (void);
void gnm_conf_set_printsetup_gtk_setting (GSList *x);

GOConfNode *gnm_conf_get_printsetup_header_node (void);
GSList *gnm_conf_get_printsetup_header (void);
void gnm_conf_set_printsetup_header (GSList *x);

GOConfNode *gnm_conf_get_printsetup_hf_font_bold_node (void);
gboolean gnm_conf_get_printsetup_hf_font_bold (void);
void gnm_conf_set_printsetup_hf_font_bold (gboolean x);

GOConfNode *gnm_conf_get_printsetup_hf_font_italic_node (void);
gboolean gnm_conf_get_printsetup_hf_font_italic (void);
void gnm_conf_set_printsetup_hf_font_italic (gboolean x);

GOConfNode *gnm_conf_get_printsetup_hf_font_name_node (void);
const char *gnm_conf_get_printsetup_hf_font_name (void);
void gnm_conf_set_printsetup_hf_font_name (const char *x);

GOConfNode *gnm_conf_get_printsetup_hf_font_size_node (void);
double gnm_conf_get_printsetup_hf_font_size (void);
void gnm_conf_set_printsetup_hf_font_size (double x);

GOConfNode *gnm_conf_get_printsetup_hf_left_node (void);
GSList *gnm_conf_get_printsetup_hf_left (void);
void gnm_conf_set_printsetup_hf_left (GSList *x);

GOConfNode *gnm_conf_get_printsetup_hf_middle_node (void);
GSList *gnm_conf_get_printsetup_hf_middle (void);
void gnm_conf_set_printsetup_hf_middle (GSList *x);

GOConfNode *gnm_conf_get_printsetup_hf_right_node (void);
GSList *gnm_conf_get_printsetup_hf_right (void);
void gnm_conf_set_printsetup_hf_right (GSList *x);

GOConfNode *gnm_conf_get_printsetup_margin_bottom_node (void);
double gnm_conf_get_printsetup_margin_bottom (void);
void gnm_conf_set_printsetup_margin_bottom (double x);

GOConfNode *gnm_conf_get_printsetup_margin_gtk_bottom_node (void);
double gnm_conf_get_printsetup_margin_gtk_bottom (void);
void gnm_conf_set_printsetup_margin_gtk_bottom (double x);

GOConfNode *gnm_conf_get_printsetup_margin_gtk_left_node (void);
double gnm_conf_get_printsetup_margin_gtk_left (void);
void gnm_conf_set_printsetup_margin_gtk_left (double x);

GOConfNode *gnm_conf_get_printsetup_margin_gtk_right_node (void);
double gnm_conf_get_printsetup_margin_gtk_right (void);
void gnm_conf_set_printsetup_margin_gtk_right (double x);

GOConfNode *gnm_conf_get_printsetup_margin_gtk_top_node (void);
double gnm_conf_get_printsetup_margin_gtk_top (void);
void gnm_conf_set_printsetup_margin_gtk_top (double x);

GOConfNode *gnm_conf_get_printsetup_margin_top_node (void);
double gnm_conf_get_printsetup_margin_top (void);
void gnm_conf_set_printsetup_margin_top (double x);

GOConfNode *gnm_conf_get_printsetup_paper_node (void);
const char *gnm_conf_get_printsetup_paper (void);
void gnm_conf_set_printsetup_paper (const char *x);

GOConfNode *gnm_conf_get_printsetup_paper_orientation_node (void);
int gnm_conf_get_printsetup_paper_orientation (void);
void gnm_conf_set_printsetup_paper_orientation (int x);

GOConfNode *gnm_conf_get_printsetup_preferred_unit_node (void);
GtkUnit gnm_conf_get_printsetup_preferred_unit (void);
void gnm_conf_set_printsetup_preferred_unit (GtkUnit x);

GOConfNode *gnm_conf_get_printsetup_print_black_n_white_node (void);
gboolean gnm_conf_get_printsetup_print_black_n_white (void);
void gnm_conf_set_printsetup_print_black_n_white (gboolean x);

GOConfNode *gnm_conf_get_printsetup_print_even_if_only_styles_node (void);
gboolean gnm_conf_get_printsetup_print_even_if_only_styles (void);
void gnm_conf_set_printsetup_print_even_if_only_styles (gboolean x);

GOConfNode *gnm_conf_get_printsetup_print_grid_lines_node (void);
gboolean gnm_conf_get_printsetup_print_grid_lines (void);
void gnm_conf_set_printsetup_print_grid_lines (gboolean x);

GOConfNode *gnm_conf_get_printsetup_print_titles_node (void);
gboolean gnm_conf_get_printsetup_print_titles (void);
void gnm_conf_set_printsetup_print_titles (gboolean x);

GOConfNode *gnm_conf_get_printsetup_repeat_left_node (void);
const char *gnm_conf_get_printsetup_repeat_left (void);
void gnm_conf_set_printsetup_repeat_left (const char *x);

GOConfNode *gnm_conf_get_printsetup_repeat_top_node (void);
const char *gnm_conf_get_printsetup_repeat_top (void);
void gnm_conf_set_printsetup_repeat_top (const char *x);

GOConfNode *gnm_conf_get_printsetup_scale_height_node (void);
int gnm_conf_get_printsetup_scale_height (void);
void gnm_conf_set_printsetup_scale_height (int x);

GOConfNode *gnm_conf_get_printsetup_scale_percentage_node (void);
gboolean gnm_conf_get_printsetup_scale_percentage (void);
void gnm_conf_set_printsetup_scale_percentage (gboolean x);

GOConfNode *gnm_conf_get_printsetup_scale_percentage_value_node (void);
double gnm_conf_get_printsetup_scale_percentage_value (void);
void gnm_conf_set_printsetup_scale_percentage_value (double x);

GOConfNode *gnm_conf_get_printsetup_scale_width_node (void);
int gnm_conf_get_printsetup_scale_width (void);
void gnm_conf_set_printsetup_scale_width (int x);

GOConfNode *gnm_conf_get_searchreplace_change_cell_expressions_node (void);
gboolean gnm_conf_get_searchreplace_change_cell_expressions (void);
void gnm_conf_set_searchreplace_change_cell_expressions (gboolean x);

GOConfNode *gnm_conf_get_searchreplace_change_cell_other_node (void);
gboolean gnm_conf_get_searchreplace_change_cell_other (void);
void gnm_conf_set_searchreplace_change_cell_other (gboolean x);

GOConfNode *gnm_conf_get_searchreplace_change_cell_strings_node (void);
gboolean gnm_conf_get_searchreplace_change_cell_strings (void);
void gnm_conf_set_searchreplace_change_cell_strings (gboolean x);

GOConfNode *gnm_conf_get_searchreplace_change_comments_node (void);
gboolean gnm_conf_get_searchreplace_change_comments (void);
void gnm_conf_set_searchreplace_change_comments (gboolean x);

GOConfNode *gnm_conf_get_searchreplace_columnmajor_node (void);
gboolean gnm_conf_get_searchreplace_columnmajor (void);
void gnm_conf_set_searchreplace_columnmajor (gboolean x);

GOConfNode *gnm_conf_get_searchreplace_error_behaviour_node (void);
int gnm_conf_get_searchreplace_error_behaviour (void);
void gnm_conf_set_searchreplace_error_behaviour (int x);

GOConfNode *gnm_conf_get_searchreplace_ignore_case_node (void);
gboolean gnm_conf_get_searchreplace_ignore_case (void);
void gnm_conf_set_searchreplace_ignore_case (gboolean x);

GOConfNode *gnm_conf_get_searchreplace_keep_strings_node (void);
gboolean gnm_conf_get_searchreplace_keep_strings (void);
void gnm_conf_set_searchreplace_keep_strings (gboolean x);

GOConfNode *gnm_conf_get_searchreplace_preserve_case_node (void);
gboolean gnm_conf_get_searchreplace_preserve_case (void);
void gnm_conf_set_searchreplace_preserve_case (gboolean x);

GOConfNode *gnm_conf_get_searchreplace_query_node (void);
gboolean gnm_conf_get_searchreplace_query (void);
void gnm_conf_set_searchreplace_query (gboolean x);

GOConfNode *gnm_conf_get_searchreplace_regex_node (void);
int gnm_conf_get_searchreplace_regex (void);
void gnm_conf_set_searchreplace_regex (int x);

GOConfNode *gnm_conf_get_searchreplace_scope_node (void);
int gnm_conf_get_searchreplace_scope (void);
void gnm_conf_set_searchreplace_scope (int x);

GOConfNode *gnm_conf_get_searchreplace_search_results_node (void);
gboolean gnm_conf_get_searchreplace_search_results (void);
void gnm_conf_set_searchreplace_search_results (gboolean x);

GOConfNode *gnm_conf_get_searchreplace_whole_words_only_node (void);
gboolean gnm_conf_get_searchreplace_whole_words_only (void);
void gnm_conf_set_searchreplace_whole_words_only (gboolean x);

GOConfNode *gnm_conf_get_stf_export_encoding_node (void);
const char *gnm_conf_get_stf_export_encoding (void);
void gnm_conf_set_stf_export_encoding (const char *x);

GOConfNode *gnm_conf_get_stf_export_format_node (void);
GnmStfFormatMode gnm_conf_get_stf_export_format (void);
void gnm_conf_set_stf_export_format (GnmStfFormatMode x);

GOConfNode *gnm_conf_get_stf_export_locale_node (void);
const char *gnm_conf_get_stf_export_locale (void);
void gnm_conf_set_stf_export_locale (const char *x);

GOConfNode *gnm_conf_get_stf_export_quoting_node (void);
GsfOutputCsvQuotingMode gnm_conf_get_stf_export_quoting (void);
void gnm_conf_set_stf_export_quoting (GsfOutputCsvQuotingMode x);

GOConfNode *gnm_conf_get_stf_export_separator_node (void);
const char *gnm_conf_get_stf_export_separator (void);
void gnm_conf_set_stf_export_separator (const char *x);

GOConfNode *gnm_conf_get_stf_export_stringindicator_node (void);
const char *gnm_conf_get_stf_export_stringindicator (void);
void gnm_conf_set_stf_export_stringindicator (const char *x);

GOConfNode *gnm_conf_get_stf_export_terminator_node (void);
const char *gnm_conf_get_stf_export_terminator (void);
void gnm_conf_set_stf_export_terminator (const char *x);

GOConfNode *gnm_conf_get_stf_export_transliteration_node (void);
gboolean gnm_conf_get_stf_export_transliteration (void);
void gnm_conf_set_stf_export_transliteration (gboolean x);

GtkToolbarStyle gnm_conf_get_toolbar_style (void);
void gnm_conf_set_toolbar_style (GtkToolbarStyle x);

GOConfNode *gnm_conf_get_undo_max_descriptor_width_node (void);
int gnm_conf_get_undo_max_descriptor_width (void);
void gnm_conf_set_undo_max_descriptor_width (int x);

GOConfNode *gnm_conf_get_undo_maxnum_node (void);
int gnm_conf_get_undo_maxnum (void);
void gnm_conf_set_undo_maxnum (int x);

GOConfNode *gnm_conf_get_undo_show_sheet_name_node (void);
gboolean gnm_conf_get_undo_show_sheet_name (void);
void gnm_conf_set_undo_show_sheet_name (gboolean x);

GOConfNode *gnm_conf_get_undo_size_node (void);
int gnm_conf_get_undo_size (void);
void gnm_conf_set_undo_size (int x);

GOConfNode *gnm_conf_get_autocorrect_dir_node (void);
GOConfNode *gnm_conf_get_autoformat_dir_node (void);
GOConfNode *gnm_conf_get_core_defaultfont_dir_node (void);
GOConfNode *gnm_conf_get_core_file_save_dir_node (void);
GOConfNode *gnm_conf_get_core_gui_cells_dir_node (void);
GOConfNode *gnm_conf_get_core_gui_editing_dir_node (void);
GOConfNode *gnm_conf_get_core_gui_screen_dir_node (void);
GOConfNode *gnm_conf_get_core_gui_toolbars_dir_node (void);
GOConfNode *gnm_conf_get_core_gui_window_dir_node (void);
GOConfNode *gnm_conf_get_core_sort_default_dir_node (void);
GOConfNode *gnm_conf_get_core_sort_dialog_dir_node (void);
GOConfNode *gnm_conf_get_core_workbook_dir_node (void);
GOConfNode *gnm_conf_get_core_xml_dir_node (void);
GOConfNode *gnm_conf_get_cut_and_paste_dir_node (void);
GOConfNode *gnm_conf_get_dialogs_rs_dir_node (void);
GOConfNode *gnm_conf_get_functionselector_dir_node (void);
GOConfNode *gnm_conf_get_plugin_glpk_dir_node (void);
GOConfNode *gnm_conf_get_plugin_latex_dir_node (void);
GOConfNode *gnm_conf_get_plugin_lpsolve_dir_node (void);
GOConfNode *gnm_conf_get_plugins_dir_node (void);
GOConfNode *gnm_conf_get_printsetup_dir_node (void);
GOConfNode *gnm_conf_get_searchreplace_dir_node (void);
GOConfNode *gnm_conf_get_stf_export_dir_node (void);
GOConfNode *gnm_conf_get_toolbar_style_dir_node (void);
GOConfNode *gnm_conf_get_undo_dir_node (void);

/* ----------- AUTOMATICALLY GENERATED CODE ABOVE -- DO NOT EDIT ----------- */

G_END_DECLS

#endif /* _GNM_GCONF_H_ */
