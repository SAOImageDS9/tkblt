/*
 * Smithsonian Astrophysical Observatory, Cambridge, MA, USA
 * This code has been modified under the terms listed below and is made
 * available under the same terms.
 */

/*
 *	Copyright 1993-2004 George A Howlett.
 *
 *	Permission is hereby granted, free of charge, to any person obtaining
 *	a copy of this software and associated documentation files (the
 *	"Software"), to deal in the Software without restriction, including
 *	without limitation the rights to use, copy, modify, merge, publish,
 *	distribute, sublicense, and/or sell copies of the Software, and to
 *	permit persons to whom the Software is furnished to do so, subject to
 *	the following conditions:
 *
 *	The above copyright notice and this permission notice shall be
 *	included in all copies or substantial portions of the Software.
 *
 *	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *	NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 *	LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 *	OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *	WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _BLT_GR_ELEM_H
#define _BLT_GR_ELEM_H

#include <bltVector.h>

#define ELEM_SOURCE_VALUES	0
#define ELEM_SOURCE_VECTOR	1

#define SEARCH_X	0
#define SEARCH_Y	1
#define SEARCH_BOTH	2

#define SHOW_NONE	0
#define SHOW_X		1
#define SHOW_Y		2
#define SHOW_BOTH	3

#define SEARCH_POINTS	0	/* Search for closest data point. */
#define SEARCH_TRACES	1	/* Search for closest point on trace.
				 * Interpolate the connecting line segments if
				 * necessary. */
#define SEARCH_AUTO	2	/* Automatically determine whether to search
				 * for data points or traces.  Look for traces
				 * if the linewidth is > 0 and if there is
				 * more than one data point. */

#define	LABEL_ACTIVE 	(1<<9)	/* Non-zero indicates that the element's entry
				 * in the legend should be drawn in its active
				 * foreground and background colors. */
#define SCALE_SYMBOL	(1<<10)

#define NUMBEROFPOINTS(e)	MIN((e)->x.nValues, (e)->y.nValues)

#define NORMALPEN(e)		((((e)->normalPenPtr == NULL) ?	\
				  (e)->builtinPenPtr :		\
				  (e)->normalPenPtr))

typedef struct {
  double min, max, range;
} Weight;

#define SetRange(l)							\
  ((l).range = ((l).max > (l).min) ? ((l).max - (l).min) : DBL_EPSILON)
#define SetScale(l)				\
  ((l).scale = 1.0 / (l).range)
#define SetWeight(l, lo, hi)			\
  ((l).min = (lo), (l).max = (hi), SetRange(l))

typedef struct {
  Segment2d* segments;	/* Point to start of this pen's X-error bar
			 * segments in the element's array. */
  int nSegments;
} ErrorBarSegments;

struct _Pen {
  const char *name;			/* Pen style identifier.  If NULL pen
					 * was statically allocated. */
  ClassId classId;			/* Type element using this pen. */
  const char *typeId;			/* String token identifying the type of
					 * pen. */
  unsigned int flags;			/* Indicates if the pen element is
					 * active or normal. */
  int refCount;			/* Reference count for elements using
				 * this pen. */
  Tcl_HashEntry *hashPtr;
  Tk_OptionTable optionTable;	/* Configuration specifications */
  PenConfigureProc *configProc;
  PenDestroyProc *destroyProc;
  Graph *graphPtr;			/* Graph that the pen is associated*/
};

/* 
 * An element has one or more vectors plus several attributes, such as line
 * style, thickness, color, and symbol type.  It has an identifier which
 * distinguishes it among the list of all elements.
 */
typedef struct {
  Weight weight;		/* Weight range where this pen is valid. */
  Pen* penPtr;		/* Pen to use. */
} PenStyle;

typedef struct {
  XColor* color;		/* Color of error bar */
  int lineWidth;		/* Width of the error bar segments. */
  GC gc;
  int show;			/* Flags for errorbars: none, x, y, or both */
} ErrorBarAttributes;

typedef void (ElementDrawProc) (Graph *graphPtr, Drawable drawable, 
				Element* elemPtr);

typedef void (ElementToPostScriptProc) (Graph *graphPtr, Blt_Ps ps, 
					Element* elemPtr);

typedef void (ElementDestroyProc) (Graph *graphPtr, Element* elemPtr);

typedef int (ElementConfigProc) (Graph *graphPtr, Element* elemPtr);

typedef void (ElementMapProc) (Graph *graphPtr, Element* elemPtr);

