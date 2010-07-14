/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_GCONF_H_
# define _GNM_GCONF_H_

#include "gnumeric.h"

G_BEGIN_DECLS

void     gnm_conf_init (void);
void     gnm_conf_shutdown (void);
GOConfNode *gnm_conf_get_root (void);

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

GtkToolbarStyle gnm_conf_get_toolbar_style (void);
void gnm_conf_set_toolbar_style (GtkToolbarStyle);

GOConfNode *gnm_conf_get_autocorrect_first_letter_node (void);
gboolean gnm_conf_get_autocorrect_first_letter (void);
void gnm_conf_set_autocorrect_first_letter (gboolean);

GOConfNode *gnm_conf_get_autocorrect_first_letter_list_node (void);
GSList *gnm_conf_get_autocorrect_first_letter_list (void);
void gnm_conf_set_autocorrect_first_letter_list (GSList *);

GOConfNode *gnm_conf_get_autocorrect_init_caps_node (void);
gboolean gnm_conf_get_autocorrect_init_caps (void);
void gnm_conf_set_autocorrect_init_caps (gboolean);

GOConfNode *gnm_conf_get_autocorrect_init_caps_list_node (void);
GSList *gnm_conf_get_autocorrect_init_caps_list (void);
void gnm_conf_set_autocorrect_init_caps_list (GSList *);

GOConfNode *gnm_conf_get_autocorrect_names_of_days_node (void);
gboolean gnm_conf_get_autocorrect_names_of_days (void);
void gnm_conf_set_autocorrect_names_of_days (gboolean);

GOConfNode *gnm_conf_get_autocorrect_replace_node (void);
gboolean gnm_conf_get_autocorrect_replace (void);
void gnm_conf_set_autocorrect_replace (gboolean);

GOConfNode *gnm_conf_get_autoformat_extra_dirs_node (void);
GSList *gnm_conf_get_autoformat_extra_dirs (void);
void gnm_conf_set_autoformat_extra_dirs (GSList *);

GOConfNode *gnm_conf_get_autoformat_sys_dir_node (void);
const char *gnm_conf_get_autoformat_sys_dir (void);
void gnm_conf_set_autoformat_sys_dir (const char *);

GOConfNode *gnm_conf_get_autoformat_usr_dir_node (void);
const char *gnm_conf_get_autoformat_usr_dir (void);
void gnm_conf_set_autoformat_usr_dir (const char *);

GOConfNode *gnm_conf_get_core_defaultfont_bold_node (void);
gboolean gnm_conf_get_core_defaultfont_bold (void);
void gnm_conf_set_core_defaultfont_bold (gboolean);

GOConfNode *gnm_conf_get_core_defaultfont_italic_node (void);
gboolean gnm_conf_get_core_defaultfont_italic (void);
void gnm_conf_set_core_defaultfont_italic (gboolean);

GOConfNode *gnm_conf_get_core_defaultfont_name_node (void);
const char *gnm_conf_get_core_defaultfont_name (void);
void gnm_conf_set_core_defaultfont_name (const char *);

GOConfNode *gnm_conf_get_core_defaultfont_size_node (void);
double gnm_conf_get_core_defaultfont_size (void);
void gnm_conf_set_core_defaultfont_size (double);

GOConfNode *gnm_conf_get_core_file_save_def_overwrite_node (void);
gboolean gnm_conf_get_core_file_save_def_overwrite (void);
void gnm_conf_set_core_file_save_def_overwrite (gboolean);

GOConfNode *gnm_conf_get_core_file_save_extension_check_disabled_node (void);
GSList *gnm_conf_get_core_file_save_extension_check_disabled (void);
void gnm_conf_set_core_file_save_extension_check_disabled (GSList *);

GOConfNode *gnm_conf_get_core_file_save_single_sheet_node (void);
gboolean gnm_conf_get_core_file_save_single_sheet (void);
void gnm_conf_set_core_file_save_single_sheet (gboolean);

GOConfNode *gnm_conf_get_core_gui_editing_autocomplete_node (void);
gboolean gnm_conf_get_core_gui_editing_autocomplete (void);
void gnm_conf_set_core_gui_editing_autocomplete (gboolean);

