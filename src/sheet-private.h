#ifndef GNUMERIC_SHEET_PRIVATE_H
#define GNUMERIC_SHEET_PRIVATE_H

struct _SheetPrivate {
#ifdef ENABLE_BONOBO
	void            *corba_server;

	GSList          *sheet_vectors;
#endif
	/* TODO Add span recomputation here too. */
	gboolean	 edit_pos_changed; /* either location or content */
	gboolean	 selection_content_changed;
	gboolean	 recompute_visibility;
	int		 reposition_row_comment;
	int		 reposition_col_comment;
};

#endif /* GNUMERIC_SHEET_PRIVATE_H */
