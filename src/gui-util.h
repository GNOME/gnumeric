#ifndef _GNM_GUI_UTIL_H_
# define _GNM_GUI_UTIL_H_

#include <gnumeric-fwd.h>
#include <goffice/goffice.h>
#include <numbers.h>

G_BEGIN_DECLS

#define GNM_ACTION_DEF(name)			\
	void name (GtkAction *a, WBCGtk *wbcg)

GtkWidget* gnm_go_error_info_dialog_create (GOErrorInfo *error);
void       gnm_go_error_info_dialog_show (GtkWindow *parent,
					       GOErrorInfo *error);
void       gnm_go_error_info_list_dialog_show (GtkWindow *parent,
						    GSList *errs);
void       gnm_restore_window_geometry (GtkWindow *dialog,
					     const char *key);
void       gnm_keyed_dialog (WBCGtk *wbcg,
				  GtkWindow *dialog,
				  char const *key);
gpointer   gnm_dialog_raise_if_exists (WBCGtk *wbcg,
					    char const *key);
void       gnm_editable_enters	(GtkWindow *window, GtkWidget *editable);

/* Utility routine as Gtk does not have any decent routine to do this */
int gnm_gtk_radio_group_get_selected (GSList *radio_group);
/* Utility routine as libglade does not have any decent routine to do this */
int gnm_gui_group_value (gpointer gui, char const * const group[]);

/* Use this on menus that are popped up */
void gnumeric_popup_menu (GtkMenu *menu, GdkEvent *event);

/*
 * Pseudo-tooltip support code.
 */
void        gnm_position_tooltip (GtkWidget *tip, int px, int py,
				       gboolean horizontal);
GtkWidget  *gnm_create_tooltip (GtkWidget *ref_widget);
GtkWidget  *gnm_convert_to_tooltip (GtkWidget *ref_widget,
					 GtkWidget *widget);

GtkBuilder *gnm_gtk_builder_load (char const *uifile, char const *domain,
				 GOCmdContext *cc);

typedef struct {
	char const *name;
	char const *pixmap;
	int display_filter;
	int sensitive_filter;

	int index;

	char *allocated_name;
} GnmPopupMenuElement;

typedef void (*GnmPopupMenuHandler) (GnmPopupMenuElement const *e,
					  gpointer user_data);

void gnm_create_popup_menu (GnmPopupMenuElement const *elements,
			    GnmPopupMenuHandler handler,
			    gpointer user_data,
			    GDestroyNotify notify,
			    int display_filter,
			    int sensitive_filter,
			    GdkEvent *event);

#define gnm_filter_modifiers(a) ((a) & (GDK_MODIFIER_MASK & (~(GDK_LOCK_MASK|GDK_MOD2_MASK|GDK_MOD5_MASK))))

void gnm_init_help_button	(GtkWidget *w, char const *lnk);

char *gnm_textbuffer_get_text (GtkTextBuffer *buf);
char *gnm_textview_get_text (GtkTextView *text_view);
void  gnm_textview_set_text (GtkTextView *text_view, char const *txt);
void  gnm_load_pango_attributes_into_buffer (PangoAttrList  *markup,
					     GtkTextBuffer *buffer,
					     gchar const *str);
PangoAttrList *gnm_get_pango_attributes_from_buffer (GtkTextBuffer *buffer);

void focus_on_entry (GtkEntry *entry);

/* WARNING : These do not handle dates correctly
 * We should be passing in a DateConvention */
#define entry_to_float(entry, the_float, update)	\
	entry_to_float_with_format (entry, the_float, update, NULL)
gboolean entry_to_float_with_format (GtkEntry *entry,
				     gnm_float *the_float,
				     gboolean update,
				     GOFormat const *format);
gboolean entry_to_float_with_format_default (GtkEntry *entry,
					     gnm_float *the_float,
					     gboolean update,
					     GOFormat const *format,
					     gnm_float num);
gboolean entry_to_int	(GtkEntry *entry, gint *the_int, gboolean update);
void	 float_to_entry	(GtkEntry *entry, gnm_float the_float);
void	 int_to_entry	(GtkEntry *entry, gint the_int);

void gnm_link_button_and_entry (GtkWidget *button, GtkWidget *entry);

