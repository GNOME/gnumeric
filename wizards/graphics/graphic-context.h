#ifndef GNUMERIC_WIZARD_GRAPHICS_CONTEXT_H
#define GNUMERIC_WIZARD_GRAPHICS_CONTEXT_H

typedef enum {
	SERIES_COLUMNS,
	SERIES_ROWS
} SeriesLocation;

typedef struct {
	String   *name;
	ExprTree *tree;
} DataRange;

DataRange *data_range_new     (const char *name, const char *expression);
void       data_range_destroy (DataRange *data_range);

typedef struct {
	guint           signature;
	GtkWidget      *dialog_toplevel;
	Workbook       *workbook;
	GladeXML       *gui;
	
	int             current_page;
	GtkNotebook    *steps_notebook;
	
	/* Data for the various pages */
	int             graphic_type;

	String          *data_range;
	SeriesLocation   series_location;
		        
	GList           *data_range_list;	
	String          *x_axis_label;
		        
	String          *plot_title;
	String          *y_axis_label;

	GnomeObjectClient *guppi;
	GnomeClientSite   *client_site;
	GnomeContainer    *container;
} GraphicContext;

#define GC_SIGNATURE ((('G' << 8) | ('C' << 8)) | 'o')
#define IS_GRAPHIC_CONTEXT(gc) (gc->signature == GC_SIGNATURE)

GraphicContext *graphic_context_new     (Workbook *wb, GladeXML *gui);
void            graphic_context_destroy (GraphicContext *gc);

void            graphic_context_data_range_remove (GraphicContext *gc, const char *range_name);
void            graphic_context_data_range_add    (GraphicContext *gc, DataRange *data_range);

#endif /* GNUMERIC_WIZARD_GRAPHICS_CONTEXT_H */

