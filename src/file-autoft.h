#ifndef GNUMERIC_FILE_AUTOFT_H
#define GNUMERIC_FILE_AUTOFT_H

#include "gnumeric.h"

GList        *template_list_load (void);
void          template_list_free (GList *list);

GList        *category_list_load (void);
void          category_list_free (GList *list);

#endif /* GNUMERIC_FILE_AUTOFT_H */
