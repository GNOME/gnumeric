#ifndef GNUMERIC_SHEET_PRIVATE_H
#define GNUMERIC_SHEET_PRIVATE_H

struct _SheetPrivate {
	Range		 unhidden_region;

	/* State of menu items */
	unsigned char    enable_showhide_detail;

	unsigned char	 recompute_visibility;
	unsigned char	 recompute_spans;
	unsigned char	 recompute_max_col_group;
	unsigned char	 recompute_max_row_group;
	unsigned char	 resize_scrollbar;
	unsigned char	 resize;
	CellPos		 reposition_objects;
};

#endif /* GNUMERIC_SHEET_PRIVATE_H */
