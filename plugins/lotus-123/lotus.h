#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#     define LOTUS_GETDOUBLE(p)   (*((double*)(p)))
#     define LOTUS_SETDOUBLE(p,q) (*((double*)(p))=(q))
#else
#     define LOTUS_GETDOUBLE(p)   (lotus_getdouble(p))
#     define LOTUS_SETDOUBLE(p,q) (lotus_setdouble(p,q))

double lotus_getdouble (const guint8 *p);
void   lotus_setdouble (guint8 *p, double d);
#endif

gboolean lotus_read (Workbook *wb, const char *filename);
