/*
 * ms-ole.h: MS Office OLE support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 */
#ifndef GNUMERIC_MS_OLE_H
#define GNUMERIC_MS_OLE_H

typedef unsigned char  BYTE ;
typedef short          WORD ;
typedef long           LONG ;

// EXTREMELY IMPORTANT TO PASS A BYTE PTR !
#define GETBYTE(p) (*(p+0))
#define GETWORD(p) (*(p+0)+(*(p+1)<<8))
#define GETLONG(p) (*(p+0)+ \
		    (*(p+1)<<8)+ \
		    (*(p+2)<<16)+ \
		    (*(p+3)<<24))

// Private, don't you dare !
#define MS_OLE_SPECIAL_BLOCK  0xfffffffd
#define MS_OLE_END_OF_CHAIN   0xfffffffe
#define MS_OLE_UNUSED_BLOCK   0xffffffff

typedef long BBPtr ;
#define MS_OLE_BB_BLOCK_SIZE     512
// N.B. First block is block '-1' :-)
#define MS_OLE_GET_BB_START_PTR(f,n) (f->mem+(n+1)*MS_OLE_BB_BLOCK_SIZE)

#define MS_OLE_BB_THRESHOLD   0x1000

// But small block numbers  are actualy offsets into a file
// of Big Blocks...
typedef long SBPtr ;
#define MS_OLE_SB_BLOCK_SIZE      64

typedef struct _MS_OLE_Header
{
  // bbd = Big Block Depot
  LONG number_of_bbd_blocks ;
  BBPtr *bbd_list ; // [number_of_bbd_blocks] very often 1

  // sbd = Small Block Depot ( made up of BB's BTW )
  BBPtr sbd_startblock ;
  LONG  number_of_sbd_blocks ;
  BBPtr *sbd_list ; // [number_of_sbd_blocks] very often 1 NB.

  LONG  number_of_sbf_blocks ;
  BBPtr sbf_startblock ; // This identifies the file that all small blocks are in.
  BBPtr *sbf_list ; // [number_of_sbf_blocks]

  LONG  number_of_root_blocks ;
  BBPtr root_startblock ;
  BBPtr *root_list ;

} MS_OLE_Header ;

#define MS_OLE_PPS_BLOCK_SIZE 0x80
#define MS_OLE_PPS_END_OF_CHAIN 0xffffffff
typedef enum _PPS_TYPE {eStorage=0, eStream=1, eRoot=2} PPS_TYPE ;
typedef long PPSPtr ;
// MS_OLE Property Storage
// Similar to a directory structure
typedef struct _MS_OLE_PPS
{
  WORD      *pps_name ;
  WORD      pps_sizeofname ;
  PPS_TYPE  pps_type ;
  PPSPtr    pps_me ;
  PPSPtr    pps_prev ;
  PPSPtr    pps_next ;
  PPSPtr    pps_dir  ; // Points to sub-dir if there is one.
  LONG      pps_size ;
  LONG      pps_startblock ; // BBPtr or SBPtr depending on pps_size

  struct    _MS_OLE_PPS *next ;
} MS_OLE_PPS ;


// Public, use this
typedef struct _MS_OLE_FILE
{
  // UNIX Stuff
  int fd ;
  BYTE *mem ;
  long length ;

  // Internal stuff
  MS_OLE_Header header ;
  // Ignore the silly clever directory structure, we have a 1D linked list !
  MS_OLE_PPS    *root, *end ;

} MS_OLE_FILE ;

typedef struct _MS_OLE_STREAM_POS
{
  LONG       block ;		// the MS_OLE Block we are in
  int        small_block ;	// Whether small or large blocks
  BYTE       *mem ;		// Current offset into block, postinc.
  int        block_left ;       // Length in bytes left in block - sort of redundant, = (mem-startmem)%SMALL/BIG_BLOCK_SIZE
  int        length_left ;	// Length in bytes left in stream.
  MS_OLE_FILE   *f ;		// the MS_OLE structure pointer
  MS_OLE_PPS    *p ;            // This stores the real length eg.
} MS_OLE_STREAM_POS ;

extern MS_OLE_STREAM_POS *MS_OLE_open_stream (MS_OLE_FILE *f, MS_OLE_PPS *p) ;

extern void dump (BYTE *ptr, int len) ;
extern MS_OLE_FILE *new_ms_ole_file (const char *name) ;
extern int ms_ole_analyse_file (MS_OLE_FILE *f) ;
extern void free_ms_ole_file (MS_OLE_FILE *ptr) ;

#endif


