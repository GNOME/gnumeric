#ifndef GNUMERIC_FILE_AUTOFT_H
#define GNUMERIC_FILE_AUTOFT_H

#include "gnumeric.h"
#include "format-template.h"

gchar                       *gnumeric_sys_autoformat_dir (void);
gchar                       *gnumeric_usr_autoformat_dir (void);
GList                       *gnumeric_extra_autoformat_dirs (void);

void                         category_free (FormatTemplateCategory *category);
GSList                      *category_get_templates_list (FormatTemplateCategory *category,
							  CommandContext *context);

void                         category_list_free (GList *categories);

void                         category_group_free (FormatTemplateCategoryGroup *category_group);
GSList                      *category_group_get_templates_list (FormatTemplateCategoryGroup *category_group,
								CommandContext *context);

GList                       *category_group_list_get (void);
FormatTemplateCategoryGroup *category_group_list_find_category_by_name (GList *category_groups,
									gchar const *name);
GList                       *category_group_list_get_names_list (GList *category_groups);
void                         category_group_list_free (GList *category_groups);

#endif /* GNUMERIC_FILE_AUTOFT_H */
