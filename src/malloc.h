/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: malloc.h,v 35004.7 1997/11/17 08:34:11 hawkeye Exp $ */

#ifndef MALLOC_H
#define MALLOC_H

extern int low_memory_warning;

#define XMALLOC(size) xmalloc((size), __FILE__, __LINE__)
#define XREALLOC(ptr, size) xrealloc((ptr), (size), __FILE__, __LINE__)
#define MALLOC(size) dmalloc((size), __FILE__, __LINE__)
#define REALLOC(ptr, size) drealloc((ptr), (size), __FILE__, __LINE__)
#define FREE(ptr) xfree((GENERIC*)(ptr), __FILE__, __LINE__)

#ifdef DMALLOC
extern GENERIC  *FDECL(dmalloc,(long unsigned size,
                       CONST char *file, CONST int line));
extern GENERIC  *FDECL(drealloc,(GENERIC *ptr, long unsigned size,
                       CONST char *file, CONST int line));
extern void   FDECL(dfree,(GENERIC *ptr, CONST char *file, CONST int line));
#else
# define dmalloc(size, file, line) malloc(size)
# define drealloc(ptr, size, file, line) realloc((GENERIC*)(ptr), (size))
# define dfree(ptr, file, line) free((GENERIC*)(ptr))
#endif

extern GENERIC  *FDECL(xmalloc,(long unsigned size,
                       CONST char *file, CONST int line));
extern GENERIC  *FDECL(xrealloc,(GENERIC *ptr, long unsigned size,
                       CONST char *file, CONST int line));
extern void      FDECL(xfree,(GENERIC *ptr, CONST char *file, CONST int line));
extern void      NDECL(init_malloc);

/* Fast allocation from pool.
 * Should be used only on objects which are freed frequently.
 */
#define palloc(item, type, pool, next) \
    ((pool) ? \
      ((item) = (pool), (pool) = pool->next) : \
      ((item) = (type *)XMALLOC(sizeof(type))))

#ifndef DMALLOC
#define pfree(item, pool, next)  (item->next = (pool), (pool) = (item))
#else
#define pfree(item, pool, next)  FREE(item)
#endif


#ifdef DMALLOC
extern void   NDECL(free_reserve);
extern void   FDECL(debug_mstats,(CONST char *s));
#endif

#endif /* MALLOC_H */
