#ifndef _GNM_CLIPBOARD_H_
# define _GNM_CLIPBOARD_H_

#include <gnumeric.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

typedef enum {
	PASTE_CONTENTS		= 1 << 0, /* either CONTENTS or AS_VALUES */
	PASTE_AS_VALUES		= 1 << 1, /*  can be applied, not both */
	PASTE_FORMATS		= 1 << 2,
	PASTE_COMMENTS		= 1 << 3,
	PASTE_OBJECTS		= 1 << 4,

	/* Operations that can be performed at paste time on a cell */
	PASTE_OPER_ADD		= 1 << 5,
	PASTE_OPER_SUB		= 1 << 6,
	PASTE_OPER_MULT		= 1 << 7,
	PASTE_OPER_DIV		= 1 << 8,

	/* Whether the paste transposes or not */
	PASTE_TRANSPOSE		= 1 << 9,

	PASTE_LINK              = 1 << 10,

	/* If copying a range that includes blank cells, this
	   prevents pasting blank cells over existing data */
	PASTE_SKIP_BLANKS       = 1 << 11,

	/* Do not paste merged regions (probably not needed) */
	PASTE_DONT_MERGE        = 1 << 12,

	/* Internal flag : see cmd_merge_cells_undo for details */
	PASTE_IGNORE_COMMENTS_AT_ORIGIN   = 1 << 13,

	// Copy column widths
	PASTE_COLUMN_WIDTHS = 1 << 14,
	PASTE_COLUMN_WIDTHS_AUTO = 1 << 15,
	PASTE_COLUMN_WIDTHS_MASK = (PASTE_COLUMN_WIDTHS | PASTE_COLUMN_WIDTHS_AUTO),

	// Copy row heights
	PASTE_ROW_HEIGHTS = 1 << 16,
	PASTE_ROW_HEIGHTS_AUTO = 1 << 17,
	PASTE_ROW_HEIGHTS_MASK = (PASTE_ROW_HEIGHTS | PASTE_ROW_HEIGHTS_AUTO),

	PASTE_EXPR_LOCAL_RELOCATE = 1 << 18,

	/* Avoid flagging dependencies.  */
	PASTE_NO_RECALC         = 1 << 19,

	/* Whether the paste flips or not */
	PASTE_FLIP_H         = 1 << 20,
	PASTE_FLIP_V         = 1 << 21,

	PASTE_ALL_CELL = (PASTE_CONTENTS | PASTE_FORMATS | PASTE_COMMENTS | PASTE_OBJECTS),
	PASTE_ALL_SHEET = (PASTE_ALL_CELL | PASTE_COLUMN_WIDTHS_AUTO | PASTE_ROW_HEIGHTS_AUTO),
	PASTE_DEFAULT = PASTE_ALL_SHEET
} GnmPasteFlags;

#define PASTE_OPER_MASK (PASTE_OPER_ADD | PASTE_OPER_SUB | PASTE_OPER_MULT | PASTE_OPER_DIV)

typedef struct {
	GnmCellPos const  offset;	/* must be first element */
	GnmValue *val;
	GnmExprTop const *texpr;
} GnmCellCopy;

GType gnm_cell_copy_get_type (void);

struct _GnmCellRegion {
	Sheet		*origin_sheet; /* can be NULL */
	const GODateConventions *date_conv; /* can be NULL */
	GnmCellPos	 base;
	int		 cols, rows;
	ColRowStateList *col_state, *row_state;
	GHashTable	*cell_content;
	GnmStyleList	*styles;
	GSList		*merged;
	GSList		*objects;
	gboolean	 not_as_contents;

	unsigned	 ref_count;
};

struct _GnmPasteTarget {
	Sheet      *sheet;
	GnmRange    range;
	GnmPasteFlags paste_flags;
};

GType gnm_paste_target_get_type (void);

GnmPasteTarget *gnm_paste_target_new (Sheet *sheet, GnmRange *r, GnmPasteFlags flags);

GnmCellRegion  *clipboard_copy_range   (Sheet *sheet, GnmRange const *r);
GOUndo         *clipboard_copy_range_undo (Sheet *sheet, GnmRange const *r);
GOUndo         *clipboard_copy_ranges_undo (Sheet *sheet, GSList *ranges);
GnmCellRegion  *clipboard_copy_obj     (Sheet *sheet, GSList *objects);
gboolean        clipboard_paste_region (GnmCellRegion const *cr,
					GnmPasteTarget const *pt,
					GOCmdContext *cc);
GnmPasteTarget *paste_target_init      (GnmPasteTarget *pt,
					Sheet *sheet, GnmRange const *r,
					GnmPasteFlags flags);

GType          gnm_cell_region_get_type (void);
GnmCellRegion *gnm_cell_region_new	(Sheet *origin_sheet);
GnmCellRegion *cellregion_ref		(GnmCellRegion *cr);
void           cellregion_unref		(GnmCellRegion *cr);
GString	      *cellregion_to_string	(GnmCellRegion const *cr,
					 gboolean only_visible,
					 GODateConventions const *date_conv);
int            cellregion_cmd_size	(GnmCellRegion const *cr);
void	       cellregion_invalidate_sheet (GnmCellRegion *cr, Sheet *sheet);

GnmCellCopy   *gnm_cell_copy_new (GnmCellRegion *cr,
				  int col_offset, int row_offset);

void clipboard_init (void);
void clipboard_shutdown (void);


G_END_DECLS

#endif /* _GNM_CLIPBOARD_H_ */
