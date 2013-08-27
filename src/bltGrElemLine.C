
/*
 * bltGrLine.c --
 *
 * This module implements line graph and stripchart elements for the BLT graph
 * widget.
 *
 *	Copyright (c) 1993 George A Howlett.
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

#include "bltGraph.h"
#include "bltChain.h"
#include <X11/Xutil.h>
#include "bltGrElem.h"
#include "tkDisplay.h"
#include "bltBgStyle.h"
#include "bltImage.h"
#include "bltBitmap.h"

#define COLOR_DEFAULT	(XColor *)1
#define PATTERN_SOLID	((Pixmap)1)

#define PEN_INCREASING  1		/* Draw line segments for only those
					 * data points whose abscissas are
					 * monotonically increasing in
					 * order. */
#define PEN_DECREASING  2		/* Lines will be drawn between only
					 * those points whose abscissas are
					 * decreasing in order. */

#define PEN_BOTH_DIRECTIONS	(PEN_INCREASING | PEN_DECREASING)

/* Lines will be drawn between points regardless of the ordering of the
 * abscissas */

#define BROKEN_TRACE(dir,last,next) \
    (((((dir) & PEN_DECREASING) == 0) && ((next) < (last))) || \
     ((((dir) & PEN_INCREASING) == 0) && ((next) > (last))))

#define DRAW_SYMBOL(linePtr) \
	(((linePtr)->symbolCounter % (linePtr)->symbolInterval) == 0)

typedef enum { 
    PEN_SMOOTH_LINEAR,			/* Line segments */
    PEN_SMOOTH_STEP,			/* Step-and-hold */
    PEN_SMOOTH_NATURAL,			/* Natural cubic spline */
    PEN_SMOOTH_QUADRATIC,		/* Quadratic spline */
    PEN_SMOOTH_CATROM,			/* Catrom parametric spline */
    PEN_SMOOTH_LAST			/* Sentinel */
} Smoothing;

typedef struct {
    const char *name;
    Smoothing value;
} SmoothingInfo;

static SmoothingInfo smoothingInfo[] = {
    { "none",		PEN_SMOOTH_LINEAR	},
    { "linear",		PEN_SMOOTH_LINEAR	},
    { "step",		PEN_SMOOTH_STEP		},
    { "natural",	PEN_SMOOTH_NATURAL	},
    { "cubic",		PEN_SMOOTH_NATURAL	},
    { "quadratic",	PEN_SMOOTH_QUADRATIC	},
    { "catrom",		PEN_SMOOTH_CATROM	},
    { (char *)NULL,	PEN_SMOOTH_LAST		}
};


typedef struct {
    Point2d *screenPts;			/* Array of transformed coordinates */
    int nScreenPts;			/* Number of coordinates */
    int *styleMap;			/* Index of pen styles  */
    int *map;				/* Maps segments/traces to data
					 * points */
} MapInfo;

/* Symbol types for line elements */
typedef enum {
    SYMBOL_NONE,
    SYMBOL_SQUARE,
    SYMBOL_CIRCLE,
    SYMBOL_DIAMOND,
    SYMBOL_PLUS,
    SYMBOL_CROSS,
    SYMBOL_SPLUS,
    SYMBOL_SCROSS,
    SYMBOL_TRIANGLE,
    SYMBOL_ARROW,
    SYMBOL_BITMAP,
    SYMBOL_IMAGE
} SymbolType;

typedef struct {
    const char *name;
    unsigned int minChars;
    SymbolType type;
} GraphSymbolType;

static GraphSymbolType graphSymbols[] = {
    { "arrow",	  1, SYMBOL_ARROW,	},
    { "circle",	  2, SYMBOL_CIRCLE,	},
    { "cross",	  2, SYMBOL_CROSS,	}, 
    { "diamond",  1, SYMBOL_DIAMOND,	}, 
    { "none",	  1, SYMBOL_NONE,	}, 
    { "plus",	  1, SYMBOL_PLUS,	}, 
    { "scross",	  2, SYMBOL_SCROSS,	}, 
    { "splus",	  2, SYMBOL_SPLUS,	}, 
    { "square",	  2, SYMBOL_SQUARE,	}, 
    { "triangle", 1, SYMBOL_TRIANGLE,	}, 
    { NULL,       0, 0			}, 
};

typedef struct {
    SymbolType type;			/* Type of symbol to be drawn/printed */

    int size;				/* Requested size of symbol in pixels */

    XColor *outlineColor;		/* Outline color */

    int outlineWidth;			/* Width of the outline */

    GC outlineGC;			/* Outline graphics context */

    XColor *fillColor;			/* Normal fill color */

    GC fillGC;				/* Fill graphics context */

    Tk_Image image;			/* This is used of image symbols.  */

    /* The last two fields are used only for bitmap symbols. */

    Pixmap bitmap;			/* Bitmap to determine
					* foreground/background pixels of the
					* symbol */
    Pixmap mask;			/* Bitmap representing the transparent
					 * pixels of the symbol */
} Symbol;

typedef struct {
    int start;				/* Index into the X-Y coordinate arrays
					 * indicating where trace starts. */
    GraphPoints screenPts;		/* Array of screen coordinates
					 * (malloc-ed) representing the
					 * trace. */
} Trace;

typedef struct {
    const char *name;			/* Pen style identifier.  If NULL pen
					 * was statically allocated. */
    ClassId classId;			/* Type of pen */
    const char *typeId;			/* String token identifying the type of
					 * pen */
    unsigned int flags;			/* Indicates if the pen element is
					 * active or normal */
    int refCount;			/* Reference count for elements using
					 * this pen. */
    Blt_HashEntry *hashPtr;

    Blt_ConfigSpec *configSpecs;	/* Configuration specifications */

    PenConfigureProc *configProc;
    PenDestroyProc *destroyProc;
    Graph *graphPtr;			/* Graph that the pen is associated
					 * with. */

    /* Symbol attributes. */
    Symbol symbol;			/* Element symbol type */

    /* Trace attributes. */
    int traceWidth;			/* Width of the line segments. If
					 * lineWidth is 0, no line will be
					 * drawn, only symbols. */

    Blt_Dashes traceDashes;		/* Dash on-off list value */

    XColor *traceColor;			/* Line segment color */

    XColor *traceOffColor;		/* Line segment dash gap color */

    GC traceGC;				/* Line segment graphics context */
    
    /* Error bar attributes. */
    int errorBarShow;		       /* Describes which error bars to display:
					* none, x, y, or * both. */

    int errorBarLineWidth;		/* Width of the error bar segments. */

    int errorBarCapWidth;		/* Width of the cap on error bars. */

    XColor *errorBarColor;		/* Color of the error bar. */

    GC errorBarGC;			/* Error bar graphics context. */

    /* Show value attributes. */
    int valueShow;			/* Indicates whether to display data
					 * value.  Values are x, y, both, or
					 * none. */
    const char *valueFormat;		/* A printf format string. */

    TextStyle valueStyle;		/* Text attributes (color, font,
					 * rotation, etc.) of the value. */
} LinePen;

typedef struct {
    Weight weight;			/* Weight range where this pen is
					 * valid. */
    LinePen *penPtr;			/* Pen to use. */
    GraphPoints symbolPts;

    GraphSegments lines;		/* Points to start of the line segments
					 * for this pen. */
    GraphSegments xeb, yeb;		/* X and Y axis error bars. */

    int symbolSize;			/* Size of the pen's symbol scaled to
					 * the current graph size. */
    int errorBarCapWidth;		/* Length of the cap ends on each error
					 * bar. */
} LineStyle;

typedef struct {
    GraphObj obj;			/* Must be first field in element. */
    unsigned int flags;		
    Blt_HashEntry *hashPtr;

    /* Fields specific to elements. */
    const char *label;			/* Label displayed in legend */
    unsigned short row, col;		/* Position of the entry in the
					 * legend. */
    int legendRelief;			/* Relief of label in legend. */
    Axis2d axes;			/* X-axis and Y-axis mapping the
					 * element */
    ElemValues x, y, w;			/* Contains array of floating point
					 * graph coordinate values. Also holds
					 * min/max * and the number of
					 * coordinates */
    int *activeIndices;			/* Array of indices (malloc-ed) which
					 * indicate which data points are active
					 * (drawn * with "active" colors). */
    int nActiveIndices;			/* Number of active data points.
					 * Special case: if nActiveIndices < 0
					 * and the * active bit is set in
					 * "flags", then all data points are
					 * drawn active. */
    ElementProcs *procsPtr;
    Blt_ConfigSpec *configSpecs;	/* Configuration specifications. */
    LinePen *activePenPtr;		/* Standard Pens */
    LinePen *normalPenPtr;
    LinePen *builtinPenPtr;
    Blt_Chain styles;			/* Palette of pens. */

    /* Symbol scaling */
    int scaleSymbols;			/* If non-zero, the symbols will scale
					 * in size as the graph is zoomed
					 * in/out.  */

    double xRange, yRange;		/* Initial X-axis and Y-axis ranges:
					 * used to scale the size of element's
					 * symbol. */
    int state;
    Blt_ChainLink link;			/* Element's link in display list. */

    /* The line element specific fields start here. */

    ElemValues xError;			/* Relative/symmetric X error values. */
    ElemValues yError;			/* Relative/symmetric Y error values. */
    ElemValues xHigh, xLow;		/* Absolute/asymmetric X-coordinate
					 * high/low error values. */
    ElemValues yHigh, yLow;		/* Absolute/asymmetric Y-coordinate
					 * high/low error values. */
    LinePen builtinPen;
    int errorBarCapWidth;		/* Length of cap on error bars */

    /* Line smoothing */
    Smoothing reqSmooth;		/* Requested smoothing function to use
					 * for connecting the data points */
    Smoothing smooth;			/* Smoothing function used. */
    float rTolerance;			/* Tolerance to reduce the number of
					 * points displayed. */

    /* Drawing-related data structures. */

    /* Area-under-curve fill attributes. */
    XColor *fillFgColor;
    XColor *fillBgColor;
    GC fillGC;

    Blt_Background fillBg;		/* Background for fill area. */

    Point2d *fillPts;			/* Array of points used to draw polygon
					 * to fill area under the curve */
    int nFillPts;

    /* Symbol points */
    GraphPoints symbolPts;

    /* Active symbol points */
    GraphPoints activePts;
    GraphSegments xeb, yeb;		/* Point to start of this pen's X-error
					 * bar segments in the element's
					 * array. */
    int reqMaxSymbols;
    int symbolInterval;
    int symbolCounter;

    /* X-Y graph-specific fields */

    int penDir;				/* Indicates if a change in the pen
					 * direction should be considered a
					 * retrace (line segment is not
					 * drawn). */
    Blt_Chain traces;			/* List of traces (a trace is a series
					 * of contiguous line segments).  New
					 * traces are generated when either
					 * the next segment changes the pen
					 * direction, or the end point is
					 * clipped by the plotting area. */

    /* Stripchart-specific fields */

    GraphSegments lines;		/* Holds the the line segments of the
					 * element trace. The segments are
					 * grouped by pen style. */
} LineElement;

static Blt_OptionParseProc ObjToSmoothProc;
static Blt_OptionPrintProc SmoothToObjProc;
static Blt_CustomOption smoothOption =
{
    ObjToSmoothProc, SmoothToObjProc, NULL, (ClientData)0
};

static Blt_OptionParseProc ObjToPenDirProc;
static Blt_OptionPrintProc PenDirToObjProc;
static Blt_CustomOption penDirOption =
{
    ObjToPenDirProc, PenDirToObjProc, NULL, (ClientData)0
};

static Blt_OptionFreeProc FreeSymbolProc;
static Blt_OptionParseProc ObjToSymbolProc;
static Blt_OptionPrintProc SymbolToObjProc;
static Blt_CustomOption symbolOption =
{
    ObjToSymbolProc, SymbolToObjProc, FreeSymbolProc, (ClientData)0
};

BLT_EXTERN Blt_CustomOption bltLineStylesOption;
BLT_EXTERN Blt_CustomOption bltColorOption;
BLT_EXTERN Blt_CustomOption bltValuesOption;
BLT_EXTERN Blt_CustomOption bltValuePairsOption;
BLT_EXTERN Blt_CustomOption bltLinePenOption;
BLT_EXTERN Blt_CustomOption bltXAxisOption;
BLT_EXTERN Blt_CustomOption bltYAxisOption;

#define DEF_LINE_ACTIVE_PEN		"activeLine"
#define DEF_LINE_AXIS_X			"x"
#define DEF_LINE_AXIS_Y			"y"
#define DEF_LINE_DASHES			(char *)NULL
#define DEF_LINE_DATA			(char *)NULL
#define DEF_LINE_FILL_COLOR    		"defcolor"
#define DEF_LINE_HIDE			"no"
#define DEF_LINE_LABEL			(char *)NULL
#define DEF_LINE_LABEL_RELIEF		"flat"
#define DEF_LINE_MAX_SYMBOLS		"0"
#define DEF_LINE_OFFDASH_COLOR    	(char *)NULL
#define DEF_LINE_OUTLINE_COLOR		"defcolor"
#define DEF_LINE_OUTLINE_WIDTH 		"1"
#define DEF_LINE_PATTERN_BG		(char *)NULL
#define DEF_LINE_PATTERN_FG		"black"
#define DEF_LINE_PEN_COLOR		RGB_NAVYBLUE
#define DEF_LINE_PEN_DIRECTION		"both"
#define DEF_LINE_PEN_WIDTH		"1"
#define DEF_LINE_PIXELS			"0.1i"
#define DEF_LINE_REDUCE			"0.0"
#define DEF_LINE_SCALE_SYMBOLS		"yes"
#define DEF_LINE_SMOOTH			"linear"
#define DEF_LINE_STATE			"normal"
#define DEF_LINE_STIPPLE		(char *)NULL
#define DEF_LINE_STYLES			""
#define DEF_LINE_SYMBOL			"circle"
#define DEF_LINE_TAGS			"all"
#define DEF_LINE_X_DATA			(char *)NULL
#define DEF_LINE_Y_DATA			(char *)NULL

#define DEF_LINE_ERRORBAR_COLOR		"defcolor"
#define DEF_LINE_ERRORBAR_LINE_WIDTH	"2"
#define DEF_LINE_ERRORBAR_CAP_WIDTH	"2"
#define DEF_LINE_SHOW_ERRORBARS		"both"

#define DEF_PEN_ACTIVE_COLOR		RGB_BLUE
#define DEF_PEN_DASHES			(char *)NULL
#define DEF_PEN_FILL_COLOR    		"defcolor"
#define DEF_PEN_LINE_WIDTH		"1"
#define DEF_PEN_NORMAL_COLOR		RGB_NAVYBLUE
#define DEF_PEN_OFFDASH_COLOR    	(char *)NULL
#define DEF_PEN_OUTLINE_COLOR		"defcolor"
#define DEF_PEN_OUTLINE_WIDTH 		"1"
#define DEF_PEN_PIXELS			"0.1i"
#define DEF_PEN_SYMBOL			"circle"
#define DEF_PEN_TYPE			"line"
#define	DEF_PEN_VALUE_ANCHOR		"s"
#define	DEF_PEN_VALUE_COLOR		RGB_BLACK
#define	DEF_PEN_VALUE_FONT		STD_FONT_NUMBERS
#define	DEF_PEN_VALUE_FORMAT		"%g"
#define	DEF_PEN_VALUE_ANGLE		(char *)NULL
#define DEF_PEN_SHOW_VALUES		"no"

