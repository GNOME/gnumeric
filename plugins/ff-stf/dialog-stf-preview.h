#ifndef GNUMERIC_DIALOG_STF_PREVIEW_H
#define GNUMERIC_DIALOG_STF_PREVIEW_H

#error "DO _NOT_ USE THE OLD FF-STF PLUGIN! IT IS OBSOLETE, READ README.NOW IN THE gnumeric/plugins/ff-stf DIRECTORY FOR DETAILS"

#include "sheet.h"
#include "stf.h"

typedef struct {
	GnomeCanvas      *canvas;         /* Gnomecanvas to render on */
	FileSource_t     *src;            /* Contains some file information and the file data */
	gboolean          formatted;      /* True if you want the RENDERED values to be displayed */
	int               startrow;       /* Row at which to start rendering */

	GnomeCanvasGroup *group;          /* Group used to hold items put on the canvas in 1 render cycle */
	int               rowsrendered;   /* Number of rows rendered in the previous render cycle */
} RenderData_t;

/* This will actually draw the stuff on screen */	
void            stf_preview_render       (RenderData_t *renderdata);

/* These are for creation/deletion */
RenderData_t*   stf_preview_new          (GnomeCanvas *canvas, FileSource_t *src, gboolean formatted);
void            stf_preview_free         (RenderData_t *data);

/* These are for manipulation */
void            stf_preview_set_startrow (RenderData_t *data, int startrow);

#endif /* GNUMERIC_DIALOG_STF_PREVIEW_H */



