#ifndef GNUMERIC_GCONF_H
#define GNUMERIC_GCONF_H

#include <gconf/gconf-client.h>
#include <numbers.h>

typedef struct {
	struct {
		GSList	*extra_dirs;
		char	*sys_dir;
		char	*usr_dir;
	} autoformat;

	gint     	 file_history_max;
	GSList		*file_history_files;
	guint    	 num_of_recent_funcs;
	GSList 		*recent_funcs;

	GSList		*plugin_file_states;
	GSList		*plugin_extra_dirs;
	GSList		*active_plugins;
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
	gboolean 	 import_uses_all_openers;
	gboolean 	 file_overwrite_default_answer;
	gboolean 	 file_ask_single_sheet_save;

	gboolean 	 sort_default_by_case;
	gboolean 	 sort_default_retain_formats;
	gboolean 	 sort_default_ascending;
	gint     	 sort_max_initial_clauses;

	gboolean	 print_all_sheets; /* vs print only selected */
	gchar   	*printer;
	gchar   	*printer_backend;
	gchar   	*printer_filename;
	gchar   	*printer_command;
	gchar   	*printer_lpr_P;

	float		 horizontal_dpi;
	float		 vertical_dpi;
	gboolean	 auto_complete;
	gboolean	 live_scrolling;
	gint		 recalc_lag;
	gboolean	 unfocused_range_selection;
} GnmAppPrefs;
extern GnmAppPrefs const *gnm_app_prefs;

void     gnm_conf_init (void);
void     gnm_conf_sync (void);
guint    gnm_gconf_rm_notification (guint id);

/* autocorrect */
guint    gnm_gconf_add_notification_autocorrect (GConfClientNotifyFunc func);
void     gnm_gconf_set_autocorrect_init_caps (gboolean val);
void     gnm_gconf_set_autocorrect_first_letter (gboolean val);
void     gnm_gconf_set_autocorrect_names_of_days (gboolean val);
void     gnm_gconf_set_autocorrect_replace (gboolean val);
void     gnm_gconf_set_autocorrect_init_caps_exceptions (GSList *list);
void     gnm_gconf_set_autocorrect_first_letter_exceptions (GSList *list);

/* autoformat */
void     gnm_gconf_set_autoformat_extra_dirs (GSList *list);
void     gnm_gconf_set_autoformat_sys_dirs (char const * string);
void     gnm_gconf_set_autoformat_usr_dirs (char const * string);

/* file history */
void     gnm_gconf_set_file_history_max (gint val);
void     gnm_gconf_set_file_history_files (GSList *list);

/* plugins */
guint    gnm_gconf_add_notification_plugin_directories (GConfClientNotifyFunc func, gpointer data);
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
void     gnm_gconf_set_horizontal_window_fraction  (gnum_float val);
void     gnm_gconf_set_vertical_window_fraction  (gnum_float val);
void     gnm_gconf_set_zoom  (gnum_float val);

/* xml/files */
void     gnm_gconf_set_xml_compression_level (gint val);
void     gnm_gconf_set_recent_funcs (GSList *list);
void     gnm_gconf_set_num_of_recent_funcs (guint val);
void     gnm_gconf_set_import_uses_all_openers (gboolean val);
void     gnm_gconf_set_file_overwrite_default_answer (gboolean val);
void     gnm_gconf_set_file_ask_single_sheet_save (gboolean val);

/* sort */
void     gnm_gconf_set_sort_default_by_case (gboolean val);
void     gnm_gconf_set_sort_default_retain_formats (gboolean val);
void     gnm_gconf_set_sort_default_ascending (gboolean val);
void     gnm_gconf_set_sort_max_initial_clauses (gint val);

/* print-setup & printing */
void     gnm_gconf_set_all_sheets (gboolean val);
void     gnm_gconf_set_printer (gchar *str);
void     gnm_gconf_set_printer_backend (gchar *str);
void     gnm_gconf_set_printer_filename (gchar *str);
gchar   *gnm_gconf_get_printer_command (void);
void     gnm_gconf_set_printer_command (gchar *str);
void     gnm_gconf_set_printer_lpr_P (gchar *str);

/* others */
void     gnm_gconf_set_horizontal_dpi  (gnum_float val);
void     gnm_gconf_set_vertical_dpi  (gnum_float val);
void     gnm_gconf_set_auto_complete (gboolean val);
void     gnm_gconf_set_live_scrolling (gboolean val);
void     gnm_gconf_set_recalc_lag (gint val);
void     gnm_gconf_set_unfocused_range_selection (gboolean val);

#endif /* GNUMERIC_GRAPH_H */
