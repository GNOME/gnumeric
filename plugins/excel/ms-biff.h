/*
 * ms-biff.h: MS Excel BIFF support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 */
#ifndef GNUMERIC_BIFF_H
#define GNUMERIC_BIFF_H
#include "ms-ole.h"

// EXTREMELY IMPORTANT TO PASS A BYTE PTR !
#define BIFF_GETBYTE(p) (*(p+0))
#define BIFF_GETWORD(p) (*(p+0)+(*(p+1)<<8))
#define BIFF_GETLONG(p) (*(p+0)+ \
		        (*(p+1)<<8)+ \
		        (*(p+2)<<16)+ \
		        (*(p+3)<<24))
// p must be a BYTE* !
#define BIFF_GETDLONG(p) (long long int)(GETLONG(p)+(((long long int)GETLONG(p+4))<<32))
// Oh dear, silly really, brutal endianness hack: FIXME
// #define GETDOUBLE(p)   ((double)GETDLONG(p))
#define BIFF_GETDOUBLE(p) (*((double *)(p)))
// Pass this a BIFF_QUERY *
#define BIFF_GETROW(p)      (GETWORD(p->data + 0))
#define BIFF_GETCOL(p)      (GETWORD(p->data + 2))
#define BIFF_GETXF(p)       (GETWORD(p->data + 4))
#define BIFF_GETSTRLEN(p)   (GETWORD(p->data + 6))

typedef struct _BIFF_QUERY
{
  BYTE       ms_op ;
  BYTE       ls_op ;
  WORD       opcode ;
  WORD       length ; // NB. can be extended by a continue opcode
  BYTE       *data ;
  int        data_malloced ;	// is *data a copy ?
  LONG       streamPos ;        // count og bytes into the stream
  MS_OLE_STREAM_POS *pos ;
} BIFF_QUERY ;


//------------------------------------------------------------------------

//    This set of functions is for parsing an entire file's raw BIFF records
// it is recommended that you use the above subset of the API handling 'files'
// to split the stream into files first, before using ms_next_biff

// Opens OLE file 'workbook' or 'book' depending.
extern BIFF_QUERY *new_ms_biff_query_file (MS_OLE_FILE *) ;
extern BIFF_QUERY *new_ms_biff_query_here (MS_OLE_STREAM_POS *p) ;
extern BIFF_QUERY *copy_ms_biff_query (const BIFF_QUERY *p) ;
// Updates the BIFF_QUERY Structure
extern int ms_next_biff (BIFF_QUERY *) ;
// Free it then.
extern void free_ms_biff_query (BIFF_QUERY *) ;

//------------------------------------------------------------------------

//   This API firstly generates a list of available 'files' within an OLE2
// stream, and allows you to selectivly read from them.

typedef enum _eBiff_version { eBiffV2=2, eBiffV3=3, eBiffV4=4, eBiffV5=5, eBiffV7=7,
			      eBiffV8=8, eBiffVUnknown=0} eBiff_version ;
typedef enum _eBiff_filetype { eBiffTWorkbook=0, eBiffTVBModule=1, eBiffTWorksheet=2,
			       eBiffTChart=3, eBiffTMacrosheet=4, eBiffTWorkspace=5,
			       eBiffTUnknown=6 } eBiff_filetype ;
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

#define BIFF_BLANK                      0x01
#define BIFF_NUMBER                     0x03
#define BIFF_LABEL                      0x04
#define BIFF_FORMULA                    0x06
#define BIFF_ROW                        0x08
#define BIFF_BOF                        0x09
#define BIFF_EOF                        0x0a
#define BIFF_PRECISION                  0x0e

#define BIFF_FONT                       0x31
#define BIFF_XF_OLD                     0x43

#define BIFF_RK                         0x7e
#define BIFF_BOUNDSHEET                 0x85
#define BIFF_PALETTE                    0x92
#define BIFF_MULBLANK                   0xbe
#define BIFF_RSTRING                    0xd6
#define BIFF_XF                         0xe0

//------------------------------------------------------------------------
#endif




