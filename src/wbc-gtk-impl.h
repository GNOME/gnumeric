#ifndef _GNM_WBC_GTK_IMPL_H_
# define _GNM_WBC_GTK_IMPL_H_

#include <gnumeric.h>
#include <wbc-gtk.h>
#include <workbook-control-priv.h>
#include <style.h>
#include <widgets/gnm-expr-entry.h>
#include <widgets/gnm-notebook.h>
#include <gui-util.h>

#include <goffice/goffice.h>

G_BEGIN_DECLS

struct _WBCGtk {
	WorkbookControl	base;

	GtkWidget   *toplevel;

	GtkBuilder  *gui;

	/* The area that contains the sheet and the sheet tabs.  */
	GtkWidget   *notebook_area;

	/* The notebook that contains the sheets.  */
	GtkNotebook *snotebook;

	/* The notebook that contains the sheet tabs.  */
	GnmNotebook *bnotebook;

	/* The GtkPaned that contains the sheet tabs and the status area.  */
	GtkPaned    *tabs_paned;

	GtkWidget   *progress_bar;

	struct {
		GnmExprEntry *entry; /* The real edit line */
		GnmExprEntry *temp_entry; /* A tmp overlay eg from a guru */
		GtkWidget *guru;
		gulong         signal_changed, signal_insert, signal_delete;
		gulong         signal_cursor_pos, signal_selection_bound;
		PangoAttrList *cell_attrs;   /* Attrs from cell format. */
		PangoAttrList *markup;	     /* just the markup */
		PangoAttrList *full_content; /* cell_attrs+markup */
		PangoAttrList *cur_fmt;	/* attrs for new text (depends on position) */
	} edit_line;

	/* While editing these should be visible */
	GtkWidget *ok_button, *cancel_button;

	/* While not editing these should be visible */
	GtkWidget *func_button;

	gboolean    updating_ui;
	gboolean    inside_editing;

	/* Auto completion */
	GnmComplete	*auto_complete;
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
	GHashTable *visibility_widgets;

	gboolean is_fullscreen;
	GOUndo *undo_for_fullscreen;
	GSList *hide_for_fullscreen;

	/* Edit area */
	GtkWidget *selection_descriptor;	/* A GtkEntry */

	/* Autosave */
        gboolean   autosave_prompt;
        gint       autosave_time;
        guint      autosave_timer;

	PangoFontDescription *font_desc;

	SheetControlGUI *active_scg;
	gulong sig_view_changed;
	gulong sig_auto_expr_text, sig_auto_expr_attrs;
	gulong sig_show_horizontal_scrollbar, sig_show_vertical_scrollbar;
	gulong sig_show_notebook_tabs;
	gulong sig_sheet_order, sig_notify_uri, sig_notify_dirty;
	gpointer sig_wbv;

/**********************************************/
	GtkWidget	 *status_area;
	GtkUIManager     *ui;
	GtkActionGroup   *permanent_actions, *actions, *font_actions,
			 *data_only_actions, *semi_permanent_actions;
	struct {
		GtkActionGroup   *actions;
		guint		  merge_id;
	} file_history, toolbar, windows, templates;

	guint template_loader_handler;

	GOActionComboStack	*undo_haction, *redo_haction;
	GtkAction		*undo_vaction, *redo_vaction;
	GOActionComboColor	*fore_color, *back_color;
	GtkAction               *font_name_haction, *font_name_vaction;
	GOActionComboText	*zoom_haction;
	GtkAction               *zoom_vaction;
	GOActionComboPixmaps	*borders, *halignment, *valignment;
	struct {
		GtkToggleAction	 *bold, *italic, *underline, *d_underline,
			*sl_underline, *dl_underline;
		GtkToggleAction	 *superscript, *subscript, *strikethrough;
	} font;
	struct {
		GtkToggleAction	 *left, *center, *right, *center_across_selection;
	} h_align;
	struct {
		GtkToggleAction	 *top, *center, *bottom;
	} v_align;

	GtkWidget *menu_zone, *toolbar_zones[4];
	GHashTable *custom_uis;

	guint idle_update_style_feedback;

	/* When editing a cell: the cell (may be NULL) */
	GnmCell     *editing_cell;
	Sheet       *editing_sheet;
	gboolean     editing;

	SheetObject	 *new_object;	/* A newly created object that has yet to be realized */

	char *preferred_geometry;
};

typedef struct {
	WorkbookControlClass base;

	/* signals */
	void (*markup_changed)		(WBCGtk const *wbcg);
} WBCGtkClass;

#define GNM_RESPONSE_SAVE_ALL (-1000)
#define GNM_RESPONSE_DISCARD_ALL (-1001)

/* Protected functions */
gboolean wbc_gtk_close		(WBCGtk *wbcg);
void	 wbcg_insert_sheet	(GtkWidget *ignored, WBCGtk *wbcg);
void	 wbcg_append_sheet	(GtkWidget *ignored, WBCGtk *wbcg);
void	 wbcg_clone_sheet	(GtkWidget *ignored, WBCGtk *wbcg);

void	 wbc_gtk_init_editline	(WBCGtk *wbcg);
void	 wbc_gtk_init_actions	(WBCGtk *wbcg);
void	 wbc_gtk_markup_changer	(WBCGtk *wbcg);

void     wbcg_font_action_set_font_desc (GtkAction *act, PangoFontDescription *desc);

gboolean wbc_gtk_load_templates (WBCGtk *gtk);

GtkAction *wbcg_find_action (WBCGtk *wbcg, const char *name);

G_MODULE_EXPORT void set_uifilename (char const *name, GnmActionEntry const *actions, int nb);

G_END_DECLS

#endif /* _GNM_WBC_GTK_IMPL_H_ */
