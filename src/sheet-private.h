#ifndef GNUMERIC_SHEET_PRIVATE_H
#define GNUMERIC_SHEET_PRIVATE_H

struct _SheetPrivate {
	/* TODO Add span recomputation here too. */
	struct {
		gboolean location_changed;
		gboolean content_changed; /* entered content NOT value */
		gboolean format_changed;
	} edit_pos;

	Range		 unhidden_region;

	/* State of menu items */
	gboolean         enable_insert_rows		: 1;
	gboolean         enable_insert_cols		: 1;
	gboolean         enable_insert_cells		: 1;
	gboolean         enable_paste_special		: 1;
	gboolean         enable_showhide_detail		: 1;

	gboolean	 selection_content_changed	: 1;
	gboolean	 reposition_selection		: 1;
	gboolean	 recompute_visibility		: 1;
	gboolean	 recompute_spans		: 1;
	gboolean	 recompute_max_col_group	: 1;
	gboolean	 recompute_max_row_group	: 1;
	gboolean	 resize_scrollbar		: 1;
	gboolean	 resize				: 1;
	CellPos		 reposition_objects;

	guint            auto_expr_timer;

#ifdef WITH_BONOBO
	void            *corba_server;
#endif
};

#endif /* GNUMERIC_SHEET_PRIVATE_H */
