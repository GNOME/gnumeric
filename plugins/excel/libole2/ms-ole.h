/**
 * ms-ole.h: MS Office OLE support for Gnumeric
 *
 * Authors:
 *    Michael Meeks (michael@imaginator.com)
 *    Arturo Tena (arturo@directmail.org)
 **/

#ifndef MS_OLE_H
#define MS_OLE_H

#include <fcntl.h>	/* for mode_t */
#include <glib.h>

typedef enum {
	MS_OLE_ERR_OK,
	MS_OLE_ERR_EXIST,
	MS_OLE_ERR_INVALID,
	MS_OLE_ERR_FORMAT,
	MS_OLE_ERR_PERM,
	MS_OLE_ERR_MEM,
	MS_OLE_ERR_SPACE,
	MS_OLE_ERR_NOTEMPTY,
	MS_OLE_ERR_BADARG
} MsOleErr;

typedef enum {
	MsOleSeekSet,
	MsOleSeekCur,
	MsOleSeekEnd
} MsOleSeek;

typedef enum  {
	MsOleStorageT = 1,
	MsOleStreamT  = 2,
	MsOleRootT    = 5
} MsOleType;

typedef guint32 MsOlePos;
typedef gint32  MsOleSPos;
/*
#ifdef G_HAVE_GINT64
	typedef guint64 MsOlePos;
	typedef gint64  MsOleSPos;
#else
	typedef guint32 MsOlePos;
	typedef gint32  MsOleSPos;
#endif
*/


typedef struct _MsOle             MsOle;
typedef struct _MsOleStat         MsOleStat;
typedef struct _MsOleStream       MsOleStream;
typedef struct _MsOleSysWrappers  MsOleSysWrappers;

struct _MsOleSysWrappers {
	int     (*open2)	(const char *pathname, int flags);
	int     (*open3)	(const char *pathname, int flags, mode_t mode);
	ssize_t (*read)		(int fd, void *buf, size_t count);
	int     (*close)	(int fd);
	ssize_t (*write)	(int fd, const void *buf, size_t count);
	off_t   (*lseek)	(int fd, off_t offset, int whence);
	int     (*isregfile)	(int fd);
	int     (*getfilesize)	(int fd, guint32 *size);
};

struct _MsOleStat {
	MsOleType type;
	MsOlePos  size;
};

#define                 ms_ole_open(fs,path)     ms_ole_open_vfs ((fs), (path), TRUE, NULL)
extern MsOleErr		ms_ole_open_vfs		(MsOle **fs,
						 const char *path,
						 gboolean try_mmap,
						 MsOleSysWrappers *wrappers);
#define                 ms_ole_create(fs,path)   ms_ole_create_vfs ((fs), (path), TRUE, NULL)
extern MsOleErr		ms_ole_create_vfs	(MsOle **fs,
						 const char *path,
						 int try_mmap,
						 MsOleSysWrappers *wrappers);
extern void		ms_ole_destroy		(MsOle **fs);
extern MsOleErr		ms_ole_unlink		(MsOle *fs,
						 const char *path);
extern MsOleErr		ms_ole_directory	(char ***names,
						 MsOle *fs,
						 const char *dirpath);
extern MsOleErr		ms_ole_stat		(MsOleStat *stat,
						 MsOle *fs,
						 const char *dirpath,
						 const char *name);

struct _MsOleStream {

	MsOlePos size;

	gint		(*read_copy)	(MsOleStream *stream,
					 guint8 *ptr,
					 MsOlePos length);

	guint8 *	(*read_ptr)	(MsOleStream *stream,
					 MsOlePos length);

	MsOleSPos	(*lseek)	(MsOleStream *stream,
					 MsOleSPos bytes,
					 MsOleSeek type);

	MsOlePos	(*tell)		(MsOleStream *stream);

	MsOlePos	(*write)	(MsOleStream *stream,
					 guint8 *ptr,
					 MsOlePos length);


	/**
	 * Private.
	 **/
	enum {
		MsOleSmallBlock,
		MsOleLargeBlock
	} type;
	MsOle		*file;
	void		*pps;		/* Straight PPS */
	GArray		*blocks;	/* A list of the blocks in the file
					   if NULL: no file */
	MsOlePos	 position;	/* Current offset into file.
					   Points to the next byte to read */
};

#define MS_OLE_GET_GUINT8(p)  (*((const guint8 *)(p) + 0))
#define MS_OLE_GET_GUINT16(p) (guint16)(*((const guint8 *)(p)+0) |        \
					(*((const guint8 *)(p)+1)<<8))
#define MS_OLE_GET_GUINT32(p) (guint32)(*((const guint8 *)(p)+0) |        \
					(*((const guint8 *)(p)+1)<<8) |   \
					(*((const guint8 *)(p)+2)<<16) |  \
					(*((const guint8 *)(p)+3)<<24))
#define MS_OLE_GET_GUINT64(p) (MS_OLE_GET_GUINT32(p) | \
			       (((guint32)MS_OLE_GET_GUINT32((const guint8 *)(p)+4))<<32))

#define MS_OLE_SET_GUINT8(p,n)  (*((guint8 *)(p) + 0) = n)
#define MS_OLE_SET_GUINT16(p,n) ((*((guint8 *)(p)+0)=((n)&0xff)),         \
				 (*((guint8 *)(p)+1)=((n)>>8)&0xff))
#define MS_OLE_SET_GUINT32(p,n) ((*((guint8 *)(p)+0)=((n))&0xff),         \
				 (*((guint8 *)(p)+1)=((n)>>8)&0xff),      \
				 (*((guint8 *)(p)+2)=((n)>>16)&0xff),     \
				 (*((guint8 *)(p)+3)=((n)>>24)&0xff))

extern MsOleErr		ms_ole_stream_open	(MsOleStream ** const stream,
						 MsOle *fs,
						 const char *dirpath,
						 const char *name,
						 char mode);
extern MsOleErr		ms_ole_stream_close	(MsOleStream ** const stream);
extern MsOleErr		ms_ole_stream_duplicate	(MsOleStream ** const stream_copy,
						 const MsOleStream *
						 const stream);

extern void		ms_ole_dump		(guint8 const *ptr,
						 guint32 len);

extern void		ms_ole_ref		(MsOle *fs);
extern void		ms_ole_unref		(MsOle *fs);
extern void		ms_ole_debug		(MsOle *fs,
						 int magic);



#endif	/* MS_OLE_H */
