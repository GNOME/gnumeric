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

gboolean gnm_conf_get_autocorrect_first_letter (void);
void gnm_conf_set_autocorrect_first_letter (gboolean);

GSList *gnm_conf_get_autocorrect_first_letter_list (void);
void gnm_conf_set_autocorrect_first_letter_list (GSList *);

gboolean gnm_conf_get_autocorrect_init_caps (void);
void gnm_conf_set_autocorrect_init_caps (gboolean);

GSList *gnm_conf_get_autocorrect_init_caps_list (void);
void gnm_conf_set_autocorrect_init_caps_list (GSList *);

gboolean gnm_conf_get_autocorrect_names_of_days (void);
void gnm_conf_set_autocorrect_names_of_days (gboolean);

gboolean gnm_conf_get_autocorrect_replace (void);
void gnm_conf_set_autocorrect_replace (gboolean);

GSList *gnm_conf_get_autoformat_extra_dirs (void);
void gnm_conf_set_autoformat_extra_dirs (GSList *);

const char *gnm_conf_get_autoformat_sys_dir (void);
void gnm_conf_set_autoformat_sys_dir (const char *);

const char *gnm_conf_get_autoformat_usr_dir (void);
void gnm_conf_set_autoformat_usr_dir (const char *);

gboolean gnm_conf_get_core_defaultfont_bold (void);
void gnm_conf_set_core_defaultfont_bold (gboolean);

gboolean gnm_conf_get_core_defaultfont_italic (void);
void gnm_conf_set_core_defaultfont_italic (gboolean);

const char *gnm_conf_get_core_defaultfont_name (void);
void gnm_conf_set_core_defaultfont_name (const char *);

double gnm_conf_get_core_defaultfont_size (void);
void gnm_conf_set_core_defaultfont_size (double);

gboolean gnm_conf_get_core_file_save_def_overwrite (void);
void gnm_conf_set_core_file_save_def_overwrite (gboolean);

gboolean gnm_conf_get_core_file_save_single_sheet (void);
void gnm_conf_set_core_file_save_single_sheet (gboolean);

gboolean gnm_conf_get_core_gui_editing_autocomplete (void);
void gnm_conf_set_core_gui_editing_autocomplete (gboolean);

GODirection gnm_conf_get_core_gui_editing_enter_moves_dir (void);
void gnm_conf_set_core_gui_editing_enter_moves_dir (GODirection);

gboolean gnm_conf_get_core_gui_editing_livescrolling (void);
void gnm_conf_set_core_gui_editing_livescrolling (gboolean);

int gnm_conf_get_core_gui_editing_recalclag (void);
void gnm_conf_set_core_gui_editing_recalclag (int);

gboolean gnm_conf_get_core_gui_editing_transitionkeys (void);
void gnm_conf_set_core_gui_editing_transitionkeys (gboolean);

double gnm_conf_get_core_gui_screen_horizontaldpi (void);
void gnm_conf_set_core_gui_screen_horizontaldpi (double);

double gnm_conf_get_core_gui_screen_verticaldpi (void);
void gnm_conf_set_core_gui_screen_verticaldpi (double);

gboolean gnm_conf_get_core_gui_toolbars_FormatToolbar (void);
void gnm_conf_set_core_gui_toolbars_FormatToolbar (gboolean);

GtkPositionType gnm_conf_get_core_gui_toolbars_FormatToolbar_position (void);
void gnm_conf_set_core_gui_toolbars_FormatToolbar_position (GtkPositionType);

gboolean gnm_conf_get_core_gui_toolbars_LongFormatToolbar (void);
void gnm_conf_set_core_gui_toolbars_LongFormatToolbar (gboolean);

GtkPositionType gnm_conf_get_core_gui_toolbars_LongFormatToolbar_position (void);
void gnm_conf_set_core_gui_toolbars_LongFormatToolbar_position (GtkPositionType);

gboolean gnm_conf_get_core_gui_toolbars_ObjectToolbar (void);
void gnm_conf_set_core_gui_toolbars_ObjectToolbar (gboolean);

GtkPositionType gnm_conf_get_core_gui_toolbars_ObjectToolbar_position (void);
void gnm_conf_set_core_gui_toolbars_ObjectToolbar_position (GtkPositionType);

