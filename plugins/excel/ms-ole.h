/*
 * ms-ole.h: MS Office OLE support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 */
#ifndef GNUMERIC_MS_OLE_H
#define GNUMERIC_MS_OLE_H

#include <glib.h>

typedef long BBPtr ;
typedef long SBPtr ;

typedef struct _MS_OLE_Header
{
  // bbd = Big Block Depot
  guint32 number_of_bbd_blocks ;
  BBPtr   *bbd_list ; // [number_of_bbd_blocks] very often 1

  // sbd = Small Block Depot ( made up of BB's BTW )
  BBPtr   sbd_startblock ;
  guint32 number_of_sbd_blocks ;
  BBPtr   *sbd_list ; // [number_of_sbd_blocks] very often 1 NB.

  guint32 number_of_sbf_blocks ;
  BBPtr   sbf_startblock ; // This identifies the file that all small blocks are in.
  BBPtr   *sbf_list ; // [number_of_sbf_blocks]

  guint32 number_of_root_blocks ;
  BBPtr   root_startblock ;
  BBPtr   *root_list ;

} MS_OLE_Header ;

typedef enum _PPS_TYPE { MS_OLE_PPS_STORAGE = 0,
			 MS_OLE_PPS_STREAM  = 1,
			 MS_OLE_PPS_ROOT    = 2} PPS_TYPE ;
typedef guint32 PPSPtr ;
/* MS_OLE Property Storage
   Similar to a directory structure */
typedef struct _MS_OLE_PPS
{
  char      *pps_name ;
  PPS_TYPE  pps_type ;
  PPSPtr    pps_me ;
  PPSPtr    pps_prev ;
  PPSPtr    pps_next ;
  PPSPtr    pps_dir  ; // Points to sub-dir if there is one.
  guint32   pps_size ;
  guint32   pps_startblock ; // BBPtr or SBPtr depending on pps_size

  struct    _MS_OLE_PPS *next ;
} MS_OLE_PPS ;

/**
 * Structure describing an OLE file
 **/
typedef struct _MS_OLE
{
  guint8  *mem ;
  guint32 length ;

  /* To be considered private */
  MS_OLE_Header header ;
  MS_OLE_PPS    *root, *end ;

} MS_OLE ;

extern MS_OLE *ms_ole_new     (const char *name) ;
extern void    ms_ole_destroy (MS_OLE *ptr) ;

typedef struct _MS_OLE_DIRECTORY
{
  char      *name ;
  PPS_TYPE  type ;
  guint32   length ;

  /* Private */
  MS_OLE *file ;
  MS_OLE_PPS *pps ;
} MS_OLE_DIRECTORY ;

extern MS_OLE_DIRECTORY *ms_ole_directory_new (MS_OLE *) ;
extern int  ms_ole_directory_next (MS_OLE_DIRECTORY *) ;
extern void ms_ole_directory_destroy (MS_OLE_DIRECTORY *) ;

typedef struct _MS_OLE_STREAM
{
  guint32    block ;		// the MS_OLE Block we are in
  int        small_block ;	// Whether small or large blocks
  guint8     *mem ;		// Current offset into block, postinc.
  int        block_left ;       // Length in guint8s left in block - sort of redundant, = (mem-startmem)%SMALL/BIG_BLOCK_SIZE
  int        length_left ;	// Length in guint8s left in stream.
  MS_OLE     *f ;		// the MS_OLE structure pointer
  MS_OLE_PPS *p ;               // This stores the real length eg.
} MS_OLE_STREAM ;

/* Mode = 'r' or 'w' */
extern MS_OLE_STREAM *ms_ole_stream_open (MS_OLE *f, char *name, char mode) ;
extern void ms_ole_stream_close (MS_OLE_STREAM *st) ;

extern void dump (guint8 *ptr, int len) ;

#endif





