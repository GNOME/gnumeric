/**
 * biff-types.h: A long and dull list of BIFF types.
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
/**
 * See S59D52.HTM for the spec.
 * As you add low numbers, check you don't glup an odd-ball
 **/

#define BIFF_DIMENSIONS                 0x00
#define BIFF_BLANK                      0x01
#define BIFF_NUMBER                     0x03
#define BIFF_LABEL                      0x04
#define BIFF_FORMULA                    0x06
#define BIFF_ROW                        0x08
#define BIFF_BOF                        0x09
#define BIFF_EOF                        0x0a
#define BIFF_PRECISION                  0x0e
#define BIFF_HEADER                     0x14
#define BIFF_FOOTER                     0x15
#define BIFF_EXTERNCOUNT                0x16 /* number of external references*/
#define BIFF_EXTERNSHEET                0x17
#define BIFF_NOTE                       0x1c
#define BIFF_SELECTION                  0x1d
#define BIFF_FORMAT                     0x1e
#define BIFF_ARRAY                      0x21
#define BIFF_EXTERNNAME                 0x23
#define BIFF_FILEPASS                   0x2f
#define BIFF_FONT                       0x31
#define BIFF_CONTINUE                   0x3c
#define BIFF_XF_OLD                     0x43

#define BIFF_OBJ                        0x5d
#define BIFF_COLINFO                    0x7d
#define BIFF_RK                         0x7e
#define BIFF_IMDATA                     0x7f
#define BIFF_BOUNDSHEET                 0x85
#define BIFF_PALETTE                    0x92
#define BIFF_SHRFMLA                    0xbc
#define BIFF_MULRK                      0xbd
#define BIFF_MULBLANK                   0xbe
#define BIFF_RSTRING                    0xd6
#define BIFF_DBCELL                     0xd7
#define BIFF_OLESIZE                    0xde
#define BIFF_XF                         0xe0
#define BIFF_MS_O_DRAWING_GROUP         0xeb
#define BIFF_MS_O_DRAWING               0xec
#define BIFF_MS_O_DRAWING_SELECTION     0xed
#define BIFF_SST                        0xfc
#define BIFF_LABELSST                   0xfd
#define BIFF_EXTSST                     0xff

/* Odd balls */
#define BIFF_SUPBOOK                   0x1ae /* Supporting Workbook */
#define BIFF_DV                        0x1be
#define BIFF_BOOLERR                   0x205
#define BIFF_STRING                    0x207
#define BIFF_NAME                      0x218
#define BIFF_WINDOW2                   0x23e