void gnm_widget_set_cursor_type (GtkWidget *w, GdkCursorType ct);
void gnm_widget_set_cursor (GtkWidget *w, GdkCursor *ct);

int gnm_widget_measure_string (GtkWidget *w, const char *s);

GtkWidget * gnm_message_dialog_create (GtkWindow * parent,
                                            GtkDialogFlags flags,
                                            GtkMessageType type,
                                            char const *primary_message,
                                            char const *secondary_message);

typedef gboolean (*gnm_iter_search_t) (GtkTreeModel *model, GtkTreeIter* iter);

typedef enum {
	GNM_DIALOG_DESTROY_SHEET_ADDED = 0x01,
	GNM_DIALOG_DESTROY_SHEET_REMOVED = 0x02,
	GNM_DIALOG_DESTROY_SHEET_RENAMED = 0x04,
	GNM_DIALOG_DESTROY_SHEETS_REORDERED = 0x08,
	GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED = 0x100,
	GNM_DIALOG_DESTROY_CURRENT_SHEET_RENAMED = 0x200
} GnmDialogDestroyOptions;

void gnm_dialog_setup_destroy_handlers (GtkDialog *dialog,
					WBCGtk *wbcg,
					GnmDialogDestroyOptions what);

void gnm_canvas_get_position (GocCanvas *canvas, int *x, int *y,
			      gint64 px, gint64 py);
void gnm_canvas_get_screen_position (GocCanvas *canvas,
				     double x, double y,
				     int *ix, int *iy);

gboolean gnm_check_for_plugins_missing (char const **ids, GtkWindow *parent);

void
gnm_cell_renderer_text_copy_background_to_cairo (GtkCellRendererText *crt,
						 cairo_t *cr);


/*
 * These macros exist to replace the old gtk+ stock items.  Note, that these only
 * cover strings with mnemonics.  I.e., you must handle icons and accelerators
 * in some other way.  (So why do we have them in the first place?  To ensure
 * the user interface is as consistent as possible.)
 */
#define GNM_STOCK_LABEL_CONTEXT "Stock label"

#define GNM_STOCK_OPEN g_dpgettext2(GETTEXT_PACKAGE, "Stock label", GNM_N_STOCK_OPEN)
#define GNM_N_STOCK_OPEN NC_("Stock label", "_Open")

#define GNM_STOCK_SAVE g_dpgettext2(GETTEXT_PACKAGE, "Stock label", GNM_N_STOCK_SAVE)
#define GNM_N_STOCK_SAVE NC_("Stock label", "_Save")

#define GNM_STOCK_SAVE_AS g_dpgettext2(GETTEXT_PACKAGE, "Stock label", GNM_N_STOCK_SAVE_AS)
#define GNM_N_STOCK_SAVE_AS NC_("Stock label", "Save _As")

#define GNM_STOCK_CANCEL g_dpgettext2(GETTEXT_PACKAGE, "Stock label", GNM_N_STOCK_CANCEL)
#define GNM_N_STOCK_CANCEL NC_("Stock label", "_Cancel")

#define GNM_STOCK_OK g_dpgettext2(GETTEXT_PACKAGE, "Stock label", GNM_N_STOCK_OK)
#define GNM_N_STOCK_OK NC_("Stock label", "_OK")


typedef struct
{
	const gchar *name;
	const gchar *icon;
	const gchar *label;
	const gchar *label_context;
	const gchar *accelerator;
	const gchar *tooltip;
	GCallback callback;

	// Visibility.
	guint hide_horizontal : 1;
	guint hide_vertical : 1;

	/* Fields for toggles.  */
	guint toggle : 1;
	guint is_active : 1;
} GnmActionEntry;

void gnm_action_group_add_actions (GtkActionGroup *group,
				   GnmActionEntry const *actions, size_t n,
				   gpointer user);
void gnm_action_group_add_action (GtkActionGroup *group, GtkAction *act);

void gnm_style_context_get_color (GtkStyleContext *context,
				  GtkStateFlags state,
				  GdkRGBA *color);

void gnm_get_link_color (GtkWidget *widget, GdkRGBA *res);
gboolean gnm_theme_is_dark (GtkWidget *widget);

void gnm_css_debug_color (const char *name, const GdkRGBA *color);
void gnm_css_debug_int (const char *name, int i);

G_END_DECLS

#endif /* _GNM_GUI_UTIL_H_ */
