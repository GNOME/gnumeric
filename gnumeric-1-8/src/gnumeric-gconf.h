/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_GCONF_H_
# define _GNM_GCONF_H_

#include "numbers.h"
#include <gnumeric.h>
#include <glib-object.h>
#include <print-info.h>
#include <libgnumeric.h>
#include <goffice/utils/go-geometry.h>
#include <gtk/gtkprintsettings.h>

G_BEGIN_DECLS

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

	gboolean 	 sort_default_has_header;
	gboolean 	 sort_default_by_case;
	gboolean 	 sort_default_retain_formats;
	gboolean 	 sort_default_ascending;
	gint     	 sort_max_initial_clauses;

	gboolean	 print_all_sheets; /* vs print only selected */
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
	gboolean         print_order_across_then_down;
	gboolean         print_scale_percentage;
	float            print_scale_percentage_value;
	gint             print_scale_width;
	gint             print_scale_height;
	gchar           *print_repeat_top;
	gchar           *print_repeat_left;
	double	         print_margin_top;
	double       	 print_margin_bottom;
	GtkUnit          desired_display;

  /* Also acts as flag whether the print defaults are loaded  */
        GtkPrintSettings *print_settings;
        GtkPageSetup     *page_setup;


	float		 horizontal_dpi;
	float		 vertical_dpi;

	gboolean	 auto_complete;
	GODirection	 enter_moves_dir;	/* Which way does hitting <Enter> go */
	gboolean	 transition_keys;

	gboolean	 live_scrolling;
	GHashTable      *toolbars;
	GHashTable      *toolbar_positions;
	gint		 recalc_lag;
	gboolean	 unfocused_range_selection;
	gboolean         prefer_clipboard_selection;  /* As opposed to "primary".  */
	gboolean	 latex_use_utf8;
} GnmAppPrefs;
GNM_VAR_DECL GnmAppPrefs const *gnm_app_prefs;

typedef struct _GOConfNode GOConfNode;

void     gnm_conf_init (gboolean fast);
void     gnm_conf_shutdown (void);
GOConfNode *gnm_conf_get_root (void);

/* autocomplete */
void     gnm_gconf_set_autocomplete (gboolean val);

/* autoformat */
void     gnm_gconf_set_autoformat_sys_dirs (char const * string);
void     gnm_gconf_set_autoformat_usr_dirs (char const * string);

/* plugins */
void     gnm_gconf_set_plugin_file_states (GSList *list);
void     gnm_gconf_set_plugin_extra_dirs (GSList *list);
void     gnm_gconf_set_active_plugins (GSList *list);
void     gnm_gconf_set_activate_new_plugins (gboolean val);

/* undo */
void     gnm_gconf_set_show_sheet_name (gboolean val);
void     gnm_gconf_set_max_descriptor_width (gint val);
void     gnm_gconf_set_undo_size (gint val);
void     gnm_gconf_set_undo_max_number (gint val);

/* xml/files */
void     gnm_gconf_set_recent_funcs (GSList *list);
void     gnm_gconf_set_xml_compression (gint value);
void     gnm_gconf_set_file_overwrite (gboolean value);
void     gnm_gconf_set_file_single_sheet_save (gboolean value);

/* print-setup & printing */
void     gnm_gconf_init_printer_defaults (void);

void     gnm_gconf_set_all_sheets (gboolean val);
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
void     gnm_gconf_set_print_order_across_then_down (gboolean val);
void     gnm_gconf_set_print_scale_percentage (gboolean val);
void     gnm_gconf_set_print_scale_percentage_value (gnm_float val);
void     gnm_gconf_set_print_tb_margins (double edge_to_header,
					 double edge_to_footer,
					 GtkUnit unit);
void     gnm_gconf_set_print_header_formats (GSList *left, GSList *middle,
					     GSList *right);
void	 gnm_gconf_set_print_settings (GtkPrintSettings *settings);
void     gnm_gconf_set_page_setup (GtkPageSetup *setup);
GtkPrintSettings  *gnm_gconf_get_print_settings (void);
GtkPageSetup      *gnm_gconf_get_page_setup (void);

