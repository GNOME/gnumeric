#ifndef GNUMERIC_SHEET_PRIVATE_H
#define GNUMERIC_SHEET_PRIVATE_H

struct _SheetPrivate {
#ifdef ENABLE_BONOBO
	void            *corba_server;

	GSList          *sheet_vectors;
#endif
	/* TODO Add span recomputation here too. */
	struct {
		gboolean location_changed;
		gboolean content_changed; /* entered content NOT value */
		gboolean format_changed;
	} edit_pos;

	/* State of menu items */
	gboolean         enable_insert_rows;
	gboolean         enable_insert_cols;
	gboolean         enable_insert_cells;
	gboolean         enable_paste_special;
	gboolean         enable_showhide_detail;

	gboolean	 selection_content_changed;
	gboolean	 reposition_selection;
	gboolean	 recompute_visibility;
	gboolean	 recompute_spans;
	gboolean	 resize_scrollbar;
	CellPos		 reposition_objects;

	guint            auto_expr_idle_id;
};

#endif /* GNUMERIC_SHEET_PRIVATE_H */
