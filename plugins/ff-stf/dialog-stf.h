#ifndef GNUMERIC_DIALOG_STF_H
#define GNUMERIC_DIALOG_STF_H

#include <glade/glade.h>

#include "formats.h"
#include "style.h"
#include "mstyle.h"

#include "stf.h"
#include "stf-separated.h"
#include "stf-fixed.h"
#include "dialog-stf-preview.h"

/* Define for text offsets used troughout the druid */
#define TEXT_OFFSET 10.0
#define TEXT_VPADDING 10.0

/* for the main_page */
typedef struct {
	/* Page members that are always present */
	GtkRadioButton *main_separated, *main_fixed;
	GtkSpinButton  *main_startrow;
	GtkLabel       *main_lines;
	GtkFrame       *main_frame;
	GnomeCanvas    *main_canvas;
	
	/* Page members that are created at run-time */
	GnomeCanvasText *main_run_text;
	GnomeCanvasRect *main_run_rect;
} MainInfo_t;

/* for the csv_page */
typedef struct {
	GtkCheckButton  *csv_tab, *csv_colon, *csv_comma, *csv_space, *csv_custom;
	GtkEntry        *csv_customseparator;
	GtkCheckButton  *csv_duplicates;
	GtkCombo        *csv_textindicator;
	GtkEntry        *csv_textfield;
	GnomeCanvas     *csv_canvas;
	GtkVScrollbar   *csv_scroll;

	/* Page members that are created at run-time */
	RenderData_t    *csv_run_renderdata;
	SeparatedInfo_t *csv_run_sepinfo;
	int              csv_run_scrollpos;
} CsvInfo_t;

/* for the fixed_page */
typedef struct {
	GtkCList      *fixed_collist;
	GtkSpinButton *fixed_colend;
	GtkButton     *fixed_add, *fixed_remove;
	GnomeCanvas   *fixed_canvas;
	GtkVScrollbar *fixed_scroll;

	/* Page members that are created at run-time */
	RenderData_t     *fixed_run_renderdata;
	ParseFixedInfo_t *fixed_run_fixinfo;
	int               fixed_run_index;
	gboolean          fixed_run_manual;
	gboolean          fixed_run_modified;
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
	GSList            *format_run_list;
	int                format_run_index;
	gboolean           format_run_manual_change;
} FormatInfo_t;


/* Global stuff */
typedef enum {
	DPG_MAIN,
	DPG_CSV,
	DPG_FIXED,
	DPG_FORMAT,
	DPG_STOP
} DruidPosition_t;

/* The MOTHER struct, passed trough signal handlers etc
 * contains pointers to nearly all members in the druid 
 */
typedef struct {
	DruidPosition_t position;                                        /* Current position */

	GtkWindow      *window;                                            /* The main window */
	GnomeDruid     *druid;                                             /* The gnome druid */
	GnomeDruidPage *stop_page;                                         /* end page */
	GnomeDruidPage *main_page, *csv_page, *fixed_page, *format_page;   /* Rest of the pages */

	FileSource_t *src;

	MainInfo_t   *main_info;
	CsvInfo_t    *csv_info;
	FixedInfo_t  *fixed_info;
	FormatInfo_t *format_info;

	gboolean canceled;   /* Indicates weather the user pressed cancel button */
} DruidPageData_t;

/* This is the main function which handles all the dialog import stuff */
char*   dialog_stf             (FileSource_t *src);

/* INIT FUNCTIONS
 *
 * These are called when the Dialog is created and give
 * each page the opportunity to connect signal handlers and set the contents
 * of their Info_t record
 */
void    main_page_init         (GladeXML *gui, DruidPageData_t *pagedata);
void    csv_page_init          (GladeXML *gui, DruidPageData_t *pagedata);
void    fixed_page_init        (GladeXML *gui, DruidPageData_t *pagedata);
void    format_page_init       (GladeXML *gui, DruidPageData_t *pagedata);

/* PREPARE functions
 *
 * These functions are called just before a page is shown
 * and allows a page to set stuff that depends on previous
 * pages
 * NOTE : These are signal handlers which are directly coupled to
 *        the corresponding page. (in dialog_stf()) Don't call them directly!
 */
void    csv_page_prepare       (GnomeDruidPage *page, GnomeDruid *druid, DruidPageData_t *data);
void    fixed_page_prepare     (GnomeDruidPage *page, GnomeDruid *druid, DruidPageData_t *data);
void    format_page_prepare    (GnomeDruidPage *page, GnomeDruid *druid, DruidPageData_t *data);


/* CLEANUP functions
 *
 * These are called when the druid has finished.
 * Pages can free dynamic run-time data here
 * Not every page MUST have this, it is optional
 */
void    csv_page_cleanup       (DruidPageData_t *pagedata);
void    fixed_page_cleanup     (DruidPageData_t *pagedata);
void    format_page_cleanup    (DruidPageData_t *pagedata);
 
#endif /* GNUMERIC_DIALOG_STF_H */