gboolean gnm_conf_get_core_gui_toolbars_StandardToolbar (void);
void gnm_conf_set_core_gui_toolbars_StandardToolbar (gboolean);

GtkPositionType gnm_conf_get_core_gui_toolbars_StandardToolbar_position (void);
void gnm_conf_set_core_gui_toolbars_StandardToolbar_position (GtkPositionType);

double gnm_conf_get_core_gui_window_x (void);
void gnm_conf_set_core_gui_window_x (double);

double gnm_conf_get_core_gui_window_y (void);
void gnm_conf_set_core_gui_window_y (double);

double gnm_conf_get_core_gui_window_zoom (void);
void gnm_conf_set_core_gui_window_zoom (double);

gboolean gnm_conf_get_core_sort_default_ascending (void);
void gnm_conf_set_core_sort_default_ascending (gboolean);

gboolean gnm_conf_get_core_sort_default_by_case (void);
void gnm_conf_set_core_sort_default_by_case (gboolean);

gboolean gnm_conf_get_core_sort_default_retain_formats (void);
void gnm_conf_set_core_sort_default_retain_formats (gboolean);

int gnm_conf_get_core_sort_dialog_max_initial_clauses (void);
void gnm_conf_set_core_sort_dialog_max_initial_clauses (int);

int gnm_conf_get_core_workbook_autosave_time (void);
void gnm_conf_set_core_workbook_autosave_time (int);

int gnm_conf_get_core_workbook_n_cols (void);
void gnm_conf_set_core_workbook_n_cols (int);

int gnm_conf_get_core_workbook_n_rows (void);
void gnm_conf_set_core_workbook_n_rows (int);

int gnm_conf_get_core_workbook_n_sheet (void);
void gnm_conf_set_core_workbook_n_sheet (int);

int gnm_conf_get_core_xml_compression_level (void);
void gnm_conf_set_core_xml_compression_level (int);

gboolean gnm_conf_get_cut_and_paste_prefer_clipboard (void);
void gnm_conf_set_cut_and_paste_prefer_clipboard (gboolean);

gboolean gnm_conf_get_dialogs_rs_unfocused (void);
void gnm_conf_set_dialogs_rs_unfocused (gboolean);

int gnm_conf_get_functionselector_num_of_recent (void);
void gnm_conf_set_functionselector_num_of_recent (int);

GSList *gnm_conf_get_functionselector_recentfunctions (void);
void gnm_conf_set_functionselector_recentfunctions (GSList *);

gboolean gnm_conf_get_plugin_latex_use_utf8 (void);
void gnm_conf_set_plugin_latex_use_utf8 (gboolean);

gboolean gnm_conf_get_plugins_activate_new (void);
void gnm_conf_set_plugins_activate_new (gboolean);

GSList *gnm_conf_get_plugins_active (void);
void gnm_conf_set_plugins_active (GSList *);

GSList *gnm_conf_get_plugins_extra_dirs (void);
void gnm_conf_set_plugins_extra_dirs (GSList *);

GSList *gnm_conf_get_plugins_file_states (void);
void gnm_conf_set_plugins_file_states (GSList *);

GSList *gnm_conf_get_plugins_known (void);
void gnm_conf_set_plugins_known (GSList *);

gboolean gnm_conf_get_printsetup_across_then_down (void);
void gnm_conf_set_printsetup_across_then_down (gboolean);

gboolean gnm_conf_get_printsetup_all_sheets (void);
void gnm_conf_set_printsetup_all_sheets (gboolean);

gboolean gnm_conf_get_printsetup_center_horizontally (void);
void gnm_conf_set_printsetup_center_horizontally (gboolean);

gboolean gnm_conf_get_printsetup_center_vertically (void);
void gnm_conf_set_printsetup_center_vertically (gboolean);

GSList *gnm_conf_get_printsetup_footer (void);
void gnm_conf_set_printsetup_footer (GSList *);

GSList *gnm_conf_get_printsetup_gtk_setting (void);
void gnm_conf_set_printsetup_gtk_setting (GSList *);

GSList *gnm_conf_get_printsetup_header (void);
void gnm_conf_set_printsetup_header (GSList *);

