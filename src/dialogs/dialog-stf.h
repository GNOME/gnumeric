#ifndef GNUMERIC_DIALOG_STF_H
#define GNUMERIC_DIALOG_STF_H

#include "dialog-stf-preview.h"
#include "gui-util.h"
#include <stf-parse.h>
#include <libgnomeui/gnome-druid.h>

/* Define for text offsets used on the main page of the druid */
#define TEXT_OFFSET 10.0

/* for the main_page */
typedef struct {
	/* Page members that are always present */
	GtkRadioButton  *main_separated;
	GtkRadioButton  *main_fixed;
	GtkSpinButton   *main_startrow;
	GtkSpinButton   *main_stoprow;
	GtkLabel        *main_lines;
	GtkWidget       *main_data_container;
	GtkCheckButton  *main_2x_indicator;
	GtkCombo        *main_textindicator;
	GtkEntry        *main_textfield;
     
	/* Page members that are created at run-time */
	RenderData_t    *main_run_renderdata;
} MainInfo_t;

/* for the csv_page */
typedef struct {
	GtkCheckButton  *csv_tab, *csv_colon, *csv_comma;
	GtkCheckButton  *csv_space, *csv_semicolon, *csv_pipe;
	GtkCheckButton  *csv_slash, *csv_hyphen, *csv_bang;
	GtkCheckButton  *csv_custom;
	GtkEntry        *csv_customseparator;
	GtkCheckButton  *csv_duplicates;
	GtkWidget       *csv_data_container;

	/* Page members that are created at run-time */
	RenderData_t       *csv_run_renderdata;
	StfParseOptions_t  *csv_run_parseoptions;
	int                 csv_run_scrollpos;
} CsvInfo_t;

/* for the fixed_page */
typedef struct {
	/*GtkCList*/void      *fixed_collist;
	GtkSpinButton *fixed_colend;
	GtkButton     *fixed_add, *fixed_remove, *fixed_clear, *fixed_auto;
	GtkWidget     *fixed_data_container;

	/* Page members that are created at run-time */
	RenderData_t      *fixed_run_renderdata;
	StfParseOptions_t *fixed_run_parseoptions;
	int                fixed_run_index;
	gboolean           fixed_run_manual;
	gboolean           fixed_run_mousedown;
	double             fixed_run_xorigin;
	int                fixed_run_column;
} FixedInfo_t;

/* for the format_page */
typedef struct {
	/*GtkCList*/ void          *format_collist, *format_sublist;
	GtkScrolledWindow *format_sublistholder;
	GtkEntry          *format_format;
	GtkWidget         *format_data_container;
        GtkOptionMenu     *format_trim;
     
	/* Page members that are created at run-time */
	RenderData_t      *format_run_renderdata;
	StfParseOptions_t *format_run_parseoptions;  /* Note : refers to either FixedInfo_t or CsvInfo_t parseoptions */
	RenderData_t      *format_run_source_hash;   /* Note : refers to either FixedInfo_t or CsvInfo_t RenderData_t */
	GPtrArray         *format_run_list; /* Contains StyleFormat* */
	int                format_run_index;
	gboolean           format_run_manual_change;
	gboolean           format_run_sublist_select;
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

	WorkbookControlGUI	*wbcg;
	GtkWindow      *window;                                            /* The main window */
	GnomeDruid     *druid;                                             /* The gnome druid */
	GnomeDruidPage *main_page, *csv_page, *fixed_page, *format_page;   /* Rest of the pages */

	const char   *filename;     /* File we are reading from */
	const char   *data;         /* Pointer to beginning of data */
	const char   *cur;          /* Pointer pointing to position in data to start parsing */
	int           lines;        /* Number of lines @data consists of */
	int           importlines;  /* Number of lines to import */


	MainInfo_t   *main_info;
	CsvInfo_t    *csv_info;
	FixedInfo_t  *fixed_info;
	FormatInfo_t *format_info;

	gboolean       canceled;   /* Indicates whether the user pressed cancel button */
	StfParseType_t parsetype;  /* Indicates the parse type the user choose */

	StfTrimType_t  trim;       /* Do we want to trim and if so -> how? */
} DruidPageData_t;

typedef struct {
	char const        *newstart;      /* New start position */
	int                lines;         /* Number of lines to parse */
	StfParseOptions_t *parseoptions;  /* parse options */
	GPtrArray         *formats;       /* Contains StyleFormat *s */
} DialogStfResult_t;

/* This is the main function which handles all the dialog import stuff */
DialogStfResult_t *stf_dialog                           (WorkbookControlGUI *wbcg, const char *filename,
							 const char *data);
void               stf_dialog_result_free               (DialogStfResult_t *dialogresult);

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
void    stf_dialog_main_page_cleanup                    (DruidPageData_t *pagedata);
void    stf_dialog_csv_page_cleanup                     (DruidPageData_t *pagedata);
void    stf_dialog_fixed_page_cleanup                   (DruidPageData_t *pagedata);
void    stf_dialog_format_page_cleanup                  (DruidPageData_t *pagedata);

#endif /* GNUMERIC_DIALOG_STF_H */
