/**
 * ms-excel-biff.h: MS Excel BIFF header for Gnumeric
 * contains data about the Excel BIFF records
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#ifndef GNUMERIC_EXCEL_BIFF_H
#define GNUMERIC_EXCEL_BIFF_H

/* Pass this a BIFF_QUERY * */
#define EX_GETROW(p)      (BIFF_GETWORD(p->data + 0))
#define EX_GETCOL(p)      (BIFF_GETWORD(p->data + 2))
#define EX_GETXF(p)       (BIFF_GETWORD(p->data + 4))
#define EX_GETSTRLEN(p)   (BIFF_GETWORD(p->data + 6))

/* Version info types as found in various Biff records */
typedef enum _eBiff_filetype { eBiffTWorkbook=0, eBiffTVBModule=1, eBiffTWorksheet=2,
			       eBiffTChart=3, eBiffTMacrosheet=4, eBiffTWorkspace=5,
			       eBiffTUnknown=6 } eBiff_filetype ;


/* Cell / XF types */
typedef enum _eBiff_hidden { eBiffHVisible=0, eBiffHHidden=1,
			     eBiffHVeryHidden=2 } eBiff_hidden ;
typedef enum _eBiff_locked { eBiffLLocked=1, eBiffLUnlocked=0 } eBiff_locked ;
typedef enum _eBiff_xftype { eBiffXStyle=0, eBiffXCell=1 } eBiff_xftype ;
typedef enum _eBiff_format { eBiffFMS=0, eBiffFLotus=1 } eBiff_format ;
typedef enum _eBiff_wrap { eBiffWWrap=0, eBiffWNoWrap=1 } eBiff_wrap ;
typedef enum _eBiff_eastern { eBiffEContext=0, eBiffEleftToRight=1,
			      eBiffErightToLeft=2 } eBiff_eastern ;

typedef enum _eBiff_border_orientation { eBiffBONone=0,
                                         eBiffBODiagDown=1,
					 eBiffBODiagUp=2,
					 eBiffBODiagBoth=3 } eBiff_border_orientation ;
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

extern BIFF_BOF_DATA *new_ms_biff_bof_data  (BIFF_QUERY *pos) ;
extern void           free_ms_biff_bof_data (BIFF_BOF_DATA *data) ;

#include "biff-types.h"

#endif



