#ifndef GNUMERIC_PLUGIN_LOTUS_123_LOTUS_TYPES_H
#define GNUMERIC_PLUGIN_LOTUS_123_LOTUS_TYPES_H

/* Lotus' record types */

#define LOTUS_BOF                    0x0
#define LOTUS_EOF                    0x1
#define LOTUS_CALCMODE               0x2
#define LOTUS_CALCORDER              0x3
#define LOTUS_SPLIT                  0x4
#define LOTUS_SYNC                   0x5
#define LOTUS_RANGE                  0x6
#define LOTUS_WINDOW1                0x7
#define LOTUS_COLW1                  0x8

#define LOTUS_WINTWO                 0x9
#define LOTUS_COLW2                  0xa

#define LOTUS_NAME                   0xb
#define LOTUS_BLANK                  0xc
#define LOTUS_INTEGER                0xd
#define LOTUS_NUMBER                 0xe
#define LOTUS_LABEL                  0xf
#define LOTUS_FORMULA                0x10
#define LOTUS_TABLE                  0x18
#define LOTUS_ORANGE                 0x19
#define LOTUS_PRANGE                 0x1a
#define LOTUS_SRANGE                 0x1b
#define LOTUS_FRANGE                 0x1c
#define LOTUS_KRANGE1                0x1d
#define LOTUS_HRANGE                 0x20
#define LOTUS_KRANGE2                0x23

#define LOTUS_PROTEC                 0x24
#define LOTUS_FOOTER                 0x25
#define LOTUS_HEADER                 0x26
#define LOTUS_SETUP                  0x27
#define LOTUS_MARGINS                0x28
#define LOTUS_STRING                 0x33

/* Stuff observed in new formats only: */
#define LOTUS_SHEETCELLPTR           0x5
#define LOTUS_SHEETLAYOUT            0x6
#define LOTUS_COLW4                  0x7
#define LOTUS_HIDDENCOL              0x8
#define LOTUS_USER_RANGE             0x9
#define LOTUS_SYSTEMRANGE            0xa
#define LOTUS_ZEROFORCE              0xb
#define LOTUS_SORTKEY_DIR            0xc
#define LOTUS_FORMAT                 0x13
#define LOTUS_ERRCELL                0x14
#define LOTUS_NACELL                 0x15
#define LOTUS_LABEL2                 0x16
#define LOTUS_EXTENDED_FLOAT         0x17    /* wk4...  */
#define LOTUS_SMALLNUM               0x18    /* wk3,wk4 */
#define LOTUS_FORMULA3               0x19    /* wk3,wk4 */
#define LOTUS_FORMULASTRING          0x1a    /* wk3,wk4 */
#define LOTUS_STYLE                  0x1b
#define LOTUS_DTLABELMISC            0x1c
#define LOTUS_CPA                    0x1f
#define LOTUS_NAMED_SHEET            0x23
#define LOTUS_PACKED_NUMBER          0x25
#define LOTUS_CELL_COMMENT           0x26
#define LOTUS_NUMBER2                0x27
#define LOTUS_FORMULA2               0x28
#define LOTUS_PERSISTENT_ID          0x100
#define LOTUS_WINDOW                 0x103
#define LOTUS_BEGIN_OBJECT           0x104
#define LOTUS_END_OBJECT             0x105
#define LOTUS_BEGIN_GROUP            0x106
#define LOTUS_END_GROUP              0x107
#define LOTUS_DOCUMENT_WINDOW        0x109
#define LOTUS_DOCUMENT_1             0x10a
#define LOTUS_OBJECT_SELECT          0x10b
#define LOTUS_OBJECT_NAME_INDEX      0x10c
#define LOTUS_LARGE_DATA             0x10d
#define LOTUS_STYLE_MANAGER_BEGIN    0x10e
#define LOTUS_STYLE_MANAGER_END      0x10f
#define LOTUS_DOCUMENT_2             0x200
#define LOTUS_WORKBOOK_VIEW          0x201
#define LOTUS_SPLIT_MANAGEMENT       0x202
#define LOTUS_SHEET_NAME             0x204
#define LOTUS_SHEET_OBJECT_ID        0x205
#define LOTUS_SHEET                  0x280
#define LOTUS_SHEET_VIEW             0x281
#define LOTUS_RLDB_DEFAULTS          0x282
#define LOTUS_RLDB_NAMEDSTYLES       0x283
#define LOTUS_RLDB_STYLES            0x284
#define LOTUS_FIRST_WORKSHEET        0x285
#define LOTUS_CA_DB                  0x286
#define LOTUS_SHEET_PROPS            0x287
#define LOTUS_RESERVED_288           0x288
#define LOTUS_DEFAULTS_DB            0x292
#define LOTUS_RLDB_FORMATS           0x293
#define LOTUS_RLDB_BORDERS           0x294
#define LOTUS_RLDB_COLWIDTHS         0x295
#define LOTUS_RLDB_ROWHEIGHTS        0x296
#define LOTUS_RL2DB                  0x299
#define LOTUS_RL3DB                  0x29a
#define LOTUS_SCRIPT_STREAM          0x304
#define LOTUS_PRINT_SETTINGS         0x400
#define LOTUS_PRINT_STRINGS          0x401
#define LOTUS_RANGE_REGION           0x640
#define LOTUS_RANGE_MISC             0x642
#define LOTUS_RANGE_ALIAS            0x643
#define LOTUS_DATA_FILL              0x701
#define LOTUS_BACKSOLVER             0x702
#define LOTUS_SORT_HEADER            0x703
#define LOTUS_CELL_EOF               0x704
#define LOTUS_FILE_PREFERENCE        0x780
#define LOTUS_RLDB_NODE              0x800
#define LOTUS_RLDB_DATANODE          0x801
#define LOTUS_RLDB_REGISTERID        0x802
#define LOTUS_RLDB_USEREGISTEREDID   0x803
#define LOTUS_RLDB_PACKINFO          0x804
#define LOTUS_NAMED_STYLE_DB         0xa80
#define LOTUS_END_DATA               0x2af6

/* -------------------------------------------------------------------------- */

#define WORKS_VERSION_3 0x0404   /* == LOTUS_VERSION_ORIG_123 */

#define WORKS_BOF 0xff
#define WORKS_FONT  0x5456
#define WORKS_STYLE 0x545a
#define WORKS_SMALL_FLOAT 0x545b

/* -------------------------------------------------------------------------- */

#endif