GOConfNode *gnm_conf_get_core_gui_editing_enter_moves_dir_node (void);
GODirection gnm_conf_get_core_gui_editing_enter_moves_dir (void);
void gnm_conf_set_core_gui_editing_enter_moves_dir (GODirection);

GOConfNode *gnm_conf_get_core_gui_editing_livescrolling_node (void);
gboolean gnm_conf_get_core_gui_editing_livescrolling (void);
void gnm_conf_set_core_gui_editing_livescrolling (gboolean);

GOConfNode *gnm_conf_get_core_gui_editing_recalclag_node (void);
int gnm_conf_get_core_gui_editing_recalclag (void);
void gnm_conf_set_core_gui_editing_recalclag (int);

GOConfNode *gnm_conf_get_core_gui_editing_transitionkeys_node (void);
gboolean gnm_conf_get_core_gui_editing_transitionkeys (void);
void gnm_conf_set_core_gui_editing_transitionkeys (gboolean);

GOConfNode *gnm_conf_get_core_gui_screen_horizontaldpi_node (void);
double gnm_conf_get_core_gui_screen_horizontaldpi (void);
void gnm_conf_set_core_gui_screen_horizontaldpi (double);

GOConfNode *gnm_conf_get_core_gui_screen_verticaldpi_node (void);
double gnm_conf_get_core_gui_screen_verticaldpi (void);
void gnm_conf_set_core_gui_screen_verticaldpi (double);

GOConfNode *gnm_conf_get_core_gui_toolbars_FormatToolbar_node (void);
gboolean gnm_conf_get_core_gui_toolbars_FormatToolbar (void);
void gnm_conf_set_core_gui_toolbars_FormatToolbar (gboolean);

GOConfNode *gnm_conf_get_core_gui_toolbars_FormatToolbar_position_node (void);
GtkPositionType gnm_conf_get_core_gui_toolbars_FormatToolbar_position (void);
void gnm_conf_set_core_gui_toolbars_FormatToolbar_position (GtkPositionType);

GOConfNode *gnm_conf_get_core_gui_toolbars_LongFormatToolbar_node (void);
gboolean gnm_conf_get_core_gui_toolbars_LongFormatToolbar (void);
void gnm_conf_set_core_gui_toolbars_LongFormatToolbar (gboolean);

GOConfNode *gnm_conf_get_core_gui_toolbars_LongFormatToolbar_position_node (void);
GtkPositionType gnm_conf_get_core_gui_toolbars_LongFormatToolbar_position (void);
void gnm_conf_set_core_gui_toolbars_LongFormatToolbar_position (GtkPositionType);

GOConfNode *gnm_conf_get_core_gui_toolbars_ObjectToolbar_node (void);
gboolean gnm_conf_get_core_gui_toolbars_ObjectToolbar (void);
void gnm_conf_set_core_gui_toolbars_ObjectToolbar (gboolean);

GOConfNode *gnm_conf_get_core_gui_toolbars_ObjectToolbar_position_node (void);
GtkPositionType gnm_conf_get_core_gui_toolbars_ObjectToolbar_position (void);
void gnm_conf_set_core_gui_toolbars_ObjectToolbar_position (GtkPositionType);

GOConfNode *gnm_conf_get_core_gui_toolbars_StandardToolbar_node (void);
gboolean gnm_conf_get_core_gui_toolbars_StandardToolbar (void);
void gnm_conf_set_core_gui_toolbars_StandardToolbar (gboolean);

GOConfNode *gnm_conf_get_core_gui_toolbars_StandardToolbar_position_node (void);
GtkPositionType gnm_conf_get_core_gui_toolbars_StandardToolbar_position (void);
void gnm_conf_set_core_gui_toolbars_StandardToolbar_position (GtkPositionType);

GOConfNode *gnm_conf_get_core_gui_window_x_node (void);
double gnm_conf_get_core_gui_window_x (void);
void gnm_conf_set_core_gui_window_x (double);

GOConfNode *gnm_conf_get_core_gui_window_y_node (void);
double gnm_conf_get_core_gui_window_y (void);
void gnm_conf_set_core_gui_window_y (double);

