#ifndef GNUMERIC_HLINK_H
#define GNUMERIC_HLINK_H

#include "gnumeric.h"
#include <glib-object.h>

#define GNM_HLINK_TYPE		(gnm_hlink_get_type ())
#define GNM_HLINK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_HLINK_TYPE, GnmHLink))
#define GNM_IS_HLINK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_HLINK_TYPE))

GnmHLink	*sheet_hlink_find   (Sheet const *sheet, CellPos const *pos);

GType gnm_hlink_get_type (void);
gboolean         gnm_hlink_activate   (GnmHLink *l, WorkbookControl *wbc);
guchar const	*gnm_hlink_get_target (GnmHLink const *lnk);
void	      	 gnm_hlink_set_target (GnmHLink *lnk, guchar const *url);
guchar const	*gnm_hlink_get_tip    (GnmHLink const *l);
void		 gnm_hlink_set_tip    (GnmHLink *l, guchar const *tip);

GType gnm_hlink_cur_wb_get_type (void);
GType gnm_hlink_url_get_type (void);
GType gnm_hlink_email_get_type (void);
GType gnm_hlink_external_get_type (void);

#endif /* GNUMERIC_HLINK_H */
