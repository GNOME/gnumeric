#ifndef GNUMERIC_WIZARD_GRAPHICS_CONTEXT_H
#define GNUMERIC_WIZARD_GRAPHICS_CONTEXT_H

#include "str.h"
#include "expr.h"
#include "sheet-vector.h"
#include "Graph.h"

typedef enum {
	SERIES_COLUMNS,
	SERIES_ROWS
} SeriesOrientation;

typedef struct {
	ExprTree    *name_expr;
	String      *entered_expr;
	SheetVector *vector;
} DataRange;

DataRange *data_range_new             (Workbook *wb, const char *name_expr);
DataRange *data_range_new_from_expr   (Workbook *wb, const char *name_expr, const char *expression);
DataRange *data_range_new_from_vector (Workbook *wb, const char *name_expr, SheetVector *vector);
void       data_range_destroy         (DataRange *data_range, gboolean detach_from_sheet);

typedef struct {
	guint           signature;
	GtkWidget      *dialog_toplevel;
	Workbook       *workbook;
	GladeXML       *gui;
	
	int             current_page;
	int             last_graphic_type_page;
	GtkNotebook    *steps_notebook, *graphic_types_notebook;
	
	int graphic_type;

	/* Data for the various pages */
	String             *data_range;
	SeriesOrientation   series_location;
	GList              *data_range_list;

	String             *x_axis_label;
		        
	String             *plot_title;
	String             *y_axis_label;

	BonoboObjectClient *graphics_server;
	BonoboClientSite   *client_site;

	/* Interface pointer for the Layout interface */
	GNOME_Graph_Layout layout;

	/* Interface pointer for the actual chart */
	GNOME_Graph_Chart  chart;

	/*
	 * A List of BonoboViewFrames
	 */
	GList *view_frames;
} WizardGraphicContext;

#define GC_SIGNATURE ((('G' << 8) | ('C' << 8)) | 'o')
#define IS_GRAPHIC_CONTEXT(gc) (gc->signature == GC_SIGNATURE)

WizardGraphicContext *graphic_context_new     (Workbook *wb, GladeXML *gui);
BonoboViewFrame      *graphic_context_new_chart_view_frame (WizardGraphicContext *gc);

void            graphic_context_destroy           (WizardGraphicContext *gc);

void            graphic_context_data_range_remove (WizardGraphicContext *gc,
						   DataRange *data_range);
void            graphic_context_data_range_add    (WizardGraphicContext *gc,
						   DataRange *data_range);

/*
 * Wizardy functions have a graphic_wizard prefix, because they do
 * magic.
 */
void            graphic_wizard_guess_series       (WizardGraphicContext *gc,
						   SeriesOrientation orientation,
						   gboolean first_item_is_series_name);

#endif /* GNUMERIC_WIZARD_GRAPHICS_CONTEXT_H */

