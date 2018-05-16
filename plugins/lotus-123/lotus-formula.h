#ifndef GNUMERIC_PLUGIN_LOTUS_123_LOTUS_FORMULA_H
#define GNUMERIC_PLUGIN_LOTUS_123_LOTUS_FORMULA_H

#include <gnumeric.h>
#include <sheet.h>

#define LOTUS_FORMULA_CONSTANT    0x0
#define LOTUS_FORMULA_VARIABLE    0x1
#define LOTUS_FORMULA_RANGE       0x2
#define LOTUS_FORMULA_RETURN      0x3
#define LOTUS_FORMULA_BRACKET     0x4
#define LOTUS_FORMULA_INTEGER     0x5
#define LOTUS_FORMULA_STRING	  0x6

#define LOTUS_FORMULA_PACKED_NUMBER 0x5
#define LOTUS_FORMULA_NAMED         0x7
#define LOTUS_FORMULA_ABS_NAMED     0x8
#define LOTUS_FORMULA_ERR_RREF      0x9
#define LOTUS_FORMULA_ERR_CREF      0xa
#define LOTUS_FORMULA_ERR_CONSTANT  0xb
#define LOTUS_FORMULA_OP_NEG        0x0E
#define LOTUS_FORMULA_OP_PLU        0x0F
#define LOTUS_FORMULA_OP_MNS        0x10
#define LOTUS_FORMULA_OP_MUL        0x11
#define LOTUS_FORMULA_OP_DIV        0x12
#define LOTUS_FORMULA_OP_POW        0x13
#define LOTUS_FORMULA_OP_EQ         0x14
#define LOTUS_FORMULA_OP_NE         0x15
#define LOTUS_FORMULA_OP_LE         0x16
#define LOTUS_FORMULA_OP_GE         0x17
#define LOTUS_FORMULA_OP_LT         0x18
#define LOTUS_FORMULA_OP_GT         0x19
#define LOTUS_FORMULA_OP_AND        0x1A
#define LOTUS_FORMULA_OP_OR         0x1B
#define LOTUS_FORMULA_OP_NOT        0x1C
#define LOTUS_FORMULA_OP_UPLU       0x1D
#define LOTUS_FORMULA_OP_CAT        0x1E

#define LOTUS_FORMULA_SPLFUNC       0x7A

GnmExprTop const *lotus_parse_formula (LotusState *state, GnmParsePos *pos,
				       guint8 const *data, guint32 len);

void lotus_formula_init (void);
void lotus_formula_shutdown (void);

#endif
