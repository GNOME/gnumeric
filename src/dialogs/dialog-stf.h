#ifndef GNUMERIC_DIALOG_STF_H
#define GNUMERIC_DIALOG_STF_H

#include <config.h>
#include <gnome.h>
#include <glade/glade.h>

#include <stdlib.h>
#include <string.h>

#include "formats.h"
#include "style.h"
#include "mstyle.h"
#include "gnumeric-util.h"

#include "stf-parse.h"

#include "dialog-stf-preview.h"

#define LINE_DISPLAY_LIMIT 128

/* Define for text offsets used on the main page of the druid */
#define TEXT_OFFSET 10.0

/* for the main_page */
typedef struct {
	/* Page members that are always present */
	GtkRadioButton *main_separated;
	GtkRadioButton *main_fixed;
	GtkSpinButton  *main_startrow;
	GtkSpinButton  *main_stoprow;
	GtkOptionMenu  *main_trim;
	GtkLabel       *main_lines;
	GtkFrame       *main_frame;
	GnomeCanvas    *main_canvas;

	/* Page members that are created at run-time */
	GnomeCanvasText *main_run_text;
	GnomeCanvasRect *main_run_rect;
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
	GtkCombo        *csv_textindicator;
	GtkEntry        *csv_textfield;
	GnomeCanvas     *csv_canvas;
	GtkVScrollbar   *csv_scroll;

	/* Page members that are created at run-time */
	RenderData_t       *csv_run_renderdata;
	StfParseOptions_t  *csv_run_parseoptions;
	int                 csv_run_scrollpos;
	int                 csv_run_displayrows;
} CsvInfo_t;

/* for the fixed_page */
typedef struct {
	GtkCList      *fixed_collist;
	GtkSpinButton *fixed_colend;
	GtkButton     *fixed_add, *fixed_remove, *fixed_clear, *fixed_auto;
	GnomeCanvas   *fixed_canvas;
	GtkVScrollbar *fixed_scroll;

	/* Page members that are created at run-time */
	RenderData_t      *fixed_run_renderdata;
	StfParseOptions_t *fixed_run_parseoptions;
	int                fixed_run_index;
	gboolean           fixed_run_manual;
	int                fixed_run_displayrows;
	gboolean           fixed_run_mousedown;
	double             fixed_run_xorigin;
	int                fixed_run_column;
} FixedInfo_t;

/* for the format_page */
typedef struct {
	GtkCList          *format_collist, *format_sublist;
	GtkScrolledWindow *format_sublistholder;
	GtkEntry          *format_format;
	GnomeCanvas       *format_canvas;
	GtkVScrollbar     *format_scroll;

	/* Page members that are created at run-time */
	RenderData_t      *format_run_renderdata;
	StfParseOptions_t *format_run_parseoptions;  /* Note : refers to either FixedInfo_t or CsvInfo_t parseoptions */
	RenderData_t      *format_run_source_hash;   /* Note : refers to either FixedInfo_t or CsvInfo_t RenderData_t */
	GSList            *format_run_list; /* List of StyleFormat * */
	int                format_run_index;
	gboolean           format_run_manual_change;
	gboolean           format_run_sublist_select;
	int                format_run_displayrows;   /* Number of rows to display in the preview window */
} FormatInfo_t;


/* Global stuff */
typedef enum {
	DPG_MAIN,
	DPG_CSV,
	DPG_FIXED,
	DPG_FORMAT
} DruidPosition_t;

/* The MOTHER struct, passed trough signal handlers etc
 * contains pointers to nearly all members in the druid
 */
typedef struct {
	DruidPosition_t position;                                        /* Current position */

	GtkWindow      *window;                                            /* The main window */
	GnomeDruid     *druid;                                             /* The gnome druid */
	GnomeDruidPage *main_page, *csv_page, *fixed_page, *format_page;   /* Rest of the pages */

	const char   *filename;     /* File we are reading from */
	const char   *data;         /* Pointer to beginning of data */
	const char   *cur;          /* Pointer pointing to position in data to start parsing */
	int           lines;        /* Number of lines @data consists of */
	int           colcount;     /* Number of columns @data consists of */
	int           importlines;  /* Number of lines to import */


	MainInfo_t   *main_info;
	CsvInfo_t    *csv_info;
	FixedInfo_t  *fixed_info;
	FormatInfo_t *format_info;

	gboolean       canceled;   /* Indicates weather the user pressed cancel button */
	StfParseType_t parsetype;  /* Indicates the parse type the user choose */

	StfTrimType_t  trim;       /* Do we want to trim and if so -> how? */
} DruidPageData_t;

typedef struct {
	const char        *newstart;      /* New start position */
	StfParseOptions_t *parseoptions;  /* parse options */
	GSList            *formats;       /* A list of char*'s corresponding to each columns format */
} DialogStfResult_t;

/* This is the main function which handles all the dialog import stuff */
DialogStfResult_t *stf_dialog                           (WorkbookControlGUI *wbcg, const char *filename,
							 const char *data);
void               stf_dialog_result_free               (DialogStfResult_t *dialogresult);

/* UTILITY FUNCTIONS
 *
 * These are utility functions that can be used by the separate pages
 */
void    stf_dialog_set_scroll_region_and_prevent_center (GnomeCanvas *canvas, GnomeCanvasRect *rectangle,
							 double width, double height);

/* INIT FUNCTIONS
 *
 * These are called when the Dialog is created and give
 * each page the opportunity to connect signal handlers and set the contents
 * of their Info_t record
 */
void    stf_dialog_main_page_init                       (GladeXML *gui, DruidPageData_t *pagedata);
void    stf_dialog_csv_page_init                        (GladeXML *gui, DruidPageData_t *pagedata);
void    stf_dialog_fixed_page_init                      (GladeXML *gui, DruidPageData_t *pagedata);
void    stf_dialog_format_page_init                     (GladeXML *gui, DruidPageData_t *pagedata);

/* PREPARE functions
 *
 * These functions are called just before a page is shown
 * and allows a page to set stuff that depends on previous
 * pages
 * NOTE : These are signal handlers which are directly coupled to
 *        the corresponding page. (in dialog_stf()) Don't call them directly!
 */
void    stf_dialog_csv_page_prepare                     (GnomeDruidPage *page, GnomeDruid *druid,
							 DruidPageData_t *data);
void    stf_dialog_fixed_page_prepare                   (GnomeDruidPage *page, GnomeDruid *druid,
							 DruidPageData_t *data);
void    stf_dialog_format_page_prepare                  (GnomeDruidPage *page, GnomeDruid *druid,
							 DruidPageData_t *data);

/* CLEANUP functions
 *
 * These are called when the druid has finished.
 * Pages can free dynamic run-time data here
 * Not every page MUST have this, it is optional
 */
void    stf_dialog_csv_page_cleanup                     (DruidPageData_t *pagedata);
void    stf_dialog_fixed_page_cleanup                   (DruidPageData_t *pagedata);
void    stf_dialog_format_page_cleanup                  (DruidPageData_t *pagedata);

#endif /* GNUMERIC_DIALOG_STF_H */
