/*
 * ms-biff.h: MS Excel BIFF support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 */
#ifndef GNUMERIC_BIFF_H
#define GNUMERIC_BIFF_H
#include "ms-ole.h"

typedef guint8  BYTE ;
typedef guint16 WORD ;
typedef guint32 LONG ;

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
extern BIFF_QUERY *ms_biff_query_new (MS_OLE *) ;
extern BIFF_QUERY *ms_biff_query_copy (const BIFF_QUERY *p) ;
// Updates the BIFF_QUERY Structure
extern int ms_biff_query_next (BIFF_QUERY *) ;
// Free it then.
extern void ms_biff_query_destroy (BIFF_QUERY *) ;

//------------------------------------------------------------------------

//   This API firstly generates a list of available 'files' within an OLE2
// stream, and allows you to selectivly read from them.

typedef enum _eBiff_version { eBiffV2=2, eBiffV3=3, eBiffV4=4, eBiffV5=5, eBiffV7=7,
			      eBiffV8=8, eBiffVUnknown=0} eBiff_version ;
typedef enum _eBiff_filetype { eBiffTWorkbook=0, eBiffTVBModule=1, eBiffTWorksheet=2,
			       eBiffTChart=3, eBiffTMacrosheet=4, eBiffTWorkspace=5,
			       eBiffTUnknown=6 } eBiff_filetype ;
#endif






