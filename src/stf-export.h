#ifndef GNUMERIC_STF_EXPORT_H
#define GNUMERIC_STF_EXPORT_H

#include "gnumeric.h"

/*
 * Callback functions
 */
typedef gboolean (* StfEWriteFunc) (const char *string, gpointer data);

typedef enum {
	TERMINATOR_TYPE_LINEFEED        = 1 << 0, /* \n */
	TERMINATOR_TYPE_RETURN          = 1 << 1, /* \r */
	TERMINATOR_TYPE_RETURN_LINEFEED = 1 << 2, /* \r\n */
	TERMINATOR_TYPE_UNKNOWN         = 1 << 3  /* Dummy entry */
} StfTerminatorType_t;

typedef enum {
	QUOTING_MODE_AUTO    = 1 << 0, /* Automatically quote where needed */
	QUOTING_MODE_ALWAYS  = 1 << 1, /* Always quote */
	QUOTING_MODE_NEVER   = 1 << 2, /* Never quote */
	QUOTING_MODE_UNKNOWN = 1 << 3  /* Dummy entry */
} StfQuotingMode_t;

typedef enum {
	TRANSLITERATE_MODE_TRANS   = 0, /* Automatically quote where needed */
	TRANSLITERATE_MODE_ESCAPE  = 1, /* Always quote */
	TRANSLITERATE_MODE_UNKNOWN = 2  /* Dummy entry */
} StfTransliterateMode_t;

/*
 * Export Options struct
 */
typedef struct
{

	StfTerminatorType_t terminator_type;      /* Desired line terminator */
	char                cell_separator;       /* Desired cell separator */

	GSList             *sheet_list;           /* Sheets to export */

	StfQuotingMode_t    quoting_mode;         /* When to quote */
	gunichar            quoting_char;         /* Quoting char */

	char const *	    charset;		  /* Desired charset */
	StfTransliterateMode_t transliterate_mode;/* How to handle strange chars */

	StfEWriteFunc       write_func;           /* Write callback routine */
	gpointer            write_data;           /* Data to pass to callback routine (2nd param)*/
} StfExportOptions_t;

/*
 * Creation/Destruction of StfExportOptions struct
 */
StfExportOptions_t  *stf_export_options_new  (void);
void                 stf_export_options_free (StfExportOptions_t *export_options);

/*
 * Manipulation of StfExportOptions struct
 */
void stf_export_options_set_terminator_type (StfExportOptions_t *export_options, StfTerminatorType_t terminator_type);
void stf_export_options_set_cell_separator  (StfExportOptions_t *export_options, char cell_separator);
void stf_export_options_set_quoting_mode    (StfExportOptions_t *export_options, StfQuotingMode_t quoting_mode);
void stf_export_options_set_quoting_char    (StfExportOptions_t *export_options, gunichar quoting_char);
void stf_export_options_set_charset 	    (StfExportOptions_t *export_options, char const * charset);
void stf_export_options_set_transliterate_mode (StfExportOptions_t *export_options, StfTransliterateMode_t transliterate_mode);
void stf_export_options_set_write_callback  (StfExportOptions_t *export_options, StfEWriteFunc write_func, gpointer data);
void stf_export_options_sheet_list_clear    (StfExportOptions_t *export_options);
void stf_export_options_sheet_list_add      (StfExportOptions_t *export_options, Sheet *sheet);

gboolean stf_export_can_transliterate (void);


/*
 * Functions that do the actual thing
 */
gboolean             stf_export                             (StfExportOptions_t *export_options);

#endif