gboolean gnm_conf_get_printsetup_hf_font_bold (void);
void gnm_conf_set_printsetup_hf_font_bold (gboolean);

gboolean gnm_conf_get_printsetup_hf_font_italic (void);
void gnm_conf_set_printsetup_hf_font_italic (gboolean);

const char *gnm_conf_get_printsetup_hf_font_name (void);
void gnm_conf_set_printsetup_hf_font_name (const char *);

double gnm_conf_get_printsetup_hf_font_size (void);
void gnm_conf_set_printsetup_hf_font_size (double);

GSList *gnm_conf_get_printsetup_hf_left (void);
void gnm_conf_set_printsetup_hf_left (GSList *);

GSList *gnm_conf_get_printsetup_hf_middle (void);
void gnm_conf_set_printsetup_hf_middle (GSList *);

GSList *gnm_conf_get_printsetup_hf_right (void);
void gnm_conf_set_printsetup_hf_right (GSList *);

double gnm_conf_get_printsetup_margin_bottom (void);
void gnm_conf_set_printsetup_margin_bottom (double);

double gnm_conf_get_printsetup_margin_gtk_bottom (void);
void gnm_conf_set_printsetup_margin_gtk_bottom (double);

double gnm_conf_get_printsetup_margin_gtk_left (void);
void gnm_conf_set_printsetup_margin_gtk_left (double);

double gnm_conf_get_printsetup_margin_gtk_right (void);
void gnm_conf_set_printsetup_margin_gtk_right (double);

double gnm_conf_get_printsetup_margin_gtk_top (void);
void gnm_conf_set_printsetup_margin_gtk_top (double);

double gnm_conf_get_printsetup_margin_top (void);
void gnm_conf_set_printsetup_margin_top (double);

const char *gnm_conf_get_printsetup_paper (void);
void gnm_conf_set_printsetup_paper (const char *);

int gnm_conf_get_printsetup_paper_orientation (void);
void gnm_conf_set_printsetup_paper_orientation (int);

GtkUnit gnm_conf_get_printsetup_preferred_unit (void);
void gnm_conf_set_printsetup_preferred_unit (GtkUnit);

gboolean gnm_conf_get_printsetup_print_black_n_white (void);
void gnm_conf_set_printsetup_print_black_n_white (gboolean);

gboolean gnm_conf_get_printsetup_print_even_if_only_styles (void);
void gnm_conf_set_printsetup_print_even_if_only_styles (gboolean);

gboolean gnm_conf_get_printsetup_print_grid_lines (void);
void gnm_conf_set_printsetup_print_grid_lines (gboolean);

gboolean gnm_conf_get_printsetup_print_titles (void);
void gnm_conf_set_printsetup_print_titles (gboolean);

const char *gnm_conf_get_printsetup_repeat_left (void);
void gnm_conf_set_printsetup_repeat_left (const char *);

const char *gnm_conf_get_printsetup_repeat_top (void);
void gnm_conf_set_printsetup_repeat_top (const char *);

int gnm_conf_get_printsetup_scale_height (void);
void gnm_conf_set_printsetup_scale_height (int);

gboolean gnm_conf_get_printsetup_scale_percentage (void);
void gnm_conf_set_printsetup_scale_percentage (gboolean);

double gnm_conf_get_printsetup_scale_percentage_value (void);
void gnm_conf_set_printsetup_scale_percentage_value (double);

int gnm_conf_get_printsetup_scale_width (void);
void gnm_conf_set_printsetup_scale_width (int);

int gnm_conf_get_undo_max_descriptor_width (void);
void gnm_conf_set_undo_max_descriptor_width (int);

int gnm_conf_get_undo_maxnum (void);
void gnm_conf_set_undo_maxnum (int);

gboolean gnm_conf_get_undo_show_sheet_name (void);
void gnm_conf_set_undo_show_sheet_name (gboolean);

int gnm_conf_get_undo_size (void);
void gnm_conf_set_undo_size (int);

/* ----------- AUTOMATICALLY GENERATED CODE ABOVE -- DO NOT EDIT ----------- */

G_END_DECLS

#endif /* _GNM_GCONF_H_ */
