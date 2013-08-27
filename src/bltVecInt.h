
/*
 * bltVecInt.h --
 *
 *	Copyright 1995-2004 George A Howlett.
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


#include "bltInt.h"
#include <bltHash.h>
#include <bltChain.h>
#include <bltVector.h>

#define VECTOR_THREAD_KEY	"BLT Vector Data"
#define VECTOR_MAGIC		((unsigned int) 0x46170277)

/* These defines allow parsing of different types of indices */

#define INDEX_SPECIAL	(1<<0)	/* Recognize "min", "max", and "++end" as
				 * valid indices */
#define INDEX_COLON	(1<<1)	/* Also recognize a range of indices separated
				 * by a colon */
#define INDEX_CHECK	(1<<2)	/* Verify that the specified index or range of
				 * indices are within limits */
#define INDEX_ALL_FLAGS    (INDEX_SPECIAL | INDEX_COLON | INDEX_CHECK)

#define SPECIAL_INDEX		-2

#define FFT_NO_CONSTANT		(1<<0)
#define FFT_BARTLETT		(1<<1)
#define FFT_SPECTRUM		(1<<2)

typedef struct {
    Blt_HashTable vectorTable;	/* Table of vectors */
    Blt_HashTable mathProcTable; /* Table of vector math functions */
    Blt_HashTable indexProcTable;
    Tcl_Interp *interp;
    unsigned int nextId;
} VectorInterpData;

/*
 * Vector --
 *
 *	A vector is an array of double precision values.  It can be accessed
 *	through a TCL command, a TCL array variable, or C API. The storage for
 *	the array points initially to a statically allocated buffer, but to
 *	malloc-ed memory if more is necessary.
 *
 *	Vectors can be shared by several clients (for example, two different
 *	graph widgets).  The data is shared. When a client wants to use a
 *	vector, it allocates a vector identifier, which identifies the client.
 *	Clients use this ID to specify a callback routine to be invoked
 *	whenever the vector is modified or destroyed.  Whenever the vector is
 *	updated or destroyed, each client is notified of the change by their
 *	callback routine.
 */

typedef struct {

    /*
     * If you change these fields, make sure you change the definition of
     * Blt_Vector in bltInt.h and blt.h too.
     */

    double *valueArr;		/* Array of values (malloc-ed) */

    int length;			/* Current number of values in the array. */

    int size;			/* Maximum number of values that can be stored
				 * in the value array. */

    double min, max;		/* Minimum and maximum values in the vector */

    int dirty;			/* Indicates if the vector has been updated */

    int reserved;

    /* The following fields are local to this module  */

    const char *name;		/* The namespace-qualified name of the vector.
				 * It points to the hash key allocated for the
				 * entry in the vector hash table. */

    VectorInterpData *dataPtr;
    Tcl_Interp *interp;		/* Interpreter associated with the
				 * vector */

    Blt_HashEntry *hashPtr;	/* If non-NULL, pointer in a hash table to
				 * track the vectors in use. */

    Tcl_FreeProc *freeProc;	/* Address of procedure to call to release
				 * storage for the value array, Optionally can
				 * be one of the following: TCL_STATIC,
				 * TCL_DYNAMIC, or TCL_VOLATILE. */

    const char *arrayName;	/* The name of the TCL array variable mapped
				 * to the vector (malloc'ed). If NULL,
				 * indicates that the vector isn't mapped to
				 * any variable */

    Tcl_Namespace *nsPtr;	/* Namespace context of the vector itself. */

    int offset;			/* Offset from zero of the vector's starting
				 * index */

    Tcl_Command cmdToken;	/* Token for vector's TCL command. */

    Blt_Chain chain;		/* List of clients using this vector */

    int notifyFlags;		/* Notification flags. See definitions
				 * below */

    int varFlags;		/* Indicate if the variable is global,
				 * namespace, or local */

    int freeOnUnset;		/* For backward compatibility only: If
				 * non-zero, free the vector when its variable
				 * is unset. */
    int flush;

    int first, last;		/* Selected region of vector. This is used
				 * mostly for the math routines */
} Vector;

#define NOTIFY_UPDATED		((int)BLT_VECTOR_NOTIFY_UPDATE)
#define NOTIFY_DESTROYED	((int)BLT_VECTOR_NOTIFY_DESTROY)

#define NOTIFY_NEVER		(1<<3)	/* Never notify clients of updates to
					 * the vector */
#define NOTIFY_ALWAYS		(1<<4)	/* Notify clients after each update
					 * of the vector is made */
#define NOTIFY_WHENIDLE		(1<<5)	/* Notify clients at the next idle point
					 * that the vector has been updated. */

