/*
 * ms-excel-biff.h: MS Excel BIFF header for Gnumeric
 * contains data about the Excel BIFF records
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 */
#ifndef GNUMERIC_EXCEL_BIFF_H
#define GNUMERIC_EXCEL_BIFF_H

/* Pass this a BIFF_QUERY * */
#define EX_GETROW(p)      (BIFF_GETWORD(p->data + 0))
#define EX_GETCOL(p)      (BIFF_GETWORD(p->data + 2))
#define EX_GETXF(p)       (BIFF_GETWORD(p->data + 4))
#define EX_GETSTRLEN(p)   (BIFF_GETWORD(p->data + 6))


// Cell / XF types
typedef enum _eBiff_hidden { eBiffHVisible=0, eBiffHHidden=1,
			     eBiffHVeryHidden=2 } eBiff_hidden ;
typedef enum _eBiff_locked { eBiffLLocked=1, eBiffLUnlocked=0 } eBiff_locked ;
typedef enum _eBiff_xftype { eBiffXStyle=0, eBiffXCell=1 } eBiff_xftype ;
typedef enum _eBiff_format { eBiffFMS=0, eBiffFLotus=1 } eBiff_format ;
typedef enum _eBiff_wrap { eBiffWWrap=0, eBiffWNoWrap=1 } eBiff_wrap ;
typedef enum _eBiff_eastern { eBiffEContext=0, eBiffEleftToRight=1,
			      eBiffErightToLeft=2 } eBiff_eastern ;

typedef enum _eBiff_direction { eBiffDirTop=0, eBiffDirBottom=1,
				eBiffDirLeft=2, eBiffDirRight=3 } eBiff_direction ;
typedef enum _eBiff_border_orientation { eBiffBONone=0,
                                         eBiffBODiagDown=1,
					 eBiffBODiagUp=2,
					 eBiffBODiagBoth=3 } eBiff_border_orientation ;
typedef enum _eBiff_border_linestyle // Magic numbers !
{
  eBiffBorderNone=0, eBiffBorderThin=1, eBiffBorderMedium=2,
  eBiffBorderDashed=3, eBiffBorderDotted=4, eBiffBorderThick=5,
  eBiffBorderDouble=6, eBiffBorderHair=7, eBiffBorderMediumDash=8,
  eBiffBorderDashDot=9, eBiffBorderMediumDashDot=10,
  eBiffBorderDashDotDot=11, eBiffBorderMediumDashDotDot=12,
  eBiffBorderSlantedDashDot=13
} eBiff_border_linestyle ;

typedef enum _eBiffFontUnderline
{
  eBiffFUNone=1, eBiffFUSingle=2, eBiffFUDouble=3,
  eBiffFUSingleAcc=4, eBiffFUDoubleAcc=5
} eBiffFontUnderline ;

typedef enum _eBiffFontScript { eBiffFSNone, eBiffFSSub, eBiffFSSuper } eBiffFontScript ;

typedef struct _BIFF_BOF_DATA
{
  eBiff_version version ;
  eBiff_filetype type ;
} BIFF_BOF_DATA ;

// Privatish BIFF_FILE functions
extern BIFF_BOF_DATA *new_ms_biff_bof_data (BIFF_QUERY *pos) ;
extern void free_ms_biff_bof_data (BIFF_BOF_DATA *data) ;

//------------------------------------------------------------------------
/* See S59D52.HTM */

#define BIFF_DIMENSIONS                 0x00
#define BIFF_BLANK                      0x01
#define BIFF_NUMBER                     0x03
#define BIFF_LABEL                      0x04
#define BIFF_FORMULA                    0x06
#define BIFF_ROW                        0x08
#define BIFF_BOF                        0x09
#define BIFF_EOF                        0x0a
#define BIFF_PRECISION                  0x0e
#define BIFF_ARRAY                      0x21

#define BIFF_FONT                       0x31
#define BIFF_XF_OLD                     0x43

#define BIFF_RK                         0x7e
#define BIFF_BOUNDSHEET                 0x85
#define BIFF_PALETTE                    0x92
#define BIFF_MULBLANK                   0xbe
#define BIFF_RSTRING                    0xd6
#define BIFF_XF                         0xe0

/* Odd balls */
#define BIFF_DV                        0x1be

//------------------------------------------------------------------------
#endif
