/**
 * ms-ole.h: MS Office OLE support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#ifndef GNUMERIC_MS_OLE_H
#define GNUMERIC_MS_OLE_H

#include <glib.h>

typedef guint32 BBPtr ;
typedef guint32 SBPtr ;

typedef struct _MS_OLE_HEADER
{
	/* sbd = Small Block Depot ( made up of BB's BTW ) */
	BBPtr   sbd_startblock ;
	guint32 number_of_sbd_blocks ;
	BBPtr   *sbd_list ; /* [number_of_sbd_blocks] very often 1 */

	guint32 number_of_sbf_blocks ;
	BBPtr   sbf_startblock ; /* Identifies the stream containing all small blocks are in. */
	BBPtr   *sbf_list ; /* [number_of_sbf_blocks] */

	guint32 number_of_root_blocks ;
	BBPtr   root_startblock ;
	BBPtr   *root_list ;
} MS_OLE_HEADER ;

typedef guint32 PPS_IDX ;
typedef enum _PPS_TYPE { MS_OLE_PPS_STORAGE = 1,
			 MS_OLE_PPS_STREAM  = 2,
			 MS_OLE_PPS_ROOT    = 5} PPS_TYPE ;

/**
 * Structure describing an OLE file
 **/
typedef struct _MS_OLE
{
	guint8  *mem ;
	guint32 length ;

	/**
	 * To be considered private
	 **/

	MS_OLE_HEADER header ; /* For speed cut down dereferences */
	int file_descriptor ;
} MS_OLE ;

/* Create new OLE file */
extern MS_OLE *ms_ole_create  (const char *name) ;
/* Open existing OLE file */
extern MS_OLE *ms_ole_new     (const char *name) ;
extern void    ms_ole_destroy (MS_OLE *ptr) ;

typedef struct _MS_OLE_DIRECTORY
{
	char      *name ;
	PPS_TYPE  type ;
	guint32   length ;
	PPS_IDX   pps ;
	/* Brain damaged linked list workaround */
	PPS_IDX   primary_entry ;

	/* Private */
	MS_OLE *file ;
} MS_OLE_DIRECTORY ;

extern MS_OLE_DIRECTORY *ms_ole_directory_new (MS_OLE *) ;
extern int  ms_ole_directory_next (MS_OLE_DIRECTORY *) ;
extern void ms_ole_directory_enter (MS_OLE_DIRECTORY *) ;
/* Pointer to the directory in which to create a new stream / storage object */
extern MS_OLE_DIRECTORY *ms_ole_directory_create (MS_OLE_DIRECTORY *d, char *name, PPS_TYPE type) ;
extern void ms_ole_directory_unlink (MS_OLE_DIRECTORY *) ;
extern void ms_ole_directory_destroy (MS_OLE_DIRECTORY *) ;

typedef struct _MS_OLE_STREAM
{
	guint32 block ;
	/**
	 * This has limits 0-SB_BLOCK_SIZE or 0-BB_BLOCK_SIZE.
	 * The last extra state saving an unused block at EOStream
	 **/
	guint16 offset ;
	guint32 end_block ;

	/**
	 * Attempts to copy length bytes into *ptr, returns true if
	 * successful, _does_ advance the stream pointer.
	 **/
	gboolean (*read_copy )(struct _MS_OLE_STREAM *, guint8 *ptr, guint32 length) ;
	/**
	 * Acertains whether there is a contiguous block length bytes,
	 * if so returns a pointer to it, to save a copy, does _not_
	 * advance the stream pointer.
	 **/
	guint8*  (*read_ptr  )(struct _MS_OLE_STREAM *, guint32 length) ;
	/**
	 * This function must be called to advance the stream pointer after
	 * any read.
	 **/
	void     (*advance   )(struct _MS_OLE_STREAM *, gint32 BYTES) ;
	/**
	 * This writes length bytes at *ptr to the stream, it _does_ advance
	 * the stream pointer.
	 **/
	void     (*write     )(struct _MS_OLE_STREAM *, guint8 *ptr, guint32 length) ;

	/**
	 * PRIVATE
	 **/
	MS_OLE *file ;
	PPS_IDX pps ;  
} MS_OLE_STREAM ;

/* Mode = 'r' or 'w' */
extern MS_OLE_STREAM *ms_ole_stream_open (MS_OLE_DIRECTORY *d, char mode) ;
extern void ms_ole_stream_close  (MS_OLE_STREAM *) ;

extern void dump (guint8 *ptr, int len) ;

#endif