typedef void (ElementExtentsProc) (Element* elemPtr, Region2d *extsPtr);

typedef void (ElementClosestProc) (Graph *graphPtr, Element* elemPtr);

typedef void (ElementDrawSymbolProc) (Graph *graphPtr, Drawable drawable, 
				      Element* elemPtr, int x, int y, int symbolSize);

typedef void (ElementSymbolToPostScriptProc) (Graph *graphPtr, 
					      Blt_Ps ps, Element* elemPtr, double x, double y, int symSize);

typedef struct {
  ElementClosestProc *closestProc;
  ElementConfigProc *configProc;
  ElementDestroyProc *destroyProc;
  ElementDrawProc *drawActiveProc;
  ElementDrawProc *drawNormalProc;
  ElementDrawSymbolProc *drawSymbolProc;
  ElementExtentsProc *extentsProc;
  ElementToPostScriptProc *printActiveProc;
  ElementToPostScriptProc *printNormalProc;
  ElementSymbolToPostScriptProc *printSymbolProc;
  ElementMapProc *mapProc;
} ElementProcs;

typedef struct {
  Blt_VectorId vector;
} VectorDataSource;

/* 
 * The data structure below contains information pertaining to a line vector.
 * It consists of an array of floating point data values and for convenience,
 * the number and minimum/maximum values.
 */
typedef struct {
  int type;			/* Selects the type of data populating this
				 * vector: ELEM_SOURCE_VECTOR,
				 * ELEM_SOURCE_TABLE, or ELEM_SOURCE_VALUES
				 */
  Element* elemPtr;		/* Element associated with vector. */
  VectorDataSource vectorSource;
  double *values;
  int nValues;
  int arraySize;
  double min, max;
} ElemValues;

struct _Element {
  GraphObj obj;			/* Must be first field in element. */
  unsigned int flags;		
  int hide;
  Tcl_HashEntry *hashPtr;

  /* Fields specific to elements. */
  const char *label;			/* Label displayed in legend */
  unsigned short row, col;		/* Position of the entry in the
					 * legend. */
  int legendRelief;			/* Relief of label in legend. */
  Axis2d axes;			/* X-axis and Y-axis mapping the
				 * element */
  ElemValues x, y, w;			/* Contains array of floating point
					 * graph coordinate values. Also holds
					 * min/max and the number of
					 * coordinates */
  int *activeIndices;			/* Array of indices (malloc-ed) which
					 * indicate which data points are
					 * active (drawn with "active"
					 * colors). */
  int nActiveIndices;			/* Number of active data points.
					 * Special case: if nActiveIndices < 0
					 * and the active bit is set in
					 * "flags", then all data points are
					 * drawn active. */
  ElementProcs *procsPtr;
  Tk_OptionTable optionTable;	/* Configuration specifications. */
  Pen *activePenPtr;			/* Standard Pens */
  Pen *normalPenPtr;
  Pen *builtinPenPtr;
  Blt_Chain stylePalette;		/* Palette of pens. */

  /* Symbol scaling */
  int scaleSymbols;			/* If non-zero, the symbols will scale
					 * in size as the graph is zoomed
					 * in/out.  */
  double xRange, yRange;		/* Initial X-axis and Y-axis ranges:
					 * used to scale the size of element's
					 * symbol. */
  int state;
  Blt_ChainLink link;			/* Element's link in display list. */
};

extern char* fillObjOption[];
extern Tk_CustomOptionSetProc StyleSetProc;
extern Tk_CustomOptionGetProc StyleGetProc;

extern double Blt_FindElemValuesMinimum(ElemValues *vecPtr, double minLimit);
extern void Blt_ResizeStatusArray(Element* elemPtr, int nPoints);
extern int Blt_GetPenStyle(Graph *graphPtr, char *name, size_t classId, 
			   PenStyle *stylePtr);
extern void Blt_FreeStylePalette (Blt_Chain stylePalette);
extern PenStyle **Blt_StyleMap (Element* elemPtr);
extern void Blt_MapErrorBars(Graph *graphPtr, Element* elemPtr, 
			     PenStyle **dataToStyle);
extern void Blt_FreeDataValues(ElemValues *evPtr);
extern int Blt_GetElement(Tcl_Interp* interp, Graph *graphPtr, 
			  Tcl_Obj *objPtr, Element **elemPtrPtr);

#endif /* _BLT_GR_ELEM_H */
