#ifndef _GNM_HLINK_H_
# define _GNM_HLINK_H_

#include <gnumeric.h>
#include <wbc-gtk.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GNM_HLINK_TYPE		(gnm_hlink_get_type ())
#define GNM_HLINK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_HLINK_TYPE, GnmHLink))
#define GNM_IS_HLINK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_HLINK_TYPE))

#define GNM_HLINK_URL_TYPE		(gnm_hlink_url_get_type ())
#define GNM_HLINK_URL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_HLINK_URL_TYPE, GnmHLinkURL))
#define GNM_IS_HLINK_URL(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_HLINK_URL_TYPE))

#define GNM_HLINK_EXTERNAL_TYPE		(gnm_hlink_external_get_type ())
#define GNM_IS_HLINK_EXTERNAL(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_HLINK_EXTERNAL_TYPE))

GnmHLink	*gnm_sheet_hlink_find   (Sheet const *sheet, GnmCellPos const *pos);

GType gnm_hlink_get_type (void);

GnmHLink *gnm_hlink_new (GType typ, Sheet *sheet);
GnmHLink *gnm_hlink_dup_to (GnmHLink *lnk, Sheet *sheet);

gboolean  gnm_hlink_equal (GnmHLink const *a, GnmHLink const *b, gboolean relax_sheet);

gboolean         gnm_hlink_activate   (GnmHLink *lnk, WBCGtk *wbcg);

const char	*gnm_hlink_get_target (GnmHLink const *lnk);
void	     	 gnm_hlink_set_target (GnmHLink *lnk, gchar const *url);

const char	*gnm_hlink_get_tip    (GnmHLink const *lnk);
void		 gnm_hlink_set_tip    (GnmHLink *lnk, gchar const *tip);

Sheet *gnm_hlink_get_sheet (GnmHLink *lnk);

GType gnm_hlink_cur_wb_get_type (void);
GType gnm_hlink_url_get_type (void);
GType gnm_hlink_email_get_type (void);
GType gnm_hlink_external_get_type (void);

// For internal links only
gboolean gnm_hlink_get_range_target (GnmHLink const *lnk, GnmSheetRange *sr);
GnmExprTop const *gnm_hlink_get_target_expr (GnmHLink const *lnk);

/* Protected. */
void gnm_hlink_init_ (void);

G_END_DECLS

#endif /* _GNM_HLINK_H_ */
