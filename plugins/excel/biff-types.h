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
#define BIFF_CALCCOUNT                  0x0c
#define BIFF_CALCMODE                   0x0d
#define BIFF_PRECISION                  0x0e
#define BIFF_REFMODE                    0x0f
#define BIFF_DELTA                      0x10
#define BIFF_ITERATION                  0x11
#define BIFF_PROTECT                    0x12
#define BIFF_PASSWORD                   0x13
#define BIFF_HEADER                     0x14
#define BIFF_FOOTER                     0x15
#define BIFF_EXTERNCOUNT                0x16
#define BIFF_EXTERNSHEET                0x17
#define BIFF_NAME                       0x18
#define BIFF_WINDOWPROTECT              0x19
#define BIFF_VERTICALPAGEBREAKS         0x1a
#define BIFF_HORIZONTALPAGEBREAKS       0x1b
#define BIFF_NOTE                       0x1c
#define BIFF_SELECTION                  0x1d
#define BIFF_FORMAT                     0x1e
#define BIFF_ARRAY                      0x21
#define BIFF_1904                       0x22
#define BIFF_EXTERNNAME                 0x23
#define BIFF_LEFTMARGIN                 0x26
#define BIFF_RIGHTMARGIN                0x27
#define BIFF_TOPMARGIN                  0x28
#define BIFF_BOTTOMMARGIN               0x29
#define BIFF_PRINTHEADERS               0x2a
#define BIFF_PRINTGRIDLINES             0x2b
#define BIFF_FILEPASS                   0x2f
#define BIFF_FONT                       0x31
#define BIFF_CONTINUE                   0x3c
#define BIFF_WINDOW1                    0x3d
#define BIFF_PANE                       0x40
#define BIFF_CODENAME                   0x41
#define BIFF_CODEPAGE                   0x42
#define BIFF_XF_OLD                     0x43
#define BIFF_PLS                        0x4D
#define BIFF_DCON                       0x50
#define BIFF_DCONREF                    0x51
#define BIFF_DCONNAME                   0x52
#define BIFF_DEFCOLWIDTH                0x55

#define BIFF_XCT                        0x59
#define BIFF_CRN                        0x5a
#define BIFF_FILESHARING                0x5b
#define BIFF_WRITEACCESS                0x5c
#define BIFF_OBJ                        0x5d
#define BIFF_SAVERECALC                 0x5f
#define BIFF_COLINFO                    0x7d
#define BIFF_RK                         0x7e
#define BIFF_IMDATA                     0x7f
#define BIFF_GUTS                       0x80
#define BIFF_WSBOOL                     0x81
#define BIFF_GRIDSET                    0x82
#define BIFF_HCENTER                    0x83
#define BIFF_VCENTER                    0x84
#define BIFF_BOUNDSHEET                 0x85
#define BIFF_WRITEPROT                  0x86
#define BIFF_ADDIN                      0x87
#define BIFF_ENG                        0x88
#define BIFF_PUB                        0x89
#define BIFF_COUNTRY                    0x8c
#define BIFF_HIDEOBJ                    0x8d
#define BIFF_PALETTE                    0x92
#define BIFF_FNGROUPCOUNT               0x9c
#define BIFF_AUTOFILTERINFO             0x9d
#define BIFF_AUTOFILTER                 0x9e
#define BIFF_SETUP                      0xa1
#define BIFF_SHRFMLA                    0xbc
#define BIFF_MULRK                      0xbd
#define BIFF_MULBLANK                   0xbe
#define BIFF_MMS                        0xc1
#define BIFF_ADDMENU                    0xc2
#define BIFF_DELMENU                    0xc3
#define BIFF_SXDI                       0xc5
#define BIFF_SXDB                       0xc6
#define BIFF_SXSTRING                   0xcd
#define BIFF_RSTRING                    0xd6
#define BIFF_DBCELL                     0xd7
#define BIFF_OLESIZE                    0xde
#define BIFF_BOOKBOOL                   0xda
#define BIFF_XF                         0xe0
#define BIFF_INTERFACEHDR               0xe1
#define BIFF_INTERFACEEND               0xe2
#define BIFF_SXVX                       0xe3
#define BIFF_TABIDCONF                  0xea
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
#define BIFF_INDEX                     0x20b
#define BIFF_DEFAULTROWHEIGHT	       0x225
#define BIFF_WINDOW2                   0x23e
#define BIFF_STYLE                     0x293