GOConfNode *gnm_conf_get_core_gui_window_zoom_node (void);
double gnm_conf_get_core_gui_window_zoom (void);
void gnm_conf_set_core_gui_window_zoom (double);

GOConfNode *gnm_conf_get_core_sort_default_ascending_node (void);
gboolean gnm_conf_get_core_sort_default_ascending (void);
void gnm_conf_set_core_sort_default_ascending (gboolean);

GOConfNode *gnm_conf_get_core_sort_default_by_case_node (void);
gboolean gnm_conf_get_core_sort_default_by_case (void);
void gnm_conf_set_core_sort_default_by_case (gboolean);

GOConfNode *gnm_conf_get_core_sort_default_retain_formats_node (void);
gboolean gnm_conf_get_core_sort_default_retain_formats (void);
void gnm_conf_set_core_sort_default_retain_formats (gboolean);

GOConfNode *gnm_conf_get_core_sort_dialog_max_initial_clauses_node (void);
int gnm_conf_get_core_sort_dialog_max_initial_clauses (void);
void gnm_conf_set_core_sort_dialog_max_initial_clauses (int);

GOConfNode *gnm_conf_get_core_workbook_autosave_time_node (void);
int gnm_conf_get_core_workbook_autosave_time (void);
void gnm_conf_set_core_workbook_autosave_time (int);

GOConfNode *gnm_conf_get_core_workbook_n_cols_node (void);
int gnm_conf_get_core_workbook_n_cols (void);
void gnm_conf_set_core_workbook_n_cols (int);

GOConfNode *gnm_conf_get_core_workbook_n_rows_node (void);
int gnm_conf_get_core_workbook_n_rows (void);
void gnm_conf_set_core_workbook_n_rows (int);

GOConfNode *gnm_conf_get_core_workbook_n_sheet_node (void);
int gnm_conf_get_core_workbook_n_sheet (void);
void gnm_conf_set_core_workbook_n_sheet (int);

GOConfNode *gnm_conf_get_core_xml_compression_level_node (void);
int gnm_conf_get_core_xml_compression_level (void);
void gnm_conf_set_core_xml_compression_level (int);

GOConfNode *gnm_conf_get_cut_and_paste_prefer_clipboard_node (void);
gboolean gnm_conf_get_cut_and_paste_prefer_clipboard (void);
void gnm_conf_set_cut_and_paste_prefer_clipboard (gboolean);

GOConfNode *gnm_conf_get_dialogs_rs_unfocused_node (void);
gboolean gnm_conf_get_dialogs_rs_unfocused (void);
void gnm_conf_set_dialogs_rs_unfocused (gboolean);

GOConfNode *gnm_conf_get_functionselector_num_of_recent_node (void);
int gnm_conf_get_functionselector_num_of_recent (void);
void gnm_conf_set_functionselector_num_of_recent (int);

GOConfNode *gnm_conf_get_functionselector_recentfunctions_node (void);
GSList *gnm_conf_get_functionselector_recentfunctions (void);
void gnm_conf_set_functionselector_recentfunctions (GSList *);

GOConfNode *gnm_conf_get_plugin_glpk_glpsol_path_node (void);
const char *gnm_conf_get_plugin_glpk_glpsol_path (void);
void gnm_conf_set_plugin_glpk_glpsol_path (const char *);

GOConfNode *gnm_conf_get_plugin_latex_use_utf8_node (void);
gboolean gnm_conf_get_plugin_latex_use_utf8 (void);
void gnm_conf_set_plugin_latex_use_utf8 (gboolean);

GOConfNode *gnm_conf_get_plugin_lpsolve_lpsolve_path_node (void);
const char *gnm_conf_get_plugin_lpsolve_lpsolve_path (void);
void gnm_conf_set_plugin_lpsolve_lpsolve_path (const char *);

GOConfNode *gnm_conf_get_plugins_activate_new_node (void);
gboolean gnm_conf_get_plugins_activate_new (void);
void gnm_conf_set_plugins_activate_new (gboolean);

GOConfNode *gnm_conf_get_plugins_active_node (void);
GSList *gnm_conf_get_plugins_active (void);
void gnm_conf_set_plugins_active (GSList *);

