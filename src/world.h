/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: world.h,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

#ifndef WORLD_H
#define WORLD_H

#define WORLD_TEMP     001
#define WORLD_CONN     002
#define WORLD_ACTIVE   004

typedef struct World {         /* World structure */
    int flags;
    struct World *next;
    char *name;                /* name of world */
    char *character;           /* login name */
    char *pass;                /* password */
    char *address;             /* host name */
    char *port;                /* port number or service name */
    char *mfile;               /* macro file */
    char *type;                /* server type (tiny, lp...) */
    struct Sock *socket;       /* open socket, if any */
#ifndef NO_HISTORY
    struct History *history;   /* history and logging info */
#endif
} World;


extern World *FDECL(new_world,(char *name, char *character, char *pass,
                    char *address, char *port, char *mfile, char *type));
extern int    FDECL(list_worlds,(int full, char *pattern, TFILE *file));
extern void   FDECL(free_world,(World *w));
extern void   FDECL(nuke_world,(World *w));
extern World *NDECL(get_default_world);
extern World *NDECL(get_world_header);
extern World *FDECL(find_world,(char *name));

#endif /* WORLD_H */