#ifndef GNUMERIC_WORKBOOK_H
#define GNUMERIC_WORKBOOK_H

#ifdef ENABLE_BONOBO
#   include <bonobo/gnome-object.h>
#else
#   include <gtk/gtkobject.h>
#endif

#define WORKBOOK_TYPE        (workbook_get_type ())
#define WORKBOOK(o)          (GTK_CHECK_CAST ((o), WORKBOOK_TYPE, Workbook))
#define WORKBOOK_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), WORKBOOK_TYPE, WorkbookClass))
#define IS_WORKBOOK(o)       (GTK_CHECK_TYPE ((o), WORKBOOK_TYPE))
#define IS_WORKBOOK_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), WORKBOOK_TYPE))

#ifdef ENABLE_BONOBO
#   define WORKBOOK_PARENT_CLASS      GnomeObject
#   define WORKBOOK_PARENT_CLASS_TYPE GNOME_OBJECT_TYPE
#else
#   define WORKBOOK_PARENT_CLASS      GtkObject
#   define WORKBOOK_PARENT_CLASS_TYPE gtk_object_get_type()
#endif

/* Forward declaration */
struct _PrintInformation;
typedef struct _PrintInformation PrintInformation;


struct _Workbook {
#ifdef ENABLE_BONOBO
	/* The base object for the Workbook */
	GnomeObject bonobo_object;

	/* A GnomeContainer */
	GnomeContainer *gnome_container;

	/* A list of EmbeddableGrids exported to the world */
	GList      *bonobo_regions;
#else
	GtkObject  gtk_object;
#endif

	char       *filename;

        GtkWidget  *toplevel; 
	GtkWidget  *notebook;
	GtkWidget  *table;

	/* Edit area */
	GtkWidget  *ea_status;
	GtkWidget  *ea_button_box;
	GtkWidget  *ea_input;

        /* The status bar */
        GnomeAppBar * appbar;
  
	/* The auto-expression */
	ExprTree   *auto_expr;
	String     *auto_expr_desc;
	GnomeCanvasItem  *auto_expr_label;
	
	/* Styles */
	Style      style;

	/* The sheets */ 
	GHashTable *sheets;	/* keeps a list of the Sheets on this workbook */

	/* User defined names */
	GList      *names;
	
	/* A list with all of the formulas */
	GList      *formula_cell_list;

	/* A queue with the cells to be evaluated */
	GList      *eval_queue;
	int        max_iterations;

	int        generation;

	/* The clipboard for this workbook */
	CellRegion *clipboard_contents;

	gboolean   have_x_selection;

	/* The Symbol table used for naming cell ranges in the workbook */
	SymbolTable *symbol_names;

	/* Attached summary information */
	SummaryInfo *sin;

	/*
	 * This is  used during the clipboard paste command to pass information
	 * to the asyncronous paste callback
	 */
	void       *clipboard_paste_callback_data;

	PrintInformation *print_info;

	void       *toolbar;

	void       *corba_server;
};

typedef struct {
#ifdef ENABLE_BONOBO
	GnomeObjectClass bonobo_parent_class;
#else
	GtkObjectClass   gtk_parent_class;
#endif
} WorkbookClass;

GtkType     workbook_get_type            (void);
Workbook   *workbook_new                 (void);
Workbook   *workbook_core_new            (void);
Workbook   *workbook_new_with_sheets     (int sheet_count);

void        workbook_set_filename        (Workbook *, const char *);
void        workbook_set_title           (Workbook *, const char *);
Workbook   *workbook_read                (const char *filename);

void        workbook_save_as             (Workbook *);
void        workbook_save                (Workbook *);
void        workbook_print               (Workbook *);
void        workbook_attach_sheet        (Workbook *, Sheet *);
gboolean    workbook_detach_sheet        (Workbook *, Sheet *, gboolean);
Sheet      *workbook_focus_current_sheet (Workbook *wb);
void        workbook_focus_sheet         (Sheet *sheet);
Sheet      *workbook_get_current_sheet   (Workbook *wb);
char       *workbook_sheet_get_free_name (Workbook *wb);
void        workbook_auto_expr_label_set (Workbook *wb, const char *text);
void        workbook_next_generation     (Workbook *wb);
void        workbook_set_region_status   (Workbook *wb, const char *str);
int         workbook_parse_and_jump      (Workbook *wb, const char *text);
Sheet      *workbook_sheet_lookup        (Workbook *wb, const char *sheet_name);
void        workbook_mark_clean          (Workbook *wb);
void        workbook_set_dirty           (Workbook *wb, gboolean is_dirty);
gboolean    workbook_rename_sheet        (Workbook *wb,
					  const char *old_name,
					  const char *new_name);
int         workbook_sheet_count         (Workbook *wb);
gboolean    workbook_can_detach_sheet    (Workbook *wb, Sheet *sheet);
GList      *workbook_sheets              (Workbook *wb);
char       *workbook_selection_to_string (Workbook *wb, Sheet *base_sheet);

/*
 * Does any pending recalculations
 */
void        workbook_recalc              (Workbook *wb);
void        workbook_recalc_all          (Workbook *wb);

/*
 * Callback routine: invoked when the first view ItemGrid
 * is realized to allocate the default styles
 */
void     workbook_realized            (Workbook *, GdkWindow *);

typedef gboolean (*WorkbookCallback)(Workbook *, gpointer data);

void     workbook_foreach             (WorkbookCallback cback,
				       gpointer data);

void	workbook_fixup_references	(Workbook *wb, Sheet *sheet,
					 int col, int row,
					 int coldelta, int rowdelta);
void	workbook_invalidate_references	(Workbook *wb, Sheet *sheet,
					 int col, int row,
					 int colcount, int rowcount);


/*
 * Feedback routines
 */
typedef enum {
	WORKBOOK_FEEDBACK_BOLD,
	WORKBOOK_FEEDBACK_ITALIC
} WorkbookFeedbackType;

void     workbook_feedback_set        (Workbook *,
				       WorkbookFeedbackType type,
				       void *data);

extern   Workbook *current_workbook;

/*
 * Hooks for CORBA bootstrap: they create the 
 */
void workbook_corba_setup    (Workbook *);
void workbook_corba_shutdown (Workbook *);

#endif
