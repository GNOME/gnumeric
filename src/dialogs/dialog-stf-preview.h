#ifndef GNUMERIC_DIALOG_STF_PREVIEW_H
#define GNUMERIC_DIALOG_STF_PREVIEW_H

#include <gui-gnumeric.h>
#include <sheet.h>
#include <stf.h>

#define LINE_COLOR "gray"
#define CAPTION_LINE_COLOR "black"
#define CAPTION_COLOR "silver"
#define CAPTION_COLOR_ACTIVE "gray"
#define ROW_COLOR "white"
#define ROW_COLOR_ACTIVE "lavender"
#define TEXT_COLOR "black"
#define TEXT_COLOR_ACTIVE "black"

#define COLUMN_CAPTION N_("Column %d")

#define CELL_VPAD 10
#define CELL_HPAD 10

#define MOUSE_SENSITIVITY 5

typedef struct {
	GnomeCanvas      *canvas;         /* Gnomecanvas to render on */
	gboolean          formatted;      /* True if you want the RENDERED values to be displayed */
	int               startrow;       /* Row at which to start rendering */

	GnomeCanvasGroup *group;          /* Group used to hold items put on the canvas in 1 render cycle */
	GnomeCanvasGroup *gridgroup;      /* Used to hold the grid */

	GArray           *colwidths;      /* An array containing the column widths */
	GArray           *temp;           /* temporary array holder */
	GArray           *actualwidths;   /* Array containing the actual column widths (without caption sizes) */
	GPtrArray        *colformats;     /* Array containing the desired column formats */

	double            charwidth;      /* Width of 1 character */
	double            charheight;     /* Height of 1 character */

	int               activecolumn;   /* active column */
} RenderData_t;

/* This will actually draw the stuff on screen */
void               stf_preview_render                    (RenderData_t *renderdata, GList *list, int rowcount, int colcount);

/* These are for creation/deletion */
RenderData_t*      stf_preview_new                       (GnomeCanvas *canvas, gboolean formatted);
void               stf_preview_free                      (RenderData_t *data);

/* These are for manipulation */
void               stf_preview_set_startrow              (RenderData_t *data, int startrow);
void               stf_preview_set_activecolumn          (RenderData_t *data, int column);

void               stf_preview_colwidths_clear           (RenderData_t *renderdata);
void               stf_preview_colwidths_add             (RenderData_t *renderdata, int width);

void               stf_preview_colformats_clear          (RenderData_t *renderdata);
void               stf_preview_colformats_add            (RenderData_t *renderdata, StyleFormat *format);

/* These are public utility functions */
int                stf_preview_get_displayed_rowcount    (RenderData_t *renderdata);
int                stf_preview_get_column_at_x           (RenderData_t *renderdata, double x);
int                stf_preview_get_column_border_at_x    (RenderData_t *renderdata, double x);
int                stf_preview_get_char_at_x             (RenderData_t *renderdata, double x);

#endif /* GNUMERIC_DIALOG_STF_PREVIEW_H */
