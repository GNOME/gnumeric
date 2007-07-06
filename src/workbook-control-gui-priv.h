#ifndef GNUMERIC_WORKBOOK_CONTROL_GUI_PRIV_H
#define GNUMERIC_WORKBOOK_CONTROL_GUI_PRIV_H

#include "gnumeric.h"
#include "workbook-control-gui.h"
#include "workbook-control-priv.h"
#include <goffice/app/file.h>
#include "style.h"
#include "widgets/gnumeric-expr-entry.h"

#include <gtk/gtknotebook.h>

#ifdef USE_HILDON
#include <hildon-widgets/hildon-program.h>
#endif

struct _WorkbookControlGUI {
	WorkbookControl	wb_control;

	GtkWidget   *toplevel;
#ifdef USE_HILDON
	HildonProgram *hildon_prog;
#endif
	GtkNotebook *notebook;
	GtkWidget   *progress_bar;

	struct {
		GnmExprEntry *entry; /* The real edit line */
		GnmExprEntry *temp_entry; /* A tmp overlay eg from a guru */
		GtkWidget*guru;
		gulong         signal_changed, signal_insert, signal_delete;
		gulong         signal_cursor_pos, signal_selection_bound;
		PangoAttrList *full_content;	/* include the cell attrs too */
		PangoAttrList *markup;	/* just the markup */
		PangoAttrList *cur_fmt;	/* attrs for new text (depends on position) */
	} edit_line;

	/* While editing these should be visible */
	GtkWidget *ok_button, *cancel_button;

	/* While not editing these should be visible */
	GtkWidget *func_button;

	gboolean    updating_ui;

	/* Auto completion */
	GObject		*auto_complete;         /* GType is (Complete *) */
	gboolean	 auto_completing;
	char		*auto_complete_text;

	/* Used to detect if the user has backspaced, so we turn off auto-complete */
	int              auto_max_size;

	/* Keep track of whether the last key pressed was END, so end-mode works */
	gboolean last_key_was_end;

	SheetControlGUI *rangesel;

	GtkWidget  *table;
	GtkWidget  *auto_expr_label;
	GtkWidget  *status_text;

	/* Widgets whose visibility should be copied.  */
	GHashTable *visibility_widgets, *toggle_for_fullscreen;
	gboolean is_fullscreen;

	/* Edit area */
	GtkWidget *selection_descriptor;	/* A GtkEntry */

	/* Autosave */
        gboolean   autosave_prompt;
        gint       autosave_time;
        gint       autosave_timer;

	PangoFontDescription *font_desc;

	GOFileSaver *current_saver;

	gulong sig_view_changed;
	gulong sig_auto_expr_text;
	gulong sig_sheet_order, sig_notify_uri, sig_notify_dirty;
	gpointer sig_wbv;
};

typedef struct {
	WorkbookControlClass base;

	/* signals */
	void (*markup_changed)		(WorkbookControlGUI const *wbcg);

	/* virtuals */
	void (*set_transient)		(WorkbookControlGUI *wbcg, GtkWindow *window);
	void (*create_status_area)	(WorkbookControlGUI *wbcg, GtkWidget *progress,
					 GtkWidget *status, GtkWidget *autoexpr);
	void (*actions_sensitive)	(WorkbookControlGUI *wbcg,
					 gboolean actions, gboolean font_actions);
	void (*set_zoom_label)		(WorkbookControlGUI const *wbcg, char const *label);
	void (*reload_recent_file_menu)	(WorkbookControlGUI const *wbcg);
	void (*set_action_sensitivity)  (WorkbookControlGUI const *wbcg,
					 char const *action,
					 gboolean sensitive);
	void (*set_action_label)        (WorkbookControlGUI const *wbcg,
					 char const *action,
					 char const *prefix,
					 char const *suffix,
					 char const *new_tip);
	void (*set_toggle_action_state) (WorkbookControlGUI const *wbcg,
					 char const *action,
					 gboolean state);
} WorkbookControlGUIClass;

#define WORKBOOK_CONTROL_GUI_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), WORKBOOK_CONTROL_GUI_TYPE, WorkbookControlGUIClass))

#define GNM_RESPONSE_SAVE_ALL -1000
#define GNM_RESPONSE_DISCARD_ALL -1001

/* Protected functions */
void	 wbcg_set_toplevel	      (WorkbookControlGUI *wbcg, GtkWidget *w);
gboolean wbcg_close_control	      (WorkbookControlGUI *wbcg);
void	 scg_delete_sheet_if_possible (GtkWidget *ignored, SheetControlGUI *scg);
void	 wbcg_insert_sheet	      (GtkWidget *ignored, WorkbookControlGUI *wbcg);
void	 wbcg_append_sheet	      (GtkWidget *ignored, WorkbookControlGUI *wbcg);
void	 wbcg_clone_sheet	      (GtkWidget *ignored, WorkbookControlGUI *wbcg);
void	 wbcg_set_selection_halign    (WorkbookControlGUI *wbcg, GnmHAlign halign);
void	 wbcg_set_selection_valign    (WorkbookControlGUI *wbcg, GnmVAlign valign);

enum {
	WBCG_MARKUP_CHANGED,
	WBCG_LAST_SIGNAL
};

extern guint wbcg_signals [WBCG_LAST_SIGNAL];

#endif /* GNUMERIC_WORKBOOK_CONTROL_GUI_PRIV_H */

