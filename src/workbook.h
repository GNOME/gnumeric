#ifndef GNUMERIC_WORKBOOK_H
#define GNUMERIC_WORKBOOK_H

#ifdef ENABLE_BONOBO
#   include <bonobo.h>
#else
#   include <gtk/gtkobject.h>
#endif
#include <gtk/gtkwidget.h>
#include <gnome.h>

#define GNUMERIC_WORKBOOK_GOAD_ID         "IDL:GNOME:Gnumeric:Workbook:1.0"
#define GNUMERIC_WORKBOOK_FACTORY_GOAD_ID "IDL:GNOME:Gnumeric:WorkbookFactory:1.0"

#define WORKBOOK_TYPE        (workbook_get_type ())
#define WORKBOOK(o)          (GTK_CHECK_CAST ((o), WORKBOOK_TYPE, Workbook))
#define WORKBOOK_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), WORKBOOK_TYPE, WorkbookClass))
#define IS_WORKBOOK(o)       (GTK_CHECK_TYPE ((o), WORKBOOK_TYPE))
#define IS_WORKBOOK_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), WORKBOOK_TYPE))

#ifdef ENABLE_BONOBO
#   define WORKBOOK_PARENT_CLASS      BonoboObject
#   define WORKBOOK_PARENT_CLASS_TYPE BONOBO_OBJECT_TYPE
#else
#   define WORKBOOK_PARENT_CLASS      GtkObject
#   define WORKBOOK_PARENT_CLASS_TYPE gtk_object_get_type()
#endif

/*
 * FIXME FIXME FIXME
 * WARNING WARNING WARNING
 * Inorder for this file to work config.h MUST
 * be included first.
 *
 * This seems a poor choice (IMHO).
 * It would be better for this file to include it directly.
 */
#include "gnumeric.h"
#include "symbol.h"
#include "summary.h"
#include "file.h"

typedef struct _WorkbookPrivate WorkbookPrivate;
struct _Workbook {
#ifdef ENABLE_BONOBO
	/* The base object for the Workbook */
	BonoboObject bonobo_object;

	/* A BonoboContainer */
	BonoboContainer   *bonobo_container;

	BonoboPersistFile *persist_file;
	
	/* A list of EmbeddableGrids exported to the world */
	GList      *workbook_views;

	BonoboUIHandler *uih;
#else
	GtkObject  gtk_object;
#endif
	/* Attribute list */
	GList *attributes;

	/* { Start view specific elements */
        GtkWidget  *toplevel; 
	GtkWidget  *notebook;

	/* Edit area */
	GtkWidget  *ea_input;
	/* } End view specific elements */

	char       *filename;
	FileFormatLevel file_format_level;
	FileFormatSave  file_save_fn;

	/* Undo support */
	GSList	   *undo_commands;
	GSList	   *redo_commands;

	/* The auto-expression */
	ExprTree   *auto_expr;
	String     *auto_expr_desc;
	
	/* The sheets */ 
	GHashTable *sheets;	/* keeps a list of the Sheets on this workbook */
	Sheet	   *current_sheet;

	/* User defined names */
	GList      *names;
	
	/* A list with all of the formulas */
	GList      *formula_cell_list;

	/* A queue with the cells to be evaluated */
	GList     *eval_queue;
	int        max_iterations;

	guint8     generation;

	/* The Symbol table used for naming cell ranges in the workbook */
	SymbolTable *symbol_names;

	/* Attached summary information */
	SummaryInfo *summary_info;

	/* When editing a cell: the cell (may be NULL) */
	Cell        *editing_cell;
	Sheet       *editing_sheet;
	gboolean     editing;
	gboolean     use_absolute_cols;
	gboolean     use_absolute_rows;

	/*
	 * This is  used during the clipboard paste command to pass information
	 * to the asyncronous paste callback
	 */
	void       *clipboard_paste_callback_data;

	void       *corba_server;

	WorkbookPrivate *priv;

	/* Workbook level preferences */
        gboolean   autosave;
        gboolean   autosave_prompt;
        gint       autosave_minutes;
        gint       autosave_timer;
	gboolean   show_horizontal_scrollbar;
	gboolean   show_vertical_scrollbar;
	gboolean   show_notebook_tabs;
};

