/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_HLINK_H_
# define _GNM_HLINK_H_

#include "gnumeric.h"
#include <glib-object.h>

G_BEGIN_DECLS

#define GNM_HLINK_TYPE		(gnm_hlink_get_type ())
#define GNM_HLINK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_HLINK_TYPE, GnmHLink))
#define IS_GNM_HLINK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_HLINK_TYPE))

#define GNM_HLINK_URL_TYPE		(gnm_hlink_url_get_type ())
#define GNM_HLINK_URL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_HLINK_URL_TYPE, GnmHLinkURL))
#define IS_GNM_HLINK_URL(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_HLINK_URL_TYPE))

GnmHLink	*sheet_hlink_find   (Sheet const *sheet, GnmCellPos const *pos);

GType gnm_hlink_get_type (void);
gboolean         gnm_hlink_activate   (GnmHLink *l, WorkbookControl *wbc);
gchar const	*gnm_hlink_get_target (GnmHLink const *lnk);
void	      	 gnm_hlink_set_target (GnmHLink *lnk, gchar const *url);
gchar const	*gnm_hlink_get_tip    (GnmHLink const *l);
void		 gnm_hlink_set_tip    (GnmHLink *l, gchar const *tip);

GType gnm_hlink_cur_wb_get_type (void);
GType gnm_hlink_url_get_type (void);
GType gnm_hlink_email_get_type (void);
GType gnm_hlink_external_get_type (void);

G_END_DECLS

#endif /* _GNM_HLINK_H_ */
