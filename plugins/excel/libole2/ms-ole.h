/**
 * ms-ole.h: MS Office OLE support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#ifndef MS_OLE_H
#define MS_OLE_H

#include <glib.h>

/*
 * NB. stream->read_copy (...) will give you an array of guint8's
 * use these macros to get data from this array.
 */

#define MS_OLE_GET_GUINT8(p) (*((const guint8 *)(p)+0))
#define MS_OLE_GET_GUINT16(p) (guint16)(*((const guint8 *)(p)+0) | \
					(*((const guint8 *)(p)+1)<<8))
#define MS_OLE_GET_GUINT32(p) (guint32)(*((const guint8 *)(p)+0) | \
					(*((const guint8 *)(p)+1)<<8) | \
					(*((const guint8 *)(p)+2)<<16) | \
					(*((const guint8 *)(p)+3)<<24))
#define MS_OLE_GET_GUINT64(p) (MS_OLE_GET_GUINT32(p) | \
			       (((guint32)MS_OLE_GET_GUINT32((const guint8 *)(p)+4))<<32))

#define MS_OLE_SET_GUINT8(p,n)  (*((guint8 *)(p)+0)=n)
#define MS_OLE_SET_GUINT16(p,n) ((*((guint8 *)(p)+0)=((n)&0xff)), \
				 (*((guint8 *)(p)+1)=((n)>>8)&0xff))
#define MS_OLE_SET_GUINT32(p,n) ((*((guint8 *)(p)+0)=((n))&0xff), \
				 (*((guint8 *)(p)+1)=((n)>>8)&0xff), \
				 (*((guint8 *)(p)+2)=((n)>>16)&0xff), \
				 (*((guint8 *)(p)+3)=((n)>>24)&0xff))

/* Whether to use memory mapped IO */
#define OLE_MMAP  1

typedef enum { MS_OLE_ERR_OK,
	       MS_OLE_ERR_EXIST,
	       MS_OLE_ERR_INVALID,
	       MS_OLE_ERR_FORMAT,
	       MS_OLE_ERR_PERM,
	       MS_OLE_ERR_MEM,
	       MS_OLE_ERR_SPACE,
	       MS_OLE_ERR_BADARG } MsOleErr;

/* Block pointer */
typedef guint32 BLP;

/* Forward declarations of types */
typedef struct _MsOle          MsOle;
typedef struct _MsOleStream    MsOleStream;

typedef enum { MsOleSeekSet, MsOleSeekCur, MsOleSeekEnd } MsOleSeek;
#ifdef G_HAVE_GINT64
        typedef guint32 MsOlePos;
        typedef gint32  MsOleSPos;
#else
        typedef gint32  MsOleSPos;
        typedef guint32 MsOlePos;
#endif

#ifndef MS_OLE_H_IMPLEMENTATION
	struct _MsOle {
		int dummy;
	};
#endif /* MS_OLE_H_IMPLEMENTATION */

/* Create new OLE file */
extern MsOleErr ms_ole_create      (MsOle **, const char *name) ;
/* Open existing OLE file */
extern MsOleErr ms_ole_open        (MsOle **, const char *name) ;
extern void     ms_ole_ref         (MsOle *);
extern void     ms_ole_unref       (MsOle *);
extern void     ms_ole_destroy     (MsOle **);

struct _MsOleStream
{
	enum { MsOleSmallBlock, MsOleLargeBlock } type;

	MsOlePos        size; /* Size in bytes */

	/**
	 * Attempts to copy length bytes into *ptr, returns true if
	 * successful, _does_ advance the stream pointer.
	 **/
	MsOlePos  (*read_copy )(MsOleStream *, guint8 *ptr, MsOlePos length) ;
	/**
	 * Acertains whether there is a contiguous block length bytes,
	 * if so returns a pointer to it and _does_ advance the stream pointer.
	 * otherwise returns NULL and does _not_ advance the stream pointer.
	 **/
	guint8*   (*read_ptr  )(MsOleStream *, MsOlePos length) ;
	MsOleSPos (*lseek     )(MsOleStream *, MsOleSPos bytes, MsOleSeek type) ;
	MsOlePos  (*tell      )(MsOleStream *);
	/**
	 * This writes length bytes at *ptr to the stream, and advances
	 * the stream pointer.
	 **/
	MsOlePos  (*write     )(MsOleStream *, guint8 *ptr, MsOlePos length) ;

	/**
	 * PRIVATE
	 **/
	MsOle      *file ;
	void       *pps ;     /* Straight PPS * */
	GArray     *blocks;   /* A list of the blocks in the file if NULL: no file */
	MsOlePos    position; /* Current offset into file. Points to the next byte to read */
};

/* Mode = 'r' or 'w' */
extern MsOleErr ms_ole_stream_open       (MsOleStream ** const stream, MsOle *f,
					  const char *path, const char *fname, char mode);
extern MsOleErr ms_ole_stream_close      (MsOleStream ** const stream);
extern MsOleErr ms_ole_stream_duplicate  (MsOleStream ** const copy,
					  const MsOleStream * const stream);
extern MsOleErr ms_ole_storage_unlink    (MsOle *f, const char *path);
extern MsOleErr ms_ole_stream_unlink     (MsOle *f, const char *path);
extern MsOleErr ms_ole_storage_directory (char ***names, const char *path);

extern void dump (guint8 const *ptr, guint32 len) ;

/* Do not use */
extern void ms_ole_debug (MsOle *, int magic);
#endif
