#ifndef EXCEL_GB_COMMON_H
#define EXCEL_GB_COMMON_H

#include "value.h"
#include <gbrun/libgbrun.h>

GBValue *value_to_gb (Value *val);
Value   *gb_to_value (GBValue *v);

#endif /* EXCEL_GB_COMMON_H */