static Blt_ConfigSpec lineElemConfigSpecs[] =
{
    {BLT_CONFIG_CUSTOM, "-activepen", "activePen", "ActivePen",
	DEF_LINE_ACTIVE_PEN, Blt_Offset(LineElement, activePenPtr),
	BLT_CONFIG_NULL_OK, &bltLinePenOption},
    {BLT_CONFIG_COLOR, "-areaforeground", "areaForeground", "AreaForeground",
	DEF_LINE_PATTERN_FG, Blt_Offset(LineElement, fillFgColor), 
	BLT_CONFIG_NULL_OK},
    {BLT_CONFIG_BACKGROUND, "-areabackground", "areaBackground", 
	"AreaBackground", DEF_LINE_PATTERN_BG, Blt_Offset(LineElement, fillBg),
	 BLT_CONFIG_NULL_OK},
    {BLT_CONFIG_LIST, "-bindtags", "bindTags", "BindTags", DEF_LINE_TAGS, 
	Blt_Offset(LineElement, obj.tags), BLT_CONFIG_NULL_OK},
    {BLT_CONFIG_COLOR, "-color", "color", "Color", DEF_LINE_PEN_COLOR, 
	Blt_Offset(LineElement, builtinPen.traceColor), 0},
    {BLT_CONFIG_DASHES, "-dashes", "dashes", "Dashes", DEF_LINE_DASHES, 
	Blt_Offset(LineElement, builtinPen.traceDashes), BLT_CONFIG_NULL_OK},
    {BLT_CONFIG_CUSTOM, "-data", "data", "Data", DEF_LINE_DATA, 0, 0, 
	&bltValuePairsOption},
    {BLT_CONFIG_CUSTOM, "-errorbarcolor", "errorBarColor", "ErrorBarColor",
	DEF_LINE_ERRORBAR_COLOR, 
	Blt_Offset(LineElement, builtinPen.errorBarColor), 0, &bltColorOption},
    {BLT_CONFIG_PIXELS_NNEG,"-errorbarwidth", "errorBarWidth", "ErrorBarWidth",
	DEF_LINE_ERRORBAR_LINE_WIDTH, 
	Blt_Offset(LineElement, builtinPen.errorBarLineWidth),
        BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_PIXELS_NNEG, "-errorbarcap", "errorBarCap", "ErrorBarCap", 
	DEF_LINE_ERRORBAR_CAP_WIDTH, 
	Blt_Offset(LineElement, builtinPen.errorBarCapWidth),
        BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_CUSTOM, "-fill", "fill", "Fill", DEF_LINE_FILL_COLOR, 
	Blt_Offset(LineElement, builtinPen.symbol.fillColor), 
	BLT_CONFIG_NULL_OK, &bltColorOption},
    {BLT_CONFIG_BITMASK, "-hide", "hide", "Hide", DEF_LINE_HIDE, 
        Blt_Offset(LineElement, flags), BLT_CONFIG_DONT_SET_DEFAULT,
        (Blt_CustomOption *)HIDE},
    {BLT_CONFIG_STRING, "-label", "label", "Label", (char *)NULL, 
	Blt_Offset(LineElement, label), BLT_CONFIG_NULL_OK},
    {BLT_CONFIG_RELIEF, "-legendrelief", "legendRelief", "LegendRelief",
	DEF_LINE_LABEL_RELIEF, Blt_Offset(LineElement, legendRelief),
	BLT_CONFIG_DONT_SET_DEFAULT}, 
    {BLT_CONFIG_PIXELS_NNEG, "-linewidth", "lineWidth", "LineWidth",
	DEF_LINE_PEN_WIDTH, Blt_Offset(LineElement, builtinPen.traceWidth),
        BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_CUSTOM, "-mapx", "mapX", "MapX",
        DEF_LINE_AXIS_X, Blt_Offset(LineElement, axes.x), 0, &bltXAxisOption},
    {BLT_CONFIG_CUSTOM, "-mapy", "mapY", "MapY",
	DEF_LINE_AXIS_Y, Blt_Offset(LineElement, axes.y), 0, &bltYAxisOption},
    {BLT_CONFIG_INT_NNEG, "-maxsymbols", "maxSymbols", "MaxSymbols",
	DEF_LINE_MAX_SYMBOLS, Blt_Offset(LineElement, reqMaxSymbols),
	BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_CUSTOM, "-offdash", "offDash", "OffDash", 
	DEF_LINE_OFFDASH_COLOR, 
	Blt_Offset(LineElement, builtinPen.traceOffColor),
	BLT_CONFIG_NULL_OK, &bltColorOption},
    {BLT_CONFIG_CUSTOM, "-outline", "outline", "Outline", 
	DEF_LINE_OUTLINE_COLOR, 
	Blt_Offset(LineElement, builtinPen.symbol.outlineColor), 
	0, &bltColorOption},
    {BLT_CONFIG_PIXELS_NNEG, "-outlinewidth", "outlineWidth", "OutlineWidth",
	DEF_LINE_OUTLINE_WIDTH, 
	Blt_Offset(LineElement, builtinPen.symbol.outlineWidth),
	BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_CUSTOM, "-pen", "pen", "Pen", (char *)NULL, 
	Blt_Offset(LineElement, normalPenPtr), BLT_CONFIG_NULL_OK, 
	&bltLinePenOption},
    {BLT_CONFIG_PIXELS_NNEG, "-pixels", "pixels", "Pixels", DEF_LINE_PIXELS, 
	Blt_Offset(LineElement, builtinPen.symbol.size), GRAPH | STRIPCHART}, 
    {BLT_CONFIG_FLOAT, "-reduce", "reduce", "Reduce",
	DEF_LINE_REDUCE, Blt_Offset(LineElement, rTolerance),
	GRAPH | STRIPCHART | BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_BOOLEAN, "-scalesymbols", "scaleSymbols", "ScaleSymbols",
	DEF_LINE_SCALE_SYMBOLS, Blt_Offset(LineElement, scaleSymbols),
	BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_FILL, "-showerrorbars", "showErrorBars", "ShowErrorBars",
	DEF_LINE_SHOW_ERRORBARS, 
	Blt_Offset(LineElement, builtinPen.errorBarShow), 
	BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_FILL, "-showvalues", "showValues", "ShowValues",
	DEF_PEN_SHOW_VALUES, Blt_Offset(LineElement, builtinPen.valueShow),
	BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_CUSTOM, "-smooth", "smooth", "Smooth", DEF_LINE_SMOOTH, 
	Blt_Offset(LineElement, reqSmooth), BLT_CONFIG_DONT_SET_DEFAULT, 
	&smoothOption},
    {BLT_CONFIG_STATE, "-state", "state", "State", DEF_LINE_STATE, 
	Blt_Offset(LineElement, state), BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_CUSTOM, "-styles", "styles", "Styles", DEF_LINE_STYLES, 
	Blt_Offset(LineElement, styles), 0, &bltLineStylesOption},
    {BLT_CONFIG_CUSTOM, "-symbol", "symbol", "Symbol", DEF_LINE_SYMBOL, 
	Blt_Offset(LineElement, builtinPen.symbol), 
	BLT_CONFIG_DONT_SET_DEFAULT, &symbolOption},
    {BLT_CONFIG_CUSTOM, "-trace", "trace", "Trace", DEF_LINE_PEN_DIRECTION, 
	Blt_Offset(LineElement, penDir), 
	BLT_CONFIG_DONT_SET_DEFAULT, &penDirOption},
    {BLT_CONFIG_ANCHOR, "-valueanchor", "valueAnchor", "ValueAnchor",
	DEF_PEN_VALUE_ANCHOR, 
	Blt_Offset(LineElement, builtinPen.valueStyle.anchor), 0},
    {BLT_CONFIG_COLOR, "-valuecolor", "valueColor", "ValueColor",
	DEF_PEN_VALUE_COLOR, 
	Blt_Offset(LineElement, builtinPen.valueStyle.color), 0},
    {BLT_CONFIG_FONT, "-valuefont", "valueFont", "ValueFont",
	DEF_PEN_VALUE_FONT, 
	Blt_Offset(LineElement, builtinPen.valueStyle.font), 0},
    {BLT_CONFIG_STRING, "-valueformat", "valueFormat", "ValueFormat",
	DEF_PEN_VALUE_FORMAT, Blt_Offset(LineElement, builtinPen.valueFormat),
	BLT_CONFIG_NULL_OK},
    {BLT_CONFIG_FLOAT, "-valuerotate", "valueRotate", "ValueRotate",
	DEF_PEN_VALUE_ANGLE, 
	Blt_Offset(LineElement, builtinPen.valueStyle.angle), 0},
    {BLT_CONFIG_CUSTOM, "-weights", "weights", "Weights", (char *)NULL, 
	Blt_Offset(LineElement, w), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-x", "xData", "XData", (char *)NULL, 
        Blt_Offset(LineElement, x), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-xdata", "xData", "XData", (char *)NULL, 
	Blt_Offset(LineElement, x), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-xerror", "xError", "XError", (char *)NULL, 
	Blt_Offset(LineElement, xError), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-xhigh", "xHigh", "XHigh", (char *)NULL, 
	Blt_Offset(LineElement, xHigh), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-xlow", "xLow", "XLow", (char *)NULL, 
	Blt_Offset(LineElement, xLow), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-y", "yData", "YData", (char *)NULL, 
	Blt_Offset(LineElement, y), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-ydata", "yData", "YData", (char *)NULL, 
	Blt_Offset(LineElement, y), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-yerror", "yError", "YError", (char *)NULL, 
	Blt_Offset(LineElement, yError), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-yhigh", "yHigh", "YHigh", (char *)NULL, 
	Blt_Offset(LineElement, yHigh), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-ylow", "yLow", "YLow", (char *)NULL, 
	Blt_Offset(LineElement, yLow), 0, &bltValuesOption},
    {BLT_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};


static Blt_ConfigSpec stripElemConfigSpecs[] =
{
    {BLT_CONFIG_CUSTOM, "-activepen", "activePen", "ActivePen",
	DEF_LINE_ACTIVE_PEN, Blt_Offset(LineElement, activePenPtr), 
	BLT_CONFIG_NULL_OK, &bltLinePenOption},
    {BLT_CONFIG_COLOR, "-areaforeground", "areaForeground", "areaForeground",
	DEF_LINE_PATTERN_FG, Blt_Offset(LineElement, fillFgColor), 
	BLT_CONFIG_NULL_OK},
    {BLT_CONFIG_BACKGROUND, "-areabackground", "areaBackground", 
	"areaBackground", DEF_LINE_PATTERN_BG, Blt_Offset(LineElement, fillBg), 
	BLT_CONFIG_NULL_OK},
    {BLT_CONFIG_LIST, "-bindtags", "bindTags", "BindTags", DEF_LINE_TAGS, 
	Blt_Offset(LineElement, obj.tags), BLT_CONFIG_NULL_OK},
    {BLT_CONFIG_COLOR, "-color", "color", "Color",
	DEF_LINE_PEN_COLOR, Blt_Offset(LineElement, builtinPen.traceColor), 0},
    {BLT_CONFIG_DASHES, "-dashes", "dashes", "Dashes",
	DEF_LINE_DASHES, Blt_Offset(LineElement, builtinPen.traceDashes),
	BLT_CONFIG_NULL_OK},
    {BLT_CONFIG_CUSTOM, "-data", "data", "Data", DEF_LINE_DATA, 0, 0, 
	&bltValuePairsOption},
    {BLT_CONFIG_CUSTOM, "-errorbarcolor", "errorBarColor", "ErrorBarColor",
	DEF_LINE_ERRORBAR_COLOR, 
	Blt_Offset(LineElement, builtinPen.errorBarColor), 0, &bltColorOption},
    {BLT_CONFIG_PIXELS_NNEG, "-errorbarwidth", "errorBarWidth", "ErrorBarWidth",
	DEF_LINE_ERRORBAR_LINE_WIDTH, 
	Blt_Offset(LineElement, builtinPen.errorBarLineWidth),
        BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_PIXELS_NNEG, "-errorbarcap", "errorBarCap", "ErrorBarCap", 
        DEF_LINE_ERRORBAR_CAP_WIDTH, 
	Blt_Offset(LineElement, builtinPen.errorBarCapWidth), 
        BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_CUSTOM, "-fill", "fill", "Fill", DEF_LINE_FILL_COLOR, 
	Blt_Offset(LineElement, builtinPen.symbol.fillColor),
	BLT_CONFIG_NULL_OK, &bltColorOption},
    {BLT_CONFIG_BITMASK, "-hide", "hide", "Hide", DEF_LINE_HIDE, 
	Blt_Offset(LineElement, flags), BLT_CONFIG_DONT_SET_DEFAULT,
        (Blt_CustomOption *)HIDE},
    {BLT_CONFIG_STRING, "-label", "label", "Label", (char *)NULL, 
	Blt_Offset(LineElement, label), BLT_CONFIG_NULL_OK},
    {BLT_CONFIG_RELIEF, "-legendrelief", "legendRelief", "LegendRelief",
	DEF_LINE_LABEL_RELIEF, Blt_Offset(LineElement, legendRelief),
	BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_PIXELS_NNEG, "-linewidth", "lineWidth", "LineWidth", 
	DEF_LINE_PEN_WIDTH, Blt_Offset(LineElement, builtinPen.traceWidth), 
        BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_CUSTOM, "-mapx", "mapX", "MapX", DEF_LINE_AXIS_X, 
	Blt_Offset(LineElement, axes.x), 0, &bltXAxisOption},
    {BLT_CONFIG_CUSTOM, "-mapy", "mapY", "MapY", DEF_LINE_AXIS_Y, 
	Blt_Offset(LineElement, axes.y), 0, &bltYAxisOption},
    {BLT_CONFIG_INT_NNEG, "-maxsymbols", "maxSymbols", "MaxSymbols",
	DEF_LINE_MAX_SYMBOLS, Blt_Offset(LineElement, reqMaxSymbols),
	BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_CUSTOM, "-offdash", "offDash", "OffDash",
	DEF_LINE_OFFDASH_COLOR, Blt_Offset(LineElement, builtinPen.traceOffColor),
	BLT_CONFIG_NULL_OK, &bltColorOption},
    {BLT_CONFIG_CUSTOM, "-outline", "outline", "Outline",
	DEF_LINE_OUTLINE_COLOR, 
	Blt_Offset(LineElement, builtinPen.symbol.outlineColor), 0, 
	&bltColorOption},
    {BLT_CONFIG_PIXELS_NNEG, "-outlinewidth", "outlineWidth", "OutlineWidth",
	DEF_LINE_OUTLINE_WIDTH, 
	Blt_Offset(LineElement, builtinPen.symbol.outlineWidth),
	BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_CUSTOM, "-pen", "pen", "Pen", (char *)NULL, 
	Blt_Offset(LineElement, normalPenPtr), BLT_CONFIG_NULL_OK, 
	&bltLinePenOption},
    {BLT_CONFIG_PIXELS_NNEG, "-pixels", "pixels", "Pixels", DEF_LINE_PIXELS, 
	Blt_Offset(LineElement, builtinPen.symbol.size), 0},
    {BLT_CONFIG_BOOLEAN, "-scalesymbols", "scaleSymbols", "ScaleSymbols",
	DEF_LINE_SCALE_SYMBOLS, Blt_Offset(LineElement, scaleSymbols),
	BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_FILL, "-showerrorbars", "showErrorBars", "ShowErrorBars",
	DEF_LINE_SHOW_ERRORBARS, 
	Blt_Offset(LineElement, builtinPen.errorBarShow),
	BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_FILL, "-showvalues", "showValues", "ShowValues",
	DEF_PEN_SHOW_VALUES, Blt_Offset(LineElement, builtinPen.valueShow),
	BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_CUSTOM, "-smooth", "smooth", "Smooth", DEF_LINE_SMOOTH, 
        Blt_Offset(LineElement, reqSmooth), BLT_CONFIG_DONT_SET_DEFAULT, 
	&smoothOption},
    {BLT_CONFIG_CUSTOM, "-styles", "styles", "Styles", DEF_LINE_STYLES, 
	Blt_Offset(LineElement, styles), 0, &bltLineStylesOption},
    {BLT_CONFIG_CUSTOM, "-symbol", "symbol", "Symbol", DEF_LINE_SYMBOL, 
	Blt_Offset(LineElement, builtinPen.symbol), 
	BLT_CONFIG_DONT_SET_DEFAULT, &symbolOption},
    {BLT_CONFIG_ANCHOR, "-valueanchor", "valueAnchor", "ValueAnchor",
	DEF_PEN_VALUE_ANCHOR, 
        Blt_Offset(LineElement, builtinPen.valueStyle.anchor), 0},
    {BLT_CONFIG_COLOR, "-valuecolor", "valueColor", "ValueColor",
	DEF_PEN_VALUE_COLOR, 
	Blt_Offset(LineElement, builtinPen.valueStyle.color), 0},
    {BLT_CONFIG_FONT, "-valuefont", "valueFont", "ValueFont",
	DEF_PEN_VALUE_FONT, 
	Blt_Offset(LineElement, builtinPen.valueStyle.font), 0},
    {BLT_CONFIG_STRING, "-valueformat", "valueFormat", "ValueFormat",
	DEF_PEN_VALUE_FORMAT, Blt_Offset(LineElement, builtinPen.valueFormat),
	BLT_CONFIG_NULL_OK},
    {BLT_CONFIG_FLOAT, "-valuerotate", "valueRotate", "ValueRotate",
	DEF_PEN_VALUE_ANGLE, 
	Blt_Offset(LineElement, builtinPen.valueStyle.angle),0},
    {BLT_CONFIG_CUSTOM, "-weights", "weights", "Weights", (char *)NULL, 
	Blt_Offset(LineElement, w), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-x", "xData", "XData", (char *)NULL, 
	Blt_Offset(LineElement, x), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-xdata", "xData", "XData", (char *)NULL, 
	Blt_Offset(LineElement, x), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-y", "yData", "YData", (char *)NULL, 
	Blt_Offset(LineElement, y), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-xerror", "xError", "XError", (char *)NULL, 
	Blt_Offset(LineElement, xError), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-ydata", "yData", "YData", (char *)NULL, 
	Blt_Offset(LineElement, y), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-yerror", "yError", "YError", (char *)NULL, 
	Blt_Offset(LineElement, yError), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-xhigh", "xHigh", "XHigh", (char *)NULL, 
	Blt_Offset(LineElement, xHigh), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-xlow", "xLow", "XLow", (char *)NULL, 
	Blt_Offset(LineElement, xLow), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-yhigh", "yHigh", "YHigh", (char *)NULL, 
	Blt_Offset(LineElement, xHigh), 0, &bltValuesOption},
    {BLT_CONFIG_CUSTOM, "-ylow", "yLow", "YLow", (char *)NULL, 
	Blt_Offset(LineElement, yLow), 0, &bltValuesOption},
    {BLT_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

static Blt_ConfigSpec linePenConfigSpecs[] =
{
    {BLT_CONFIG_COLOR, "-color", "color", "Color", DEF_PEN_ACTIVE_COLOR, 
	Blt_Offset(LinePen, traceColor), ACTIVE_PEN},
    {BLT_CONFIG_COLOR, "-color", "color", "Color", DEF_PEN_NORMAL_COLOR, 
	Blt_Offset(LinePen, traceColor), NORMAL_PEN},
    {BLT_CONFIG_DASHES, "-dashes", "dashes", "Dashes", DEF_PEN_DASHES, 
	Blt_Offset(LinePen, traceDashes), BLT_CONFIG_NULL_OK | ALL_PENS},
    {BLT_CONFIG_CUSTOM, "-errorbarcolor", "errorBarColor", "ErrorBarColor",
	DEF_LINE_ERRORBAR_COLOR, Blt_Offset(LinePen, errorBarColor), 
	ALL_PENS, &bltColorOption},
    {BLT_CONFIG_PIXELS_NNEG, "-errorbarwidth", "errorBarWidth", "ErrorBarWidth",
	DEF_LINE_ERRORBAR_LINE_WIDTH, Blt_Offset(LinePen, errorBarLineWidth),
        ALL_PENS | BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_PIXELS_NNEG, "-errorbarcap", "errorBarCap", "ErrorBarCap", 
	DEF_LINE_ERRORBAR_CAP_WIDTH, Blt_Offset(LinePen, errorBarCapWidth),
        BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_CUSTOM, "-fill", "fill", "Fill", DEF_PEN_FILL_COLOR, 
	Blt_Offset(LinePen, symbol.fillColor), BLT_CONFIG_NULL_OK | ALL_PENS, 
	&bltColorOption},
    {BLT_CONFIG_PIXELS_NNEG, "-linewidth", "lineWidth", "LineWidth",
        (char *)NULL, Blt_Offset(LinePen, traceWidth), 
	ALL_PENS| BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_CUSTOM, "-offdash", "offDash", "OffDash", DEF_PEN_OFFDASH_COLOR,
	Blt_Offset(LinePen, traceOffColor), BLT_CONFIG_NULL_OK | ALL_PENS, 
	&bltColorOption},
    {BLT_CONFIG_CUSTOM, "-outline", "outline", "Outline", DEF_PEN_OUTLINE_COLOR,
	Blt_Offset(LinePen, symbol.outlineColor), ALL_PENS, &bltColorOption},
    {BLT_CONFIG_PIXELS_NNEG, "-outlinewidth", "outlineWidth", "OutlineWidth",
	DEF_PEN_OUTLINE_WIDTH, Blt_Offset(LinePen, symbol.outlineWidth),
	BLT_CONFIG_DONT_SET_DEFAULT | ALL_PENS},
    {BLT_CONFIG_PIXELS_NNEG, "-pixels", "pixels", "Pixels", DEF_PEN_PIXELS, 
	Blt_Offset(LinePen, symbol.size), ALL_PENS},
    {BLT_CONFIG_FILL, "-showerrorbars", "showErrorBars", "ShowErrorBars",
	DEF_LINE_SHOW_ERRORBARS, Blt_Offset(LinePen, errorBarShow),
	BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_FILL, "-showvalues", "showValues", "ShowValues",
	DEF_PEN_SHOW_VALUES, Blt_Offset(LinePen, valueShow),
	ALL_PENS | BLT_CONFIG_DONT_SET_DEFAULT},
    {BLT_CONFIG_CUSTOM, "-symbol", "symbol", "Symbol", DEF_PEN_SYMBOL, 
	Blt_Offset(LinePen, symbol), BLT_CONFIG_DONT_SET_DEFAULT | ALL_PENS, 
	&symbolOption},
    {BLT_CONFIG_STRING, "-type", (char *)NULL, (char *)NULL, DEF_PEN_TYPE, 
	Blt_Offset(Pen, typeId), ALL_PENS | BLT_CONFIG_NULL_OK},
    {BLT_CONFIG_ANCHOR, "-valueanchor", "valueAnchor", "ValueAnchor",
	DEF_PEN_VALUE_ANCHOR, Blt_Offset(LinePen, valueStyle.anchor), ALL_PENS},
    {BLT_CONFIG_COLOR, "-valuecolor", "valueColor", "ValueColor",
	DEF_PEN_VALUE_COLOR, Blt_Offset(LinePen, valueStyle.color), ALL_PENS},
    {BLT_CONFIG_FONT, "-valuefont", "valueFont", "ValueFont",
	DEF_PEN_VALUE_FONT, Blt_Offset(LinePen, valueStyle.font), ALL_PENS},
    {BLT_CONFIG_STRING, "-valueformat", "valueFormat", "ValueFormat",
	DEF_PEN_VALUE_FORMAT, Blt_Offset(LinePen, valueFormat),
	ALL_PENS | BLT_CONFIG_NULL_OK},
    {BLT_CONFIG_FLOAT, "-valuerotate", "valueRotate", "ValueRotate",
	DEF_PEN_VALUE_ANGLE, Blt_Offset(LinePen, valueStyle.angle), ALL_PENS},
    {BLT_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

typedef double (DistanceProc)(int x, int y, Point2d *p, Point2d *q, Point2d *t);

/* Forward declarations */
static PenConfigureProc ConfigurePenProc;
static PenDestroyProc DestroyPenProc;
static ElementClosestProc ClosestLineProc;
static ElementConfigProc ConfigureLineProc;
static ElementDestroyProc DestroyLineProc;
static ElementDrawProc DrawActiveLineProc;
static ElementDrawProc DrawNormalLineProc;
static ElementDrawSymbolProc DrawSymbolProc;
static ElementExtentsProc GetLineExtentsProc;
static ElementToPostScriptProc ActiveLineToPostScriptProc;
static ElementToPostScriptProc NormalLineToPostScriptProc;
static ElementSymbolToPostScriptProc SymbolToPostScriptProc;
static ElementMapProc MapLineProc;
static DistanceProc DistanceToYProc;
static DistanceProc DistanceToXProc;
static DistanceProc DistanceToLineProc;
static Blt_BackgroundChangedProc BackgroundChangedProc;

#ifdef WIN32

static int tkpWinRopModes[] =
{
    R2_BLACK,		/* GXclear */
    R2_MASKPEN,		/* GXand */
    R2_MASKPENNOT,	/* GXandReverse */
    R2_COPYPEN,		/* GXcopy */
    R2_MASKNOTPEN,	/* GXandInverted */
    R2_NOT,		/* GXnoop */
    R2_XORPEN,		/* GXxor */
    R2_MERGEPEN,	/* GXor */
    R2_NOTMERGEPEN,	/* GXnor */
    R2_NOTXORPEN,	/* GXequiv */
    R2_NOT,		/* GXinvert */
    R2_MERGEPENNOT,	/* GXorReverse */
    R2_NOTCOPYPEN,	/* GXcopyInverted */
    R2_MERGENOTPEN,	/* GXorInverted */
    R2_NOTMASKPEN,	/* GXnand */
    R2_WHITE		/* GXset */
};

#endif

#ifndef notdef
INLINE static int
Round(double x)
{
    return (int) (x + ((x < 0.0) ? -0.5 : 0.5));
}
#else 
#define Round Round
#endif
/*
 *---------------------------------------------------------------------------
 * 	Custom configuration option (parse and print) routines
 *---------------------------------------------------------------------------
 */

static void
DestroySymbol(Display *display, Symbol *symbolPtr)
{
    if (symbolPtr->image != NULL) {
	Tk_FreeImage(symbolPtr->image);
	symbolPtr->image = NULL;
    }
    if (symbolPtr->bitmap != None) {
	Tk_FreeBitmap(display, symbolPtr->bitmap);
	symbolPtr->bitmap = None;
    }
    if (symbolPtr->mask != None) {
	Tk_FreeBitmap(display, symbolPtr->mask);
	symbolPtr->mask = None;
    }
    symbolPtr->type = SYMBOL_NONE;
}

/*
 *---------------------------------------------------------------------------
 *
 * ImageChangedProc
 *
 *
 * Results:
 *	None.
 *
 *---------------------------------------------------------------------------
 */
/* ARGSUSED */
static void
ImageChangedProc(
    ClientData clientData,
    int x, int y, int w, int h,		/* Not used. */
    int imageWidth, int imageHeight)	/* Not used. */
{
    Element *elemPtr;
    Graph *graphPtr;

    elemPtr = clientData;
    elemPtr->flags |= MAP_ITEM;
    graphPtr = elemPtr->obj.graphPtr;
    graphPtr->flags |= CACHE_DIRTY;
    Blt_EventuallyRedrawGraph(graphPtr);
}

/*ARGSUSED*/
static void
FreeSymbolProc(
    ClientData clientData,		/* Not used. */
    Display *display,			/* Not used. */
    char *widgRec,
    int offset)
{
    Symbol *symbolPtr = (Symbol *)(widgRec + offset);

    DestroySymbol(display, symbolPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * ObjToSymbol --
 *
 *	Convert the string representation of a line style or symbol name into
 *	its numeric form.
 *
 * Results:
 *	The return value is a standard TCL result.  The symbol type is written
 *	into the widget record.
 *
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static int
ObjToSymbolProc(
    ClientData clientData,		/* Not used. */
    Tcl_Interp *interp,			/* Interpreter to send results back
					 * to */
    Tk_Window tkwin,			/* Not used. */
    Tcl_Obj *objPtr,			/* String representing symbol type */
    char *widgRec,			/* Element information record */
    int offset,				/* Offset to field in structure */
    int flags)				/* Not used. */
{
    Symbol *symbolPtr = (Symbol *)(widgRec + offset);
    const char *string;

    {
	int length;
	GraphSymbolType *p;
	char c;

	string = Tcl_GetStringFromObj(objPtr, &length);
	if (length == 0) {
	    DestroySymbol(Tk_Display(tkwin), symbolPtr);
	    symbolPtr->type = SYMBOL_NONE;
	    return TCL_OK;
	}
	c = string[0];
	for (p = graphSymbols; p->name != NULL; p++) {
	    if (length < p->minChars) {
		continue;
	    }
	    if ((c == p->name[0]) && (strncmp(string, p->name, length) == 0)) {
		DestroySymbol(Tk_Display(tkwin), symbolPtr);
		symbolPtr->type = p->type;
		return TCL_OK;
	    }
	}
    }
    {
	Tk_Image tkImage;
	Element *elemPtr = (Element *)widgRec;

	tkImage = Tk_GetImage(interp, tkwin, string, ImageChangedProc, elemPtr);
	if (tkImage != NULL) {
	    DestroySymbol(Tk_Display(tkwin), symbolPtr);
	    symbolPtr->image = tkImage;
	    symbolPtr->type = SYMBOL_IMAGE;
	    return TCL_OK;
	}
    }
    {
	Pixmap bitmap, mask;
	Tcl_Obj **objv;
	int objc;

	if ((Tcl_ListObjGetElements(NULL, objPtr, &objc, &objv) != TCL_OK) || 
	    (objc > 2)) {
	    goto error;
	}
	bitmap = mask = None;
	if (objc > 0) {
	    bitmap = Tk_AllocBitmapFromObj((Tcl_Interp *)NULL, tkwin, objv[0]);
	    if (bitmap == None) {
		goto error;
	    }
	}
	if (objc > 1) {
	    mask = Tk_AllocBitmapFromObj((Tcl_Interp *)NULL, tkwin, objv[1]);
	    if (mask == None) {
		goto error;
	    }
	}
	DestroySymbol(Tk_Display(tkwin), symbolPtr);
	symbolPtr->bitmap = bitmap;
	symbolPtr->mask = mask;
	symbolPtr->type = SYMBOL_BITMAP;
	return TCL_OK;
    }
 error:
    Tcl_AppendResult(interp, "bad symbol \"", string, 
	"\": should be \"none\", \"circle\", \"square\", \"diamond\", "
	"\"plus\", \"cross\", \"splus\", \"scross\", \"triangle\", "
	"\"arrow\" or the name of a bitmap", (char *)NULL);
    return TCL_ERROR;
}

/*
 *---------------------------------------------------------------------------
 *
 * SymbolToObj --
 *
 *	Convert the symbol value into a string.
 *
 * Results:
 *	The string representing the symbol type or line style is returned.
 *
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static Tcl_Obj *
SymbolToObjProc(
    ClientData clientData,		/* Not used. */
    Tcl_Interp *interp,
    Tk_Window tkwin,
    char *widgRec,			/* Element information record */
    int offset,				/* Offset to field in structure */
    int flags)				/* Not used. */
{
    Symbol *symbolPtr = (Symbol *)(widgRec + offset);

    if (symbolPtr->type == SYMBOL_BITMAP) {
	Tcl_Obj *listObjPtr, *objPtr;
	const char *name;

	listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **)NULL);
	name = Tk_NameOfBitmap(Tk_Display(tkwin), symbolPtr->bitmap);
	objPtr = Tcl_NewStringObj(name, -1);
	Tcl_ListObjAppendElement(interp, listObjPtr, objPtr);
	if (symbolPtr->mask == None) {
	    objPtr = Tcl_NewStringObj("", -1);
	} else {
	    name = Tk_NameOfBitmap(Tk_Display(tkwin), symbolPtr->mask);
	    objPtr = Tcl_NewStringObj(name, -1);
	}
	Tcl_ListObjAppendElement(interp, listObjPtr, objPtr);
	return listObjPtr;
    } else {
	GraphSymbolType *p;

	for (p = graphSymbols; p->name != NULL; p++) {
	    if (p->type == symbolPtr->type) {
		return Tcl_NewStringObj(p->name, -1);
	    }
	}
	return Tcl_NewStringObj("?unknown symbol type?", -1);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * NameOfSmooth --
 *
 *	Converts the smooth value into its string representation.
 *
 * Results:
 *	The static string representing the smooth type is returned.
 *
 *---------------------------------------------------------------------------
 */
static const char *
NameOfSmooth(Smoothing value)
{
    SmoothingInfo *siPtr;

    for (siPtr = smoothingInfo; siPtr->name != NULL; siPtr++) {
	if (siPtr->value == value) {
	    return siPtr->name;
	}
    }
    return "unknown smooth value";
}

/*
 *---------------------------------------------------------------------------
 *
 * ObjToSmooth --
 *
 *	Convert the string representation of a line style or smooth name
 *	into its numeric form.
 *
 * Results:
 *	The return value is a standard TCL result.  The smooth type is
 *	written into the widget record.
 *
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static int
ObjToSmoothProc(
    ClientData clientData,		/* Not used. */
    Tcl_Interp *interp,			/* Interpreter to send results back
					 * to */
    Tk_Window tkwin,			/* Not used. */
    Tcl_Obj *objPtr,			/* String representing smooth type */
    char *widgRec,			/* Element information record */
    int offset,				/* Offset to field in structure */
    int flags)				/* Not used. */
{
    Smoothing *valuePtr = (Smoothing *)(widgRec + offset);
    SmoothingInfo *siPtr;
    const char *string;
    char c;

    string = Tcl_GetString(objPtr);
    c = string[0];
    for (siPtr = smoothingInfo; siPtr->name != NULL; siPtr++) {
	if ((c == siPtr->name[0]) && (strcmp(string, siPtr->name) == 0)) {
	    *valuePtr = siPtr->value;
	    return TCL_OK;
	}
    }
    Tcl_AppendResult(interp, "bad smooth value \"", string, "\": should be \
linear, step, natural, or quadratic", (char *)NULL);
    return TCL_ERROR;
}

/*
 *---------------------------------------------------------------------------
 *
 * SmoothToObj --
 *
 *	Convert the smooth value into a string.
 *
 * Results:
 *	The string representing the smooth type or line style is returned.
 *
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static Tcl_Obj *
SmoothToObjProc(
    ClientData clientData,		/* Not used. */
    Tcl_Interp *interp,			/* Not used. */
    Tk_Window tkwin,			/* Not used. */
    char *widgRec,			/* Element information record */
    int offset,				/* Offset to field in structure */
    int flags)				/* Not used. */
{
    int smooth = *(int *)(widgRec + offset);

    return Tcl_NewStringObj(NameOfSmooth(smooth), -1);
}

/*
 *---------------------------------------------------------------------------
 *
 * ObjToPenDir --
 *
 *	Convert the string representation of a line style or symbol name
 *	into its numeric form.
 *
 * Results:
 *	The return value is a standard TCL result.  The symbol type is
 *	written into the widget record.
 *
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static int
ObjToPenDirProc(
    ClientData clientData,		/* Not used. */
    Tcl_Interp *interp,			/* Interpreter to send results back
					 * to */
    Tk_Window tkwin,			/* Not used. */
    Tcl_Obj *objPtr,			/* String representing pen direction */
    char *widgRec,			/* Element information record */
    int offset,				/* Offset to field in structure */
    int flags)				/* Not used. */
{
    int *penDirPtr = (int *)(widgRec + offset);
    int length;
    char c;
    char *string;

    string = Tcl_GetStringFromObj(objPtr, &length);
    c = string[0];
    if ((c == 'i') && (strncmp(string, "increasing", length) == 0)) {
	*penDirPtr = PEN_INCREASING;
    } else if ((c == 'd') && (strncmp(string, "decreasing", length) == 0)) {
	*penDirPtr = PEN_DECREASING;
    } else if ((c == 'b') && (strncmp(string, "both", length) == 0)) {
	*penDirPtr = PEN_BOTH_DIRECTIONS;
    } else {
	Tcl_AppendResult(interp, "bad trace value \"", string,
	    "\" : should be \"increasing\", \"decreasing\", or \"both\"",
	    (char *)NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * NameOfPenDir --
 *
 *	Convert the pen direction into a string.
 *
 * Results:
 *	The static string representing the pen direction is returned.
 *
 *---------------------------------------------------------------------------
 */
static const char *
NameOfPenDir(int penDir)
{
    switch (penDir) {
    case PEN_INCREASING:
	return "increasing";
    case PEN_DECREASING:
	return "decreasing";
    case PEN_BOTH_DIRECTIONS:
	return "both";
    default:
	return "unknown trace direction";
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * PenDirToObj --
 *
 *	Convert the pen direction into a string.
 *
 * Results:
 *	The string representing the pen drawing direction is returned.
 *
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static Tcl_Obj *
PenDirToObjProc(
    ClientData clientData,		/* Not used. */
    Tcl_Interp *interp,			/* Not used. */
    Tk_Window tkwin,			/* Not used. */
    char *widgRec,			/* Element information record */
    int offset,				/* Offset to field in structure */
    int flags)				/* Not used. */
{
    int penDir = *(int *)(widgRec + offset);

    return Tcl_NewStringObj(NameOfPenDir(penDir), -1);
}


/*
 * Reset the number of points and segments, in case there are no segments or
 * points
 */
static void
ResetStylePalette(Blt_Chain styles)
{
    Blt_ChainLink link;

    for (link = Blt_Chain_FirstLink(styles); link != NULL;
	 link = Blt_Chain_NextLink(link)) {
	LineStyle *stylePtr;

	stylePtr = Blt_Chain_GetValue(link);
	stylePtr->lines.length = stylePtr->symbolPts.length = 0;
	stylePtr->xeb.length = stylePtr->yeb.length = 0;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * ConfigurePenProc --
 *
 *	Sets up the appropriate configuration parameters in the GC.  It is
 *	assumed the parameters have been previously set by a call to
 *	Blt_ConfigureWidget.
 *
 * Results:
 *	The return value is a standard TCL result.  If TCL_ERROR is returned,
 *	then interp->result contains an error message.
 *
 * Side effects:
 *	Configuration information such as line width, line style, color
 *	etc. get set in a new GC.
 *
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static int
ConfigurePenProc(Graph *graphPtr, Pen *penPtr)
{
    LinePen *lpPtr = (LinePen *)penPtr;
    unsigned long gcMask;
    GC newGC;
    XGCValues gcValues;
    XColor *colorPtr;

    /*
     * Set the outline GC for this pen: GCForeground is outline color.
     * GCBackground is the fill color (only used for bitmap symbols).
     */
    gcMask = (GCLineWidth | GCForeground);
    colorPtr = lpPtr->symbol.outlineColor;
    if (colorPtr == COLOR_DEFAULT) {
	colorPtr = lpPtr->traceColor;
    }
    gcValues.foreground = colorPtr->pixel;
    if (lpPtr->symbol.type == SYMBOL_BITMAP) {
	colorPtr = lpPtr->symbol.fillColor;
	if (colorPtr == COLOR_DEFAULT) {
	    colorPtr = lpPtr->traceColor;
	}
	/*
	 * Set a clip mask if either
	 *	1) no background color was designated or
	 *	2) a masking bitmap was specified.
	 *
	 * These aren't necessarily the bitmaps we'll be using for clipping. But
	 * this makes it unlikely that anyone else will be sharing this GC when
	 * we set the clip origin (at the time the bitmap is drawn).
	 */
	if (colorPtr != NULL) {
	    gcValues.background = colorPtr->pixel;
	    gcMask |= GCBackground;
	    if (lpPtr->symbol.mask != None) {
		gcValues.clip_mask = lpPtr->symbol.mask;
		gcMask |= GCClipMask;
	    }
	} else {
	    gcValues.clip_mask = lpPtr->symbol.bitmap;
	    gcMask |= GCClipMask;
	}
    }
    gcValues.line_width = LineWidth(lpPtr->symbol.outlineWidth);
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (lpPtr->symbol.outlineGC != NULL) {
	Tk_FreeGC(graphPtr->display, lpPtr->symbol.outlineGC);
    }
    lpPtr->symbol.outlineGC = newGC;

    /* Fill GC for symbols: GCForeground is fill color */

    gcMask = (GCLineWidth | GCForeground);
    colorPtr = lpPtr->symbol.fillColor;
    if (colorPtr == COLOR_DEFAULT) {
	colorPtr = lpPtr->traceColor;
    }
    newGC = NULL;
    if (colorPtr != NULL) {
	gcValues.foreground = colorPtr->pixel;
	newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    }
    if (lpPtr->symbol.fillGC != NULL) {
	Tk_FreeGC(graphPtr->display, lpPtr->symbol.fillGC);
    }
    lpPtr->symbol.fillGC = newGC;

    /* Line segments */

    gcMask = (GCLineWidth | GCForeground | GCLineStyle | GCCapStyle |
	GCJoinStyle);
    gcValues.cap_style = CapButt;
    gcValues.join_style = JoinRound;
    gcValues.line_style = LineSolid;
    gcValues.line_width = LineWidth(lpPtr->traceWidth);

    colorPtr = lpPtr->traceOffColor;
    if (colorPtr == COLOR_DEFAULT) {
	colorPtr = lpPtr->traceColor;
    }
    if (colorPtr != NULL) {
	gcMask |= GCBackground;
	gcValues.background = colorPtr->pixel;
    }
    gcValues.foreground = lpPtr->traceColor->pixel;
    if (LineIsDashed(lpPtr->traceDashes)) {
	gcValues.line_width = lpPtr->traceWidth;
	gcValues.line_style = 
	    (colorPtr == NULL) ? LineOnOffDash : LineDoubleDash;
    }
    newGC = Blt_GetPrivateGC(graphPtr->tkwin, gcMask, &gcValues);
    if (lpPtr->traceGC != NULL) {
	Blt_FreePrivateGC(graphPtr->display, lpPtr->traceGC);
    }
    if (LineIsDashed(lpPtr->traceDashes)) {
	lpPtr->traceDashes.offset = lpPtr->traceDashes.values[0] / 2;
	Blt_SetDashes(graphPtr->display, newGC, &lpPtr->traceDashes);
    }
    lpPtr->traceGC = newGC;

    gcMask = (GCLineWidth | GCForeground);
    colorPtr = lpPtr->errorBarColor;
    if (colorPtr == COLOR_DEFAULT) {
	colorPtr = lpPtr->traceColor;
    }
    gcValues.line_width = LineWidth(lpPtr->errorBarLineWidth);
    gcValues.foreground = colorPtr->pixel;
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (lpPtr->errorBarGC != NULL) {
	Tk_FreeGC(graphPtr->display, lpPtr->errorBarGC);
    }
    lpPtr->errorBarGC = newGC;

    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * DestroyPenProc --
 *
 *	Release memory and resources allocated for the style.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Everything associated with the pen style is freed up.
 *
 *---------------------------------------------------------------------------
 */
static void
DestroyPenProc(Graph *graphPtr, Pen *basePtr)
{
    LinePen *penPtr = (LinePen *)basePtr;

    Blt_Ts_FreeStyle(graphPtr->display, &penPtr->valueStyle);
    if (penPtr->symbol.outlineGC != NULL) {
	Tk_FreeGC(graphPtr->display, penPtr->symbol.outlineGC);
    }
    if (penPtr->symbol.fillGC != NULL) {
	Tk_FreeGC(graphPtr->display, penPtr->symbol.fillGC);
    }
    if (penPtr->errorBarGC != NULL) {
	Tk_FreeGC(graphPtr->display, penPtr->errorBarGC);
    }
    if (penPtr->traceGC != NULL) {
	Blt_FreePrivateGC(graphPtr->display, penPtr->traceGC);
    }
    if (penPtr->symbol.bitmap != None) {
	Tk_FreeBitmap(graphPtr->display, penPtr->symbol.bitmap);
	penPtr->symbol.bitmap = None;
    }
    if (penPtr->symbol.mask != None) {
	Tk_FreeBitmap(graphPtr->display, penPtr->symbol.mask);
	penPtr->symbol.mask = None;
    }
}


static void
InitLinePen(LinePen *penPtr)
{
    Blt_Ts_InitStyle(penPtr->valueStyle);
    penPtr->errorBarLineWidth = 2;
    penPtr->errorBarShow = SHOW_BOTH;
    penPtr->configProc = ConfigurePenProc;
    penPtr->configSpecs = linePenConfigSpecs;
    penPtr->destroyProc = DestroyPenProc;
    penPtr->flags = NORMAL_PEN;
    penPtr->name = "";
    penPtr->symbol.bitmap = penPtr->symbol.mask = None;
    penPtr->symbol.outlineColor = penPtr->symbol.fillColor = COLOR_DEFAULT;
    penPtr->symbol.outlineWidth = penPtr->traceWidth = 1;
    penPtr->symbol.type = SYMBOL_CIRCLE;
    penPtr->valueShow = SHOW_NONE;
}

Pen *
Blt_LinePen(const char *penName)
{
    LinePen *penPtr;

    penPtr = Blt_AssertCalloc(1, sizeof(LinePen));
    InitLinePen(penPtr);
    penPtr->name = Blt_AssertStrdup(penName);
    penPtr->classId = CID_ELEM_LINE;
    if (strcmp(penName, "activeLine") == 0) {
	penPtr->flags = ACTIVE_PEN;
    }
    return (Pen *)penPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 *	In this section, the routines deal with building and filling the
 *	element's data structures with transformed screen coordinates.  They are
 *	triggered from TranformLine which is called whenever the data or
 *	coordinates axes have changed and new screen coordinates need to be
 *	calculated.
 *
 *---------------------------------------------------------------------------
 */

/*
 *---------------------------------------------------------------------------
 *
 * ScaleSymbol --
 *
 *	Returns the scaled size for the line element. Scaling depends upon when
 *	the base line ranges for the element were set and the current range of
 *	the graph.
 *
 * Results:
 *	The new size of the symbol, after considering how much the graph has
 *	been scaled, is returned.
 *
 *---------------------------------------------------------------------------
 */
static int
ScaleSymbol(LineElement *elemPtr, int normalSize)
{
    int maxSize;
    double scale;
    int newSize;

    scale = 1.0;
    if (elemPtr->scaleSymbols) {
	double xRange, yRange;

	xRange = (elemPtr->axes.x->max - elemPtr->axes.x->min);
	yRange = (elemPtr->axes.y->max - elemPtr->axes.y->min);
	if (elemPtr->flags & SCALE_SYMBOL) {
	    /* Save the ranges as a baseline for future scaling. */
	    elemPtr->xRange = xRange;
	    elemPtr->yRange = yRange;
	    elemPtr->flags &= ~SCALE_SYMBOL;
	} else {
	    double xScale, yScale;

	    /* Scale the symbol by the smallest change in the X or Y axes */
	    xScale = elemPtr->xRange / xRange;
	    yScale = elemPtr->yRange / yRange;
	    scale = MIN(xScale, yScale);
	}
    }
    newSize = Round(normalSize * scale);

    /*
     * Don't let the size of symbols go unbounded. Both X and Win32 drawing
     * routines assume coordinates to be a signed short int.
     */
    maxSize = (int)MIN(elemPtr->obj.graphPtr->hRange, 
		       elemPtr->obj.graphPtr->vRange);
    if (newSize > maxSize) {
	newSize = maxSize;
    }

    /* Make the symbol size odd so that its center is a single pixel. */
    newSize |= 0x01;
    return newSize;
}

/*
 *---------------------------------------------------------------------------
 *
 * GetScreenPoints --
 *
 *	Generates a coordinate array of transformed screen coordinates from
 *	the data points.  Coordinates with Inf, -Inf, or NaN values are
 *	removed.
 *
 * Results:
 *	The transformed screen coordinates are returned.
 *
 * Side effects:
 *	Memory is allocated for the coordinate array.
 *
 *
 * Future ideas:
 *	Allow bad values to be removed (as done currently) or break
 *	into separate traces.  Smoothing would be affected.  
 *
 *---------------------------------------------------------------------------
 */
static void
GetScreenPoints(Graph *graphPtr, LineElement *elemPtr, MapInfo *mapPtr)
{
    double *x, *y;
    int i, np;
    int count;
    Point2d *points;
    int *map;

    np = NUMBEROFPOINTS(elemPtr);
    x = elemPtr->x.values;
    y = elemPtr->y.values;
    points = Blt_AssertMalloc(sizeof(Point2d) * np);
    map = Blt_AssertMalloc(sizeof(int) * np);

    count = 0;			      /* Count the valid screen coordinates */
    if (graphPtr->inverted) {
	for (i = 0; i < np; i++) {
	    if ((FINITE(x[i])) && (FINITE(y[i]))) {
 		points[count].x = Blt_HMap(elemPtr->axes.y, y[i]);
		points[count].y = Blt_VMap(elemPtr->axes.x, x[i]);
		map[count] = i;
		count++;
	    }
	}
    } else {
	for (i = 0; i < np; i++) {
	    if ((FINITE(x[i])) && (FINITE(y[i]))) {
		points[count].x = Blt_HMap(elemPtr->axes.x, x[i]);
		points[count].y = Blt_VMap(elemPtr->axes.y, y[i]);
		map[count] = i;
		count++;
	    }
	}
    }
    mapPtr->screenPts = points;
    mapPtr->nScreenPts = count;
    mapPtr->map = map;
}

/*
 *---------------------------------------------------------------------------
 *
 * ReducePoints --
 *
 *	Generates a coordinate array of transformed screen coordinates from
 *	the data points.
 *
 * Results:
 *	The transformed screen coordinates are returned.
 *
 * Side effects:
 *	Memory is allocated for the coordinate array.
 *
 *---------------------------------------------------------------------------
 */
static void
ReducePoints(MapInfo *mapPtr, double tolerance)
{
    int i, np;
    Point2d *screenPts;
    int *map, *simple;

    simple    = Blt_AssertMalloc(mapPtr->nScreenPts * sizeof(int));
    map	      = Blt_AssertMalloc(mapPtr->nScreenPts * sizeof(int));
    screenPts = Blt_AssertMalloc(mapPtr->nScreenPts * sizeof(Point2d));
    np = Blt_SimplifyLine(mapPtr->screenPts, 0, mapPtr->nScreenPts - 1, 
		 tolerance, simple);
    for (i = 0; i < np; i++) {
	int k;

	k = simple[i];
	screenPts[i] = mapPtr->screenPts[k];
	map[i] = mapPtr->map[k];
    }
#ifdef notdef
    if (np < mapPtr->nScreenPts) {
	fprintf(stderr, "reduced from %d to %d\n", mapPtr->nScreenPts, np);
    }
#endif
    Blt_Free(mapPtr->screenPts);
    Blt_Free(mapPtr->map);
    Blt_Free(simple);
    mapPtr->screenPts = screenPts;
    mapPtr->map = map;
    mapPtr->nScreenPts = np;
}

/*
 *---------------------------------------------------------------------------
 *
 * GenerateSteps --
 *
 *	Resets the coordinate and pen index arrays adding new points for
 *	step-and-hold type smoothing.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The temporary arrays for screen coordinates and pen indices
 *	are updated.
 *
 *---------------------------------------------------------------------------
 */
static void
GenerateSteps(MapInfo *mapPtr)
{
    int newSize;
    int i, count;
    Point2d *screenPts;
    int *map;

    newSize = ((mapPtr->nScreenPts - 1) * 2) + 1;
    screenPts = Blt_AssertMalloc(newSize * sizeof(Point2d));
    map = Blt_AssertMalloc(sizeof(int) * newSize);
    screenPts[0] = mapPtr->screenPts[0];
    map[0] = 0;

    count = 1;
    for (i = 1; i < mapPtr->nScreenPts; i++) {
	screenPts[count + 1] = mapPtr->screenPts[i];

	/* Hold last y-coordinate, use new x-coordinate */
	screenPts[count].x = screenPts[count + 1].x;
	screenPts[count].y = screenPts[count - 1].y;

	/* Use the same style for both the hold and the step points */
	map[count] = map[count + 1] = mapPtr->map[i];
	count += 2;
    }
    Blt_Free(mapPtr->screenPts);
    Blt_Free(mapPtr->map);
    mapPtr->map = map;
    mapPtr->screenPts = screenPts;
    mapPtr->nScreenPts = newSize;
}

/*
 *---------------------------------------------------------------------------
 *
 * GenerateSpline --
 *
 *	Computes a spline based upon the data points, returning a new (larger)
 *	coordinate array or points.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The temporary arrays for screen coordinates and data map are updated
 *	based upon spline.
 *
 * FIXME:  Can't interpolate knots along the Y-axis.   Need to break
 *	   up point array into interchangable X and Y vectors earlier.
 *	   Pass extents (left/right or top/bottom) as parameters.
 *
 *---------------------------------------------------------------------------
 */
static void
GenerateSpline(Graph *graphPtr, LineElement *elemPtr, MapInfo *mapPtr)
{
    Point2d *origPts, *iPts;
    int *map;
    int extra;
    int niPts, nOrigPts;
    int result;
    int i, j, count;

    nOrigPts = mapPtr->nScreenPts;
    origPts = mapPtr->screenPts;
    assert(mapPtr->nScreenPts > 0);
    for (i = 0, j = 1; j < nOrigPts; i++, j++) {
	if (origPts[j].x <= origPts[i].x) {
	    return;			/* Points are not monotonically
					 * increasing */
	}
    }
    if (((origPts[0].x > (double)graphPtr->right)) ||
	((origPts[mapPtr->nScreenPts - 1].x < (double)graphPtr->left))) {
	return;				/* All points are clipped */
    }

    /*
     * The spline is computed in screen coordinates instead of data points so
     * that we can select the abscissas of the interpolated points from each
     * pixel horizontally across the plotting area.
     */
    extra = (graphPtr->right - graphPtr->left) + 1;
    if (extra < 1) {
	return;
    }
    niPts = nOrigPts + extra + 1;
    iPts = Blt_AssertMalloc(niPts * sizeof(Point2d));
    map = Blt_AssertMalloc(sizeof(int) * niPts);
    /* Populate the x2 array with both the original X-coordinates and extra
     * X-coordinates for each horizontal pixel that the line segment
     * contains. */
    count = 0;
    for (i = 0, j = 1; j < nOrigPts; i++, j++) {

	/* Add the original x-coordinate */
	iPts[count].x = origPts[i].x;

	/* Include the starting offset of the point in the offset array */
	map[count] = mapPtr->map[i];
	count++;

	/* Is any part of the interval (line segment) in the plotting area?  */
	if ((origPts[j].x >= (double)graphPtr->left) || 
	    (origPts[i].x <= (double)graphPtr->right)) {
	    double x, last;

	    x = origPts[i].x + 1.0;

	    /*
	     * Since the line segment may be partially clipped on the left or
	     * right side, the points to interpolate are always interior to
	     * the plotting area.
	     *
	     *           left			    right
	     *      x1----|---------------------------|---x2
	     *
	     * Pick the max of the starting X-coordinate and the left edge and
	     * the min of the last X-coordinate and the right edge.
	     */
	    x = MAX(x, (double)graphPtr->left);
	    last = MIN(origPts[j].x, (double)graphPtr->right);

	    /* Add the extra x-coordinates to the interval. */
	    while (x < last) {
		map[count] = mapPtr->map[i];
		iPts[count++].x = x;
		x++;
	    }
	}
    }
    niPts = count;
    result = FALSE;
    if (elemPtr->smooth == PEN_SMOOTH_NATURAL) {
	result = Blt_NaturalSpline(origPts, nOrigPts, iPts, niPts);
    } else if (elemPtr->smooth == PEN_SMOOTH_QUADRATIC) {
	result = Blt_QuadraticSpline(origPts, nOrigPts, iPts, niPts);
    }
    if (!result) {
	/* The spline interpolation failed.  We'll fallback to the current
	 * coordinates and do no smoothing (standard line segments).  */
	elemPtr->smooth = PEN_SMOOTH_LINEAR;
	Blt_Free(iPts);
	Blt_Free(map);
    } else {
	Blt_Free(mapPtr->screenPts);
	Blt_Free(mapPtr->map);
	mapPtr->map = map;
	mapPtr->screenPts = iPts;
	mapPtr->nScreenPts = niPts;
    }
}


/*
 *---------------------------------------------------------------------------
 *
 * GenerateParametricSpline --
 *
 *	Computes a spline based upon the data points, returning a new (larger)
 *	coordinate array or points.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The temporary arrays for screen coordinates and data map are updated
 *	based upon spline.
 *
 * FIXME:  Can't interpolate knots along the Y-axis.   Need to break
 *	   up point array into interchangable X and Y vectors earlier.
 *	   Pass extents (left/right or top/bottom) as parameters.
 *
 *---------------------------------------------------------------------------
 */
static void
GenerateParametricSpline(Graph *graphPtr, LineElement *elemPtr, MapInfo *mapPtr)
{
    Region2d exts;
    Point2d *origPts, *iPts;
    int *map;
    int niPts, nOrigPts;
    int result;
    int i, j, count;

    nOrigPts = mapPtr->nScreenPts;
    origPts = mapPtr->screenPts;
    assert(mapPtr->nScreenPts > 0);

    Blt_GraphExtents(graphPtr, &exts);

    /* 
     * Populate the x2 array with both the original X-coordinates and extra
     * X-coordinates for each horizontal pixel that the line segment contains.
     */
    count = 1;
    for (i = 0, j = 1; j < nOrigPts; i++, j++) {
	Point2d p, q;

        p = origPts[i];
        q = origPts[j];
	count++;
        if (Blt_LineRectClip(&exts, &p, &q)) {
	    count += (int)(hypot(q.x - p.x, q.y - p.y) * 0.5);
	}
    }
    niPts = count;
    iPts = Blt_AssertMalloc(niPts * sizeof(Point2d));
    map = Blt_AssertMalloc(sizeof(int) * niPts);

    /* 
     * FIXME: This is just plain wrong.  The spline should be computed
     *        and evaluated in separate steps.  This will mean breaking
     *	      up this routine since the catrom coefficients can be
     *	      independently computed for original data point.  This 
     *	      also handles the problem of allocating enough points 
     *	      since evaluation is independent of the number of points 
     *		to be evalualted.  The interpolated 
     *	      line segments should be clipped, not the original segments.
     */
    count = 0;
    for (i = 0, j = 1; j < nOrigPts; i++, j++) {
	Point2d p, q;
	double d;

        p = origPts[i];
        q = origPts[j];

        d = hypot(q.x - p.x, q.y - p.y);
        /* Add the original x-coordinate */
        iPts[count].x = (double)i;
        iPts[count].y = 0.0;

        /* Include the starting offset of the point in the offset array */
        map[count] = mapPtr->map[i];
        count++;

        /* Is any part of the interval (line segment) in the plotting
         * area?  */

        if (Blt_LineRectClip(&exts, &p, &q)) {
            double dp, dq;

	    /* Distance of original point to p. */
            dp = hypot(p.x - origPts[i].x, p.y - origPts[i].y);
	    /* Distance of original point to q. */
            dq = hypot(q.x - origPts[i].x, q.y - origPts[i].y);
            dp += 2.0;
            while(dp <= dq) {
                /* Point is indicated by its interval and parameter t. */
                iPts[count].x = (double)i;
                iPts[count].y =  dp / d;
                map[count] = mapPtr->map[i];
                count++;
                dp += 2.0;
            }
        }
    }
    iPts[count].x = (double)i;
    iPts[count].y = 0.0;
    map[count] = mapPtr->map[i];
    count++;
    niPts = count;
    result = FALSE;
    if (elemPtr->smooth == PEN_SMOOTH_NATURAL) {
        result = Blt_NaturalParametricSpline(origPts, nOrigPts, &exts, FALSE,
		iPts, niPts);
    } else if (elemPtr->smooth == PEN_SMOOTH_CATROM) {
        result = Blt_CatromParametricSpline(origPts, nOrigPts, iPts, niPts);
    }
    if (!result) {
        /* The spline interpolation failed.  We will fall back to the current
         * coordinates and do no smoothing (standard line segments).  */
        elemPtr->smooth = PEN_SMOOTH_LINEAR;
        Blt_Free(iPts);
        Blt_Free(map);
    } else {
        Blt_Free(mapPtr->screenPts);
        Blt_Free(mapPtr->map);
        mapPtr->map = map;
        mapPtr->screenPts = iPts;
        mapPtr->nScreenPts = niPts;
    }
}


/*
 *---------------------------------------------------------------------------
 *
 * MapSymbols --
 *
 *	Creates two arrays of points and pen map, filled with the screen
 *	coordinates of the visible
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is freed and allocated for the index array.
 *
 *---------------------------------------------------------------------------
 */
static void
MapSymbols(Graph *graphPtr, LineElement *elemPtr, MapInfo *mapPtr)
{
    Region2d exts;
    Point2d *pp, *points;
    int *map;
    int i, count;

    points = Blt_AssertMalloc(sizeof(Point2d) * mapPtr->nScreenPts);
    map    = Blt_AssertMalloc(sizeof(int)     * mapPtr->nScreenPts);

    Blt_GraphExtents(graphPtr, &exts);
    count = 0;			/* Count the number of visible points */

    for (pp = mapPtr->screenPts, i = 0; i < mapPtr->nScreenPts; i++, pp++) {
	if (PointInRegion(&exts, pp->x, pp->y)) {
	    points[count].x = pp->x;
	    points[count].y = pp->y;
	    map[count] = mapPtr->map[i];
	    count++;
	}
    }
    elemPtr->symbolPts.points = points;
    elemPtr->symbolPts.length = count;
    elemPtr->symbolPts.map = map;
}

/*
 *---------------------------------------------------------------------------
 *
 * MapActiveSymbols --
 *
 *	Creates an array of points of the active graph coordinates.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is freed and allocated for the active point array.
 *
 *---------------------------------------------------------------------------
 */
static void
MapActiveSymbols(Graph *graphPtr, LineElement *elemPtr)
{
    Point2d *points;
    Region2d exts;
    int *map;
    int count, i, np;

    if (elemPtr->activePts.points != NULL) {
	Blt_Free(elemPtr->activePts.points);
	elemPtr->activePts.points = NULL;
    }
    if (elemPtr->activePts.map != NULL) {
	Blt_Free(elemPtr->activePts.map);
	elemPtr->activePts.map = NULL;
    }
    Blt_GraphExtents(graphPtr, &exts);
    points = Blt_AssertMalloc(sizeof(Point2d) * elemPtr->nActiveIndices);
    map    = Blt_AssertMalloc(sizeof(int)     * elemPtr->nActiveIndices);
    np = NUMBEROFPOINTS(elemPtr);
    count = 0;				/* Count the visible active points */
    for (i = 0; i < elemPtr->nActiveIndices; i++) {
	double x, y;
	int iPoint;

	iPoint = elemPtr->activeIndices[i];
	if (iPoint >= np) {
	    continue;			/* Index not available */
	}
	x = elemPtr->x.values[iPoint];
	y = elemPtr->y.values[iPoint];
	points[count] = Blt_Map2D(graphPtr, x, y, &elemPtr->axes);
	map[count] = iPoint;
	if (PointInRegion(&exts, points[count].x, points[count].y)) {
	    count++;
	}
    }
    if (count > 0) {
	elemPtr->activePts.points = points;
	elemPtr->activePts.map = map;
    } else {
	/* No active points were visible. */
	Blt_Free(points);
	Blt_Free(map);	
    }
    elemPtr->activePts.length = count;
    elemPtr->flags &= ~ACTIVE_PENDING;
}

/*
 *---------------------------------------------------------------------------
 *
 * MapStrip --
 *
 *	Creates an array of line segments of the graph coordinates.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is  allocated for the line segment array.
 *
 *---------------------------------------------------------------------------
 */
static void
MapStrip(Graph *graphPtr, LineElement *elemPtr, MapInfo *mapPtr)
{
    Region2d exts;
    Segment2d *lines;
    int *indices, *indexPtr;
    Point2d *pend, *pp;
    Segment2d *sp;
    int count;

    indices = Blt_AssertMalloc(sizeof(int) * mapPtr->nScreenPts);

    /* 
     * Create array to hold points for line segments (not polyline
     * coordinates).  So allocate twice the number of points.
     */
    sp = lines = Blt_AssertMalloc(mapPtr->nScreenPts * sizeof(Segment2d));
    Blt_GraphExtents(graphPtr, &exts);
    count = 0;				/* Count the number of segments. */
    indexPtr = mapPtr->map;
    for (pp = mapPtr->screenPts, pend = pp + (mapPtr->nScreenPts - 1); 
	pp < pend; pp++, indexPtr++) {
	sp->p = pp[0], sp->q = pp[1];
	if (Blt_LineRectClip(&exts, &sp->p, &sp->q)) {
	    sp++;
	    indices[count] = *indexPtr;
	    count++;
	}
    }
    elemPtr->lines.map = indices;
    elemPtr->lines.length = count;
    elemPtr->lines.segments = lines;
}

/*
 *---------------------------------------------------------------------------
 *
 * MergePens --
 *
 *	Reorders the both arrays of points and segments to merge pens.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The old arrays are freed and new ones allocated containing
 *	the reordered points and segments.
 *
 *---------------------------------------------------------------------------
 */
static void
MergePens(LineElement *elemPtr, LineStyle **styleMap)
{
    if (Blt_Chain_GetLength(elemPtr->styles) < 2) {
	Blt_ChainLink link;
	LineStyle *stylePtr;

	link = Blt_Chain_FirstLink(elemPtr->styles);
	stylePtr = Blt_Chain_GetValue(link);
	stylePtr->errorBarCapWidth = elemPtr->errorBarCapWidth;
	stylePtr->lines.length = elemPtr->lines.length;
	stylePtr->lines.segments = elemPtr->lines.segments;
	stylePtr->symbolPts.length = elemPtr->symbolPts.length;
	stylePtr->symbolPts.points = elemPtr->symbolPts.points;
	stylePtr->xeb.length = elemPtr->xeb.length;
	stylePtr->xeb.segments = elemPtr->xeb.segments;
	stylePtr->yeb.length = elemPtr->yeb.length;
	stylePtr->yeb.segments = elemPtr->yeb.segments;
	return;
    }

    /* We have more than one style. Group line segments and points of like pen
     * styles.  */
    if (elemPtr->lines.length > 0) {
	Blt_ChainLink link;
	Segment2d *sp, *segments;
	int *ip;
	int *map;

	segments = Blt_AssertMalloc(elemPtr->lines.length * sizeof(Segment2d));
	map = Blt_AssertMalloc(elemPtr->lines.length * sizeof(int));
	sp = segments, ip = map;
	for (link = Blt_Chain_FirstLink(elemPtr->styles); 
	     link != NULL; link = Blt_Chain_NextLink(link)) {
	    LineStyle *stylePtr;
	    int i;

	    stylePtr = Blt_Chain_GetValue(link);
	    stylePtr->lines.segments = sp;
	    for (i = 0; i < elemPtr->lines.length; i++) {
		int iData;

		iData = elemPtr->lines.map[i];
		if (styleMap[iData] == stylePtr) {
		    *sp++ = elemPtr->lines.segments[i];
		    *ip++ = iData;
		}
	    }
	    stylePtr->lines.length = sp - stylePtr->lines.segments;
	}
	Blt_Free(elemPtr->lines.segments);
	elemPtr->lines.segments = segments;
	Blt_Free(elemPtr->lines.map);
	elemPtr->lines.map = map;
    }
    if (elemPtr->symbolPts.length > 0) {
	Blt_ChainLink link;
	int *ip;
	Point2d *points, *pp;
	int *map;

	points = Blt_AssertMalloc(elemPtr->symbolPts.length * sizeof(Point2d));
	map = Blt_AssertMalloc(elemPtr->symbolPts.length * sizeof(int));
	pp = points, ip = map;
	for (link = Blt_Chain_FirstLink(elemPtr->styles); 
	     link != NULL; link = Blt_Chain_NextLink(link)) {
	    LineStyle *stylePtr;
	    int i;

	    stylePtr = Blt_Chain_GetValue(link);
	    stylePtr->symbolPts.points = pp;
	    for (i = 0; i < elemPtr->symbolPts.length; i++) {
		int iData;

		iData = elemPtr->symbolPts.map[i];
		if (styleMap[iData] == stylePtr) {
		    *pp++ = elemPtr->symbolPts.points[i];
		    *ip++ = iData;
		}
	    }
	    stylePtr->symbolPts.length = pp - stylePtr->symbolPts.points;
	}
	Blt_Free(elemPtr->symbolPts.points);
	Blt_Free(elemPtr->symbolPts.map);
	elemPtr->symbolPts.points = points;
	elemPtr->symbolPts.map = map;
    }
    if (elemPtr->xeb.length > 0) {
	Segment2d *segments, *sp;
	int *map, *ip;
	Blt_ChainLink link;

	segments = Blt_AssertMalloc(elemPtr->xeb.length * sizeof(Segment2d));
	map = Blt_AssertMalloc(elemPtr->xeb.length * sizeof(int));
	sp = segments, ip = map;
	for (link = Blt_Chain_FirstLink(elemPtr->styles); 
	     link != NULL; link = Blt_Chain_NextLink(link)) {
	    LineStyle *stylePtr;
	    int i;

	    stylePtr = Blt_Chain_GetValue(link);
	    stylePtr->xeb.segments = sp;
	    for (i = 0; i < elemPtr->xeb.length; i++) {
		int iData;

		iData = elemPtr->xeb.map[i];
		if (styleMap[iData] == stylePtr) {
		    *sp++ = elemPtr->xeb.segments[i];
		    *ip++ = iData;
		}
	    }
	    stylePtr->xeb.length = sp - stylePtr->xeb.segments;
	}
	Blt_Free(elemPtr->xeb.segments);
	Blt_Free(elemPtr->xeb.map);
	elemPtr->xeb.segments = segments;
	elemPtr->xeb.map = map;
    }
    if (elemPtr->yeb.length > 0) {
	Segment2d *segments, *sp;
	int *map, *ip;
	Blt_ChainLink link;

	segments = Blt_AssertMalloc(elemPtr->yeb.length * sizeof(Segment2d));
	map = Blt_AssertMalloc(elemPtr->yeb.length * sizeof(int));
	sp = segments, ip = map;
	for (link = Blt_Chain_FirstLink(elemPtr->styles); 
	     link != NULL; link = Blt_Chain_NextLink(link)) {
	    LineStyle *stylePtr;
	    int i;

	    stylePtr = Blt_Chain_GetValue(link);
	    stylePtr->yeb.segments = sp;
	    for (i = 0; i < elemPtr->yeb.length; i++) {
		int iData;

		iData = elemPtr->yeb.map[i];
		if (styleMap[iData] == stylePtr) {
		    *sp++ = elemPtr->yeb.segments[i];
		    *ip++ = iData;
		}
	    }
	    stylePtr->yeb.length = sp - stylePtr->yeb.segments;
	}
	Blt_Free(elemPtr->yeb.segments);
	elemPtr->yeb.segments = segments;
	Blt_Free(elemPtr->yeb.map);
	elemPtr->yeb.map = map;
    }
}

#define CLIP_TOP	(1<<0)
#define CLIP_BOTTOM	(1<<1)
#define CLIP_RIGHT	(1<<2)
#define CLIP_LEFT	(1<<3)

INLINE static int
OutCode(Region2d *extsPtr, Point2d *p)
{
    int code;

    code = 0;
    if (p->x > extsPtr->right) {
	code |= CLIP_RIGHT;
    } else if (p->x < extsPtr->left) {
	code |= CLIP_LEFT;
    }
    if (p->y > extsPtr->bottom) {
	code |= CLIP_BOTTOM;
    } else if (p->y < extsPtr->top) {
	code |= CLIP_TOP;
    }
    return code;
}

static int
ClipSegment(
    Region2d *extsPtr,
    int code1, int code2,
    Point2d *p, Point2d *q)
{
    int inside, outside;

    inside = ((code1 | code2) == 0);
    outside = ((code1 & code2) != 0);

    /*
     * In the worst case, we'll clip the line segment against each of the four
     * sides of the bounding rectangle.
     */
    while ((!outside) && (!inside)) {
	if (code1 == 0) {
	    Point2d *tmp;
	    int code;

	    /* Swap pointers and out codes */
	    tmp = p, p = q, q = tmp;
	    code = code1, code1 = code2, code2 = code;
	}
	if (code1 & CLIP_LEFT) {
	    p->y += (q->y - p->y) *
		(extsPtr->left - p->x) / (q->x - p->x);
	    p->x = extsPtr->left;
	} else if (code1 & CLIP_RIGHT) {
	    p->y += (q->y - p->y) *
		(extsPtr->right - p->x) / (q->x - p->x);
	    p->x = extsPtr->right;
	} else if (code1 & CLIP_BOTTOM) {
	    p->x += (q->x - p->x) *
		(extsPtr->bottom - p->y) / (q->y - p->y);
	    p->y = extsPtr->bottom;
	} else if (code1 & CLIP_TOP) {
	    p->x += (q->x - p->x) *
		(extsPtr->top - p->y) / (q->y - p->y);
	    p->y = extsPtr->top;
	}
	code1 = OutCode(extsPtr, p);

	inside = ((code1 | code2) == 0);
	outside = ((code1 & code2) != 0);
    }
    return (!inside);
}

/*
 *---------------------------------------------------------------------------
 *
 * SaveTrace --
 *
 *	Creates a new trace and inserts it into the line's list of traces.
 *
 * Results:
 *	None.
 *
 *---------------------------------------------------------------------------
 */
static void
SaveTrace(
    LineElement *elemPtr,
    int start,			/* Starting index of the trace in data point
				 * array.  Used to figure out closest point */
    int length,			/* Number of points forming the trace */
    MapInfo *mapPtr)
{
    Trace *tracePtr;
    Point2d *screenPts;
    int *map;
    int i, j;

    tracePtr  = Blt_AssertMalloc(sizeof(Trace));
    screenPts = Blt_AssertMalloc(sizeof(Point2d) * length);
    map       = Blt_AssertMalloc(sizeof(int) * length);

    /* Copy the screen coordinates of the trace into the point array */

    if (mapPtr->map != NULL) {
	for (i = 0, j = start; i < length; i++, j++) {
	    screenPts[i].x = mapPtr->screenPts[j].x;
	    screenPts[i].y = mapPtr->screenPts[j].y;
	    map[i] = mapPtr->map[j];
	}
    } else {
	for (i = 0, j = start; i < length; i++, j++) {
	    screenPts[i].x = mapPtr->screenPts[j].x;
	    screenPts[i].y = mapPtr->screenPts[j].y;
	    map[i] = j;
	}
    }
    tracePtr->screenPts.length = length;
    tracePtr->screenPts.points = screenPts;
    tracePtr->screenPts.map = map;
    tracePtr->start = start;
    if (elemPtr->traces == NULL) {
	elemPtr->traces = Blt_Chain_Create();
    }
    Blt_Chain_Append(elemPtr->traces, tracePtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * FreeTraces --
 *
 *	Deletes all the traces for the line.
 *
 * Results:
 *	None.
 *
 *---------------------------------------------------------------------------
 */
static void
FreeTraces(LineElement *elemPtr)
{
    Blt_ChainLink link;

    for (link = Blt_Chain_FirstLink(elemPtr->traces); link != NULL;
	link = Blt_Chain_NextLink(link)) {
	Trace *tracePtr;

	tracePtr = Blt_Chain_GetValue(link);
	Blt_Free(tracePtr->screenPts.map);
	Blt_Free(tracePtr->screenPts.points);
	Blt_Free(tracePtr);
    }
    Blt_Chain_Destroy(elemPtr->traces);
    elemPtr->traces = NULL;
}

/*
 *---------------------------------------------------------------------------
 *
 * MapTraces --
 *
 *	Creates an array of line segments of the graph coordinates.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is  allocated for the line segment array.
 *
 *---------------------------------------------------------------------------
 */
static void
MapTraces(Graph *graphPtr, LineElement *elemPtr, MapInfo *mapPtr)
{
    Point2d *p, *q;
    Region2d exts;
    int code1;
    int i;
    int start, count;

    Blt_GraphExtents(graphPtr, &exts);
    count = 1;
    code1 = OutCode(&exts, mapPtr->screenPts);
    p = mapPtr->screenPts;
    q = p + 1;
    for (i = 1; i < mapPtr->nScreenPts; i++, p++, q++) {
	Point2d s;
	int code2;
	int broken, offscreen;

	s.x = s.y = 0;
	code2 = OutCode(&exts, q);
	if (code2 != 0) {
	    /* Save the coordinates of the last point, before clipping */
	    s = *q;
	}
	broken = BROKEN_TRACE(elemPtr->penDir, p->x, q->x);
	offscreen = ClipSegment(&exts, code1, code2, p, q);
	if (broken || offscreen) {

	    /*
	     * The last line segment is either totally clipped by the plotting
	     * area or the x-direction is wrong, breaking the trace.  Either
	     * way, save information about the last trace (if one exists),
	     * discarding the current line segment
	     */

	    if (count > 1) {
		start = i - count;
		SaveTrace(elemPtr, start, count, mapPtr);
		count = 1;
	    }
	} else {
	    count++;		/* Add the point to the trace. */
	    if (code2 != 0) {

		/*
		 * If the last point is clipped, this means that the trace is
		 * broken after this point.  Restore the original coordinate
		 * (before clipping) after saving the trace.
		 */

		start = i - (count - 1);
		SaveTrace(elemPtr, start, count, mapPtr);
		mapPtr->screenPts[i] = s;
		count = 1;
	    }
	}
	code1 = code2;
    }
    if (count > 1) {
	start = i - count;
	SaveTrace(elemPtr, start, count, mapPtr);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * MapFillArea --
 *
 *	Creates an array of points that represent a polygon that fills
 *	the area under the element.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is allocated for the polygon point array.
 *
 *---------------------------------------------------------------------------
 */
static void
MapFillArea(Graph *graphPtr, LineElement *elemPtr, MapInfo *mapPtr)
{
    Point2d *origPts, *clipPts;
    Region2d exts;
    int np;

    if (elemPtr->fillPts != NULL) {
	Blt_Free(elemPtr->fillPts);
	elemPtr->fillPts = NULL;
	elemPtr->nFillPts = 0;
    }
    if (mapPtr->nScreenPts < 3) {
	return;
    }
    np = mapPtr->nScreenPts + 3;
    Blt_GraphExtents(graphPtr, &exts);

    origPts = Blt_AssertMalloc(sizeof(Point2d) * np);
    if (graphPtr->inverted) {
	double minX;
	int i;

	minX = (double)elemPtr->axes.y->screenMin;
	for (i = 0; i < mapPtr->nScreenPts; i++) {
	    origPts[i].x = mapPtr->screenPts[i].x + 1;
	    origPts[i].y = mapPtr->screenPts[i].y;
	    if (origPts[i].x < minX) {
		minX = origPts[i].x;
	    }
	}	
	/* Add edges to make (if necessary) the polygon fill to the bottom of
	 * plotting window */
	origPts[i].x = minX;
	origPts[i].y = origPts[i - 1].y;
	i++;
	origPts[i].x = minX;
	origPts[i].y = origPts[0].y; 
	i++;
	origPts[i] = origPts[0];
    } else {
	double maxY;
	int i;

	maxY = (double)elemPtr->axes.y->bottom;
	for (i = 0; i < mapPtr->nScreenPts; i++) {
	    origPts[i].x = mapPtr->screenPts[i].x + 1;
	    origPts[i].y = mapPtr->screenPts[i].y;
	    if (origPts[i].y > maxY) {
		maxY = origPts[i].y;
	    }
	}	
	/* Add edges to extend the fill polygon to the bottom of plotting
	 * window */
	origPts[i].x = origPts[i - 1].x;
	origPts[i].y = maxY;
	i++;
	origPts[i].x = origPts[0].x; 
	origPts[i].y = maxY;
	i++;
	origPts[i] = origPts[0];
    }

    clipPts = Blt_AssertMalloc(sizeof(Point2d) * np * 3);
    np = Blt_PolyRectClip(&exts, origPts, np - 1, clipPts);

    Blt_Free(origPts);
    if (np < 3) {
	Blt_Free(clipPts);
    } else {
	elemPtr->fillPts = clipPts;
	elemPtr->nFillPts = np;
    }
}

static void
ResetLine(LineElement *elemPtr)
{
    FreeTraces(elemPtr);
    ResetStylePalette(elemPtr->styles);
    if (elemPtr->symbolPts.points != NULL) {
	Blt_Free(elemPtr->symbolPts.points);
    }
    if (elemPtr->symbolPts.map != NULL) {
	Blt_Free(elemPtr->symbolPts.map);
    }
    if (elemPtr->lines.segments != NULL) {
	Blt_Free(elemPtr->lines.segments);
    }
    if (elemPtr->lines.map != NULL) {
	Blt_Free(elemPtr->lines.map);
    }
    if (elemPtr->activePts.points != NULL) {
	Blt_Free(elemPtr->activePts.points);
    }
    if (elemPtr->activePts.map != NULL) {
	Blt_Free(elemPtr->activePts.map);
    }
    if (elemPtr->xeb.segments != NULL) {
	Blt_Free(elemPtr->xeb.segments);
    }
    if (elemPtr->xeb.map != NULL) {
	Blt_Free(elemPtr->xeb.map);
    }
    if (elemPtr->yeb.segments != NULL) {
	Blt_Free(elemPtr->yeb.segments);
    }
    if (elemPtr->yeb.map != NULL) {
	Blt_Free(elemPtr->yeb.map);
    }
    elemPtr->xeb.segments = elemPtr->yeb.segments = elemPtr->lines.segments = NULL;
    elemPtr->symbolPts.points = elemPtr->activePts.points = NULL;
    elemPtr->lines.map = elemPtr->symbolPts.map = elemPtr->xeb.map = 
	elemPtr->yeb.map = elemPtr->activePts.map = NULL;
    elemPtr->activePts.length = elemPtr->symbolPts.length = 
	elemPtr->lines.length = elemPtr->xeb.length = elemPtr->yeb.length = 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * MapErrorBars --
 *
 *	Creates two arrays of points and pen indices, filled with the screen
 *	coordinates of the visible
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is freed and allocated for the index array.
 *
 *---------------------------------------------------------------------------
 */
static void
MapErrorBars(Graph *graphPtr, LineElement *elemPtr, LineStyle **styleMap)
{
    int n, np;
    Region2d exts;

    Blt_GraphExtents(graphPtr, &exts);
    np = NUMBEROFPOINTS(elemPtr);
    if (elemPtr->xError.nValues > 0) {
	n = MIN(elemPtr->xError.nValues, np);
    } else {
	n = MIN3(elemPtr->xHigh.nValues, elemPtr->xLow.nValues, np);
    }
    if (n > 0) {
	Segment2d *errorBars;
	Segment2d *segPtr;
	int *errorToData;
	int *indexPtr;
	int i;
		
	segPtr = errorBars = Blt_AssertMalloc(n * 3 * sizeof(Segment2d));
	indexPtr = errorToData = Blt_AssertMalloc(n * 3 * sizeof(int));
	for (i = 0; i < n; i++) {
	    double x, y;
	    double high, low;
	    LineStyle *stylePtr;

	    x = elemPtr->x.values[i];
	    y = elemPtr->y.values[i];
	    stylePtr = styleMap[i];
	    if ((FINITE(x)) && (FINITE(y))) {
		if (elemPtr->xError.nValues > 0) {
		    high = x + elemPtr->xError.values[i];
		    low = x - elemPtr->xError.values[i];
		} else {
		    high = elemPtr->xHigh.values[i];
		    low = elemPtr->xLow.values[i];
		}
		if ((FINITE(high)) && (FINITE(low)))  {
		    Point2d p, q;

		    p = Blt_Map2D(graphPtr, high, y, &elemPtr->axes);
		    q = Blt_Map2D(graphPtr, low, y, &elemPtr->axes);
		    segPtr->p = p;
		    segPtr->q = q;
		    if (Blt_LineRectClip(&exts, &segPtr->p, &segPtr->q)) {
			segPtr++;
			*indexPtr++ = i;
		    }
		    /* Left cap */
		    segPtr->p.x = segPtr->q.x = p.x;
		    segPtr->p.y = p.y - stylePtr->errorBarCapWidth;
		    segPtr->q.y = p.y + stylePtr->errorBarCapWidth;
		    if (Blt_LineRectClip(&exts, &segPtr->p, &segPtr->q)) {
			segPtr++;
			*indexPtr++ = i;
		    }
		    /* Right cap */
		    segPtr->p.x = segPtr->q.x = q.x;
		    segPtr->p.y = q.y - stylePtr->errorBarCapWidth;
		    segPtr->q.y = q.y + stylePtr->errorBarCapWidth;
		    if (Blt_LineRectClip(&exts, &segPtr->p, &segPtr->q)) {
			segPtr++;
			*indexPtr++ = i;
		    }
		}
	    }
	}
	elemPtr->xeb.segments = errorBars;
	elemPtr->xeb.length = segPtr - errorBars;
	elemPtr->xeb.map = errorToData;
    }
    if (elemPtr->yError.nValues > 0) {
	n = MIN(elemPtr->yError.nValues, np);
    } else {
	n = MIN3(elemPtr->yHigh.nValues, elemPtr->yLow.nValues, np);
    }
    if (n > 0) {
	Segment2d *errorBars;
	Segment2d *segPtr;
	int *errorToData;
	int *indexPtr;
	int i;
		
	segPtr = errorBars = Blt_AssertMalloc(n * 3 * sizeof(Segment2d));
	indexPtr = errorToData = Blt_AssertMalloc(n * 3 * sizeof(int));
	for (i = 0; i < n; i++) {
	    double x, y;
	    double high, low;
	    LineStyle *stylePtr;

	    x = elemPtr->x.values[i];
	    y = elemPtr->y.values[i];
	    stylePtr = styleMap[i];
	    if ((FINITE(x)) && (FINITE(y))) {
		if (elemPtr->yError.nValues > 0) {
		    high = y + elemPtr->yError.values[i];
		    low = y - elemPtr->yError.values[i];
		} else {
		    high = elemPtr->yHigh.values[i];
		    low = elemPtr->yLow.values[i];
		}
		if ((FINITE(high)) && (FINITE(low)))  {
		    Point2d p, q;
		    
		    p = Blt_Map2D(graphPtr, x, high, &elemPtr->axes);
		    q = Blt_Map2D(graphPtr, x, low, &elemPtr->axes);
		    segPtr->p = p;
		    segPtr->q = q;
		    if (Blt_LineRectClip(&exts, &segPtr->p, &segPtr->q)) {
			segPtr++;
			*indexPtr++ = i;
		    }
		    /* Top cap. */
		    segPtr->p.y = segPtr->q.y = p.y;
		    segPtr->p.x = p.x - stylePtr->errorBarCapWidth;
		    segPtr->q.x = p.x + stylePtr->errorBarCapWidth;
		    if (Blt_LineRectClip(&exts, &segPtr->p, &segPtr->q)) {
			segPtr++;
			*indexPtr++ = i;
		    }
		    /* Bottom cap. */
		    segPtr->p.y = segPtr->q.y = q.y;
		    segPtr->p.x = q.x - stylePtr->errorBarCapWidth;
		    segPtr->q.x = q.x + stylePtr->errorBarCapWidth;
		    if (Blt_LineRectClip(&exts, &segPtr->p, &segPtr->q)) {
			segPtr++;
			*indexPtr++ = i;
		    }
		}
	    }
	}
	elemPtr->yeb.segments = errorBars;
	elemPtr->yeb.length = segPtr - errorBars;
	elemPtr->yeb.map = errorToData;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * MapLineProc --
 *
 *	Calculates the actual window coordinates of the line element.  The
 *	window coordinates are saved in an allocated point array.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is (re)allocated for the point array.
 *
 *---------------------------------------------------------------------------
 */
static void
MapLineProc(Graph *graphPtr, Element *basePtr)
{
    LineElement *elemPtr = (LineElement *)basePtr;
    MapInfo mi;
    int size, np;
    LineStyle **styleMap;
    Blt_ChainLink link;
    
    ResetLine(elemPtr);
    np = NUMBEROFPOINTS(elemPtr);
    if (np < 1) {
	return;				/* No data points */
    }
    GetScreenPoints(graphPtr, elemPtr, &mi);
    MapSymbols(graphPtr, elemPtr, &mi);

    if ((elemPtr->flags & ACTIVE_PENDING) && (elemPtr->nActiveIndices > 0)) {
	MapActiveSymbols(graphPtr, elemPtr);
    }
    /*
     * Map connecting line segments if they are to be displayed.
     */
    elemPtr->smooth = elemPtr->reqSmooth;
    if ((np > 1) && ((graphPtr->classId == CID_ELEM_STRIP) ||
		     (elemPtr->builtinPen.traceWidth > 0))) {
	/*
	 * Do smoothing if necessary.  This can extend the coordinate array,
	 * so both mi.points and mi.nPoints may change.
	 */
	switch (elemPtr->smooth) {
	case PEN_SMOOTH_STEP:
	    GenerateSteps(&mi);
	    break;

	case PEN_SMOOTH_NATURAL:
	case PEN_SMOOTH_QUADRATIC:
	    if (mi.nScreenPts < 3) {
		/* Can't interpolate with less than three points. */
		elemPtr->smooth = PEN_SMOOTH_LINEAR;
	    } else {
		GenerateSpline(graphPtr, elemPtr, &mi);
	    }
	    break;

	case PEN_SMOOTH_CATROM:
	    if (mi.nScreenPts < 3) {
		/* Can't interpolate with less than three points. */
		elemPtr->smooth = PEN_SMOOTH_LINEAR;
	    } else {
		GenerateParametricSpline(graphPtr, elemPtr, &mi);
	    }
	    break;

	default:
	    break;
	}
	if (elemPtr->rTolerance > 0.0) {
	    ReducePoints(&mi, elemPtr->rTolerance);
	}
	if (elemPtr->fillBg != NULL) {
	    MapFillArea(graphPtr, elemPtr, &mi);
	}
	if (graphPtr->classId == CID_ELEM_STRIP) {
	    MapStrip(graphPtr, elemPtr, &mi);
	} else {
	    MapTraces(graphPtr, elemPtr, &mi);
	}
    }
    Blt_Free(mi.screenPts);
    Blt_Free(mi.map);

    /* Set the symbol size of all the pen styles. */
    for (link = Blt_Chain_FirstLink(elemPtr->styles); link != NULL;
	 link = Blt_Chain_NextLink(link)) {
	LineStyle *stylePtr;
	LinePen *penPtr;

	stylePtr = Blt_Chain_GetValue(link);
	penPtr = (LinePen *)stylePtr->penPtr;
	size = ScaleSymbol(elemPtr, penPtr->symbol.size);
	stylePtr->symbolSize = size;
	stylePtr->errorBarCapWidth = (penPtr->errorBarCapWidth > 0) 
	    ? penPtr->errorBarCapWidth : Round(size * 0.6666666);
	stylePtr->errorBarCapWidth /= 2;
    }
    styleMap = (LineStyle **)Blt_StyleMap((Element *)elemPtr);
    if (((elemPtr->yHigh.nValues > 0) && (elemPtr->yLow.nValues > 0)) ||
	((elemPtr->xHigh.nValues > 0) && (elemPtr->xLow.nValues > 0)) ||
	(elemPtr->xError.nValues > 0) || (elemPtr->yError.nValues > 0)) {
	MapErrorBars(graphPtr, elemPtr, styleMap);
    }
    MergePens(elemPtr, styleMap);
    Blt_Free(styleMap);
}

static double
DistanceToLineProc(
    int x, int y,			/* Sample X-Y coordinate. */
    Point2d *p, Point2d *q,		/* End points of the line segment. */
    Point2d *t)				/* (out) Point on line segment. */
{
    double right, left, top, bottom;

    *t = Blt_GetProjection(x, y, p, q);
    if (p->x > q->x) {
	right = p->x, left = q->x;
    } else {
	left = p->x, right = q->x;
    }
    if (p->y > q->y) {
	bottom = p->y, top = q->y;
    } else {
	top = p->y, bottom = q->y;
    }
    if (t->x > right) {
	t->x = right;
    } else if (t->x < left) {
	t->x = left;
    }
    if (t->y > bottom) {
	t->y = bottom;
    } else if (t->y < top) {
	t->y = top;
    }
    return hypot((t->x - x), (t->y - y));
}

static double
DistanceToXProc(
    int x, int y,			/* Search X-Y coordinate. */
    Point2d *p, 
    Point2d *q,				/* End points of the line segment. */
    Point2d *t)				/* (out) Point on line segment. */
{
    double dx, dy;
    double d;

    if (p->x > q->x) {
	if ((x > p->x) || (x < q->x)) {
	    return DBL_MAX;		/* X-coordinate outside line segment. */
	}
    } else {
	if ((x > q->x) || (x < p->x)) {
	    return DBL_MAX;		/* X-coordinate outside line segment. */
	}
    }
    dx = p->x - q->x;
    dy = p->y - q->y;
    t->x = (double)x;
    if (FABS(dx) < DBL_EPSILON) {
	double d1, d2;
	/* 
	 * Same X-coordinate indicates a vertical line.  Pick the closest end
	 * point.
	 */
	d1 = p->y - y;
	d2 = q->y - y;
	if (FABS(d1) < FABS(d2)) {
	    t->y = p->y, d = d1;
	} else {
	    t->y = q->y, d = d2;
	}
    } else if (FABS(dy) < DBL_EPSILON) {
	/* Horizontal line. */
	t->y = p->y, d = p->y - y;
    } else {
	double m, b;
		
	m = dy / dx;
	b = p->y - (m * p->x);
	t->y = (x * m) + b;
	d = y - t->y;
    }
   return FABS(d);
}

static double
DistanceToYProc(
    int x, int y,		/* Search X-Y coordinate. */
    Point2d *p, Point2d *q,	/* End points of the line segment. */
    Point2d *t)			/* (out) Point on line segment. */
{
    double dx, dy;
    double d;

    if (p->y > q->y) {
	if ((y > p->y) || (y < q->y)) {
	    return DBL_MAX;
	}
    } else {
	if ((y > q->y) || (y < p->y)) {
	    return DBL_MAX;
	}
    }
    dx = p->x - q->x;
    dy = p->y - q->y;
    t->y = y;
    if (FABS(dy) < DBL_EPSILON) {
	double d1, d2;

	/* Save Y-coordinate indicates an horizontal line. Pick the closest end
	 * point. */
	d1 = p->x - x;
	d2 = q->x - x;
	if (FABS(d1) < FABS(d2)) {
	    t->x = p->x, d = d1;
	} else {
	    t->x = q->x, d = d2;
	}
    } else if (FABS(dx) < DBL_EPSILON) {
	/* Vertical line. */
	t->x = p->x, d = p->x - x;
    } else {
	double m, b;
	
	m = dy / dx;
	b = p->y - (m * p->x);
	t->x = (y - b) / m;
	d = x - t->x;
    }
    return FABS(d);
}

/*
 *---------------------------------------------------------------------------
 *
 * ClosestTrace --
 *
 *	Find the line segment closest to the given window coordinate in the
 *	element.
 *
 * Results:
 *	If a new minimum distance is found, the information regarding it is
 *	returned via searchPtr.
 *
 *---------------------------------------------------------------------------
 */
static int
ClosestTrace(
    Graph *graphPtr,			/* Graph widget record */
    LineElement *elemPtr,
    ClosestSearch *searchPtr,		/* Info about closest point in
					 * element */
    DistanceProc *distProc)
{
    Blt_ChainLink link;
    Point2d closest;
    double dMin;
    int iClose;

    iClose = -1;			/* Suppress compiler warning. */
    dMin = searchPtr->dist;
    closest.x = closest.y = 0;		/* Suppress compiler warning. */
    for (link = Blt_Chain_FirstLink(elemPtr->traces); link != NULL;
	link = Blt_Chain_NextLink(link)) {
	Trace *tracePtr;
	Point2d *p, *pend;

	tracePtr = Blt_Chain_GetValue(link);
	for (p = tracePtr->screenPts.points, 
		pend = p + (tracePtr->screenPts.length - 1); p < pend; p++) {
	    Point2d b;
	    double d;

	    d = (*distProc)(searchPtr->x, searchPtr->y, p, p + 1, &b);
	    if (d < dMin) {
		closest = b;
		iClose = tracePtr->screenPts.map[p-tracePtr->screenPts.points];
		dMin = d;
	    }
	}
    }
    if (dMin < searchPtr->dist) {
	searchPtr->dist = dMin;
	searchPtr->elemPtr = (Element *)elemPtr;
	searchPtr->index = iClose;
	searchPtr->point = Blt_InvMap2D(graphPtr, closest.x, closest.y,
	    &elemPtr->axes);
	return TRUE;
    }
    return FALSE;
}

/*
 *---------------------------------------------------------------------------
 *
 * ClosestStrip --
 *
 *	Find the line segment closest to the given window coordinate in the
 *	element.
 *
 * Results:
 *	If a new minimum distance is found, the information regarding it is
 *	returned via searchPtr.
 *
 *---------------------------------------------------------------------------
 */
static int
ClosestStrip(
    Graph *graphPtr,			/* Graph widget record */
    LineElement *elemPtr,		/* Line element record */
    ClosestSearch *searchPtr,		/* Info about closest point in
					 * element */
    DistanceProc *distProc)
{
    Point2d closest;
    double dMin;
    int count;
    int iClose;
    Segment2d *sp;

    iClose = 0;
    dMin = searchPtr->dist;
    closest.x = closest.y = 0;
    for (sp = elemPtr->lines.segments, count = 0; 
	 count < elemPtr->lines.length; count++, sp++) {
	double d;
	Point2d b;

	d = (*distProc)(searchPtr->x, searchPtr->y, &sp->p, &sp->q, &b);
	if (d < dMin) {
	    closest = b;
	    iClose = elemPtr->lines.map[count];
	    dMin = d;
	}
    }
    if (dMin < searchPtr->dist) {
	searchPtr->dist = dMin;
	searchPtr->elemPtr = (Element *)elemPtr;
	searchPtr->index = iClose;
	searchPtr->point = Blt_InvMap2D(graphPtr, closest.x, closest.y,
	    &elemPtr->axes);
	return TRUE;
    }
    return FALSE;
}

/*
 *---------------------------------------------------------------------------
 *
 * ClosestPoint --
 *
 *	Find the element whose data point is closest to the given screen
 *	coordinate.
 *
 * Results:
 *	If a new minimum distance is found, the information regarding
 *	it is returned via searchPtr.
 *
 *---------------------------------------------------------------------------
 */
static void
ClosestPoint(
    LineElement *elemPtr,		/* Line element to be searched. */
    ClosestSearch *searchPtr)		/* Assorted information related to
					 * searching for the closest point */
{
    double dMin;
    int count, iClose;
    Point2d *pp;

    dMin = searchPtr->dist;
    iClose = 0;

    /*
     * Instead of testing each data point in graph coordinates, look at the
     * array of mapped screen coordinates. The advantages are
     *   1) only examine points that are visible (unclipped), and
     *   2) the computed distance is already in screen coordinates.
     */
    for (pp = elemPtr->symbolPts.points, count = 0; 
	 count < elemPtr->symbolPts.length; count++, pp++) {
	double dx, dy;
	double d;

	dx = (double)(searchPtr->x - pp->x);
	dy = (double)(searchPtr->y - pp->y);
	if (searchPtr->along == SEARCH_BOTH) {
	    d = hypot(dx, dy);
	} else if (searchPtr->along == SEARCH_X) {
	    d = dx;
	} else if (searchPtr->along == SEARCH_Y) {
	    d = dy;
	} else {
	    /* This can't happen */
	    continue;
	}
	if (d < dMin) {
	    iClose = elemPtr->symbolPts.map[count];
	    dMin = d;
	}
    }
    if (dMin < searchPtr->dist) {
	searchPtr->elemPtr = (Element *)elemPtr;
	searchPtr->dist = dMin;
	searchPtr->index = iClose;
	searchPtr->point.x = elemPtr->x.values[iClose];
	searchPtr->point.y = elemPtr->y.values[iClose];
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * GetLineExtentsProc --
 *
 *	Retrieves the range of the line element
 *
 * Results:
 *	Returns the number of data points in the element.
 *
 *---------------------------------------------------------------------------
 */
static void
GetLineExtentsProc(Element *basePtr, Region2d *extsPtr)
{
    LineElement *elemPtr = (LineElement *)basePtr;
    int np;

    extsPtr->top = extsPtr->left = DBL_MAX;
    extsPtr->bottom = extsPtr->right = -DBL_MAX;

    np = NUMBEROFPOINTS(elemPtr);
    if (np < 1) {
	return;
    } 
    extsPtr->right = elemPtr->x.max;
    if ((elemPtr->x.min <= 0.0) && (elemPtr->axes.x->logScale)) {
	extsPtr->left = Blt_FindElemValuesMinimum(&elemPtr->x, DBL_MIN);
    } else {
	extsPtr->left = elemPtr->x.min;
    }
    extsPtr->bottom = elemPtr->y.max;
    if ((elemPtr->y.min <= 0.0) && (elemPtr->axes.y->logScale)) {
	extsPtr->top = Blt_FindElemValuesMinimum(&elemPtr->y, DBL_MIN);
    } else {
	extsPtr->top = elemPtr->y.min;
    }

    /* Correct the data limits for error bars */

    if (elemPtr->xError.nValues > 0) {
	int i;
	
	np = MIN(elemPtr->xError.nValues, np);
	for (i = 0; i < np; i++) {
	    double x;

	    x = elemPtr->x.values[i] + elemPtr->xError.values[i];
	    if (x > extsPtr->right) {
		extsPtr->right = x;
	    }
	    x = elemPtr->x.values[i] - elemPtr->xError.values[i];
	    if (elemPtr->axes.x->logScale) {
		if (x < 0.0) {
		    x = -x;		/* Mirror negative values, instead of
					 * ignoring them. */
		}
		if ((x > DBL_MIN) && (x < extsPtr->left)) {
		    extsPtr->left = x;
		}
	    } else if (x < extsPtr->left) {
		extsPtr->left = x;
	    }
	}		     
    } else {
	if ((elemPtr->xHigh.nValues > 0) && 
	    (elemPtr->xHigh.max > extsPtr->right)) {
	    extsPtr->right = elemPtr->xHigh.max;
	}
	if (elemPtr->xLow.nValues > 0) {
	    double left;
	    
	    if ((elemPtr->xLow.min <= 0.0) && 
		(elemPtr->axes.x->logScale)) {
		left = Blt_FindElemValuesMinimum(&elemPtr->xLow, DBL_MIN);
	    } else {
		left = elemPtr->xLow.min;
	    }
	    if (left < extsPtr->left) {
		extsPtr->left = left;
	    }
	}
    }
    
    if (elemPtr->yError.nValues > 0) {
	int i;
	
	np = MIN(elemPtr->yError.nValues, np);
	for (i = 0; i < np; i++) {
	    double y;

	    y = elemPtr->y.values[i] + elemPtr->yError.values[i];
	    if (y > extsPtr->bottom) {
		extsPtr->bottom = y;
	    }
	    y = elemPtr->y.values[i] - elemPtr->yError.values[i];
	    if (elemPtr->axes.y->logScale) {
		if (y < 0.0) {
		    y = -y;		/* Mirror negative values, instead of
					 * ignoring them. */
		}
		if ((y > DBL_MIN) && (y < extsPtr->left)) {
		    extsPtr->top = y;
		}
	    } else if (y < extsPtr->top) {
		extsPtr->top = y;
	    }
	}		     
    } else {
	if ((elemPtr->yHigh.nValues > 0) && 
	    (elemPtr->yHigh.max > extsPtr->bottom)) {
	    extsPtr->bottom = elemPtr->yHigh.max;
	}
	if (elemPtr->yLow.nValues > 0) {
	    double top;
	    
	    if ((elemPtr->yLow.min <= 0.0) && 
		(elemPtr->axes.y->logScale)) {
		top = Blt_FindElemValuesMinimum(&elemPtr->yLow, DBL_MIN);
	    } else {
		top = elemPtr->yLow.min;
	    }
	    if (top < extsPtr->top) {
		extsPtr->top = top;
	    }
	}
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * BackgroundChangedProc
 *
 * Results:
 *	None.
 *
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static void
BackgroundChangedProc(ClientData clientData)
{
    Element *elemPtr = clientData;
    Graph *graphPtr;

    graphPtr = elemPtr->obj.graphPtr;
    if (graphPtr->tkwin != NULL) {
	graphPtr->flags |= REDRAW_WORLD;
	Blt_EventuallyRedrawGraph(graphPtr);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * ConfigureLineProc --
 *
 *	Sets up the appropriate configuration parameters in the GC.  It is
 *	assumed the parameters have been previously set by a call to
 *	Blt_ConfigureWidget.
 *
 * Results:
 *	The return value is a standard TCL result.  If TCL_ERROR is returned,
 *	then interp->result contains an error message.
 *
 * Side effects:
 *	Configuration information such as line width, line style, color
 *	etc. get set in a new GC.
 *
 *---------------------------------------------------------------------------
 */
/*ARGSUSED*/
static int
ConfigureLineProc(Graph *graphPtr, Element *basePtr)
{
    LineElement *elemPtr = (LineElement *)basePtr;
    unsigned long gcMask;
    XGCValues gcValues;
    GC newGC;
    Blt_ChainLink link;
    LineStyle *stylePtr;

    if (ConfigurePenProc(graphPtr, (Pen *)&elemPtr->builtinPen) != TCL_OK) {
	return TCL_ERROR;
    }
    /*
     * Point to the static normal/active pens if no external pens have been
     * selected.
     */
    link = Blt_Chain_FirstLink(elemPtr->styles);
    if (link == NULL) {
	link = Blt_Chain_AllocLink(sizeof(LineStyle));
	Blt_Chain_LinkAfter(elemPtr->styles, link, NULL);
    } 
    stylePtr = Blt_Chain_GetValue(link);
    stylePtr->penPtr = NORMALPEN(elemPtr);

    if (elemPtr->fillBg != NULL) {
	Blt_SetBackgroundChangedProc(elemPtr->fillBg, BackgroundChangedProc, 
		elemPtr);
    }
    /*
     * Set the outline GC for this pen: GCForeground is outline color.
     * GCBackground is the fill color (only used for bitmap symbols).
     */
    gcMask = 0;
    if (elemPtr->fillFgColor != NULL) {
	gcMask |= GCForeground;
	gcValues.foreground = elemPtr->fillFgColor->pixel;
    }
    if (elemPtr->fillBgColor != NULL) {
	gcMask |= GCBackground;
	gcValues.background = elemPtr->fillBgColor->pixel;
    }
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (elemPtr->fillGC != NULL) {
	Tk_FreeGC(graphPtr->display, elemPtr->fillGC);
    }
    elemPtr->fillGC = newGC;

    if (Blt_ConfigModified(elemPtr->configSpecs, "-scalesymbols", 
			   (char *)NULL)) {
	elemPtr->flags |= (MAP_ITEM | SCALE_SYMBOL);
    }
    if (Blt_ConfigModified(elemPtr->configSpecs, "-pixels", "-trace", 
	"-*data", "-smooth", "-map*", "-label", "-hide", "-x", "-y", 
	"-areabackground", (char *)NULL)) {
	elemPtr->flags |= MAP_ITEM;
    }
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * ClosestLineProc --
 *
 *	Find the closest point or line segment (if interpolated) to the given
 *	window coordinate in the line element.
 *
 * Results:
 *	Returns the distance of the closest point among other information.
 *
 *---------------------------------------------------------------------------
 */
static void
ClosestLineProc(Graph *graphPtr, Element *basePtr, ClosestSearch *searchPtr)
{
    LineElement *elemPtr = (LineElement *)basePtr;
    int mode;

    mode = searchPtr->mode;
    if (mode == SEARCH_AUTO) {
	LinePen *penPtr;

	penPtr = NORMALPEN(elemPtr);
	mode = SEARCH_POINTS;
	if ((NUMBEROFPOINTS(elemPtr) > 1) && (penPtr->traceWidth > 0)) {
	    mode = SEARCH_TRACES;
	}
    }
    if (mode == SEARCH_POINTS) {
	ClosestPoint(elemPtr, searchPtr);
    } else {
	DistanceProc *distProc;
	int found;

	if (searchPtr->along == SEARCH_X) {
	    distProc = DistanceToXProc;
	} else if (searchPtr->along == SEARCH_Y) {
	    distProc = DistanceToYProc;
	} else {
	    distProc = DistanceToLineProc;
	}
	if (elemPtr->obj.classId == CID_ELEM_STRIP) {
	    found = ClosestStrip(graphPtr, elemPtr, searchPtr, distProc);
	} else {
	    found = ClosestTrace(graphPtr, elemPtr, searchPtr, distProc);
	}
	if ((!found) && (searchPtr->along != SEARCH_BOTH)) {
	    ClosestPoint(elemPtr, searchPtr);
	}
    }
}

/*
 * XDrawLines() points: XMaxRequestSize(dpy) - 3
 * XFillPolygon() points:  XMaxRequestSize(dpy) - 4
 * XDrawSegments() segments:  (XMaxRequestSize(dpy) - 3) / 2
 * XDrawRectangles() rectangles:  (XMaxRequestSize(dpy) - 3) / 2
 * XFillRectangles() rectangles:  (XMaxRequestSize(dpy) - 3) / 2
 * XDrawArcs() or XFillArcs() arcs:  (XMaxRequestSize(dpy) - 3) / 3
 */

#define MAX_DRAWLINES(d)	Blt_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWPOLYGON(d)	Blt_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWSEGMENTS(d)	Blt_MaxRequestSize(d, sizeof(XSegment))
#define MAX_DRAWRECTANGLES(d)	Blt_MaxRequestSize(d, sizeof(XRectangle))
#define MAX_DRAWARCS(d)		Blt_MaxRequestSize(d, sizeof(XArc))

#ifdef WIN32

static void
DrawCircles(
    Display *display,
    Drawable drawable,
    LineElement *elemPtr,
    LinePen *penPtr,
    int nSymbolPts,
    Point2d *symbolPts,
    int radius)
{
    HBRUSH brush, oldBrush;
    HPEN pen, oldPen;
    HDC dc;
    TkWinDCState state;

    if (drawable == None) {
	return;				/* Huh? */
    }
    if ((penPtr->symbol.fillGC == NULL) && 
	(penPtr->symbol.outlineWidth == 0)) {
	return;
    }
    dc = TkWinGetDrawableDC(display, drawable, &state);
    /* SetROP2(dc, tkpWinRopModes[penPtr->symbol.fillGC->function]); */
    if (penPtr->symbol.fillGC != NULL) {
	brush = CreateSolidBrush(penPtr->symbol.fillGC->foreground);
    } else {
	brush = GetStockBrush(NULL_BRUSH);
    }
    if (penPtr->symbol.outlineWidth > 0) {
	pen = Blt_GCToPen(dc, penPtr->symbol.outlineGC);
    } else {
	pen = GetStockPen(NULL_PEN);
    }
    oldPen = SelectPen(dc, pen);
    oldBrush = SelectBrush(dc, brush);
    {
	Point2d *pp, *pend;

	for (pp = symbolPts, pend = pp + nSymbolPts; pp < pend; pp++) {
	    int rndx, rndy;
	    rndx = Round(pp->x), rndy = Round(pp->y);
	    Ellipse(dc, rndx - radius, rndy - radius, rndx + radius + 1, 
		rndy + radius + 1);
	}
    }
    DeleteBrush(SelectBrush(dc, oldBrush));
    DeletePen(SelectPen(dc, oldPen));
    TkWinReleaseDrawableDC(drawable, dc, &state);
}

#else

static void
DrawCircles(Display *display, Drawable drawable, LineElement *elemPtr,
	    LinePen *penPtr, int nSymbolPts, Point2d *symbolPts, int radius)
{
    int i;
    XArc *arcs;				/* Array of arcs (circle) */
    int reqSize;
    int s;
    int count;

    s = radius + radius;
    arcs = Blt_AssertMalloc(nSymbolPts * sizeof(XArc));

    if (elemPtr->symbolInterval > 0) {
	Point2d *pp, *pend;
	XArc *ap;

        ap = arcs;
	count = 0;
	for (pp = symbolPts, pend = pp + nSymbolPts; pp < pend; pp++) {
	    if (DRAW_SYMBOL(elemPtr)) {
		ap->x = Round(pp->x) - radius;
		ap->y = Round(pp->y) - radius;
		ap->width = ap->height = (unsigned short)s;
		ap->angle1 = 0;
		ap->angle2 = 23040;
		ap++, count++;
	    }
	    elemPtr->symbolCounter++;
	}
    } else {
	Point2d *pp, *pend;
	XArc *ap;

        ap = arcs;
	for (pp = symbolPts, pend = pp + nSymbolPts; pp < pend; pp++) {
	    ap->x = Round(pp->x) - radius;
	    ap->y = Round(pp->y) - radius;
	    ap->width = ap->height = (unsigned short)s;
	    ap->angle1 = 0;
	    ap->angle2 = 23040;
	    ap++;
	}
	count = nSymbolPts;
    }
    reqSize = MAX_DRAWARCS(display);
    for (i = 0; i < count; i += reqSize) {
	int n;

	n = ((i + reqSize) > count) ? (count - i) : reqSize;
	if (penPtr->symbol.fillGC != NULL) {
	    XFillArcs(display, drawable, penPtr->symbol.fillGC, arcs + i, n);
	}
	if (penPtr->symbol.outlineWidth > 0) {
	    XDrawArcs(display, drawable, penPtr->symbol.outlineGC, arcs + i, n);
	}
    }
    Blt_Free(arcs);
}

#endif

static void
DrawSquares(Display *display, Drawable drawable, LineElement *elemPtr,
	    LinePen *penPtr, int nSymbolPts, Point2d *symbolPts, int r)
{
    XRectangle *rectangles;
    XRectangle *rp, *rend;
    int reqSize;
    int s, count;

    s = r + r;
    rectangles = Blt_AssertMalloc(nSymbolPts * sizeof(XRectangle));
    if (elemPtr->symbolInterval > 0) {
	Point2d *pp, *pend;
	XRectangle *rp;

	count = 0;
	rp = rectangles;
	for (pp = symbolPts, pend = pp + nSymbolPts; pp < pend; pp++) {
	    if (DRAW_SYMBOL(elemPtr)) {
		rp->x = Round(pp->x) - r;
		rp->y = Round(pp->y) - r;
		rp->width = rp->height = (unsigned short)s;
		rp++, count++;
	    }
	    elemPtr->symbolCounter++;
	}
    } else {
	Point2d *pp, *pend;
	XRectangle *rp;

	rp = rectangles;
	for (pp = symbolPts, pend = pp + nSymbolPts; pp < pend; pp++) {
	    rp->x = Round(pp->x) - r;
	    rp->y = Round(pp->y) - r;
	    rp->width = rp->height = (unsigned short)s;
	    rp++;
	}
	count = nSymbolPts;
    }
    reqSize = MAX_DRAWRECTANGLES(display) - 3;
    for (rp = rectangles, rend = rp + count; rp < rend; rp += reqSize) {
	int n;

	n = rend - rp;
	if (n > reqSize) {
	    n = reqSize;
	}
	if (penPtr->symbol.fillGC != NULL) {
	    XFillRectangles(display, drawable, penPtr->symbol.fillGC, rp, n);
	}
	if (penPtr->symbol.outlineWidth > 0) {
	    XDrawRectangles(display, drawable, penPtr->symbol.outlineGC, rp, n);
	}
    }
    Blt_Free(rectangles);
}

/*
 *---------------------------------------------------------------------------
 *
 * DrawSymbols --
 *
 * 	Draw the symbols centered at the each given x,y coordinate in the array
 * 	of points.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Draws a symbol at each coordinate given.  If active, only those
 *	coordinates which are currently active are drawn.
 *
 *---------------------------------------------------------------------------
 */
static void
DrawSymbols(
    Graph *graphPtr,			/* Graph widget record */
    Drawable drawable,			/* Pixmap or window to draw into */
    LineElement *elemPtr,
    LinePen *penPtr,
    int size,				/* Size of element */
    int nSymbolPts,			/* Number of coordinates in array */
    Point2d *symbolPts)			/* Array of x,y coordinates for line */
{
    XPoint pattern[13];			/* Template for polygon symbols */
    int r1, r2;
    int count;
#define SQRT_PI		1.77245385090552
#define S_RATIO		0.886226925452758

    if (size < 3) {
	if (penPtr->symbol.fillGC != NULL) {
	    Point2d *pp, *endp;
	    XPoint *points, *xpp;
	    
	    xpp = points = Blt_AssertMalloc(nSymbolPts * sizeof(XPoint));
	    for (pp = symbolPts, endp = pp + nSymbolPts; pp < endp; pp++) {
		xpp->x = Round(pp->x);
		xpp->y = Round(pp->y);
		xpp++;
	    }
	    XDrawPoints(graphPtr->display, drawable, penPtr->symbol.fillGC, 
			points, nSymbolPts, CoordModeOrigin);
	    Blt_Free(points);
	}
	return;
    }
    r1 = (int)ceil(size * 0.5);
    r2 = (int)ceil(size * S_RATIO * 0.5);

    switch (penPtr->symbol.type) {
    case SYMBOL_NONE:
	break;

    case SYMBOL_SQUARE:
	DrawSquares(graphPtr->display, drawable, elemPtr, penPtr, nSymbolPts,
	    symbolPts, r2);
	break;

    case SYMBOL_CIRCLE:
	DrawCircles(graphPtr->display, drawable, elemPtr, penPtr, nSymbolPts,
	    symbolPts, r1);
	break;

    case SYMBOL_SPLUS:
    case SYMBOL_SCROSS:
	{
	    XSegment *segments;		/* Array of line segments (splus,
					 * scross) */
	    int i;
	    int reqSize, nSegs;

	    if (penPtr->symbol.type == SYMBOL_SCROSS) {
		r2 = Round((double)r2 * M_SQRT1_2);
		pattern[3].y = pattern[2].x = pattern[0].x = pattern[0].y = -r2;
		pattern[3].x = pattern[2].y = pattern[1].y = pattern[1].x = r2;
	    } else {
		pattern[0].y = pattern[1].y = pattern[2].x = pattern[3].x = 0;
		pattern[0].x = pattern[2].y = -r2;
		pattern[1].x = pattern[3].y = r2;
	    }
	    segments = Blt_AssertMalloc(nSymbolPts * 2 * sizeof(XSegment));
	    if (elemPtr->symbolInterval > 0) {
		Point2d *pp, *endp;
		XSegment *sp;

		sp = segments;
		count = 0;
		for (pp = symbolPts, endp = pp + nSymbolPts; pp < endp; pp++) {
		    if (DRAW_SYMBOL(elemPtr)) {
			int rndx, rndy;
			rndx = Round(pp->x), rndy = Round(pp->y);
			sp->x1 = pattern[0].x + rndx;
			sp->y1 = pattern[0].y + rndy;
			sp->x2 = pattern[1].x + rndx;
			sp->y2 = pattern[1].y + rndy;
			sp++;
			sp->x1 = pattern[2].x + rndx;
			sp->y1 = pattern[2].y + rndy;
			sp->x2 = pattern[3].x + rndx;
			sp->y2 = pattern[3].y + rndy;
			sp++;
			count++;
		    }
		    elemPtr->symbolCounter++;
		}
	    } else {
		Point2d *pp, *endp;
		XSegment *sp;

		sp = segments;
		count = nSymbolPts;
		for (pp = symbolPts, endp = pp + nSymbolPts; pp < endp; pp++) {
		    int rndx, rndy;
		    rndx = Round(pp->x), rndy = Round(pp->y);
		    sp->x1 = pattern[0].x + rndx;
		    sp->y1 = pattern[0].y + rndy;
		    sp->x2 = pattern[1].x + rndx;
		    sp->y2 = pattern[1].y + rndy;
		    sp++;
		    sp->x1 = pattern[2].x + rndx;
		    sp->y1 = pattern[2].y + rndy;
		    sp->x2 = pattern[3].x + rndx;
		    sp->y2 = pattern[3].y + rndy;
		    sp++;
		}
	    }
	    nSegs = count * 2;
	    /* Always draw skinny symbols regardless of the outline width */
	    reqSize = MAX_DRAWSEGMENTS(graphPtr->display);
	    for (i = 0; i < nSegs; i += reqSize) {
		int chunk;

		chunk = ((i + reqSize) > nSegs) ? (nSegs - i) : reqSize;
		XDrawSegments(graphPtr->display, drawable, 
			penPtr->symbol.outlineGC, segments + i, chunk);
	    }
	    Blt_Free(segments);
	}
	break;

    case SYMBOL_PLUS:
    case SYMBOL_CROSS:
	{
	    XPoint *polygon;
	    int d;			/* Small delta for cross/plus
					 * thickness */

	    d = (r2 / 3);

	    /*
	     *
	     *          2   3       The plus/cross symbol is a closed polygon
	     *                      of 12 points. The diagram to the left
	     *    0,12  1   4    5  represents the positions of the points
	     *           x,y        which are computed below. The extra
	     *     11  10   7    6  (thirteenth) point connects the first and
	     *                      last points.
	     *          9   8
	     */

	    pattern[0].x = pattern[11].x = pattern[12].x = -r2;
	    pattern[2].x = pattern[1].x = pattern[10].x = pattern[9].x = -d;
	    pattern[3].x = pattern[4].x = pattern[7].x = pattern[8].x = d;
	    pattern[5].x = pattern[6].x = r2;
	    pattern[2].y = pattern[3].y = -r2;
	    pattern[0].y = pattern[1].y = pattern[4].y = pattern[5].y =
		pattern[12].y = -d;
	    pattern[11].y = pattern[10].y = pattern[7].y = pattern[6].y = d;
	    pattern[9].y = pattern[8].y = r2;

	    if (penPtr->symbol.type == SYMBOL_CROSS) {
		int i;

		/* For the cross symbol, rotate the points by 45 degrees. */
		for (i = 0; i < 12; i++) {
		    double dx, dy;

		    dx = (double)pattern[i].x * M_SQRT1_2;
		    dy = (double)pattern[i].y * M_SQRT1_2;
		    pattern[i].x = Round(dx - dy);
		    pattern[i].y = Round(dx + dy);
		}
		pattern[12] = pattern[0];
	    }
	    polygon = Blt_AssertMalloc(nSymbolPts * 13 * sizeof(XPoint));
	    if (elemPtr->symbolInterval > 0) {
		Point2d *pp, *endp;
		XPoint *xpp;

		count = 0;
		xpp = polygon;
		for (pp = symbolPts, endp = pp + nSymbolPts; pp < endp; pp++) {
		    if (DRAW_SYMBOL(elemPtr)) {
			int i;
			int rndx, rndy;

			rndx = Round(pp->x), rndy = Round(pp->y);
			for (i = 0; i < 13; i++) {
			    xpp->x = pattern[i].x + rndx;
			    xpp->y = pattern[i].y + rndy;
			    xpp++;
			}
			count++;
		    }
		    elemPtr->symbolCounter++;
		}
	    } else {
		Point2d *pp, *endp;
		XPoint *xpp;

		xpp = polygon;
		for (pp = symbolPts, endp = pp + nSymbolPts; pp < endp; pp++) {
		    int i;
		    int rndx, rndy;

		    rndx = Round(pp->x), rndy = Round(pp->y);
		    for (i = 0; i < 13; i++) {
			xpp->x = pattern[i].x + rndx;
			xpp->y = pattern[i].y + rndy;
			xpp++;
		    }
		}
		count = nSymbolPts;
	    }
	    if (penPtr->symbol.fillGC != NULL) {
		int i;
		XPoint *xpp;

		for (xpp = polygon, i = 0; i < count; i++, xpp += 13) {
		    XFillPolygon(graphPtr->display, drawable, 
			penPtr->symbol.fillGC, xpp, 13, Complex, 
			CoordModeOrigin);
		}
	    }
	    if (penPtr->symbol.outlineWidth > 0) {
		int i;
		XPoint *xpp;

		for (xpp = polygon, i = 0; i < count; i++, xpp += 13) {
		    XDrawLines(graphPtr->display, drawable, 
			penPtr->symbol.outlineGC, xpp, 13, CoordModeOrigin);
		}
	    }
	    Blt_Free(polygon);
	}
	break;

    case SYMBOL_DIAMOND:
	{
	    XPoint *polygon;

	    /*
	     *
	     *                      The plus symbol is a closed polygon
	     *            1         of 4 points. The diagram to the left
	     *                      represents the positions of the points
	     *       0,4 x,y  2     which are computed below. The extra
	     *                      (fifth) point connects the first and
	     *            3         last points.
	     *
	     */
	    pattern[1].y = pattern[0].x = -r1;
	    pattern[2].y = pattern[3].x = pattern[0].y = pattern[1].x = 0;
	    pattern[3].y = pattern[2].x = r1;
	    pattern[4] = pattern[0];

	    polygon = Blt_AssertMalloc(nSymbolPts * 5 * sizeof(XPoint));
	    if (elemPtr->symbolInterval > 0) {
		Point2d *pp, *endp;
		XPoint *xpp;

		xpp = polygon;
		count = 0;
		for (pp = symbolPts, endp = pp + nSymbolPts; pp < endp; pp++) {
		    int i;

		    if (DRAW_SYMBOL(elemPtr)) {
			int rndx, rndy;
			
			rndx = Round(pp->x), rndy = Round(pp->y);
			for (i = 0; i < 5; i++) {
			    xpp->x = pattern[i].x + rndx;
			    xpp->y = pattern[i].y + rndy;
			    xpp++;
			}
			count++;
		    }
		    elemPtr->symbolCounter++;
		}
	    } else {
		Point2d *pp, *endp;
		XPoint *xpp;

		xpp = polygon;
		for (pp = symbolPts, endp = pp + nSymbolPts; pp < endp; pp++) {
		    int i;
		    int rndx, rndy;
			
		    rndx = Round(pp->x), rndy = Round(pp->y);
		    for (i = 0; i < 5; i++) {
			xpp->x = pattern[i].x + rndx;
			xpp->y = pattern[i].y + rndy;
			xpp++;
		    }
		}
		count = nSymbolPts;
	    }
	    if (penPtr->symbol.fillGC != NULL) {
		XPoint *xpp;
		int i;

		for (xpp = polygon, i = 0; i < count; i++, xpp += 5) {
		    XFillPolygon(graphPtr->display, drawable, 
			penPtr->symbol.fillGC, xpp, 5, Convex, CoordModeOrigin);

		}
	    }
	    if (penPtr->symbol.outlineWidth > 0) {
		XPoint *xpp;
		int i;

		for (xpp = polygon, i = 0; i < count; i++, xpp += 5) {
		    XDrawLines(graphPtr->display, drawable, 
		       penPtr->symbol.outlineGC, xpp, 5, CoordModeOrigin);
		}
	    }
	    Blt_Free(polygon);
	}
	break;

    case SYMBOL_TRIANGLE:
    case SYMBOL_ARROW:
	{
	    XPoint *polygon;
	    double b;
	    int b2, h1, h2;
#define H_RATIO		1.1663402261671607
#define B_RATIO		1.3467736870885982
#define TAN30		0.57735026918962573
#define COS30		0.86602540378443871

	    b = Round(size * B_RATIO * 0.7);
	    b2 = Round(b * 0.5);
	    h2 = Round(TAN30 * b2);
	    h1 = Round(b2 / COS30);
	    /*
	     *
	     *                      The triangle symbol is a closed polygon
	     *           0,3         of 3 points. The diagram to the left
	     *                      represents the positions of the points
	     *           x,y        which are computed below. The extra
	     *                      (fourth) point connects the first and
	     *      2           1   last points.
	     *
	     */

	    if (penPtr->symbol.type == SYMBOL_ARROW) {
		pattern[3].x = pattern[0].x = 0;
		pattern[3].y = pattern[0].y = h1;
		pattern[1].x = b2;
		pattern[2].y = pattern[1].y = -h2;
		pattern[2].x = -b2;
	    } else {
		pattern[3].x = pattern[0].x = 0;
		pattern[3].y = pattern[0].y = -h1;
		pattern[1].x = b2;
		pattern[2].y = pattern[1].y = h2;
		pattern[2].x = -b2;
	    }
	    polygon = Blt_AssertMalloc(nSymbolPts * 4 * sizeof(XPoint));
	    if (elemPtr->symbolInterval > 0) {
		Point2d *pp, *endp;
		XPoint *xpp;

		xpp = polygon;
		count = 0;
		for (pp = symbolPts, endp = pp + nSymbolPts; pp < endp; pp++) {
		    int i;

		    if (DRAW_SYMBOL(elemPtr)) {
			int rndx, rndy;

			rndx = Round(pp->x), rndy = Round(pp->y);
			for (i = 0; i < 4; i++) {
			    xpp->x = pattern[i].x + rndx;
			    xpp->y = pattern[i].y + rndy;
			    xpp++;
			}
			count++;
		    }
		    elemPtr->symbolCounter++;
		}
	    } else {
		Point2d *pp, *endp;
		XPoint *xpp;

		xpp = polygon;
		for (pp = symbolPts, endp = pp + nSymbolPts; pp < endp; pp++) {
		    int i;
		    int rndx, rndy;

		    rndx = Round(pp->x), rndy = Round(pp->y);
		    for (i = 0; i < 4; i++) {
			xpp->x = pattern[i].x + rndx;
			xpp->y = pattern[i].y + rndy;
			xpp++;
		    }
		}
		count = nSymbolPts;
	    }
	    if (penPtr->symbol.fillGC != NULL) {
		XPoint *xpp;
		int i;

		xpp = polygon;
		for (xpp = polygon, i = 0; i < count; i++, xpp += 4) {
		    XFillPolygon(graphPtr->display, drawable, 
			penPtr->symbol.fillGC, xpp, 4, Convex, CoordModeOrigin);
		}
	    }
	    if (penPtr->symbol.outlineWidth > 0) {
		XPoint *xpp;
		int i;

		xpp = polygon;
		for (xpp = polygon, i = 0; i < count; i++, xpp += 4) {
		    XDrawLines(graphPtr->display, drawable, 
			penPtr->symbol.outlineGC, xpp, 4, CoordModeOrigin);
		}
	    }
	    Blt_Free(polygon);
	}
	break;

    case SYMBOL_IMAGE:
	{
	    int w, h;
	    int dx, dy;

	    Tk_SizeOfImage(penPtr->symbol.image, &w, &h);

	    dx = w / 2;
	    dy = h / 2;
	    if (elemPtr->symbolInterval > 0) {
		Point2d *pp, *endp;

		for (pp = symbolPts, endp = pp + nSymbolPts; pp < endp; pp++) {
		    if (DRAW_SYMBOL(elemPtr)) {
			int x, y;
	    
			x = Round(pp->x) - dx;
			y = Round(pp->y) - dy;
			Tk_RedrawImage(penPtr->symbol.image, 0, 0, w, h, 
				       drawable, x, y);
		    }
		    elemPtr->symbolCounter++;
		}
	    } else {
		Point2d *pp, *endp;

		for (pp = symbolPts, endp = pp + nSymbolPts; pp < endp; pp++) {
		    int x, y;

		    x = Round(pp->x) - dx;
		    y = Round(pp->y) - dy;
		    Tk_RedrawImage(penPtr->symbol.image, 0, 0, w, h, 
				   drawable, x, y);
		}
	    }
	}
	break;

    case SYMBOL_BITMAP:
	{
	    Pixmap bitmap, mask;
	    int w, h, bw, bh;
	    double scale, sx, sy;
	    int dx, dy;

	    Tk_SizeOfBitmap(graphPtr->display, penPtr->symbol.bitmap, &w, &h);
	    mask = None;

	    /*
	     * Compute the size of the scaled bitmap.  Stretch the bitmap to fit
	     * a nxn bounding box.
	     */
	    sx = (double)size / (double)w;
	    sy = (double)size / (double)h;
	    scale = MIN(sx, sy);
	    bw = (int)(w * scale);
	    bh = (int)(h * scale);

	    XSetClipMask(graphPtr->display, penPtr->symbol.outlineGC, None);
	    if (penPtr->symbol.mask != None) {
		mask = Blt_ScaleBitmap(graphPtr->tkwin, penPtr->symbol.mask,
		    w, h, bw, bh);
		XSetClipMask(graphPtr->display, penPtr->symbol.outlineGC, mask);
	    }
	    bitmap = Blt_ScaleBitmap(graphPtr->tkwin, penPtr->symbol.bitmap,
		w, h, bw, bh);
	    if (penPtr->symbol.fillGC == NULL) {
		XSetClipMask(graphPtr->display, penPtr->symbol.outlineGC, 
			     bitmap);
	    }
	    dx = bw / 2;
	    dy = bh / 2;
	    if (elemPtr->symbolInterval > 0) {
		Point2d *pp, *endp;

		for (pp = symbolPts, endp = pp + nSymbolPts; pp < endp; pp++) {
		    if (DRAW_SYMBOL(elemPtr)) {
			int x, y;
	    
			x = Round(pp->x) - dx;
			y = Round(pp->y) - dy;
			if ((penPtr->symbol.fillGC == NULL) || (mask !=None)) {
			    XSetClipOrigin(graphPtr->display,
				penPtr->symbol.outlineGC, x, y);
			}
			XCopyPlane(graphPtr->display, bitmap, drawable,
			    penPtr->symbol.outlineGC, 0, 0, bw, bh, x, y, 1);
		    }
		    elemPtr->symbolCounter++;
		}
	    } else {
		Point2d *pp, *endp;

		for (pp = symbolPts, endp = pp + nSymbolPts; pp < endp; pp++) {
		    int x, y;

		    x = Round(pp->x) - dx;
		    y = Round(pp->y) - dy;
		    if ((penPtr->symbol.fillGC == NULL) || (mask != None)) {
			XSetClipOrigin(graphPtr->display, 
				penPtr->symbol.outlineGC, x, y);
		    }
		    XCopyPlane(graphPtr->display, bitmap, drawable,
			penPtr->symbol.outlineGC, 0, 0, bw, bh, x, y, 1);
		}
	    }
	    Tk_FreePixmap(graphPtr->display, bitmap);
	    if (mask != None) {
		Tk_FreePixmap(graphPtr->display, mask);
	    }
	}
	break;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * DrawSymbolProc --
 *
 * 	Draw the symbol centered at the each given x,y coordinate.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Draws a symbol at the coordinate given.
 *
 *---------------------------------------------------------------------------
 */
static void
DrawSymbolProc(
    Graph *graphPtr,			/* Graph widget record */
    Drawable drawable,			/* Pixmap or window to draw into */
    Element *basePtr,			/* Line element information */
    int x, int y,			/* Center position of symbol */
    int size)				/* Size of symbol. */
{
    LineElement *elemPtr = (LineElement *)basePtr;
    LinePen *penPtr;

    penPtr = NORMALPEN(elemPtr);
    if (penPtr->traceWidth > 0) {
	/*
	 * Draw an extra line offset by one pixel from the previous to give a
	 * thicker appearance.  This is only for the legend entry.  This routine
	 * is never called for drawing the actual line segments.
	 */
	XDrawLine(graphPtr->display, drawable, penPtr->traceGC, x - size, y, 
		x + size, y);
	XDrawLine(graphPtr->display, drawable, penPtr->traceGC, x - size, y + 1,
		x + size, y + 1);
    }
    if (penPtr->symbol.type != SYMBOL_NONE) {
	Point2d point;

	point.x = x, point.y = y;
	DrawSymbols(graphPtr, drawable, elemPtr, penPtr, size, 1, &point);
    }
}

#ifdef WIN32

static void
DrawTraces(
    Graph *graphPtr,
    Drawable drawable,			/* Pixmap or window to draw into */
    LineElement *elemPtr,
    LinePen *penPtr)
{
    Blt_ChainLink link;
    HBRUSH brush, oldBrush;
    HDC dc;
    HPEN pen, oldPen;
    POINT *points;
    TkWinDCState state;
    int np;

    /*  
     * Depending if the line is wide (> 1 pixel), arbitrarily break the line in
     * sections of 100 points.  This bit of weirdness has to do with wide
     * geometric pens.  The longer the polyline, the slower it draws.  The trade
     * off is that we lose dash and cap uniformity for unbearably slow polyline
     * draws.
     */
    if (penPtr->traceGC->line_width > 1) {
	np = 100;
    } else {
	np = Blt_MaxRequestSize(graphPtr->display, sizeof(POINT)) - 1;
    }
    points = Blt_AssertMalloc((np + 1) * sizeof(POINT));

    dc = TkWinGetDrawableDC(graphPtr->display, drawable, &state);

    pen = Blt_GCToPen(dc, penPtr->traceGC);
    oldPen = SelectPen(dc, pen);
    brush = CreateSolidBrush(penPtr->traceGC->foreground);
    oldBrush = SelectBrush(dc, brush);
    SetROP2(dc, tkpWinRopModes[penPtr->traceGC->function]);

    for (link = Blt_Chain_FirstLink(elemPtr->traces); link != NULL;
	link = Blt_Chain_NextLink(link)) {
	POINT *p;
	Trace *tracePtr;
	int count, remaining;

	tracePtr = Blt_Chain_GetValue(link);

	/*
	 * If the trace has to be split into separate XDrawLines calls, then the
	 * end point of the current trace is also the starting point of the new
	 * split.
	 */

	/* Step 1. Convert and draw the first section of the trace.
	 *	   It may contain the entire trace. */

	for (p = points, count = 0; 
	     count < MIN(np, tracePtr->screenPts.length); 
	     count++, p++) {
	    p->x = Round(tracePtr->screenPts.points[count].x);
	    p->y = Round(tracePtr->screenPts.points[count].y);
	}
	Polyline(dc, points, count);

	/* Step 2. Next handle any full-size chunks left. */

	while ((count + np) < tracePtr->screenPts.length) {
	    int j;

	    /* Start with the last point of the previous trace. */
	    points[0].x = points[np - 1].x;
	    points[0].y = points[np - 1].y;

	    for (p = points + 1, j = 0; j < np; j++, count++, p++) {
		p->x = Round(tracePtr->screenPts.points[count].x);
		p->y = Round(tracePtr->screenPts.points[count].y);
	    }
	    Polyline(dc, points, np + 1);
	}
	
	/* Step 3. Convert and draw the remaining points. */

	remaining = tracePtr->screenPts.length - count;
	if (remaining > 0) {
	    /* Start with the last point of the previous trace. */
	    points[0].x = points[np - 1].x;
	    points[0].y = points[np - 1].y;

	    for (p = points + 1; count < tracePtr->screenPts.length; 
		 count++, p++) {
		p->x = Round(tracePtr->screenPts.points[count].x);
		p->y = Round(tracePtr->screenPts.points[count].y);
	    }	    
	    Polyline(dc, points, remaining + 1);
	}
    }
    Blt_Free(points);
    DeletePen(SelectPen(dc, oldPen));
    DeleteBrush(SelectBrush(dc, oldBrush));
    TkWinReleaseDrawableDC(drawable, dc, &state);
}

#else

static void
DrawTraces(Graph *graphPtr, Drawable drawable, LineElement *elemPtr, 
	   LinePen *penPtr)
{
    Blt_ChainLink link;
    XPoint *points;
    int np;

    np = Blt_MaxRequestSize(graphPtr->display, sizeof(XPoint)) - 1;
    points = Blt_AssertMalloc((np + 1) * sizeof(XPoint));
	    
    for (link = Blt_Chain_FirstLink(elemPtr->traces); link != NULL;
	link = Blt_Chain_NextLink(link)) {
	XPoint *xpp;
	Trace *tracePtr;
	int remaining, count;
	int n;

	tracePtr = Blt_Chain_GetValue(link);

	/*
	 * If the trace has to be split into separate XDrawLines calls, then the
	 * end point of the current trace is also the starting point of the new
	 * split.
	 */
	/* Step 1. Convert and draw the first section of the trace.
	 *	   It may contain the entire trace. */

	n = MIN(np, tracePtr->screenPts.length); 
	for (xpp = points, count = 0; count < n; count++, xpp++) {
	    xpp->x = Round(tracePtr->screenPts.points[count].x);
	    xpp->y = Round(tracePtr->screenPts.points[count].y);
	}
	XDrawLines(graphPtr->display, drawable, penPtr->traceGC, points, 
	   count, CoordModeOrigin);

	/* Step 2. Next handle any full-size chunks left. */

	while ((count + np) < tracePtr->screenPts.length) {
	    int j;

	    /* Start with the last point of the previous trace. */
	    points[0].x = points[np - 1].x;
	    points[0].y = points[np - 1].y;
	    
	    for (xpp = points + 1, j = 0; j < np; j++, count++, xpp++) {
		xpp->x = Round(tracePtr->screenPts.points[count].x);
		xpp->y = Round(tracePtr->screenPts.points[count].y);
	    }
	    XDrawLines(graphPtr->display, drawable, penPtr->traceGC, points, 
		       np + 1, CoordModeOrigin);
	}
	
	/* Step 3. Convert and draw the remaining points. */

	remaining = tracePtr->screenPts.length - count;
	if (remaining > 0) {
	    /* Start with the last point of the previous trace. */
	    points[0].x = points[np - 1].x;
	    points[0].y = points[np - 1].y;
	    for (xpp = points + 1; count < tracePtr->screenPts.length; count++, 
		     xpp++) {
		xpp->x = Round(tracePtr->screenPts.points[count].x);
		xpp->y = Round(tracePtr->screenPts.points[count].y);
	    }	    
	    XDrawLines(graphPtr->display, drawable, penPtr->traceGC, points, 
		remaining + 1, CoordModeOrigin);
	}
    }
    Blt_Free(points);
}
#endif /* WIN32 */

static void
DrawValues(Graph *graphPtr, Drawable drawable, LineElement *elemPtr, 
	   LinePen *penPtr, int length, Point2d *points, int *map)
{
    Point2d *pp, *endp;
    double *xval, *yval;
    const char *fmt;
    char string[TCL_DOUBLE_SPACE * 2 + 2];
    int count;
    
    fmt = penPtr->valueFormat;
    if (fmt == NULL) {
	fmt = "%g";
    }
    count = 0;
    xval = elemPtr->x.values, yval = elemPtr->y.values;
    for (pp = points, endp = points + length; pp < endp; pp++) {
	double x, y;

	x = xval[map[count]];
	y = yval[map[count]];
	count++;
	if (penPtr->valueShow == SHOW_X) {
	    sprintf_s(string, TCL_DOUBLE_SPACE, fmt, x); 
	} else if (penPtr->valueShow == SHOW_Y) {
	    sprintf_s(string, TCL_DOUBLE_SPACE, fmt, y); 
	} else if (penPtr->valueShow == SHOW_BOTH) {
	    sprintf_s(string, TCL_DOUBLE_SPACE, fmt, x);
	    strcat(string, ",");
	    sprintf_s(string + strlen(string), TCL_DOUBLE_SPACE, fmt, y);
	}
	Blt_DrawText(graphPtr->tkwin, drawable, string, &penPtr->valueStyle, 
		Round(pp->x), Round(pp->y));
    } 
}

/*
 *---------------------------------------------------------------------------
 *
 * DrawActiveLineProc --
 *
 *	Draws the connected line(s) representing the element. If the line is
 *	made up of non-line symbols and the line width parameter has been set
 *	(linewidth > 0), the element will also be drawn as a line (with the
 *	linewidth requested).  The line may consist of separate line segments.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	X drawing commands are output.
 *
 *---------------------------------------------------------------------------
 */
static void
DrawActiveLineProc(Graph *graphPtr, Drawable drawable, Element *basePtr)
{
    LineElement *elemPtr = (LineElement *)basePtr;
    LinePen *penPtr = (LinePen *)elemPtr->activePenPtr;
    int symbolSize;

    if (penPtr == NULL) {
	return;
    }
    symbolSize = ScaleSymbol(elemPtr, penPtr->symbol.size);

    /* 
     * nActiveIndices 
     *	  > 0		Some points are active.  Uses activeArr.
     *	  < 0		All points are active.
     *    == 0		No points are active.
     */
    if (elemPtr->nActiveIndices > 0) {
	if (elemPtr->flags & ACTIVE_PENDING) {
	    MapActiveSymbols(graphPtr, elemPtr);
	}
	if (penPtr->symbol.type != SYMBOL_NONE) {
	    DrawSymbols(graphPtr, drawable, elemPtr, penPtr, symbolSize,
		elemPtr->activePts.length, elemPtr->activePts.points);
	}
	if (penPtr->valueShow != SHOW_NONE) {
	    DrawValues(graphPtr, drawable, elemPtr, penPtr, 
		elemPtr->activePts.length,
		elemPtr->activePts.points, 
		elemPtr->activePts.map);
	}
    } else if (elemPtr->nActiveIndices < 0) { 
	if (penPtr->traceWidth > 0) {
	    if (elemPtr->lines.length > 0) {
		Blt_Draw2DSegments(graphPtr->display, drawable, 
			penPtr->traceGC, elemPtr->lines.segments, 
			elemPtr->lines.length);
	    } else if (Blt_Chain_GetLength(elemPtr->traces) > 0) {
		DrawTraces(graphPtr, drawable, elemPtr, penPtr);
	    }
	}
	if (penPtr->symbol.type != SYMBOL_NONE) {
	    DrawSymbols(graphPtr, drawable, elemPtr, penPtr, symbolSize,
		elemPtr->symbolPts.length, elemPtr->symbolPts.points);
	}
	if (penPtr->valueShow != SHOW_NONE) {
	    DrawValues(graphPtr, drawable, elemPtr, penPtr, 
		elemPtr->symbolPts.length, elemPtr->symbolPts.points, 
		elemPtr->symbolPts.map);
	}
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * DrawNormalLine --
 *
 *	Draws the connected line(s) representing the element. If the line is
 *	made up of non-line symbols and the line width parameter has been set
 *	(linewidth > 0), the element will also be drawn as a line (with the
 *	linewidth requested).  The line may consist of separate line segments.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	X drawing commands are output.
 *
 *---------------------------------------------------------------------------
 */
static void
DrawNormalLineProc(Graph *graphPtr, Drawable drawable, Element *basePtr)
{
    LineElement *elemPtr = (LineElement *)basePtr;
    Blt_ChainLink link;
    unsigned int count;

    /* Fill area under the curve */
    if (elemPtr->fillPts != NULL) {
	XPoint *points;
	Point2d *endp, *pp;

	points = Blt_AssertMalloc(sizeof(XPoint) * elemPtr->nFillPts);
	count = 0;
	for (pp = elemPtr->fillPts, endp = pp + elemPtr->nFillPts; 
	     pp < endp; pp++) {
	    points[count].x = Round(pp->x);
	    points[count].y = Round(pp->y);
	    count++;
	}
	if (elemPtr->fillBg != NULL) {
	    Blt_SetBackgroundOrigin(graphPtr->tkwin, elemPtr->fillBg, 0, 0);
	    Blt_FillBackgroundPolygon(graphPtr->tkwin, drawable, 
		elemPtr->fillBg, points, elemPtr->nFillPts, 0, TK_RELIEF_FLAT);
	}
	Blt_Free(points);
    }

    /* Lines: stripchart segments or graph traces. */
    if (elemPtr->lines.length > 0) {
	for (link = Blt_Chain_FirstLink(elemPtr->styles); 
	     link != NULL; link = Blt_Chain_NextLink(link)) {
	    LineStyle *stylePtr;
	    LinePen *penPtr;

	    stylePtr = Blt_Chain_GetValue(link);
	    penPtr = (LinePen *)stylePtr->penPtr;
	    if ((stylePtr->lines.length > 0) && 
		(penPtr->errorBarLineWidth > 0)) {
		Blt_Draw2DSegments(graphPtr->display, drawable, penPtr->traceGC,
			stylePtr->lines.segments, stylePtr->lines.length);
	    }
	}
    } else {
	LinePen *penPtr;

	penPtr = NORMALPEN(elemPtr);
	if ((Blt_Chain_GetLength(elemPtr->traces) > 0) && 
	    (penPtr->traceWidth > 0)) {
	    DrawTraces(graphPtr, drawable, elemPtr, penPtr);
	}
    }

    if (elemPtr->reqMaxSymbols > 0) {
	int total;

	total = 0;
	for (link = Blt_Chain_FirstLink(elemPtr->styles); 
	     link != NULL; link = Blt_Chain_NextLink(link)) {
	    LineStyle *stylePtr;

	    stylePtr = Blt_Chain_GetValue(link);
	    total += stylePtr->symbolPts.length;
	}
	elemPtr->symbolInterval = total / elemPtr->reqMaxSymbols;
	elemPtr->symbolCounter = 0;
    }

    /* Symbols, error bars, values. */

    count = 0;
    for (link = Blt_Chain_FirstLink(elemPtr->styles); link != NULL;
	 link = Blt_Chain_NextLink(link)) {
	LineStyle *stylePtr;
	LinePen *penPtr;

	stylePtr = Blt_Chain_GetValue(link);
	penPtr = (LinePen *)stylePtr->penPtr;
	if ((stylePtr->xeb.length > 0) && (penPtr->errorBarShow & SHOW_X)) {
	    Blt_Draw2DSegments(graphPtr->display, drawable, penPtr->errorBarGC, 
		stylePtr->xeb.segments, stylePtr->xeb.length);
	}
	if ((stylePtr->yeb.length > 0) && (penPtr->errorBarShow & SHOW_Y)) {
	    Blt_Draw2DSegments(graphPtr->display, drawable, penPtr->errorBarGC, 
		stylePtr->yeb.segments, stylePtr->yeb.length);
	}
	if ((stylePtr->symbolPts.length > 0) && 
	    (penPtr->symbol.type != SYMBOL_NONE)) {
	    DrawSymbols(graphPtr, drawable, elemPtr, penPtr, 
		stylePtr->symbolSize, stylePtr->symbolPts.length, 
		stylePtr->symbolPts.points);
	}
	if (penPtr->valueShow != SHOW_NONE) {
	    DrawValues(graphPtr, drawable, elemPtr, penPtr, 
		stylePtr->symbolPts.length, stylePtr->symbolPts.points, 
		elemPtr->symbolPts.map + count);
	}
	count += stylePtr->symbolPts.length;
    }
    elemPtr->symbolInterval = 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * GetSymbolPostScriptInfo --
 *
 *	Set up the PostScript environment with the macros and attributes needed
 *	to draw the symbols of the element.
 *
 * Results:
 *	None.
 *
 *---------------------------------------------------------------------------
 */
static void
GetSymbolPostScriptInfo(
    Graph *graphPtr,
    Blt_Ps ps,
    LinePen *penPtr,
    int size)
{
    XColor *outlineColor, *fillColor, *defaultColor;

    /* Set line and foreground attributes */
    outlineColor = penPtr->symbol.outlineColor;
    fillColor = penPtr->symbol.fillColor;
    defaultColor = penPtr->traceColor;

    if (fillColor == COLOR_DEFAULT) {
	fillColor = defaultColor;
    }
    if (outlineColor == COLOR_DEFAULT) {
	outlineColor = defaultColor;
    }
    if (penPtr->symbol.type == SYMBOL_NONE) {
	Blt_Ps_XSetLineAttributes(ps, defaultColor, penPtr->traceWidth + 2,
		 &penPtr->traceDashes, CapButt, JoinMiter);
    } else {
	Blt_Ps_XSetLineWidth(ps, penPtr->symbol.outlineWidth);
	Blt_Ps_XSetDashes(ps, (Blt_Dashes *)NULL);
    }

    /*
     * Build a PostScript procedure to draw the symbols.  For bitmaps, paint
     * both the bitmap and its mask. Otherwise fill and stroke the path formed
     * already.
     */
    Blt_Ps_Append(ps, "\n/DrawSymbolProc {\n");
    switch (penPtr->symbol.type) {
    case SYMBOL_NONE:
	break;				/* Do nothing */
    case SYMBOL_BITMAP:
	{
	    int w, h;
	    double sx, sy, scale;

	    /*
	     * Compute how much to scale the bitmap.  Don't let the scaled
	     * bitmap exceed the bounding square for the symbol.
	     */
	    Tk_SizeOfBitmap(graphPtr->display, penPtr->symbol.bitmap, &w, &h);
	    sx = (double)size / (double)w;
	    sy = (double)size / (double)h;
	    scale = MIN(sx, sy);

	    if ((penPtr->symbol.mask != None) && (fillColor != NULL)) {
		Blt_Ps_VarAppend(ps, "\n  % Bitmap mask is \"",
		    Tk_NameOfBitmap(graphPtr->display, penPtr->symbol.mask),
		    "\"\n\n  ", (char *)NULL);
		Blt_Ps_XSetBackground(ps, fillColor);
		Blt_Ps_DrawBitmap(ps, graphPtr->display, penPtr->symbol.mask, 
			scale, scale);
	    }
	    Blt_Ps_VarAppend(ps, "\n  % Bitmap symbol is \"",
		Tk_NameOfBitmap(graphPtr->display, penPtr->symbol.bitmap),
		"\"\n\n  ", (char *)NULL);
	    Blt_Ps_XSetForeground(ps, outlineColor);
	    Blt_Ps_DrawBitmap(ps, graphPtr->display, penPtr->symbol.bitmap, 
		scale, scale);
	}
	break;
    default:
	if (fillColor != NULL) {
	    Blt_Ps_Append(ps, "  ");
	    Blt_Ps_XSetBackground(ps, fillColor);
	    Blt_Ps_Append(ps, "  gsave fill grestore\n");
	}
	if ((outlineColor != NULL) && (penPtr->symbol.outlineWidth > 0)) {
	    Blt_Ps_Append(ps, "  ");
	    Blt_Ps_XSetForeground(ps, outlineColor);
	    Blt_Ps_Append(ps, "  stroke\n");
	}
	break;
    }
    Blt_Ps_Append(ps, "} def\n\n");
}

/*
 *---------------------------------------------------------------------------
 *
 * SymbolsToPostScript --
 *
 * 	Draw a symbol centered at the given x,y window coordinate based upon the
 * 	element symbol type and size.
 *
 * Results:
 *	None.
 *
 * Problems:
 *	Most notable is the round-off errors generated when calculating the
 *	centered position of the symbol.
 *
 *---------------------------------------------------------------------------
 */
static void
SymbolsToPostScript(
    Graph *graphPtr,
    Blt_Ps ps,
    LinePen *penPtr,
    int size,
    int nSymbolPts,
    Point2d *symbolPts)
{
    double symbolSize;
    static const char *symbolMacros[] =
    {
	"Li", "Sq", "Ci", "Di", "Pl", "Cr", "Sp", "Sc", "Tr", "Ar", "Bm", 
	(char *)NULL,
    };
    GetSymbolPostScriptInfo(graphPtr, ps, penPtr, size);

    symbolSize = (double)size;
    switch (penPtr->symbol.type) {
    case SYMBOL_SQUARE:
    case SYMBOL_CROSS:
    case SYMBOL_PLUS:
    case SYMBOL_SCROSS:
    case SYMBOL_SPLUS:
	symbolSize = (double)Round(size * S_RATIO);
	break;
    case SYMBOL_TRIANGLE:
    case SYMBOL_ARROW:
	symbolSize = (double)Round(size * 0.7);
	break;
    case SYMBOL_DIAMOND:
	symbolSize = (double)Round(size * M_SQRT1_2);
	break;

    default:
	break;
    }
    {
	Point2d *pp, *endp;

	for (pp = symbolPts, endp = symbolPts + nSymbolPts; pp < endp; pp++) {
	    Blt_Ps_Format(ps, "%g %g %g %s\n", pp->x, pp->y, 
		symbolSize, symbolMacros[penPtr->symbol.type]);
	}
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * SymbolToPostScriptProc --
 *
 * 	Draw the symbol centered at the each given x,y coordinate.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Draws a symbol at the coordinate given.
 *
 *---------------------------------------------------------------------------
 */
static void
SymbolToPostScriptProc(
    Graph *graphPtr,			/* Graph widget record */
    Blt_Ps ps,
    Element *basePtr,			/* Line element information */
    double x, double y,			/* Center position of symbol */
    int size)				/* Size of element */
{
    LineElement *elemPtr = (LineElement *)basePtr;
    LinePen *penPtr;

    penPtr = NORMALPEN(elemPtr);
    if (penPtr->traceWidth > 0) {
	/*
	 * Draw an extra line offset by one pixel from the previous to give a
	 * thicker appearance.  This is only for the legend entry.  This routine
	 * is never called for drawing the actual line segments.
	 */
	Blt_Ps_XSetLineAttributes(ps, penPtr->traceColor,
	    penPtr->traceWidth, &penPtr->traceDashes, CapButt, JoinMiter);
	Blt_Ps_Format(ps, "%g %g %d Li\n", x, y, size + size);
    }
    if (penPtr->symbol.type != SYMBOL_NONE) {
	Point2d point;

	point.x = x, point.y = y;
	SymbolsToPostScript(graphPtr, ps, penPtr, size, 1, &point);
    }
}

static void
SetLineAttributes(Blt_Ps ps, LinePen *penPtr)
{
    /* Set the attributes of the line (color, dashes, linewidth) */
    Blt_Ps_XSetLineAttributes(ps, penPtr->traceColor,
	penPtr->traceWidth, &penPtr->traceDashes, CapButt, JoinMiter);
    if ((LineIsDashed(penPtr->traceDashes)) && 
	(penPtr->traceOffColor != NULL)) {
	Blt_Ps_Append(ps, "/DashesProc {\n  gsave\n    ");
	Blt_Ps_XSetBackground(ps, penPtr->traceOffColor);
	Blt_Ps_Append(ps, "    ");
	Blt_Ps_XSetDashes(ps, (Blt_Dashes *)NULL);
	Blt_Ps_Append(ps, "stroke\n  grestore\n} def\n");
    } else {
	Blt_Ps_Append(ps, "/DashesProc {} def\n");
    }
}

static void
TracesToPostScript(Blt_Ps ps, LineElement *elemPtr, LinePen *penPtr)
{
    Blt_ChainLink link;

    SetLineAttributes(ps, penPtr);
    for (link = Blt_Chain_FirstLink(elemPtr->traces); link != NULL;
	link = Blt_Chain_NextLink(link)) {
	Trace *tracePtr;

	tracePtr = Blt_Chain_GetValue(link);
	if (tracePtr->screenPts.length > 0) {
	    Blt_Ps_Append(ps, "% start trace\n");
	    Blt_Ps_DrawPolyline(ps, tracePtr->screenPts.points, 
		tracePtr->screenPts.length);
	    Blt_Ps_Append(ps, "% end trace\n");
	}
    }
}


static void
ValuesToPostScript(Blt_Ps ps, LineElement *elemPtr, LinePen *penPtr,
		   int nSymbolPts, Point2d *symbolPts, int *pointToData)
{
    Point2d *pp, *endp;
    int count;
    char string[TCL_DOUBLE_SPACE * 2 + 2];
    const char *fmt;
    
    fmt = penPtr->valueFormat;
    if (fmt == NULL) {
	fmt = "%g";
    }
    count = 0;
    for (pp = symbolPts, endp = symbolPts + nSymbolPts; pp < endp; pp++) {
	double x, y;

	x = elemPtr->x.values[pointToData[count]];
	y = elemPtr->y.values[pointToData[count]];
	count++;
	if (penPtr->valueShow == SHOW_X) {
	    sprintf_s(string, TCL_DOUBLE_SPACE, fmt, x); 
	} else if (penPtr->valueShow == SHOW_Y) {
	    sprintf_s(string, TCL_DOUBLE_SPACE, fmt, y); 
	} else if (penPtr->valueShow == SHOW_BOTH) {
	    sprintf_s(string, TCL_DOUBLE_SPACE, fmt, x);
	    strcat(string, ",");
	    sprintf_s(string + strlen(string), TCL_DOUBLE_SPACE, fmt, y);
	}
	Blt_Ps_DrawText(ps, string, &penPtr->valueStyle, pp->x, pp->y);
    } 
}


/*
 *---------------------------------------------------------------------------
 *
 * ActiveLineToPostScript --
 *
 *	Generates PostScript commands to draw as "active" the points (symbols)
 *	and or line segments (trace) representing the element.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	PostScript pen width, dashes, and color settings are changed.
 *
 *---------------------------------------------------------------------------
 */
static void
ActiveLineToPostScriptProc(Graph *graphPtr, Blt_Ps ps, Element *basePtr)
{
    LineElement *elemPtr = (LineElement *)basePtr;
    LinePen *penPtr = (LinePen *)elemPtr->activePenPtr;
    int symbolSize;

    if (penPtr == NULL) {
	return;
    }
    symbolSize = ScaleSymbol(elemPtr, penPtr->symbol.size);
    if (elemPtr->nActiveIndices > 0) {
	if (elemPtr->flags & ACTIVE_PENDING) {
	    MapActiveSymbols(graphPtr, elemPtr);
	}
	if (penPtr->symbol.type != SYMBOL_NONE) {
	    SymbolsToPostScript(graphPtr, ps, penPtr, symbolSize,
		elemPtr->activePts.length, elemPtr->activePts.points);
	}
	if (penPtr->valueShow != SHOW_NONE) {
	    ValuesToPostScript(ps, elemPtr, penPtr, elemPtr->activePts.length,
		       elemPtr->activePts.points, elemPtr->activePts.map);
	}
    } else if (elemPtr->nActiveIndices < 0) {
	if (penPtr->traceWidth > 0) {
	    if (elemPtr->lines.length > 0) {
		SetLineAttributes(ps, penPtr);
		Blt_Ps_Draw2DSegments(ps, elemPtr->lines.segments, 
			elemPtr->lines.length);
	    }
	    if (Blt_Chain_GetLength(elemPtr->traces) > 0) {
		TracesToPostScript(ps, elemPtr, (LinePen *)penPtr);
	    }
	}
	if (penPtr->symbol.type != SYMBOL_NONE) {
	    SymbolsToPostScript(graphPtr, ps, penPtr, symbolSize,
		elemPtr->symbolPts.length, elemPtr->symbolPts.points);
	}
	if (penPtr->valueShow != SHOW_NONE) {
	    ValuesToPostScript(ps, elemPtr, penPtr, elemPtr->symbolPts.length, 
		elemPtr->symbolPts.points, elemPtr->symbolPts.map);
	}
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * NormalLineToPostScriptProc --
 *
 *	Similar to the DrawLine procedure, prints PostScript related commands to
 *	form the connected line(s) representing the element.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	PostScript pen width, dashes, and color settings are changed.
 *
 *---------------------------------------------------------------------------
 */
static void
NormalLineToPostScriptProc(Graph *graphPtr, Blt_Ps ps, Element *basePtr)
{
    LineElement *elemPtr = (LineElement *)basePtr;
    Blt_ChainLink link;
    unsigned int count;

    /* Draw fill area */
    if (elemPtr->fillPts != NULL) {
	/* Create a path to use for both the polygon and its outline. */
	Blt_Ps_Append(ps, "% start fill area\n");
	Blt_Ps_Polyline(ps, elemPtr->fillPts, elemPtr->nFillPts);

	/* If the background fill color was specified, draw the polygon in a
	 * solid fashion with that color.  */
	if (elemPtr->fillBgColor != NULL) {
	    Blt_Ps_XSetBackground(ps, elemPtr->fillBgColor);
	    Blt_Ps_Append(ps, "gsave fill grestore\n");
	}
	Blt_Ps_XSetForeground(ps, elemPtr->fillFgColor);
	if (elemPtr->fillBg != NULL) {
	    Blt_Ps_Append(ps, "gsave fill grestore\n");
	    /* TBA: Transparent tiling is the hard part. */
	} else {
	    Blt_Ps_Append(ps, "gsave fill grestore\n");
	}
	Blt_Ps_Append(ps, "% end fill area\n");
    }

    /* Draw lines (strip chart) or traces (xy graph) */
    if (elemPtr->lines.length > 0) {
	for (link = Blt_Chain_FirstLink(elemPtr->styles); link != NULL; 
	     link = Blt_Chain_NextLink(link)) {
	    LineStyle *stylePtr;
	    LinePen *penPtr;

	    stylePtr = Blt_Chain_GetValue(link);
	    penPtr = (LinePen *)stylePtr->penPtr;
	    if ((stylePtr->lines.length > 0) && (penPtr->traceWidth > 0)) {
		SetLineAttributes(ps, penPtr);
		Blt_Ps_Append(ps, "% start segments\n");
		Blt_Ps_Draw2DSegments(ps, stylePtr->lines.segments, 
			stylePtr->lines.length);
		Blt_Ps_Append(ps, "% end segments\n");
	    }
	}
    } else {
	LinePen *penPtr;

	penPtr = NORMALPEN(elemPtr);
	if ((Blt_Chain_GetLength(elemPtr->traces) > 0) && 
	    (penPtr->traceWidth > 0)) {
	    TracesToPostScript(ps, elemPtr, penPtr);
	}
    }

    /* Draw symbols, error bars, values. */

    count = 0;
    for (link = Blt_Chain_FirstLink(elemPtr->styles); link != NULL;
	 link = Blt_Chain_NextLink(link)) {
	LineStyle *stylePtr;
	LinePen *penPtr;
	XColor *colorPtr;

	stylePtr = Blt_Chain_GetValue(link);
	penPtr = (LinePen *)stylePtr->penPtr;
	colorPtr = penPtr->errorBarColor;
	if (colorPtr == COLOR_DEFAULT) {
	    colorPtr = penPtr->traceColor;
	}
	if ((stylePtr->xeb.length > 0) && (penPtr->errorBarShow & SHOW_X)) {
	    Blt_Ps_XSetLineAttributes(ps, colorPtr, penPtr->errorBarLineWidth, 
		NULL, CapButt, JoinMiter);
	    Blt_Ps_Draw2DSegments(ps, stylePtr->xeb.segments, 
		stylePtr->xeb.length);
	}
	if ((stylePtr->yeb.length > 0) && (penPtr->errorBarShow & SHOW_Y)) {
	    Blt_Ps_XSetLineAttributes(ps, colorPtr, penPtr->errorBarLineWidth, 
		NULL, CapButt, JoinMiter);
	    Blt_Ps_Draw2DSegments(ps, stylePtr->yeb.segments,
		stylePtr->yeb.length);
	}
	if ((stylePtr->symbolPts.length > 0) &&
	    (penPtr->symbol.type != SYMBOL_NONE)) {
	    SymbolsToPostScript(graphPtr, ps, penPtr, stylePtr->symbolSize, 
		stylePtr->symbolPts.length, stylePtr->symbolPts.points);
	}
	if (penPtr->valueShow != SHOW_NONE) {
	    ValuesToPostScript(ps, elemPtr, penPtr, stylePtr->symbolPts.length, 
		stylePtr->symbolPts.points, elemPtr->symbolPts.map + count);
	}
	count += stylePtr->symbolPts.length;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * DestroyLineProc --
 *
 *	Release memory and resources allocated for the line element.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Everything associated with the line element is freed up.
 *
 *---------------------------------------------------------------------------
 */

static void
DestroyLineProc(Graph *graphPtr, Element *basePtr)
{
    LineElement *elemPtr = (LineElement *)basePtr;

    DestroyPenProc(graphPtr, (Pen *)&elemPtr->builtinPen);
    if (elemPtr->activePenPtr != NULL) {
	Blt_FreePen((Pen *)elemPtr->activePenPtr);
    }
    ResetLine(elemPtr);
    if (elemPtr->styles != NULL) {
	Blt_FreeStylePalette(elemPtr->styles);
	Blt_Chain_Destroy(elemPtr->styles);
    }
    if (elemPtr->activeIndices != NULL) {
	Blt_Free(elemPtr->activeIndices);
    }
    if (elemPtr->fillPts != NULL) {
	Blt_Free(elemPtr->fillPts);
    }
    if (elemPtr->fillGC != NULL) {
	Tk_FreeGC(graphPtr->display, elemPtr->fillGC);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * Blt_LineElement --
 *
 *	Allocate memory and initialize methods for the new line element.
 *
 * Results:
 *	The pointer to the newly allocated element structure is returned.
 *
 * Side effects:
 *	Memory is allocated for the line element structure.
 *
 *---------------------------------------------------------------------------
 */

static ElementProcs lineProcs =
{
    ClosestLineProc,			/* Finds the closest element/data
					 * point */
    ConfigureLineProc,			/* Configures the element. */
    DestroyLineProc,			/* Destroys the element. */
    DrawActiveLineProc,			/* Draws active element */
    DrawNormalLineProc,			/* Draws normal element */
    DrawSymbolProc,			/* Draws the element symbol. */
    GetLineExtentsProc,			/* Find the extents of the element's
					 * data. */
    ActiveLineToPostScriptProc,		/* Prints active element. */
    NormalLineToPostScriptProc,		/* Prints normal element. */
    SymbolToPostScriptProc,		/* Prints the line's symbol. */
    MapLineProc				/* Compute element's screen
					 * coordinates. */
};

Element *
Blt_LineElement(Graph *graphPtr, const char *name, ClassId classId)
{
    LineElement *elemPtr;

    elemPtr = Blt_AssertCalloc(1, sizeof(LineElement));
    elemPtr->procsPtr = &lineProcs;
    if (classId == CID_ELEM_LINE) {
	elemPtr->configSpecs = lineElemConfigSpecs;
    } else {
	elemPtr->configSpecs = stripElemConfigSpecs;
    }
    elemPtr->obj.name = Blt_AssertStrdup(name);
    Blt_GraphSetObjectClass(&elemPtr->obj, classId);
    elemPtr->flags = SCALE_SYMBOL;
    elemPtr->obj.graphPtr = graphPtr;
    /* By default an element's name and label are the same. */
    elemPtr->label = Blt_AssertStrdup(name);
    elemPtr->legendRelief = TK_RELIEF_FLAT;
    elemPtr->penDir = PEN_BOTH_DIRECTIONS;
    elemPtr->styles = Blt_Chain_Create();
    elemPtr->builtinPenPtr = &elemPtr->builtinPen;
    elemPtr->reqSmooth = PEN_SMOOTH_LINEAR;
    InitLinePen(elemPtr->builtinPenPtr);
    bltLineStylesOption.clientData = (ClientData)sizeof(LineStyle);
    return (Element *)elemPtr;
}

#ifdef notdef
/*
 *---------------------------------------------------------------------------
 *
 * MapLineProc --
 *
 *	Calculates the actual window coordinates of the line element.  The
 *	window coordinates are saved in an allocated point array.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is (re)allocated for the point array.
 *
 *---------------------------------------------------------------------------
 */
static void
MapLineProc(Graph *graphPtr, Element *basePtr)
{
    LineElement *elemPtr = (LineElement *)basePtr;
    MapInfo mi;
    int size, np;
    LineStyle **styleMap;
    Blt_ChainLink link;
    
    ResetLine(elemPtr);
    np = NUMBEROFPOINTS(elemPtr);
    if (np < 1) {
	return;				/* No data points */
    }
    GetScreenPoints(graphPtr, elemPtr, &mi);
    MapSymbols(graphPtr, elemPtr, &mi);

    if ((elemPtr->flags & ACTIVE_PENDING) && (elemPtr->nActiveIndices > 0)) {
	MapActiveSymbols(graphPtr, elemPtr);
    }
    /*
     * Map connecting line segments if they are to be displayed.
     */
    elemPtr->smooth = elemPtr->reqSmooth;
    if ((np > 1) && ((graphPtr->classId == CID_ELEM_STRIP) ||
	    (elemPtr->builtinPen.traceWidth > 0))) {

	/*
	 * Do smoothing if necessary.  This can extend the coordinate array,
	 * so both mi.points and mi.nPoints may change.
	 */
	switch (elemPtr->smooth) {
	case PEN_SMOOTH_STEP:
	    GenerateSteps(&mi);
	    break;

	case PEN_SMOOTH_NATURAL:
	case PEN_SMOOTH_QUADRATIC:
	    if (mi.nScreenPts < 3) {
		/* Can't interpolate with less than three points. */
		elemPtr->smooth = PEN_SMOOTH_LINEAR;
	    } else {
		GenerateSpline(graphPtr, elemPtr, &mi);
	    }
	    break;

	case PEN_SMOOTH_CATROM:
	    if (mi.nScreenPts < 3) {
		/* Can't interpolate with less than three points. */
		elemPtr->smooth = PEN_SMOOTH_LINEAR;
	    } else {
		GenerateParametricSpline(graphPtr, elemPtr, &mi);
	    }
	    break;

	default:
	    break;
	}
	if (elemPtr->rTolerance > 0.0) {
	    ReducePoints(&mi, elemPtr->rTolerance);
	}
	if (elemPtr->fillBg != NULL) {
	    MapFillArea(graphPtr, elemPtr, &mi);
	}
	if (graphPtr->classId == CID_ELEM_STRIP) {
	    MapStrip(graphPtr, elemPtr, &mi);
	} else {
	    MapTraces(graphPtr, elemPtr, &mi);
	}
    }
    Blt_Free(mi.screenPts);
    Blt_Free(mi.map);

    /* Set the symbol size of all the pen styles. */
    for (link = Blt_Chain_FirstLink(elemPtr->styles); link != NULL;
	 link = Blt_Chain_NextLink(link)) {
	LineStyle *stylePtr;
	LinePen *penPtr;

	stylePtr = Blt_Chain_GetValue(link);
	penPtr = (LinePen *)stylePtr->penPtr;
	size = ScaleSymbol(elemPtr, penPtr->symbol.size);
	stylePtr->symbolSize = size;
	stylePtr->errorBarCapWidth = (penPtr->errorBarCapWidth > 0) 
	    ? penPtr->errorBarCapWidth : Round(size * 0.6666666);
	stylePtr->errorBarCapWidth /= 2;
    }
    styleMap = (LineStyle **)Blt_StyleMap((Element *)elemPtr);
    if (((elemPtr->yHigh.nValues > 0) && (elemPtr->yLow.nValues > 0)) ||
	((elemPtr->xHigh.nValues > 0) && (elemPtr->xLow.nValues > 0)) ||
	(elemPtr->xError.nValues > 0) || (elemPtr->yError.nValues > 0)) {
	MapErrorBars(graphPtr, elemPtr, styleMap);
    }
    MergePens(elemPtr, styleMap);
    Blt_Free(styleMap);
}
#endif
