/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_STF_EXPORT_H_
# define _GNM_STF_EXPORT_H_

#include "gnumeric.h"
#include <gsf/gsf-output-csv.h>
#include <goffice/app/goffice-app.h>

G_BEGIN_DECLS

#define GNM_STF_EXPORT_TYPE        (gnm_stf_export_get_type ())
#define GNM_STF_EXPORT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_STF_EXPORT_TYPE, GnmStfExport))
#define IS_GNM_STF_EXPORT(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_STF_EXPORT_TYPE))

typedef enum {
	GNM_STF_TRANSLITERATE_MODE_TRANS,  /* Automatically quote where needed */
	GNM_STF_TRANSLITERATE_MODE_ESCAPE  /* Always quote */
} GnmStfTransliterateMode;
GType gnm_stf_transliterate_mode_get_type (void);
#define GNM_STF_TRANSLITERATE_MODE_TYPE (gnm_stf_transliterate_mode_get_type ())

typedef enum {
	GNM_STF_FORMAT_AUTO,
	GNM_STF_FORMAT_RAW,
	GNM_STF_FORMAT_PRESERVE
} GnmStfFormatMode;
GType gnm_stf_format_mode_get_type (void);
#define GNM_STF_FORMAT_MODE_TYPE (gnm_stf_format_mode_get_type ())

typedef struct _GnmStfExport GnmStfExport;
GType gnm_stf_export_get_type (void);

void gnm_stf_export_options_sheet_list_clear    (GnmStfExport *stfe);
void gnm_stf_export_options_sheet_list_add      (GnmStfExport *stfe, Sheet *sheet);
GSList *gnm_stf_export_options_sheet_list_get (const GnmStfExport *stfe);

gboolean gnm_stf_export_can_transliterate (void);

gboolean gnm_stf_export (GnmStfExport *export_options);


GOFileSaver *gnm_stf_file_saver_new (gchar const *id);

G_END_DECLS

#endif /* _GNM_STF_EXPORT_H_ */
