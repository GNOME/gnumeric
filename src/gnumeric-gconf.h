#ifndef GNUMERIC_GCONF_H
#define GNUMERIC_GCONF_H

#include <numbers.h>
#include <gnumeric.h>
#include <print-info.h>
#include <gconf/gconf-client.h>

typedef struct {
	struct {
		GSList	const *extra_dirs;
		char	*sys_dir;
		char	*usr_dir;
	} autoformat;

	struct {
		char const *name;
		float size;
		gboolean is_bold, is_italic;
	} default_font;

	gint     	 file_history_max;
	GSList const	*file_history_files;
	guint    	 num_of_recent_funcs;
	GSList const	*recent_funcs;

	GSList const	*plugin_file_states;
	GSList const	*plugin_extra_dirs;
	GSList const	*active_plugins;
	gboolean	 activate_new_plugins;

	gboolean	 show_sheet_name;
	guint		 max_descriptor_width;
	gint		 undo_size;
	gint		 undo_max_number;

	gint		 initial_sheet_number;
	float		 horizontal_window_fraction;
	float		 vertical_window_fraction;
	float		 zoom;

	gint		 xml_compression_level;
	gboolean 	 file_overwrite_default_answer;
	gboolean 	 file_ask_single_sheet_save;

	gboolean 	 sort_default_by_case;
	gboolean 	 sort_default_retain_formats;
	gboolean 	 sort_default_ascending;
	gint     	 sort_max_initial_clauses;

	gboolean	 print_all_sheets; /* vs print only selected */
	gchar           *printer_config;
	GSList const    *printer_header;
	GSList const    *printer_footer;
	GSList const    *printer_header_formats_left;
	GSList const    *printer_header_formats_middle;
	GSList const    *printer_header_formats_right;
	GnmStyle        *printer_decoration_font;
	gboolean         print_center_horizontally;
	gboolean         print_center_vertically;
	gboolean         print_grid_lines;
	gboolean         print_even_if_only_styles;
	gboolean         print_black_and_white;
	gboolean         print_titles;
	gboolean         print_order_right_then_down;
	gboolean         print_scale_percentage;
	float            print_scale_percentage_value;
	gint             print_scale_width;
	gint             print_scale_height;
	gchar           *print_repeat_top;
	gchar           *print_repeat_left;
	PrintMargins     print_tb_margins;
	
	float		 horizontal_dpi;
	float		 vertical_dpi;
	gboolean	 auto_complete;
	gboolean	 transition_keys;
	gboolean	 live_scrolling;
	gint		 recalc_lag;
	gboolean	 unfocused_range_selection;
	gboolean         prefer_clipboard_selection;  /* As opposed to "primary".  */
	gboolean	 latex_use_utf8;
} GnmAppPrefs;
extern GnmAppPrefs const *gnm_app_prefs;

void     gnm_conf_init (gboolean fast);
void     gnm_conf_shutdown (void);
void     gnm_conf_sync (void);
GConfClient *gnm_app_get_gconf_client (void);

/* autocorrect */
void     gnm_gconf_set_autocorrect_init_caps (gboolean val);
void     gnm_gconf_set_autocorrect_first_letter (gboolean val);
void     gnm_gconf_set_autocorrect_names_of_days (gboolean val);
void     gnm_gconf_set_autocorrect_replace (gboolean val);

/* autoformat */
void     gnm_gconf_set_autoformat_sys_dirs (char const * string);
void     gnm_gconf_set_autoformat_usr_dirs (char const * string);

/* file history */
void     gnm_gconf_set_file_history_max (gint val);
void     gnm_gconf_set_file_history_files (GSList *list);

/* plugins */
void     gnm_gconf_set_plugin_file_states (GSList *list);
void     gnm_gconf_set_plugin_extra_dirs (GSList *list);
void     gnm_gconf_set_active_plugins (GSList *list);
void     gnm_gconf_set_activate_new_plugins (gboolean val);

/* undo */
void     gnm_gconf_set_show_sheet_name (gboolean val);
void     gnm_gconf_set_max_descriptor_width (guint val);
void     gnm_gconf_set_undo_size (gint val);
void     gnm_gconf_set_undo_max_number (gint val);

/* new workbooks */
void     gnm_gconf_set_initial_sheet_number (gint val);
void     gnm_gconf_set_horizontal_window_fraction  (gnm_float val);
void     gnm_gconf_set_vertical_window_fraction  (gnm_float val);
void     gnm_gconf_set_zoom  (gnm_float val);

/* xml/files */
void     gnm_gconf_set_xml_compression_level (gint val);
void     gnm_gconf_set_recent_funcs (GSList *list);
void     gnm_gconf_set_num_of_recent_funcs (guint val);
void     gnm_gconf_set_file_overwrite_default_answer (gboolean val);
void     gnm_gconf_set_file_ask_single_sheet_save (gboolean val);

/* sort */
void     gnm_gconf_set_sort_default_by_case (gboolean val);
void     gnm_gconf_set_sort_default_retain_formats (gboolean val);
void     gnm_gconf_set_sort_default_ascending (gboolean val);
void     gnm_gconf_set_sort_max_initial_clauses (gint val);

/* print-setup & printing */
void     gnm_gconf_set_all_sheets (gboolean val);
void     gnm_gconf_set_printer_config (gchar *str);
void     gnm_gconf_set_printer_header (gchar const *left, gchar const *middle, 
				       gchar const *right);
void     gnm_gconf_set_printer_footer (gchar const *left, gchar const *middle, 
				       gchar const *right);
void     gnm_gconf_set_print_center_horizontally (gboolean val);
void     gnm_gconf_set_print_center_vertically (gboolean val);
void     gnm_gconf_set_print_grid_lines (gboolean val);
void     gnm_gconf_set_print_even_if_only_styles (gboolean val);
void     gnm_gconf_set_print_black_and_white (gboolean val);
void     gnm_gconf_set_print_titles (gboolean val);
void     gnm_gconf_set_print_order_right_then_down (gboolean val);
void     gnm_gconf_set_print_scale_percentage (gboolean val);
void     gnm_gconf_set_print_scale_percentage_value (gnm_float val);
void     gnm_gconf_set_print_scale_width (gint val);
void     gnm_gconf_set_print_scale_height (gint val);
void     gnm_gconf_set_print_repeat_top (gchar const *str);
void     gnm_gconf_set_print_repeat_left (gchar const *str);
void     gnm_gconf_set_print_tb_margins (PrintMargins const *pm);
void     gnm_gconf_set_print_header_formats (GSList *left, GSList *middle, 
					     GSList *right);

/* others */
void     gnm_gconf_set_horizontal_dpi  (gnm_float val);
void     gnm_gconf_set_vertical_dpi  (gnm_float val);
void     gnm_gconf_set_auto_complete (gboolean val);
void	 gnm_gconf_set_transition_keys (gboolean val);
void     gnm_gconf_set_live_scrolling (gboolean val);
void     gnm_gconf_set_recalc_lag (gint val);
void     gnm_gconf_set_unfocused_range_selection (gboolean val);
void     gnm_gconf_set_prefer_clipboard_selection (gboolean val);
void     gnm_gconf_set_latex_use_utf8 (gboolean val);

#endif /* GNUMERIC_GRAPH_H */