GOConfNode *gnm_conf_get_plugins_extra_dirs_node (void);
GSList *gnm_conf_get_plugins_extra_dirs (void);
void gnm_conf_set_plugins_extra_dirs (GSList *);

GOConfNode *gnm_conf_get_plugins_file_states_node (void);
GSList *gnm_conf_get_plugins_file_states (void);
void gnm_conf_set_plugins_file_states (GSList *);

GOConfNode *gnm_conf_get_plugins_known_node (void);
GSList *gnm_conf_get_plugins_known (void);
void gnm_conf_set_plugins_known (GSList *);

GOConfNode *gnm_conf_get_printsetup_across_then_down_node (void);
gboolean gnm_conf_get_printsetup_across_then_down (void);
void gnm_conf_set_printsetup_across_then_down (gboolean);

GOConfNode *gnm_conf_get_printsetup_all_sheets_node (void);
gboolean gnm_conf_get_printsetup_all_sheets (void);
void gnm_conf_set_printsetup_all_sheets (gboolean);

GOConfNode *gnm_conf_get_printsetup_center_horizontally_node (void);
gboolean gnm_conf_get_printsetup_center_horizontally (void);
void gnm_conf_set_printsetup_center_horizontally (gboolean);

GOConfNode *gnm_conf_get_printsetup_center_vertically_node (void);
gboolean gnm_conf_get_printsetup_center_vertically (void);
void gnm_conf_set_printsetup_center_vertically (gboolean);

GOConfNode *gnm_conf_get_printsetup_footer_node (void);
GSList *gnm_conf_get_printsetup_footer (void);
void gnm_conf_set_printsetup_footer (GSList *);

GOConfNode *gnm_conf_get_printsetup_gtk_setting_node (void);
GSList *gnm_conf_get_printsetup_gtk_setting (void);
void gnm_conf_set_printsetup_gtk_setting (GSList *);

GOConfNode *gnm_conf_get_printsetup_header_node (void);
GSList *gnm_conf_get_printsetup_header (void);
void gnm_conf_set_printsetup_header (GSList *);

GOConfNode *gnm_conf_get_printsetup_hf_font_bold_node (void);
gboolean gnm_conf_get_printsetup_hf_font_bold (void);
void gnm_conf_set_printsetup_hf_font_bold (gboolean);

GOConfNode *gnm_conf_get_printsetup_hf_font_italic_node (void);
gboolean gnm_conf_get_printsetup_hf_font_italic (void);
void gnm_conf_set_printsetup_hf_font_italic (gboolean);

GOConfNode *gnm_conf_get_printsetup_hf_font_name_node (void);
const char *gnm_conf_get_printsetup_hf_font_name (void);
void gnm_conf_set_printsetup_hf_font_name (const char *);

GOConfNode *gnm_conf_get_printsetup_hf_font_size_node (void);
double gnm_conf_get_printsetup_hf_font_size (void);
void gnm_conf_set_printsetup_hf_font_size (double);

GOConfNode *gnm_conf_get_printsetup_hf_left_node (void);
GSList *gnm_conf_get_printsetup_hf_left (void);
void gnm_conf_set_printsetup_hf_left (GSList *);

GOConfNode *gnm_conf_get_printsetup_hf_middle_node (void);
GSList *gnm_conf_get_printsetup_hf_middle (void);
void gnm_conf_set_printsetup_hf_middle (GSList *);

GOConfNode *gnm_conf_get_printsetup_hf_right_node (void);
GSList *gnm_conf_get_printsetup_hf_right (void);
void gnm_conf_set_printsetup_hf_right (GSList *);

GOConfNode *gnm_conf_get_printsetup_margin_bottom_node (void);
double gnm_conf_get_printsetup_margin_bottom (void);
void gnm_conf_set_printsetup_margin_bottom (double);

GOConfNode *gnm_conf_get_printsetup_margin_gtk_bottom_node (void);
double gnm_conf_get_printsetup_margin_gtk_bottom (void);
void gnm_conf_set_printsetup_margin_gtk_bottom (double);

GOConfNode *gnm_conf_get_printsetup_margin_gtk_left_node (void);
double gnm_conf_get_printsetup_margin_gtk_left (void);
void gnm_conf_set_printsetup_margin_gtk_left (double);

