#ifndef GNUMERIC_STF_EXPORT_H
#define GNUMERIC_STF_EXPORT_H

#include "gnumeric.h"
#include <gsf/gsf-output-csv.h>

typedef enum {
	TRANSLITERATE_MODE_TRANS   = 0, /* Automatically quote where needed */
	TRANSLITERATE_MODE_ESCAPE  = 1, /* Always quote */
	TRANSLITERATE_MODE_UNKNOWN = 2  /* Dummy entry */
} StfTransliterateMode_t;

typedef struct {
	GsfOutputCsv *csv;

	GSList             *sheet_list;           /* Sheets to export */

	char *charset;				  /* Desired charset */
	StfTransliterateMode_t transliterate_mode;/* How to handle strange chars */
	gboolean            preserve_format;      /* whether to use rendered strings */
} StfExportOptions_t;

StfExportOptions_t *stf_export_options_new (void);
void stf_export_options_free (StfExportOptions_t *export_options);

void stf_export_options_set_charset 	    (StfExportOptions_t *export_options, char const * charset);
void stf_export_options_set_transliterate_mode (StfExportOptions_t *export_options, StfTransliterateMode_t transliterate_mode);
void stf_export_options_set_format_mode (StfExportOptions_t *export_options, gboolean preserve_format);
void stf_export_options_sheet_list_clear    (StfExportOptions_t *export_options);
void stf_export_options_sheet_list_add      (StfExportOptions_t *export_options, Sheet *sheet);

gboolean stf_export_can_transliterate (void);


/*
 * Functions that do the actual thing
 */
gboolean stf_export (StfExportOptions_t *export_options, GsfOutput *sink);

#endif
