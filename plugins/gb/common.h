#ifndef EXCEL_GB_COMMON_H
#define EXCEL_GB_COMMON_H

#include "value.h"
#include <gbrun/libgbrun.h>

GBValue  *value_to_gb (GnmValue *val);
GnmValue *gb_to_value (GBValue  *val);

#endif /* EXCEL_GB_COMMON_H */
