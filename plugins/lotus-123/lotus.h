#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#     define LOTUS_GETDOUBLE(p)   (*((double*)(p)))
#     define LOTUS_SETDOUBLE(p,q) (*((double*)(p))=(q))
#else
#     define LOTUS_GETDOUBLE(p)   (lotus_getdouble(p))
#     define LOTUS_SETDOUBLE(p,q) (lotus_setdouble(p,q))

extern double lotus_get_double (guint8 *p);
extern void   lotus_set_double (guint8 *p, double d);
#endif

Workbook     *lotus_read     (const char *filename);
