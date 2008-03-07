/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_FILE_AUTOFT_H_
# define _GNM_FILE_AUTOFT_H_

#include "gnumeric.h"
#include "format-template.h"

G_BEGIN_DECLS

GSList  *category_group_get_templates_list (FormatTemplateCategoryGroup *category_group,
					    GOCmdContext *context);

GList   *category_group_list_get (void);
void     category_group_list_free (GList *category_groups);

G_END_DECLS

#endif /* _GNM_FILE_AUTOFT_H_ */