GOConfNode *gnm_conf_get_printsetup_margin_gtk_right_node (void);
double gnm_conf_get_printsetup_margin_gtk_right (void);
void gnm_conf_set_printsetup_margin_gtk_right (double);

GOConfNode *gnm_conf_get_printsetup_margin_gtk_top_node (void);
double gnm_conf_get_printsetup_margin_gtk_top (void);
void gnm_conf_set_printsetup_margin_gtk_top (double);

GOConfNode *gnm_conf_get_printsetup_margin_top_node (void);
double gnm_conf_get_printsetup_margin_top (void);
void gnm_conf_set_printsetup_margin_top (double);

GOConfNode *gnm_conf_get_printsetup_paper_node (void);
const char *gnm_conf_get_printsetup_paper (void);
void gnm_conf_set_printsetup_paper (const char *);

GOConfNode *gnm_conf_get_printsetup_paper_orientation_node (void);
int gnm_conf_get_printsetup_paper_orientation (void);
void gnm_conf_set_printsetup_paper_orientation (int);

GOConfNode *gnm_conf_get_printsetup_preferred_unit_node (void);
GtkUnit gnm_conf_get_printsetup_preferred_unit (void);
void gnm_conf_set_printsetup_preferred_unit (GtkUnit);

GOConfNode *gnm_conf_get_printsetup_print_black_n_white_node (void);
gboolean gnm_conf_get_printsetup_print_black_n_white (void);
void gnm_conf_set_printsetup_print_black_n_white (gboolean);

GOConfNode *gnm_conf_get_printsetup_print_even_if_only_styles_node (void);
gboolean gnm_conf_get_printsetup_print_even_if_only_styles (void);
void gnm_conf_set_printsetup_print_even_if_only_styles (gboolean);

GOConfNode *gnm_conf_get_printsetup_print_grid_lines_node (void);
gboolean gnm_conf_get_printsetup_print_grid_lines (void);
void gnm_conf_set_printsetup_print_grid_lines (gboolean);

GOConfNode *gnm_conf_get_printsetup_print_titles_node (void);
gboolean gnm_conf_get_printsetup_print_titles (void);
void gnm_conf_set_printsetup_print_titles (gboolean);

GOConfNode *gnm_conf_get_printsetup_repeat_left_node (void);
const char *gnm_conf_get_printsetup_repeat_left (void);
void gnm_conf_set_printsetup_repeat_left (const char *);

GOConfNode *gnm_conf_get_printsetup_repeat_top_node (void);
const char *gnm_conf_get_printsetup_repeat_top (void);
void gnm_conf_set_printsetup_repeat_top (const char *);

GOConfNode *gnm_conf_get_printsetup_scale_height_node (void);
int gnm_conf_get_printsetup_scale_height (void);
void gnm_conf_set_printsetup_scale_height (int);

GOConfNode *gnm_conf_get_printsetup_scale_percentage_node (void);
gboolean gnm_conf_get_printsetup_scale_percentage (void);
void gnm_conf_set_printsetup_scale_percentage (gboolean);

GOConfNode *gnm_conf_get_printsetup_scale_percentage_value_node (void);
double gnm_conf_get_printsetup_scale_percentage_value (void);
void gnm_conf_set_printsetup_scale_percentage_value (double);

GOConfNode *gnm_conf_get_printsetup_scale_width_node (void);
int gnm_conf_get_printsetup_scale_width (void);
void gnm_conf_set_printsetup_scale_width (int);

GOConfNode *gnm_conf_get_searchreplace_change_cell_expressions_node (void);
gboolean gnm_conf_get_searchreplace_change_cell_expressions (void);
void gnm_conf_set_searchreplace_change_cell_expressions (gboolean);

GOConfNode *gnm_conf_get_searchreplace_change_cell_other_node (void);
gboolean gnm_conf_get_searchreplace_change_cell_other (void);
void gnm_conf_set_searchreplace_change_cell_other (gboolean);

GOConfNode *gnm_conf_get_searchreplace_change_cell_strings_node (void);
gboolean gnm_conf_get_searchreplace_change_cell_strings (void);
void gnm_conf_set_searchreplace_change_cell_strings (gboolean);