typedef struct {
#ifdef ENABLE_BONOBO
	BonoboObjectClass bonobo_parent_class;
#else
	GtkObjectClass   gtk_parent_class;
#endif
	
	/* Signals */
	void (*sheet_entered) (Sheet *sheet);
	void (*cell_changed)  (Sheet *sheet, char *contents,
			       int col, int row);
} WorkbookClass;

GtkType     workbook_get_type            (void);
Workbook   *workbook_new                 (void);
Workbook   *workbook_core_new            (void);
Workbook   *workbook_new_with_sheets     (int sheet_count);

void        workbook_set_attributev      (Workbook *wb, GList *list);
GtkArg     *workbook_get_attributev      (Workbook *wb, guint *n_args);

gboolean    workbook_set_filename        (Workbook *, const char *);
gboolean    workbook_set_saveinfo        (Workbook *,  const char *,
					  FileFormatLevel, FileFormatSave);
Workbook   *workbook_try_read            (CommandContext *context,
					  const char *filename);
Workbook   *workbook_read                (CommandContext *context,
					  const char *filename);

gboolean    workbook_save_as             (CommandContext *context, Workbook *);
gboolean    workbook_save                (CommandContext *context, Workbook *);
void        workbook_print               (Workbook *, gboolean);
void        workbook_attach_sheet        (Workbook *, Sheet *);
gboolean    workbook_detach_sheet        (Workbook *, Sheet *, gboolean);
Sheet      *workbook_focus_current_sheet (Workbook *wb);
void        workbook_focus_sheet         (Sheet *sheet);
char       *workbook_sheet_get_free_name (Workbook *wb,
					  const char * const base,
					  gboolean always_suffix);
void        workbook_auto_expr_label_set (Workbook *wb, const char *text);
void        workbook_set_region_status   (Workbook *wb, const char *str);
int         workbook_parse_and_jump      (Workbook *wb, const char *text);
Sheet      *workbook_sheet_lookup        (Workbook *wb, const char *sheet_name);
void        workbook_mark_clean          (Workbook *wb);
void        workbook_set_dirty           (Workbook *wb, gboolean is_dirty);
gboolean    workbook_is_dirty            (Workbook *wb);
gboolean    workbook_is_pristine         (Workbook *wb);
gboolean    workbook_rename_sheet        (Workbook *wb,
					  const char *old_name,
					  const char *new_name);
int         workbook_sheet_count         (Workbook *wb);
GList      *workbook_sheets              (Workbook *wb);
char       *workbook_selection_to_string (Workbook *wb, Sheet *base_sheet);

GSList     *workbook_expr_relocate       (Workbook *wb,
					  ExprRelocateInfo const *info);
void        workbook_expr_unrelocate     (Workbook *wb, GSList *info);
void        workbook_expr_unrelocate_free(GSList *info);

void        workbook_move_sheet          (Sheet *sheet, int direction);
void        workbook_delete_sheet        (Sheet *sheet);

/*
 * Does any pending recalculations
 */
void        workbook_recalc              (Workbook *wb);
void        workbook_recalc_all          (Workbook *wb);

typedef gboolean (*WorkbookCallback)(Workbook *, gpointer data);

void        workbook_foreach             (WorkbookCallback cback,
					  gpointer data);

CommandContext *workbook_command_context_gui (Workbook *wb);

void        workbook_autosave_cancel     (Workbook *wb);
void        workbook_autosave_set        (Workbook *wb, int minutes, gboolean prompt);

void        workbook_start_editing_at_cursor (Workbook *wb, gboolean blankp, gboolean cursorp);
void        workbook_finish_editing          (Workbook *wb, gboolean const accept);

/*
 * Feedback routines
 */
typedef enum {
	WORKBOOK_FEEDBACK_BOLD      = 1 << 0,
	WORKBOOK_FEEDBACK_ITALIC    = 1 << 1,
	WORKBOOK_FEEDBACK_FONT_SIZE = 1 << 2,
	WORKBOOK_FEEDBACK_FONT      = 1 << 3,
} WorkbookFeedbackType;

void     workbook_feedback_set        (Workbook *, MStyle *style);
void     workbook_zoom_feedback_set   (Workbook *, double zoom_factor);

/*
 * Hooks for CORBA bootstrap: they create the 
 */
void workbook_corba_setup    (Workbook *);
void workbook_corba_shutdown (Workbook *);

#endif
