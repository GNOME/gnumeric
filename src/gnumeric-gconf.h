#ifndef GNUMERIC_GCONF_H
#define GNUMERIC_GCONF_H

#include <gconf/gconf-client.h>
#include <numbers.h>

void     gnm_conf_sync (void);

guint    gnm_gconf_add_notification_autocorrect (GConfClientNotifyFunc func);
guint    gnm_gconf_rm_notification_autocorrect (guint id);


GSList * gnm_gconf_get_plugin_file_states (void);
void     gnm_gconf_set_plugin_file_states (GSList *list);

GSList * gnm_gconf_get_plugin_extra_dirs (void);
void     gnm_gconf_set_plugin_extra_dirs (GSList *list);

GSList * gnm_gconf_get_active_plugins (void);
void     gnm_gconf_set_active_plugins (GSList *list);

GSList * gnm_gconf_get_known_plugins (void);
void     gnm_gconf_set_known_plugins (GSList *list);

gboolean gnm_gconf_get_activate_new_plugins (void);
void     gnm_gconf_set_activate_new_plugins (gboolean val);


GSList * gnm_gconf_get_recent_funcs (void);
void     gnm_gconf_set_recent_funcs (GSList *list);

guint    gnm_gconf_get_num_of_recent_funcs (void);
void     gnm_gconf_set_num_of_recent_funcs (guint val);


gboolean gnm_gconf_get_autocorrect_init_caps (void);
void     gnm_gconf_set_autocorrect_init_caps (gboolean val);

gboolean gnm_gconf_get_autocorrect_first_letter (void);
void     gnm_gconf_set_autocorrect_first_letter (gboolean val);

gboolean gnm_gconf_get_autocorrect_names_of_days (void);
void     gnm_gconf_set_autocorrect_names_of_days (gboolean val);

gboolean gnm_gconf_get_autocorrect_replace (void);
void     gnm_gconf_set_autocorrect_replace (gboolean val);

GSList * gnm_gconf_get_autocorrect_init_caps_exceptions (void);
void     gnm_gconf_set_autocorrect_init_caps_exceptions (GSList *list);

GSList * gnm_gconf_get_autocorrect_first_letter_exceptions (void);
void     gnm_gconf_set_autocorrect_first_letter_exceptions (GSList *list);

gnum_float gnm_gconf_get_horizontal_dpi (void);
void     gnm_gconf_set_horizontal_dpi  (gnum_float val);

gnum_float gnm_gconf_get_vertical_dpi (void);
void     gnm_gconf_set_vertical_dpi  (gnum_float val);

gboolean gnm_gconf_get_auto_complete (void);
void     gnm_gconf_set_auto_complete (gboolean val);

gboolean gnm_gconf_get_live_scrolling (void);
void     gnm_gconf_set_live_scrolling (gboolean val);

gint     gnm_gconf_get_recalc_lag (void);
void     gnm_gconf_set_recalc_lag (gint val);

gint     gnm_gconf_get_file_history_max (void);
void     gnm_gconf_set_file_history_max (gint val);

GSList * gnm_gconf_get_file_history_files (void);
void     gnm_gconf_set_file_history_files (GSList *list);

gint     gnm_gconf_get_initial_sheet_number (void);
void     gnm_gconf_set_initial_sheet_number (gint val);






#endif /* GNUMERIC_GRAPH_H */