GOConfNode *gnm_conf_get_searchreplace_change_comments_node (void);
gboolean gnm_conf_get_searchreplace_change_comments (void);
void gnm_conf_set_searchreplace_change_comments (gboolean);

GOConfNode *gnm_conf_get_searchreplace_columnmajor_node (void);
gboolean gnm_conf_get_searchreplace_columnmajor (void);
void gnm_conf_set_searchreplace_columnmajor (gboolean);

GOConfNode *gnm_conf_get_searchreplace_error_behaviour_node (void);
int gnm_conf_get_searchreplace_error_behaviour (void);
void gnm_conf_set_searchreplace_error_behaviour (int);

GOConfNode *gnm_conf_get_searchreplace_ignore_case_node (void);
gboolean gnm_conf_get_searchreplace_ignore_case (void);
void gnm_conf_set_searchreplace_ignore_case (gboolean);

GOConfNode *gnm_conf_get_searchreplace_keep_strings_node (void);
gboolean gnm_conf_get_searchreplace_keep_strings (void);
void gnm_conf_set_searchreplace_keep_strings (gboolean);

GOConfNode *gnm_conf_get_searchreplace_preserve_case_node (void);
gboolean gnm_conf_get_searchreplace_preserve_case (void);
void gnm_conf_set_searchreplace_preserve_case (gboolean);

GOConfNode *gnm_conf_get_searchreplace_query_node (void);
gboolean gnm_conf_get_searchreplace_query (void);
void gnm_conf_set_searchreplace_query (gboolean);

GOConfNode *gnm_conf_get_searchreplace_regex_node (void);
int gnm_conf_get_searchreplace_regex (void);
void gnm_conf_set_searchreplace_regex (int);

GOConfNode *gnm_conf_get_searchreplace_scope_node (void);
int gnm_conf_get_searchreplace_scope (void);
void gnm_conf_set_searchreplace_scope (int);

GOConfNode *gnm_conf_get_searchreplace_whole_words_only_node (void);
gboolean gnm_conf_get_searchreplace_whole_words_only (void);
void gnm_conf_set_searchreplace_whole_words_only (gboolean);

GOConfNode *gnm_conf_get_stf_export_separator_node (void);
const char *gnm_conf_get_stf_export_separator (void);
void gnm_conf_set_stf_export_separator (const char *);

GOConfNode *gnm_conf_get_stf_export_stringindicator_node (void);
const char *gnm_conf_get_stf_export_stringindicator (void);
void gnm_conf_set_stf_export_stringindicator (const char *);

GOConfNode *gnm_conf_get_stf_export_terminator_node (void);
const char *gnm_conf_get_stf_export_terminator (void);
void gnm_conf_set_stf_export_terminator (const char *);

GOConfNode *gnm_conf_get_undo_max_descriptor_width_node (void);
int gnm_conf_get_undo_max_descriptor_width (void);
void gnm_conf_set_undo_max_descriptor_width (int);

GOConfNode *gnm_conf_get_undo_maxnum_node (void);
int gnm_conf_get_undo_maxnum (void);
void gnm_conf_set_undo_maxnum (int);

GOConfNode *gnm_conf_get_undo_show_sheet_name_node (void);
gboolean gnm_conf_get_undo_show_sheet_name (void);
void gnm_conf_set_undo_show_sheet_name (gboolean);

GOConfNode *gnm_conf_get_undo_size_node (void);
int gnm_conf_get_undo_size (void);
void gnm_conf_set_undo_size (int);

GOConfNode *gnm_conf_get_autocorrect_dir_node (void);
GOConfNode *gnm_conf_get_autoformat_dir_node (void);
GOConfNode *gnm_conf_get_core_defaultfont_dir_node (void);
GOConfNode *gnm_conf_get_core_file_save_dir_node (void);
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
GOConfNode *gnm_conf_get_undo_dir_node (void);

/* ----------- AUTOMATICALLY GENERATED CODE ABOVE -- DO NOT EDIT ----------- */

G_END_DECLS

#endif /* _GNM_GCONF_H_ */
