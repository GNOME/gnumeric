#ifndef SHEET_VECTOR_H
#define SHEET_VECTOR_H

#include <libgnome/gnome-defs.h>
#include <bonobo/bonobo-object.h>
#include "Gnumeric.h"

BEGIN_GNOME_DECLS

#define SHEET_VECTOR_TYPE        (sheet_vector_get_type ())
#define SHEET_VECTOR(o)          (GTK_CHECK_CAST ((o), SHEET_VECTOR_TYPE, SheetVector))
#define SHEET_VECTOR_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), SHEET_VECTOR_TYPE, SheetVectorClass))
#define IS_SHEET_VECTOR(o)       (GTK_CHECK_TYPE ((o), SHEET_VECTOR_TYPE))
#define IS_SHEET_VECTOR_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), SHEET_VECTOR_TYPE))

typedef struct _SheetVector SheetVector;

typedef struct {
	int size;
	
	Range range;
} RangeBlock;

struct _SheetVector {
	BonoboObject base;

	Sheet  *sheet;
	int len;

	/*
	 * A sheet vector consists of a group of ranges.  The user
	 * can specify multiple non-contigous ranges as a single
	 * serie.  
	 */
	int         n_blocks;	/* Number of Range blocks */
	RangeBlock *blocks;	/* The blocks. */

	/*
	 * This is passed by the Graph component as the
	 * callback to invoke when there is a change
	 */
	GNOME_Gnumeric_VectorNotify notify;
};

typedef struct {
	BonoboObjectClass parent_class;
} SheetVectorClass;

GtkType      sheet_vector_get_type      (void);
SheetVector *sheet_vector_new           (Sheet *sheet);
void         sheet_vector_reset         (SheetVector *sheet_vector);
void         sheet_vector_append_range  (SheetVector *sheet_vector, Range *range);

void         sheet_vectors_cell_changed (Cell *cell);
void         sheet_vector_attach        (SheetVector *sheet_vector, Sheet *sheet);
void         sheet_vector_detach        (SheetVector *sheet_vector);

END_GNOME_DECLS


#endif /* SHEET_VECTOR_H */
