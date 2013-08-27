
/*
 * bltPs.h --
 *
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

#ifndef _BLT_PS_H
#define _BLT_PS_H

#include "bltPicture.h"

/*
 * PageSetup --
 *
 * 	Structure contains information specific to the layout of the page for
 * 	printing the graph.
 *
 */
typedef struct  {
    /* User configurable fields */

    int reqWidth, reqHeight;	/* If greater than zero, represents the
				 * requested dimensions of the printed graph */
    int reqPaperWidth;
    int reqPaperHeight;		/* Requested dimensions for the PostScript
				 * page. Can constrain the size of the graph
				 * if the graph (plus padding) is larger than
				 * the size of the page. */
    Blt_Pad xPad, yPad;		/* Requested padding on the exterior of the
				 * graph. This forms the bounding box for
				 * the page. */
    const char *colorVarName;	/* If non-NULL, is the name of a TCL array
				 * variable containing X to output device color
				 * translations */
    const char *fontVarName;	/* If non-NULL, is the name of a TCL array
				 * variable containing X to output device font
				 * translations */
    int level;			/* PostScript Language level 1-3 */
    unsigned int flags;

    const char **comments;	/* User supplied comments to be added. */

    /* Computed fields */

    short int left, bottom;	/* Bounding box of the plot in the page. */
    short int right, top;

    float scale;		/* Scale of page. Set if "-maxpect" option
				 * is set, otherwise 1.0. */

    int paperHeight;
    int paperWidth;
    
} PageSetup;

#define PS_GREYSCALE	(1<<0)
#define PS_LANDSCAPE	(1<<2)
#define PS_CENTER	(1<<3)
#define PS_MAXPECT	(1<<4)
#define PS_DECORATIONS	(1<<5)
#define PS_FOOTER	(1<<6)
#define PS_FMT_NONE	0
#define PS_FMT_MASK	(PS_FMT_WMF|PS_FMT_EPSI|PS_FMT_TIFF)
#define PS_FMT_WMF	(1<<8)
#define PS_FMT_EPSI	(1<<9)
#define PS_FMT_TIFF	(1<<10)

typedef struct _Blt_Ps *Blt_Ps;

BLT_EXTERN Blt_Ps Blt_Ps_Create(Tcl_Interp *interp, PageSetup *setupPtr);

BLT_EXTERN void Blt_Ps_Free(Blt_Ps ps);

BLT_EXTERN const char *Blt_Ps_GetValue(Blt_Ps ps, int *lengthPtr);

BLT_EXTERN Tcl_Interp *Blt_Ps_GetInterp(Blt_Ps ps);

BLT_EXTERN Tcl_DString *Blt_Ps_GetDString(Blt_Ps ps);

BLT_EXTERN char *Blt_Ps_GetScratchBuffer(Blt_Ps ps);

BLT_EXTERN void Blt_Ps_SetInterp(Blt_Ps ps, Tcl_Interp *interp);

BLT_EXTERN void Blt_Ps_Append(Blt_Ps ps, const char *string);

BLT_EXTERN void Blt_Ps_AppendBytes(Blt_Ps ps, const char *string, int nBytes);

BLT_EXTERN void Blt_Ps_VarAppend TCL_VARARGS(Blt_Ps, ps);

BLT_EXTERN void Blt_Ps_Format TCL_VARARGS(Blt_Ps, ps);

BLT_EXTERN void Blt_Ps_SetClearBackground(Blt_Ps ps);

BLT_EXTERN int Blt_Ps_IncludeFile(Tcl_Interp *interp, Blt_Ps ps, 
	const char *fileName);

BLT_EXTERN int Blt_Ps_GetPicaFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, 
	int *picaPtr);

BLT_EXTERN int Blt_Ps_GetPadFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, 
	Blt_Pad *padPtr);

BLT_EXTERN int Blt_Ps_ComputeBoundingBox(PageSetup *setupPtr, int w, int h);

BLT_EXTERN void Blt_Ps_DrawPicture(Blt_Ps ps, Blt_Picture picture, 
	double x, double y);

BLT_EXTERN void Blt_Ps_Rectangle(Blt_Ps ps, int x, int y, int w, int h);


BLT_EXTERN int Blt_Ps_SaveFile(Tcl_Interp *interp, Blt_Ps ps, 
	const char *fileName);