#define NOTIFY_PENDING		(1<<6)	/* A do-when-idle notification of the
					 * vector's clients is pending. */
#define NOTIFY_NOW		(1<<7)	/* Notify clients of changes once
					 * immediately */

#define NOTIFY_WHEN_MASK	(NOTIFY_NEVER|NOTIFY_ALWAYS|NOTIFY_WHENIDLE)

#define UPDATE_RANGE		(1<<9)	/* The data of the vector has changed.
					 * Update the min and max limits when
					 * they are needed */

#define FindRange(array, first, last, min, max) \
{ \
    min = max = 0.0; \
    if (first <= last) { \
	register int i; \
	min = max = array[first]; \
	for (i = first + 1; i <= last; i++) { \
	    if (min > array[i]) { \
		min = array[i]; \
	    } else if (max < array[i]) { \
		max = array[i]; \
	    } \
	} \
    } \
}

BLT_EXTERN void Blt_Vec_InstallSpecialIndices(Blt_HashTable *tablePtr);

BLT_EXTERN void Blt_Vec_InstallMathFunctions(Blt_HashTable *tablePtr);

BLT_EXTERN void Blt_Vec_UninstallMathFunctions(Blt_HashTable *tablePtr);

BLT_EXTERN VectorInterpData *Blt_Vec_GetInterpData (Tcl_Interp *interp);

BLT_EXTERN double Blt_Vec_Max(Vector *vecObjPtr);
BLT_EXTERN double Blt_Vec_Min(Vector *vecObjPtr);

BLT_EXTERN Vector *Blt_Vec_New(VectorInterpData *dataPtr);

BLT_EXTERN int Blt_Vec_Duplicate(Vector *destPtr, Vector *srcPtr);

BLT_EXTERN int Blt_Vec_SetLength(Tcl_Interp *interp, Vector *vPtr, 
	int length);

BLT_EXTERN int Blt_Vec_SetSize(Tcl_Interp *interp, Vector *vPtr, 
	int size);

BLT_EXTERN int Blt_Vec_ChangeLength(Tcl_Interp *interp, Vector *vPtr, 
	int length);

BLT_EXTERN Vector *Blt_Vec_ParseElement(Tcl_Interp *interp, 
	VectorInterpData *dataPtr, const char *start, const char **endPtr, 
	int flags);

BLT_EXTERN void Blt_Vec_Free(Vector *vPtr);

BLT_EXTERN size_t *Blt_Vec_SortMap(Vector **vectors, int nVectors);

BLT_EXTERN int Blt_Vec_LookupName(VectorInterpData *dataPtr, 
	const char *vecName, Vector **vPtrPtr);

BLT_EXTERN Vector *Blt_Vec_Create(VectorInterpData *dataPtr, 
	const char *name, const char *cmdName, const char *varName, 
	int *newPtr);

BLT_EXTERN void Blt_Vec_UpdateRange(Vector *vPtr);

BLT_EXTERN void Blt_Vec_UpdateClients(Vector *vPtr);

BLT_EXTERN void Blt_Vec_FlushCache(Vector *vPtr);

BLT_EXTERN int Blt_Vec_Reset(Vector *vPtr, double *dataArr,
	int nValues, int arraySize, Tcl_FreeProc *freeProc);

BLT_EXTERN int  Blt_Vec_GetIndex(Tcl_Interp *interp, Vector *vPtr, 
	const char *string, int *indexPtr, int flags, 
	Blt_VectorIndexProc **procPtrPtr);

BLT_EXTERN int  Blt_Vec_GetIndexRange(Tcl_Interp *interp, Vector *vPtr, 
	const char *string, int flags, Blt_VectorIndexProc **procPtrPtr);

BLT_EXTERN int Blt_Vec_MapVariable(Tcl_Interp *interp, Vector *vPtr, 
	const char *name);

BLT_EXTERN int Blt_Vec_FFT(Tcl_Interp *interp, Vector *realPtr,
	Vector *phasesPtr, Vector *freqPtr, double delta, 
	int flags, Vector *srcPtr);

BLT_EXTERN int Blt_Vec_InverseFFT(Tcl_Interp *interp, Vector *iSrcPtr, 
	Vector *rDestPtr, Vector *iDestPtr, Vector *srcPtr);

BLT_EXTERN Tcl_ObjCmdProc Blt_Vec_InstCmd;

BLT_EXTERN Tcl_VarTraceProc Blt_Vec_VarTrace;

BLT_EXTERN Tcl_IdleProc Blt_Vec_NotifyClients;

