static char *IMAGE_C="\n\n#ifdef TEST\n#include <stdio.h>\n#include <math.h>\n#define IMFILTRTN _FilterImage\n#define NMASK 0\n#define MASKDIM 0\n#define _masks NULL\n#define NSHAPE 2\n#define NREGION 2\n#define FILTER ((imcircle(g,1,1,1,4,(double)x,(double)y,8.0,8.0,5.0)))&&(imcircle(g,2,2,0,1,(double)x,(double)y,8.0,8.0,3.0))\n#define FILTSTR \"((imcircle(g,1,1,1,4,(double)x,(double)y,8.0,8.0,5.0)))&&(imcircle(g,2,2,0,1,(double)x,(double)y,8.0,8.0,3.0))\"\n#define FINIT imcirclei(g,1,1,1,4,(double)x,(double)y,8.0,8.0,5.0);imcirclei(g,2,2,0,1,(double)x,(double)y,8.0,8.0,3.0);\n#include \"regions.h\"\n#endif\n\n\nFilterMask masks=NULL;		\nint maxmask;			\nint nmask;			\nint nreg;			\nint rid;			\nint x, y;			\nint rlen;			\nint *rbuf;			\nint *rptr;			\n\nvoid incnmask(void)\n{\n  int omax;\n  nmask++;\n  if( nmask >= maxmask ){\n    omax = maxmask;\n    maxmask += MASKINC;\n    masks = (FilterMask)realloc(masks, maxmask*sizeof(FilterMaskRec));\n    memset(masks+omax, 0, (maxmask-omax)*sizeof(FilterMaskRec));\n  }\n}\n\nFilterMask\nIMFILTRTN(int txmin, int txmax, int tymin, int tymax, int tblock, int *got)\n{\n  int i, j;\n  int fieldonly;\n  GFilt g;\n  Scan scan, tscan;\n\n  \n  if( NSHAPE <=0 ){\n    *got = 0;\n    return NULL;\n  }\n  \n  g = (GFilt)calloc(1, sizeof(GFiltRec));\n  \n  fieldonly = (NSHAPE==1) && strstr(FILTSTR, \"field\");\n  \n  g->nshapes = NSHAPE;\n  g->maxshapes = (NSHAPE*(XSNO+1))+1;\n  g->shapes = (Shape)calloc(g->maxshapes, sizeof(ShapeRec));\n  \n  g->block= max(1,tblock);\n  g->xmin = max(1,txmin); \n  g->xmax = txmax;\n  g->ymin = max(1,tymin);\n  g->ymax = tymax;\n  \n  g->x0 = 1;\n  g->y0 = 1;\n  g->x1 = (g->xmax-g->xmin)/g->block+1;\n  g->y1 = (g->ymax-g->ymin)/g->block+1;\n  \n  rlen = g->x1 - g->x0 + 1;\n  rbuf = (int *)calloc(rlen+1, sizeof(int));\n  \n  maxmask = MASKINC;\n  masks = (FilterMask)calloc(maxmask, sizeof(FilterMaskRec));\n  \n  nmask = 0;\n  masks[nmask].region = 0;\n  \n  nreg = 0;\n  \n  g->ybuf = (int *)calloc(g->y1+1, sizeof(int));\n  g->x0s = (int *)calloc(g->y1+1, sizeof(int));\n  g->x1s = (int *)calloc(g->y1+1, sizeof(int));\n  \n  for(i=0; i<=g->y1; i++) g->x0s[i]  = g->x1;\n  for(i=0; i<=g->y1; i++) g->x1s[i]  = g->x0;\n  \n  if( NMASK ){\n    g->nmask = NMASK; \n    g->maskdim = MASKDIM;\n    g->masks = _masks;\n  }\n  \n  FINIT;\n  \n  for(y=g->y0; y<=g->y1; y++){\n    if( fieldonly ){\n      \n      masks[nmask].region = 1;\n      masks[nmask].y = y - g->y0 + 1;\n      masks[nmask].xstart = 1;\n      masks[nmask].xstop = (g->x1 - g->x0 + 1);\n      incnmask();\n      continue;\n    }\n    if( g->ybuf[y] ){\n      \n      if( masks[nmask].region ){\n	\n	incnmask();\n	masks[nmask].region = 0;\n      }\n      \n      for(x=g->x0s[y], rptr=&rbuf[1+(g->x0s[y]-g->x0)]; x<=g->x1s[y];\n	  x++, rptr++){\n	\n	g->rid = 0;\n	if( FILTER ){\n	  \n	  if( *rptr == 0 ){\n	    nreg++;\n	    *rptr = g->rid ? g->rid : -1;\n	  }\n	  \n	  else if( (*rptr == -1) && (g->rid >0) ){\n	    *rptr = g->rid;\n	  }\n	}\n      }\n    }\n    \n    if( nreg ){\n      for(i=1; i<=rlen; i++){\n	if( rbuf[i] != masks[nmask].region ){\n	  \n	  if( masks[nmask].region ){\n	    masks[nmask].xstop = i - 1;\n	    \n	    incnmask();\n	  }\n	  masks[nmask].y = y - g->y0 + 1;\n	  masks[nmask].region = rbuf[i];\n	  masks[nmask].xstart = i;\n	}\n      }\n      \n      if( masks[nmask].region ){\n	masks[nmask].xstop = (g->x1 - g->x0 + 1);\n	\n	incnmask();\n      }\n      \n      (void)memset(rbuf, 0, (rlen+1)*sizeof(int));\n      rptr = rbuf;\n      nreg = 0;\n    }\n  }\n  \n  if( rbuf) free(rbuf);\n  \n  if( g ){\n    for(i=0; i<g->maxshapes; i++){\n      if( g->shapes[i].scanlist ){\n	for(j=0; j<=g->y1; j++){\n	  if( g->shapes[i].scanlist[j] ){\n	    for(scan=g->shapes[i].scanlist[j]; scan; ){\n	      tscan = scan->next;\n	      free(scan);\n	      scan = tscan;\n	    }\n	  }\n	}\n	free(g->shapes[i].scanlist);\n      }\n      if( g->shapes[i].pts ) free(g->shapes[i].pts);\n      if( g->shapes[i].xv ) free(g->shapes[i].xv);\n    }\n    if( g->masks )  free(g->masks);\n    if( g->shapes ) free(g->shapes);\n    if( g->ybuf )   free(g->ybuf);\n    if( g->x0s )    free(g->x0s);\n    if( g->x1s )    free(g->x1s);\n    if( g )         free(g);\n  }\n  \n  *got = nmask;\n  return masks;\n}\n\nint main(int argc, char **argv)\n{\n  int i;\n  int get, got;\n#if DO_FILTER_SWAP\n  int sgot;\n#endif\n#if HAVE_MINGW32==0\n  int pipes[4];\n#endif\n  int txmin, txmax, tymin, tymax, tblock;\n  char tbuf[SZ_LINE];\n  char *s=NULL, *t=NULL, *u=NULL;\n#if USE_WIN32\n  HANDLE hStdin, hStdout; \n  DWORD dwRead, dwWritten; \n#endif\n\n  \n#if HAVE_MINGW32==0\n  if( (s=getenv(\"LAUNCH_PIPES\")) ){\n    t = (char *)strdup(s);\n    for(i=0, u=(char *)strtok(t, \",\"); i<4 && u; \n	i++, u=(char *)strtok(NULL,\",\")){\n      pipes[i] = atoi(u);\n    }\n    if( t ) free(t);\n    if( i < 4 ) return(1);\n    close(pipes[0]);\n    close(pipes[3]);\n    dup2(pipes[2], 0);  close(pipes[2]);\n    dup2(pipes[1], 1);  close(pipes[1]);\n  }\n#endif\n\n#if USE_WIN32\n  hStdout = GetStdHandle(STD_OUTPUT_HANDLE); \n  hStdin = GetStdHandle(STD_INPUT_HANDLE); \n  if( (hStdout == INVALID_HANDLE_VALUE) || (hStdin == INVALID_HANDLE_VALUE) ){\n    unlink(argv[0]);\n    return 0;\n  }\n#endif\n\n  \n#ifdef TEST\n  while( fgets(tbuf, SZ_LINE, stdin) ){\n#else\n#if USE_WIN32\n  while((ReadFile(hStdin, &get, sizeof(int), &dwRead, NULL) != FALSE) && \n	(dwRead == sizeof(int)) ){\n#else\n  while( read(0, &get, sizeof(int)) == sizeof(int) ){\n#endif\n#if DO_FILTER_SWAP\n    switch(sizeof(int)){\n    case 2:\n      _sw2((char *)&get,2,NULL,0);\n      break;\n    case 4:\n      _sw4((char *)&get,4,NULL,0);\n      break;\n    case 8:\n      _sw8((char *)&get,8,NULL,0);\n      break;\n    }\n#endif\n#if USE_WIN32\n    if((ReadFile(hStdin, tbuf, get, &dwRead, NULL)==FALSE) || (dwRead != get))\n      break;\n#else\n    if(read(0, tbuf, get) != get) \n      break;\n#endif\n#endif \n    if(sscanf(tbuf, \"%d %d %d %d %d\",\n	      &txmin, &txmax, &tymin, &tymax, &tblock)!=5){\n      break;\n    }\n    masks = IMFILTRTN(txmin, txmax, tymin, tymax, tblock, &got);\n#ifdef TEST\n    \n    fprintf(stdout, \"nmask=%d\\n\", nmask);\n    for(i=0; i<nmask; i++){\n      fprintf(stdout, \"region: %d\tx: (%d,%d)\ty: %d\\n\",\n	      masks[i].region, masks[i].xstart, masks[i].xstop, masks[i].y);\n    }\n    fflush(stdout);\n#else\n    \n    got =  got * sizeof(FilterMaskRec);\n#if DO_FILTER_SWAP\n    sgot = got;\n    switch(sizeof(int)){\n    case 2:\n      _sw2((char *)&sgot,2,NULL,0);\n      break;\n    case 4:\n      _sw4((char *)&sgot,4,NULL,0);\n      break;\n    case 8:\n      _sw8((char *)&sgot,8,NULL,0);\n      break;\n    }\n#if USE_WIN32\n    WriteFile(hStdout, &sgot, sizeof(int), &dwWritten, NULL); \n#else\n    write(1, &sgot, sizeof(int));\n#endif\n#else\n#if USE_WIN32\n    WriteFile(hStdout, &got, sizeof(int), &dwWritten, NULL); \n#else\n    write(1, &got, sizeof(int));\n#endif\n#endif\n#if DO_FILTER_SWAP\n    switch(sizeof(int)){\n    case 2:\n      _sw2((char *)masks,got,NULL,0);\n      break;\n    case 4:\n      _sw4((char *)masks,got,NULL,0);\n      break;\n    case 8:\n      _sw8((char *)masks,got,NULL,0);\n      break;\n    }\n#endif\n#if USE_WIN32\n    WriteFile(hStdout, masks, got, &dwWritten, NULL); \n#else\n    write(1, masks, got);\n#endif\n#endif\n    \n    if( masks ) free(masks);\n  }\n#ifndef TEST\n  unlink(argv[0]);\n#endif\n  return 0;\n}\n";
