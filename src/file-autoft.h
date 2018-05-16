#ifndef _GNM_FILE_AUTOFT_H_
# define _GNM_FILE_AUTOFT_H_

#include <gnumeric.h>
#include <format-template.h>

G_BEGIN_DECLS

GSList  *gnm_ft_category_group_get_templates_list (GnmFTCategoryGroup *category_group,
					    GOCmdContext *context);

GList   *gnm_ft_category_group_list_get (void);
void     gnm_ft_category_group_list_free (GList *category_groups);

int gnm_ft_category_group_cmp (gconstpointer a, gconstpointer b);

G_END_DECLS

#endif /* _GNM_FILE_AUTOFT_H_ */
