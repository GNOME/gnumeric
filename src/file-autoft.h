#ifndef GNUMERIC_FILE_AUTOFT_H
#define GNUMERIC_FILE_AUTOFT_H

#include "gnumeric.h"
#include "format-template.h"

GSList  *category_group_get_templates_list (FormatTemplateCategoryGroup *category_group,
					    CommandContext *context);

GList   *category_group_list_get (void);
void     category_group_list_free (GList *category_groups);

#endif /* GNUMERIC_FILE_AUTOFT_H */