#ifdef _TK

#include "bltFont.h"
#include "bltText.h"

BLT_EXTERN void Blt_Ps_XSetLineWidth(Blt_Ps ps, int lineWidth);

BLT_EXTERN void Blt_Ps_XSetBackground(Blt_Ps ps, XColor *colorPtr);

BLT_EXTERN void Blt_Ps_XSetBitmapData(Blt_Ps ps, Display *display, 
	Pixmap bitmap, int width, int height);

BLT_EXTERN void Blt_Ps_XSetForeground(Blt_Ps ps, XColor *colorPtr);

BLT_EXTERN void Blt_Ps_XSetFont(Blt_Ps ps, Blt_Font font);

BLT_EXTERN void Blt_Ps_XSetDashes(Blt_Ps ps, Blt_Dashes *dashesPtr);

BLT_EXTERN void Blt_Ps_XSetLineAttributes(Blt_Ps ps, XColor *colorPtr,
	int lineWidth, Blt_Dashes *dashesPtr, int capStyle, int joinStyle);

BLT_EXTERN void Blt_Ps_XSetStipple(Blt_Ps ps, Display *display, Pixmap bitmap);

BLT_EXTERN void Blt_Ps_Polyline(Blt_Ps ps, Point2d *screenPts, int nScreenPts);

BLT_EXTERN void Blt_Ps_XDrawLines(Blt_Ps ps, XPoint *points, int n);

BLT_EXTERN void Blt_Ps_XDrawSegments(Blt_Ps ps, XSegment *segments, 
	int nSegments);

BLT_EXTERN void Blt_Ps_DrawPolyline(Blt_Ps ps, Point2d *points, int n);

BLT_EXTERN void Blt_Ps_Draw2DSegments(Blt_Ps ps, Segment2d *segments,
	int nSegments);

BLT_EXTERN void Blt_Ps_Draw3DRectangle(Blt_Ps ps, Tk_3DBorder border, 
	double x, double y, int width, int height, int borderWidth, int relief);

BLT_EXTERN void Blt_Ps_Fill3DRectangle(Blt_Ps ps, Tk_3DBorder border, double x,
	 double y, int width, int height, int borderWidth, int relief);

BLT_EXTERN void Blt_Ps_XFillRectangle(Blt_Ps ps, double x, double y, 
	int width, int height);

BLT_EXTERN void Blt_Ps_XFillRectangles(Blt_Ps ps, XRectangle *rects, int n);

BLT_EXTERN void Blt_Ps_XFillPolygon(Blt_Ps ps, Point2d *screenPts, 
	int nScreenPts);

BLT_EXTERN void Blt_Ps_DrawPhoto(Blt_Ps ps, Tk_PhotoHandle photoToken,
	double x, double y);

BLT_EXTERN void Blt_Ps_XDrawWindow(Blt_Ps ps, Tk_Window tkwin, 
	double x, double y);

BLT_EXTERN void Blt_Ps_DrawText(Blt_Ps ps, const char *string, 
	TextStyle *attrPtr, double x, double y);

BLT_EXTERN void Blt_Ps_DrawBitmap(Blt_Ps ps, Display *display, Pixmap bitmap, 
	double scaleX, double scaleY);

BLT_EXTERN void Blt_Ps_XSetCapStyle(Blt_Ps ps, int capStyle);

BLT_EXTERN void Blt_Ps_XSetJoinStyle(Blt_Ps ps, int joinStyle);

BLT_EXTERN void Blt_Ps_PolylineFromXPoints(Blt_Ps ps, XPoint *points, int n);

BLT_EXTERN void Blt_Ps_Polygon(Blt_Ps ps, Point2d *screenPts, int nScreenPts);

BLT_EXTERN void Blt_Ps_SetPrinting(Blt_Ps ps, int value);
BLT_EXTERN int Blt_Ps_IsPrinting(void);

BLT_EXTERN int Blt_Ps_TextWidth(Blt_Font font, const char *string, int nBytes);

BLT_EXTERN int Blt_Ps_GetFontMetrics(Blt_Font font, Blt_FontMetrics *fmPtr);

BLT_EXTERN void Blt_Ps_FontName(const char *family, int flags, 
	Tcl_DString *resultPtr);

#endif /* _TK */

#endif /* BLT_PS_H */
