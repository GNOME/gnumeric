#ifndef GNUMERIC_DIALOG_STF_H
#define GNUMERIC_DIALOG_STF_H

#include "dialog-stf-preview.h"
#include "gui-util.h"
#include <stf-parse.h>
#include "widgets/widget-charmap-selector.h"
#include "widgets/widget-locale-selector.h"
#include "widgets/widget-format-selector.h"

#include <gtk/gtkradiobutton.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkoptionmenu.h>
#include <gtk/gtknotebook.h>

/* for the main_page */
typedef struct {
	/* Page members that are always present */
	GtkRadioButton  *main_separated;
	GtkRadioButton  *main_fixed;
	GtkSpinButton   *main_startrow;
	GtkSpinButton   *main_stoprow;
	GtkLabel        *main_lines;
	GtkWidget       *main_data_container;
	GtkCheckButton  *line_break_unix;
	GtkCheckButton  *line_break_windows;
	GtkCheckButton  *line_break_mac;
     
	/* Page members that are created at run-time */
	CharmapSelector *charmap_selector;
	RenderData_t    *renderdata;
} MainInfo_t;

/* for the csv_page */
typedef struct {
	GtkCheckButton  *csv_tab, *csv_colon, *csv_comma;
	GtkCheckButton  *csv_space, *csv_semicolon, *csv_pipe;
	GtkCheckButton  *csv_slash, *csv_hyphen, *csv_bang;
	GtkCheckButton  *csv_custom;
	GtkEntry        *csv_customseparator;
	GtkCheckButton  *csv_duplicates;
	GtkCheckButton  *csv_2x_indicator;
	GtkWidget       *csv_textindicator;
	GtkEntry        *csv_textfield;
	GtkWidget       *csv_data_container;

	/* Page members that are created at run-time */
	RenderData_t       *renderdata;
} CsvInfo_t;

/* for the fixed_page */
typedef struct {
	GtkButton     *fixed_clear, *fixed_auto;
	GtkWidget     *fixed_data_container;

	/* Page members that are created at run-time */
	RenderData_t  *renderdata;
	int            context_col, context_dx;  
} FixedInfo_t;

/* for the format_page */
typedef struct {
	GtkWidget         *format_data_container;
        GtkWidget	  *format_trim;
	NumberFormatSelector *format_selector;
	GtkWidget         *column_selection_label;
     
	/* Page members that are created at run-time */
	LocaleSelector    *locale_selector;
	RenderData_t      *renderdata;
	GPtrArray         *formats; /* Contains GnmFormat* */
	int                index;
	gboolean           manual_change;
	gboolean           sublist_select;
        gboolean          *col_import_array;
        int                col_import_count;
        int                col_import_array_len;
        char              *col_header;
        gulong             format_changed_handler_id;
} FormatInfo_t;


/* Global stuff */
typedef enum {
	DPG_MAIN,
	DPG_CSV,
	DPG_FIXED,
	DPG_FORMAT
} StfDialogPage;

/* The MOTHER struct, passed trough signal handlers etc
 * contains pointers to nearly all members in the druid
 */
typedef struct {
	WorkbookControlGUI    *wbcg;
	GtkDialog             *dialog;                                           /* The main window */
	GtkNotebook           *notebook;
	GtkWidget             *next_button;
	GtkWidget             *back_button;
	GtkWidget             *cancel_button;
	GtkWidget             *help_button;
	GtkWidget             *finish_button;

	char                  *encoding;
	gboolean               fixed_encoding;
	char                  *locale;
	gboolean               fixed_locale;
	const char            *raw_data;     /* Raw bytes, not UTF-8.  */
	int                    raw_data_len;
	char                  *utf8_data;    /* raw_data converted into UTF-8.  */
	const char            *cur;          /* Pointer pointing to position in utf8_data to start parsing */
	const char            *cur_end;      /* Pointer pointing to position in utf8_data to stop parsing */

	const char            *source;       /* Where we are reading from (UTF-8) */

	int                    rowcount;
	int                    longest_line;  /* #characters in longest line.  */

	MainInfo_t            main;
	CsvInfo_t             csv;
	FixedInfo_t           fixed;
	FormatInfo_t          format;

	gboolean              canceled;   /* Indicates whether the user pressed cancel button */
	StfParseOptions_t    *parseoptions;
} StfDialogData;

typedef struct {
	char              *encoding;

	char              *text;          /* Decoded text.  */
	int                rowcount;      /* Number of resulting rows.  */
	StfParseOptions_t *parseoptions;  /* parse options */
} DialogStfResult_t;

/* This is the main function which handles all the dialog import stuff */
DialogStfResult_t *stf_dialog                           (WorkbookControlGUI *wbcg,
							 const char *opt_encoding,
							 gboolean fixed_encoding,
							 const char *opt_locale,
							 gboolean fixed_locale,
							 const char *source,
							 const char *data,
							 int data_len);
void               stf_dialog_result_free               (DialogStfResult_t *dialogresult);

void    stf_dialog_result_attach_formats_to_cr (DialogStfResult_t *dialogresult,
						GnmCellRegion *cr);

/* INIT FUNCTIONS
 *
 * These are called when the Dialog is created and give
 * each page the opportunity to connect signal handlers and set the contents
 * of their Info_t record
 */
void    stf_dialog_main_page_init                       (GladeXML *gui, StfDialogData *pagedata);
void    stf_dialog_csv_page_init                        (GladeXML *gui, StfDialogData *pagedata);
void    stf_dialog_fixed_page_init                      (GladeXML *gui, StfDialogData *pagedata);
void    stf_dialog_format_page_init                     (GladeXML *gui, StfDialogData *pagedata);

/* CLEANUP functions
 *
 * These are called when the druid has finished.
 * Pages can free dynamic run-time data here
 * Not every page MUST have this, it is optional
 */
void    stf_dialog_main_page_cleanup                    (StfDialogData *pagedata);
void    stf_dialog_csv_page_cleanup                     (StfDialogData *pagedata);
void    stf_dialog_fixed_page_cleanup                   (StfDialogData *pagedata);
void    stf_dialog_format_page_cleanup                  (StfDialogData *pagedata);

void    stf_dialog_main_page_prepare                    (StfDialogData *pagedata);
void    stf_dialog_csv_page_prepare                     (StfDialogData *pagedata);
void    stf_dialog_fixed_page_prepare                   (StfDialogData *pagedata);
void    stf_dialog_format_page_prepare                  (StfDialogData *pagedata);

#endif /* GNUMERIC_DIALOG_STF_H */
