#ifndef GNUMERIC_WORKBOOK_VIEW_H
#define GNUMERIC_WORKBOOK_VIEW_H

#include "gnumeric.h"

/*
 * Actions on the workbooks UI
 * 
 * These are the embryonic form of signals that will change the
 * workbook-view.  I amsure that this is the wrong place and interface
 * but it is a starting point of the list.
 *
 */
void workbook_view_set_paste_special_state (Workbook *wb, gboolean enable);

void workbook_view_set_undo_redo_state (Workbook const * const wb,
					char const * const undo_suffix,
					char const * const redo_suffix);

void workbook_view_push_undo (Workbook const * const wb,
			      char const * const cmd_text);
void workbook_view_pop_undo (Workbook const * const wb);
void workbook_view_clear_undo (Workbook const * const wb);

void workbook_view_push_redo (Workbook const * const wb,
			      char const * const cmd_text);
void workbook_view_pop_redo (Workbook const * const wb);
void workbook_view_clear_redo (Workbook const * const wb);

void workbook_view_set_size (Workbook const * const wb,
			     int width_pixels,
			     int height_pixels);

void workbook_view_set_title (Workbook const * const wb,
			      char const * const title);

void workbook_view_pref_visibility (Workbook const * const wb);

void workbook_view_history_setup  (Workbook *wb);

void workbook_view_history_update (GList *wl, gchar *filename);

void workbook_view_history_shrink (GList *wl, gint new_max);

#endif /* GNUMERIC_WORKBOOK_VIEW_H */




