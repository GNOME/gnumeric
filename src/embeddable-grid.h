#ifndef GNUMERIC_EMBEDDABLE_GRID_H
#define GNUMERIC_EMBEDDABLE_GRID_H

#include <bonobo/gnome-embeddable.h>

#define EMBEDDABLE_GRID_TYPE        (embeddable_grid_get_type ())
#define EMBEDDABLE_GRID(o)          (GTK_CHECK_CAST ((o), EMBEDDABLE_GRID_TYPE, EmbeddableGrid))
#define EMBEDDABLE_GRID_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), EMBEDDABLE_GRID_TYPE, EmbeddableGridClass))
#define IS_EMBEDDABLE_GRID(o)       (GTK_CHECK_TYPE ((o), EMBEDDABLE_GRID_TYPE))
#define IS_EMBEDDABLE_GRID_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), EMBEDDABLE_GRID_TYPE))

/*
 * The GnomeEmbeddable object
 */
struct _EmbeddableGrid;
typedef struct _EmbeddableGrid EmbeddableGrid;

struct _EmbeddableGrid {
	GnomeEmbeddable embeddable;

	/* The associated workbook */
	Workbook *workbook;

	/* The sheet, only one */
	Sheet    *sheet;

	/* A list of all the views */
	GList *views;

	gboolean show_col_title;
	gboolean show_row_title;
};

typedef struct {
	GnomeEmbeddableClass parent_class;
} EmbeddableGridClass;

GtkType         embeddable_grid_get_type     (void);
EmbeddableGrid *embeddable_grid_new_anon     (void);
EmbeddableGrid *embeddable_grid_new          (Workbook *workbook, Sheet *sheet);
void            EmbeddableGridFactory_init   (void);

/*
 * The GnomeView object
 */
#define GRID_VIEW_TYPE        (grid_view_get_type ())
#define GRID_VIEW(o)          (GTK_CHECK_CAST ((o), GRID_VIEW_TYPE, GridView))
#define GRID_VIEW_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), GRID_VIEW_TYPE, GridViewClass))
#define IS_GRID_VIEW(o)       (GTK_CHECK_TYPE ((o), GRID_VIEW_TYPE))
#define IS_GRID_VIEW_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), GRID_VIEW_TYPE))

struct _GridView;
typedef struct _GridView GridView;

struct _GridView {
	GnomeView view;

	SheetView      *sheet_view;
	EmbeddableGrid *embeddable;
};

typedef struct {
	GnomeView parent_class;
} GridViewClass;

GtkType         grid_view_get_type           (void);
GnomeView      *grid_view_new                (EmbeddableGrid *container);

#endif /* GNUMERIC_EMBEDDABLE_GRID_H */
