#ifndef GNUMERIC_PLUGIN_LOTUS_123_LOTUS_FORMULA_H
#define GNUMERIC_PLUGIN_LOTUS_123_LOTUS_FORMULA_H

#include "gnumeric.h"

#define LOTUS_FORMULA_CONSTANT    0x0
#define LOTUS_FORMULA_VARIABLE    0x1
#define LOTUS_FORMULA_RANGE       0x2
#define LOTUS_FORMULA_RETURN      0x3
#define LOTUS_FORMULA_BRACKET     0x4
#define LOTUS_FORMULA_INTEGER     0x5

#define LOTUS_FORMULA_UNARY_PLUS  0x17


extern GnmExpr const *lotus_parse_formula (Sheet *sheet, guint32 col, guint32 row,
					   guint8 *data, guint32 len);

#endif
