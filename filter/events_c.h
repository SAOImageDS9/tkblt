static char *EVENTS_C="\n\n#ifdef TEST\n#include <math.h>\n#define EVFILTRTN _FilterEvents\n#define NSHAPE 1\n#define NREGION 1\n#define _X_ X\n#define _Y_ Y\n#define FILTER ((circle(g,1,1,1,_X_,_Y_,1,2,3)))\n#define EVSIZE 4\n#define X *((short *)(eptr+0))\n#define Y *((short *)(eptr+2))\n#include \"regions.h\"\n#endif\n\nstatic char __abuf[EVSIZE+1];\nstatic char *acopy(void *s, int n)\n{\n  memset(__abuf, 0, n+1);\n  memmove(__abuf, s, n);\n  return __abuf;\n}\n\nvoid *EVFILTRTN(void *tg, char *ebuf, int ne, int esize, int *rbuf)\n{\n  int i, j;\n  int  *rptr;\n  char *eptr;\n  Scan scan, tscan;\n  GFilt g = (GFilt)tg;\n\n  \n  \n  eptr = ebuf;\n\n  if( !g ){\n    g = (GFilt)calloc(1, sizeof(GFiltRec));\n#if NSHAPE\n    \n    g->nshapes = NSHAPE;\n    g->maxshapes = (NSHAPE*(XSNO+1))+1;\n    g->shapes = (Shape)calloc(g->maxshapes, sizeof(ShapeRec));\n#endif\n#ifdef EVSECT\n    \n    g->evsect = EVSECT;\n    sscanf(g->evsect, \"%d %d %d %d %d\",\n	   &g->xmin, &g->xmax, &g->ymin, &g->ymax, &g->block);\n    \n    g->x0 = 1;\n    g->y0 = 1;\n    g->x1 = (g->xmax-g->xmin)/g->block+1;\n    g->y1 = (g->ymax-g->ymin)/g->block+1;\n    \n    g->ybuf = (int *)calloc(g->y1+1, sizeof(int));\n    g->x0s = (int *)calloc(g->y1+1, sizeof(int));\n    g->x1s = (int *)calloc(g->y1+1, sizeof(int));\n    \n    for(i=0; i<=g->y1; i++) g->x0s[i]  = g->x0;\n    for(i=0; i<=g->y1; i++) g->x1s[i]  = g->x1;\n    \n    if( NMASK ){\n      g->nmask = NMASK; \n      g->maskdim = MASKDIM;\n      g->masks = _masks;\n    }\n    \n#if DO_FILTER_SWAP\n    memset(_swf, 0, EVSIZE);\n#endif\n    if( eptr ) FINIT;\n    \n    g->tlminx = TLMINX;\n    g->tlminy = TLMINY;\n    g->usebinsiz = USEBINSIZ;\n    if( BINSIZX > 0.0 )\n      g->binsizx = BINSIZX;\n    else\n      g->binsizx = 1.0;\n    if( BINSIZY > 0.0 )\n      g->binsizy = BINSIZY;\n    else\n      g->binsizy = 1.0;\n    g->tloff =  TLOFF;\n#endif\n  }\n\n  \n  if( !ebuf && !rbuf && (ne<0) ){\n#if NSHAPE\n    \n    for(i=0; i<g->maxshapes; i++){\n      if( g->shapes[i].scanlist ){\n	for(j=0; j<g->y1; j++){\n	  if( g->shapes[i].scanlist[j] ){\n	    for(scan=g->shapes[i].scanlist[j]; scan; ){\n	      tscan = scan->next;\n	      if( scan ) free(scan);\n	      scan = tscan;\n	    }\n	  }\n	}\n	if( g->shapes[i].scanlist ) free(g->shapes[i].scanlist);\n      }\n      if( g->shapes[i].pts ) free(g->shapes[i].pts);\n      if( g->shapes[i].xv ) free(g->shapes[i].xv);\n    }\n    if( g->masks )  free(g->masks);\n    if( g->shapes ) free(g->shapes);\n    if( g->ybuf )   free(g->ybuf);\n    if( g->x0s )    free(g->x0s);\n    if( g->x1s )    free(g->x1s);\n    if( g )         free(g);\n#endif\n    return NULL;\n  }\n  else{\n    \n    for(rptr=rbuf, eptr=ebuf; ne--; rptr++, eptr += esize){\n      g->rid = 0;\n#if DO_FILTER_SWAP\n      memset(_swf, 0, EVSIZE);\n#endif\n      *rptr = ((FILTER) ? (g->rid ? g->rid : -1) : 0);\n    }\n    return (void *)g;\n  }\n}\n\nint main(int argc, char **argv)\n{\n  int i;\n#if HAVE_MINGW32==0\n  int pipes[4];\n#endif\n  int get, got;\n#if DO_FILTER_SWAP\n  int sgot;\n#endif\n  int n;\n  int *rbuf;\n  char *ebuf, *etop;\n  char *s=NULL, *t=NULL, *u=NULL;\n  void *g=NULL;\n#if USE_WIN32\n  HANDLE hStdin, hStdout; \n  DWORD dwRead, dwWritten; \n#endif\n\n  \n#if HAVE_MINGW32==0\n  if( (s=getenv(\"LAUNCH_PIPES\")) ){\n    t = (char *)strdup(s);\n    for(i=0, u=(char *)strtok(t, \",\"); i<4 && u; \n	i++, u=(char *)strtok(NULL,\",\")){\n      pipes[i] = atoi(u);\n    }\n    if( t ) free(t);\n    if( i < 4 ) return(1);\n    close(pipes[0]);\n    close(pipes[3]);\n    dup2(pipes[2], 0);  close(pipes[2]);\n    dup2(pipes[1], 1);  close(pipes[1]);\n  }\n#endif\n\n#if USE_WIN32\n  hStdout = GetStdHandle(STD_OUTPUT_HANDLE); \n  hStdin = GetStdHandle(STD_INPUT_HANDLE); \n  if( (hStdout == INVALID_HANDLE_VALUE) || (hStdin == INVALID_HANDLE_VALUE) ){\n    unlink(argv[0]);\n    return 0;\n  }\n#endif\n\n  \n#if USE_WIN32\n  while((ReadFile(hStdin, &get, sizeof(int), &dwRead, NULL) >0) && \n	(dwRead == sizeof(int)) ){\n#else\n  while( read(0, &get, sizeof(int)) == sizeof(int) ){\n#endif\n#if DO_FILTER_SWAP\n    switch(sizeof(int)){\n    case 2:\n      _sw2((char *)&get,2,NULL,0);\n      break;\n    case 4:\n      _sw4((char *)&get,4,NULL,0);\n      break;\n    case 8:\n      _sw8((char *)&get,8,NULL,0);\n      break;\n    }\n#endif\n    ebuf = (char *)calloc(get, sizeof(char));\n#if USE_WIN32\n    for(n=0, etop=ebuf; get>0; etop += got, get -= dwRead){\n      if((ReadFile(hStdin, etop, get, &dwRead, NULL)==FALSE) || (dwRead==0))\n	break;\n      n += dwRead;\n    }\n#else\n    for(n=0, etop=ebuf; get>0; etop += got, get -= got){\n      if( (got=read(0, etop, get)) <=0 ) \n	break;\n      n += got;\n    }\n#endif\n    n /= EVSIZE;\n    \n    rbuf = (int *)calloc(n, sizeof(int));\n    \n    g = EVFILTRTN(g, ebuf, n, EVSIZE, rbuf);\n    \n    got = n*sizeof(int);\n#if DO_FILTER_SWAP\n    sgot = got;\n    switch(sizeof(int)){\n    case 2:\n      _sw2((char *)&sgot,2,NULL,0);\n      break;\n    case 4:\n      _sw4((char *)&sgot,4,NULL,0);\n      break;\n    case 8:\n      _sw8((char *)&sgot,8,NULL,0);\n      break;\n    }\n#if USE_WIN32\n    WriteFile(hStdout, &sgot, sizeof(int), &dwWritten, NULL); \n#else\n    write(1, &sgot, sizeof(int));\n#endif\n#else\n#if USE_WIN32\n    WriteFile(hStdout, &got, sizeof(int), &dwWritten, NULL); \n#else\n    write(1, &got, sizeof(int));\n#endif\n#endif\n#if DO_FILTER_SWAP\n    switch(sizeof(int)){\n    case 2:\n      _sw2((char *)rbuf,got,NULL,0);\n      break;\n    case 4:\n      _sw4((char *)rbuf,got,NULL,0);\n      break;\n    case 8:\n      _sw8((char *)rbuf,got,NULL,0);\n      break;\n    }\n#endif\n#if USE_WIN32\n    WriteFile(hStdout, rbuf, got, &dwWritten, NULL); \n#else\n    write(1, rbuf, got);\n#endif\n    if( ebuf) free(ebuf);\n    if( rbuf ) free(rbuf);\n  }\n  EVFILTRTN(g, NULL, -1, 0, NULL);\n  unlink(argv[0]);\n  return 0;\n}\n";