/* gui */
void     gnm_gconf_set_gui_window_x (gnm_float val);
void     gnm_gconf_set_gui_window_y (gnm_float val);
void     gnm_gconf_set_gui_zoom (gnm_float val);
void     gnm_gconf_set_gui_transition_keys (gboolean value);
void     gnm_gconf_set_gui_livescrolling (gboolean value);
void     gnm_gconf_set_gui_resolution_h (gnm_float val);
void     gnm_gconf_set_gui_resolution_v (gnm_float val);
gboolean gnm_gconf_get_toolbar_visible (char const *name);
void     gnm_gconf_set_toolbar_visible (char const *name, gboolean vis);
int      gnm_gconf_get_toolbar_position (char const *name);
void     gnm_gconf_set_toolbar_position (char const *name, int pos);
void	 gnm_gconf_set_enter_moves_dir (GODirection val);

/* default font */
void     gnm_gconf_set_default_font_size (gnm_float val);
void     gnm_gconf_set_default_font_name (char const *str);
void     gnm_gconf_set_default_font_bold (gboolean val);
void     gnm_gconf_set_default_font_italic (gboolean val);

/* hf font */
void     gnm_gconf_set_hf_font (GnmStyle const *mstyle);

/* sorting */
void     gnm_gconf_set_sort_dialog_max_initial (gint value);
void     gnm_gconf_set_sort_retain_form (gboolean value);
void     gnm_gconf_set_sort_has_header (gboolean value);
void     gnm_gconf_set_sort_by_case (gboolean value);
void     gnm_gconf_set_sort_ascending (gboolean value);

/* workbook */
void     gnm_gconf_set_workbook_nsheets (gint value);
void     gnm_gconf_set_unfocused_rs (gboolean value);

/* function selector and formula guru */
void     gnm_gconf_set_num_recent_functions (gint value);

/* standard plugins */
void     gnm_gconf_set_latex_use_utf8 (gboolean value);

/* application interface */
void     gnm_gconf_set_prefer_clipboard  (gboolean value);

/**************************************************************/

GOConfNode * go_conf_get_node       (GOConfNode *parent, gchar const *key);
void	 go_conf_free_node	    (GOConfNode *node);

gchar	*go_conf_get_short_desc     (GOConfNode *node, gchar const *key);
gchar	*go_conf_get_long_desc      (GOConfNode *node, gchar const *key);
GType	 go_conf_get_type	    (GOConfNode *node, gchar const *key);
gchar	*go_conf_get_value_as_str   (GOConfNode *node, gchar const *key);
gboolean go_conf_set_value_from_str (GOConfNode *node, gchar const *key, gchar const *val_str);

gboolean go_conf_get_bool	(GOConfNode *node, gchar const *key);
gint	 go_conf_get_int	(GOConfNode *node, gchar const *key);
gdouble	 go_conf_get_double	(GOConfNode *node, gchar const *key);
gchar	*go_conf_get_string	(GOConfNode *node, gchar const *key);
GSList	*go_conf_get_str_list	(GOConfNode *node, gchar const *key);
gchar	*go_conf_get_enum_as_str(GOConfNode *node, gchar const *key);

gboolean go_conf_load_bool	(GOConfNode *node, gchar const *key, gboolean default_val);
gint	 go_conf_load_int	(GOConfNode *node, gchar const *key, gint minima, gint maxima, gint default_val);
gdouble	 go_conf_load_double	(GOConfNode *node, gchar const *key, gdouble minima, gdouble maxima, gdouble default_val);
gchar	*go_conf_load_string	(GOConfNode *node, gchar const *key);
GSList	*go_conf_load_str_list	(GOConfNode *node, gchar const *key);
int	 go_conf_load_enum	(GOConfNode *node, gchar const *key, GType t, int default_val);

void	 go_conf_set_bool	(GOConfNode *node, gchar const *key, gboolean val);
void	 go_conf_set_int	(GOConfNode *node, gchar const *key, gint val);
void	 go_conf_set_double	(GOConfNode *node, gchar const *key, gnm_float val);
void	 go_conf_set_string	(GOConfNode *node, gchar const *key, gchar const *str);
void	 go_conf_set_str_list	(GOConfNode *node, gchar const *key, GSList *list);
void	 go_conf_set_enum	(GOConfNode *node, gchar const *key, GType t, gint val);

void	 go_conf_sync		(GOConfNode *node);

typedef void (*GOConfMonitorFunc) (GOConfNode *node, gchar const *key, gpointer data);
void	 go_conf_remove_monitor	(guint monitor_id);
guint	 go_conf_add_monitor	(GOConfNode *node, gchar const *key,
				 GOConfMonitorFunc monitor, gpointer data);

G_END_DECLS

#endif /* _GNM_GCONF_H_ */
