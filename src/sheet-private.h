#ifndef GNUMERIC_SHEET_PRIVATE_H
#define GNUMERIC_SHEET_PRIVATE_H

struct _SheetPrivate {
	Range		 unhidden_region;

	/* State of menu items */
	gboolean         enable_showhide_detail		: 1;

	gboolean	 recompute_visibility		: 1;
	gboolean	 recompute_spans		: 1;
	gboolean	 recompute_max_col_group	: 1;
	gboolean	 recompute_max_row_group	: 1;
	gboolean	 resize_scrollbar		: 1;
	gboolean	 resize				: 1;
	CellPos		 reposition_objects;
};

#endif /* GNUMERIC_SHEET_PRIVATE_H */
