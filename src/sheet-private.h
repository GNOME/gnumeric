#ifndef _GNM_SHEET_PRIVATE_H_
# define _GNM_SHEET_PRIVATE_H_

G_BEGIN_DECLS

struct _SheetPrivate {
	GnmRange	 unhidden_region;

	/* State of menu items */
	unsigned char    enable_showhide_detail;

	unsigned char	 recompute_visibility;
	unsigned char	 recompute_spans;
	unsigned char	 recompute_max_col_group;
	unsigned char	 recompute_max_row_group;
	unsigned char	 resize_scrollbar;
	unsigned char	 resize;
	GnmCellPos	 reposition_objects;
	unsigned char	 filters_changed;
	unsigned char	 objects_changed;
};

/* for internal use only */
void gnm_sheet_cell_init (void);
void gnm_sheet_cell_shutdown (void);

G_END_DECLS

#endif /* _GNM_SHEET_PRIVATE_H_ */
