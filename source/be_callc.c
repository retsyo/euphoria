/*****************************************************************************/
/*      (c) Copyright - See License.txt       */
/*****************************************************************************/
/*                                                                           */
/*                           CALLS TO C ROUTINES                             */
/*                                                                           */
/*****************************************************************************/
/**
 * There are two function body implementations for call_c() in be_call.c.
 * The first one uses inline assembler.  The second one is written in pure C.  
 * When porting, you may be able to use the pure C, as is.
 * However, sometimes if the coding convention used on a new architecture is 
 * not the standard ones below, you will need to do something special for it.
 *
 * C Calling Conventions (there are many #define synonyms for these):
 * __cdecl: The caller must pop the arguments off the stack after the call.
 *          (i.e. increment the stack pointer, ESP). This allows a 
 *          variable number of arguments to be passed.
 *          This is the default.
 *
 * __stdcall: The subroutine must pop the arguments off the stack.
 *            It assumes a fixed number of arguments are passed.
 *            The WIN32 API is like this.
 *
 * Both contentions put arguments on the *runtime* stack starting with the
 * argument on the far right of the function call and progressing to the 
 * *left* until the argument on the far left is pushed on the stack.
 * 
 * Both conventions also return floating-point results on the top of
 * the hardware floating-point stack, except that Watcom is non-standard 
 * for __cdecl, since it returns a pointer to the floating-point result 
 * in EAX, (although the final result could also be on the top of the
 * f.p. stack just by chance).
 *
 * Watcom restores the stack pointer ESP at the end of call_c() below, 
 * just after making the call to the C routine, so it doesn't matter 
 * if it neglects to increment the stack pointer. This means that
 * Watcom can call __cdecl (of other compilers but not it's own!)
 * and __stdcall routines, using the *same* __stdcall convention.
 */
 

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#ifdef EWINDOWS
#include <windows.h>
#endif
#include "alldefs.h"
#include "be_runtime.h"
#include "be_machine.h"
#include "be_alloc.h"


/*******************/
/* Local variables */
/*******************/

/* void push() : put /arg/ on to the stack
   void pop()  : GCC:pull /as_offset/ bytes off the stack discarding the stack's values
   */

#if defined(__GNUC__) && EUNIX
#if ARCH == ix86
/** 
 * push the value arg on to the register based, **runtime** stack.  You must make sure that as_offset is
 * equal to sum of all of the sizes of the data you push to the stack by the time you call pop below.
 *
 * This is an assembler macro and is not implemented for some platforms.
*/
#define push() asm("movl %0,%%ecx; pushl (%%ecx);" : /* no out */ : "r"(last_offset) : "%ecx" )

/** 
  * pops all of the values from the call stack if you maintained as_offset correctly with push!
  *
  * This is an assembler macro and is not implemented for some platforms.  The platforms that do
  * not implement push and pop use a C-only version of callc.  They instead push to an array and
  * then the calls are handled with great big switch statements.
  */
#define  pop() asm( "movl %0,%%ecx; addl (%%ecx),%%esp;" : /* no out */ : "r"(as_offset) : "%ecx" )
#endif
#endif  // EUNIX

#ifdef EMSVC
#define push() __asm { PUSH [last_offset] } do { } while (0)
#define  pop() __asm { ADD esp,[as_offset] } do { } while (0)
#endif

typedef union {
	double dbl;
	int32_t ints[2];
	int64_t int64;
} double_arg;

typedef union {
	float flt;
	int32_t int32;
} float_arg;

#if defined(push) && defined(pop)

/** These stack types allow you take values off of the EUPHORIA c declaration
 *  And values off of the call to c_func/c_proc.
 *  
 *  In the later defined C only implementation, we pop values and types from left to right.
 *  The arguments are pushed into a virtual stack, but not the runtime stack.
 *  This stack, is implemented as an array and the values are written first one
 *  left to last one right in the call.  Ultimately, the ones on the right are
 *  first to be pushed in the **runtime** stack.
 *  
 *  The C-compiler generates code that will push these values last first, essentially
 *  reversing the order.  By the time the call is made the right most value is
 *  first on the run-time stack
 *  
 *  In the C+assembler implementation, we pop values and types from right to left.
 *  We use the assembler macros push and pop to put things directly on to the
 *  run time stack.  In the same order we removed them, right to left.
 *  It is in this order we push things on to the runtime stack. 
 *  Both cdecl and stdcall calling conventions push arguments in this order.
 * 
 */

// Creates a stack of types from a sequence of stack members. 
inline static object_ptr type_stack_init(s1_ptr type_list) {
	return type_list->base + type_list->length;
}
// pops a type off of the type stack, this_type_stack.
inline static object type_stack_pop(object_ptr * this_type_stack) {
	//*next_size_ptr--
	return *(*this_type_stack)--;
}
inline static object_ptr value_stack_init(s1_ptr type_list) {
	return type_list->base + type_list->length;
}
// pops a type off of the type stack, this_type_stack.
inline static object value_stack_pop(object_ptr * this_type_stack) {
	//*next_size_ptr--
	return *(*this_type_stack)--;
}


/*
 * 
 * DIRTY CODE! Modify this to push values onto the call stack 
 * N.B.!!! stack offset for variables "arg", and "argsize"
 * below must be correct! - make a .asm file to be sure. 
 *
 * What is meant by "correct"?
 * */
// 32-bit only
object call_c(int func, object proc_ad, object arg_list)
/* Call a WIN32 or Linux C function in a DLL or shared library. 
   Alternatively, call a machine-code routine at a given address. */
{
	volatile unsigned long arg;  // !!!! magic var to push values on the stack
	volatile int argsize;        // !!!! number of bytes to pop 
	
	/* List of arguments passed in this call to c_func/c_proc and 
	 * a list of types made in the call to define_c_func or define_c_proc,
	 * repsectively. */
	s1_ptr arg_list_ptr, arg_size_ptr;
	/* Stacks of arguments we will pop using type_stack_pop(), value_stack_pop()
	 * inlined functions. */
	object_ptr next_arg_ptr, next_size_ptr;
	object next_arg, next_size;
	int iresult, i;
	double dresult;
	double_arg dbl_arg;
	float_arg flt_arg;
	float fresult;
	unsigned long size;
	int proc_index;
	int (*int_proc_address)();
	unsigned return_type;

#if defined(EWINDOWS)
	int cdecl_call;
#endif

	unsigned long as_offset;
	unsigned long last_offset;

	// this code relies on arg always being the first variable and last_offset 
	// always being the last variable
	last_offset = (unsigned long)&arg;
	as_offset = (unsigned long)&argsize;
	// as_offset = last_offset - 4;

	// Setup and Check for Errors
	
	proc_index = get_pos_int("c_proc/c_func", proc_ad); 
	if (proc_index >= (unsigned)c_routine_next) {
		RTFatal("c_proc/c_func: bad routine number (%d)", proc_index);
	}
	
	int_proc_address = c_routine[proc_index].address;
#if defined(EWINDOWS)
	cdecl_call = c_routine[proc_index].convention;
#endif
	if (IS_ATOM(arg_list)) {
		RTFatal("c_proc/c_func: argument list must be a sequence");
	}
	
	arg_list_ptr = SEQ_PTR(arg_list);
	next_arg_ptr = type_stack_init(arg_list_ptr);
	
	// only look at length of arg size sequence for now
	arg_size_ptr = c_routine[proc_index].arg_size;
	next_size_ptr = value_stack_init(arg_size_ptr);
	
	return_type = c_routine[proc_index].return_size; // will be INT
	
	if ((func && return_type == 0) || (!func && return_type != 0) ) {
		if (c_routine[proc_index].name->length < TEMP_SIZE)
			MakeCString(TempBuff, MAKE_SEQ(c_routine[proc_index].name), TEMP_SIZE);
		else
			TempBuff[0] = '\0';
		RTFatal(func ? "%s does not return a value" :
				"%s returns a value",
				TempBuff);
	}
		
	if (arg_list_ptr->length != arg_size_ptr->length) {
		if (c_routine[proc_index].name->length < 100)
			MakeCString(TempBuff, MAKE_SEQ(c_routine[proc_index].name), TEMP_SIZE);
		else
			TempBuff[0] = '\0';
		RTFatal("C routine %s() needs %d argument%s, not %d",
				TempBuff,
				arg_size_ptr->length,
				(arg_size_ptr->length == 1) ? "" : "s",
				arg_list_ptr->length);
	}
	
	argsize = arg_list_ptr->length << 2;
	
	// Push the Arguments
	
	for (i = 1; i <= arg_list_ptr->length; i++) {
	
		next_arg = value_stack_pop(&next_arg_ptr);
		next_size = type_stack_pop(&next_size_ptr);
		
		if (IS_ATOM_INT(next_size))
			size = INT_VAL(next_size);
		else if (IS_ATOM(next_size))
			size = (uintptr_t)DBL_PTR(next_size)->dbl;
		else 
			RTFatal("This C routine was defined using an invalid argument type");

		if (size == C_DOUBLE || size == C_FLOAT) {
			/* push 8-byte double or 4-byte float */
			if (IS_ATOM_INT(next_arg))
				dbl_arg.dbl = (double)next_arg;
			else if (IS_ATOM(next_arg))
				dbl_arg.dbl = DBL_PTR(next_arg)->dbl;
			else { 
				arg = arg+argsize+9999; // 9999 = 270f hex - just a marker for asm code
				RTFatal("arguments to C routines must be atoms");
			}

			if (size == C_DOUBLE) {
				arg = dbl_arg.ints[1];

				push();  // push high-order half first
				argsize += 4;
				arg = dbl_arg.ints[0];
				push(); // don't combine this with the push() below - Lcc bug
			}
			else {
				/* C_FLOAT */
				flt_arg.flt = (float)dbl_arg.dbl;
				arg = (unsigned long)flt_arg.int32;
				push();
			}
		}
		else {
			/* push 4-byte integer */
			if (size >= E_INTEGER) {
				if (IS_ATOM_INT(next_arg)) {
					if (size == E_SEQUENCE)
						RTFatal("passing an integer where a sequence is required");
				}
				else {
					if (IS_SEQUENCE(next_arg)) {
						if (size != E_SEQUENCE && size != E_OBJECT)
							RTFatal("passing a sequence where an atom is required");
					}
					else {
						if (size == E_SEQUENCE)
							RTFatal("passing an atom where a sequence is required");
					}
					RefDS(next_arg);
				}
				arg = next_arg;
				push();
			} 
			else if (IS_ATOM_INT(next_arg)) {
				arg = next_arg;
				push();
			}
			else if (IS_ATOM(next_arg)) {
				// atoms are rounded to integers
				
				arg = (uintptr_t)DBL_PTR(next_arg)->dbl; //correct
				// if it's a -ve f.p. number, Watcom converts it to int and
				// then to unsigned int. This is exactly what we want.
				// Works with the others too. 
				push();
			}
			else {
				arg = arg+argsize+9999; // just a marker for asm code
				RTFatal("arguments to C routines must be atoms");
			}
		}
	}    

	// Make the Call - The C compiler thinks it's a 0-argument call
	
	// might be VOID C routine, but shouldn't crash

	if (return_type == C_DOUBLE) {
		// expect double to be returned from C routine
#if defined(EWINDOWS)
		if (cdecl_call) {
			dresult = (*((double (  __cdecl *)())int_proc_address))();
			pop();
		}
		else
#endif          
			dresult = (*((double (__stdcall *)())int_proc_address))();

#ifdef EUNIX
		pop();
#endif      
		return NewDouble(dresult);
	}
	
	else if (return_type == C_FLOAT) {
		// expect float to be returned from C routine
#if defined(EWINDOWS)
		if (cdecl_call) {
			fresult = (*((float (  __cdecl *)())int_proc_address))();
			pop();
		}
		else
#endif          
			fresult = (*((float (__stdcall *)())int_proc_address))();

#ifdef EUNIX
		pop();
#endif      
		return NewDouble((double)fresult);
	}
	
	else {
		// expect integer to be returned
#if defined(EWINDOWS)
		if (cdecl_call) {
			iresult = (*((int (  __cdecl *)())int_proc_address))();
			pop();
		}
		else
#endif          
			iresult = (*((int (__stdcall *)())int_proc_address))();
#ifdef EUNIX       
		pop();
#endif      
		if (return_type == C_POINTER ){
			if ((unsigned)iresult <= (unsigned)MAXINT) {
				return iresult;
			}
			else{
				return NewDouble((double)(unsigned)iresult);
			}
		}
		else if ((return_type & 0x000000FF) == 04) {
			/* 4-byte integer - usual case */
			// check if unsigned result is required 
			if ((return_type & C_TYPE) == 0x02000000) {
				// unsigned integer result
				if ((unsigned)iresult <= (unsigned)MAXINT) {
					return iresult;
				}
				else
					return NewDouble((double)(unsigned)iresult);
			}
			else {
				// signed integer result
				if (return_type >= E_INTEGER ||
					(iresult >= MININT && iresult <= MAXINT)) {
					return iresult;
				}
				else
					return NewDouble((double)iresult);
			}
		}
		else if (return_type == 0) {
			return 0; /* void - procedure */
		}
		/* less common cases */
		else if (return_type == C_UCHAR) {
			return (unsigned char)iresult;
		}
		else if (return_type == C_CHAR) {
			return (signed char)iresult;
		}
		else if (return_type == C_USHORT) {
			return (unsigned short)iresult;
		}
		else if (return_type == C_SHORT) {
			return (short)iresult;
		}
		else
			return 0; // unknown function return type
	}
}
#else //EMINGW, 64-bit, ARM
/*******************/
/* Local variables */
/*******************/

#if INTPTR_MAX == INT32_MAX
/* for c_proc */
typedef void (__stdcall *proc0)();
typedef void (__stdcall *proc1)(intptr_t);
typedef void (__stdcall *proc2)(intptr_t,intptr_t);
typedef void (__stdcall *proc3)(intptr_t,intptr_t,intptr_t);
typedef void (__stdcall *proc4)(intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__stdcall *proc5)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__stdcall *proc6)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__stdcall *proc7)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__stdcall *proc8)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__stdcall *proc9)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__stdcall *procA)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__stdcall *procB)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__stdcall *procC)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__stdcall *procD)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__stdcall *procE)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__stdcall *procF)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);

/* for c_func */
typedef intptr_t (__stdcall *func0)();
typedef intptr_t (__stdcall *func1)(intptr_t);
typedef intptr_t (__stdcall *func2)(intptr_t,intptr_t);
typedef intptr_t (__stdcall *func3)(intptr_t,intptr_t,intptr_t);
typedef intptr_t (__stdcall *func4)(intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__stdcall *func5)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__stdcall *func6)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__stdcall *func7)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__stdcall *func8)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__stdcall *func9)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__stdcall *funcA)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__stdcall *funcB)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__stdcall *funcC)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__stdcall *funcD)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__stdcall *funcE)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__stdcall *funcF)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);

/* for c_proc */
typedef void (__cdecl *cdproc0)();
typedef void (__cdecl *cdproc1)(intptr_t);
typedef void (__cdecl *cdproc2)(intptr_t,intptr_t);
typedef void (__cdecl *cdproc3)(intptr_t,intptr_t,intptr_t);
typedef void (__cdecl *cdproc4)(intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__cdecl *cdproc5)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__cdecl *cdproc6)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__cdecl *cdproc7)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__cdecl *cdproc8)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__cdecl *cdproc9)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__cdecl *cdprocA)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__cdecl *cdprocB)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__cdecl *cdprocC)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__cdecl *cdprocD)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__cdecl *cdprocE)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef void (__cdecl *cdprocF)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);

/* for c_func */
typedef intptr_t (__cdecl *cdfunc0)();
typedef intptr_t (__cdecl *cdfunc1)(intptr_t);
typedef intptr_t (__cdecl *cdfunc2)(intptr_t,intptr_t);
typedef intptr_t (__cdecl *cdfunc3)(intptr_t,intptr_t,intptr_t);
typedef intptr_t (__cdecl *cdfunc4)(intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__cdecl *cdfunc5)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__cdecl *cdfunc6)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__cdecl *cdfunc7)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__cdecl *cdfunc8)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__cdecl *cdfunc9)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__cdecl *cdfuncA)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__cdecl *cdfuncB)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__cdecl *cdfuncC)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__cdecl *cdfuncD)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__cdecl *cdfuncE)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef intptr_t (__cdecl *cdfuncF)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);

/* for c_func */
typedef float (__stdcall *ffunc0)();
typedef float (__stdcall *ffunc1)(intptr_t);
typedef float (__stdcall *ffunc2)(intptr_t,intptr_t);
typedef float (__stdcall *ffunc3)(intptr_t,intptr_t,intptr_t);
typedef float (__stdcall *ffunc4)(intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__stdcall *ffunc5)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__stdcall *ffunc6)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__stdcall *ffunc7)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__stdcall *ffunc8)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__stdcall *ffunc9)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__stdcall *ffuncA)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__stdcall *ffuncB)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__stdcall *ffuncC)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__stdcall *ffuncD)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__stdcall *ffuncE)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__stdcall *ffuncF)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);

/* for c_func */
typedef float (__cdecl *cdffunc0)();
typedef float (__cdecl *cdffunc1)(intptr_t);
typedef float (__cdecl *cdffunc2)(intptr_t,intptr_t);
typedef float (__cdecl *cdffunc3)(intptr_t,intptr_t,intptr_t);
typedef float (__cdecl *cdffunc4)(intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__cdecl *cdffunc5)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__cdecl *cdffunc6)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__cdecl *cdffunc7)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__cdecl *cdffunc8)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__cdecl *cdffunc9)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__cdecl *cdffuncA)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__cdecl *cdffuncB)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__cdecl *cdffuncC)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__cdecl *cdffuncD)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__cdecl *cdffuncE)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef float (__cdecl *cdffuncF)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);

/* for c_func */
typedef double (__stdcall *dfunc0)();
typedef double (__stdcall *dfunc1)(intptr_t);
typedef double (__stdcall *dfunc2)(intptr_t,intptr_t);
typedef double (__stdcall *dfunc3)(intptr_t,intptr_t,intptr_t);
typedef double (__stdcall *dfunc4)(intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__stdcall *dfunc5)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__stdcall *dfunc6)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__stdcall *dfunc7)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__stdcall *dfunc8)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__stdcall *dfunc9)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__stdcall *dfuncA)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__stdcall *dfuncB)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__stdcall *dfuncC)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__stdcall *dfuncD)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__stdcall *dfuncE)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__stdcall *dfuncF)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);

/* for c_func */
typedef double (__cdecl *cddfunc0)();
typedef double (__cdecl *cddfunc1)(intptr_t);
typedef double (__cdecl *cddfunc2)(intptr_t,intptr_t);
typedef double (__cdecl *cddfunc3)(intptr_t,intptr_t,intptr_t);
typedef double (__cdecl *cddfunc4)(intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__cdecl *cddfunc5)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__cdecl *cddfunc6)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__cdecl *cddfunc7)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__cdecl *cddfunc8)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__cdecl *cddfunc9)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__cdecl *cddfuncA)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__cdecl *cddfuncB)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__cdecl *cddfuncC)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__cdecl *cddfuncD)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__cdecl *cddfuncE)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
typedef double (__cdecl *cddfuncF)(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);

float float_std_func(intptr_t i, intptr_t * op, long len) {
    
	switch(len) {
	    case 0: return ((ffunc0)i)();
	    case 1: return ((ffunc1)i)(op[0]);
	    case 2: return ((ffunc2)i)(op[0],op[1]);
	    case 3: return ((ffunc3)i)(op[0],op[1],op[2]);
	    case 4: return ((ffunc4)i)(op[0],op[1],op[2],op[3]);
	    case 5: return ((ffunc5)i)(op[0],op[1],op[2],op[3],op[4]);
	    case 6: return ((ffunc6)i)(op[0],op[1],op[2],op[3],op[4],op[5]); 
		case 7: return ((func7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]);
	    case 8: return ((ffunc8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]);
	    case 9: return ((ffunc9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]);
	    case 10: return ((ffuncA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]);
	    case 11: return ((ffuncB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]);
	    case 12: return ((ffuncC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]);
	    case 13: return ((ffuncD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]);
	    case 14: return ((ffuncE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]);
	    case 15: return ((ffuncF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]);
	}
    return 0.0;
}

float float_cdecl_func(intptr_t i, intptr_t * op, long len) {
    
	switch(len) {
	    case 0: return ((cdffunc0)i)();
	    case 1: return ((cdffunc1)i)(op[0]);
	    case 2: return ((cdffunc2)i)(op[0],op[1]);
	    case 3: return ((cdffunc3)i)(op[0],op[1],op[2]);
	    case 4: return ((cdffunc4)i)(op[0],op[1],op[2],op[3]);
	    case 5: return ((cdffunc5)i)(op[0],op[1],op[2],op[3],op[4]);
	    case 6: return ((cdffunc6)i)(op[0],op[1],op[2],op[3],op[4],op[5]);
	    case 7: return ((cdffunc7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]);
	    case 8: return ((cdffunc8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]);
	    case 9: return ((cdffunc9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]);
	    case 10: return ((cdffuncA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]);
	    case 11: return ((cdffuncB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]);
	    case 12: return ((cdffuncC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]);
	    case 13: return ((cdffuncD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]);
	    case 14: return ((cdffuncE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]);
	    case 15: return ((cdffuncF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]);
	}
    return 0.0;
}

double double_std_func(intptr_t i, intptr_t * op, long len) {
    
	switch(len) {
	    case 0: return ((dfunc0)i)();
	    case 1: return ((dfunc1)i)(op[0]);
	    case 2: return ((dfunc2)i)(op[0],op[1]);
	    case 3: return ((dfunc3)i)(op[0],op[1],op[2]);
	    case 4: return ((dfunc4)i)(op[0],op[1],op[2],op[3]);
	    case 5: return ((dfunc5)i)(op[0],op[1],op[2],op[3],op[4]);
	    case 6: return ((dfunc6)i)(op[0],op[1],op[2],op[3],op[4],op[5]); 
		case 7: return ((func7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]);
	    case 8: return ((dfunc8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]);
	    case 9: return ((dfunc9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]);
	    case 10: return ((dfuncA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]);
	    case 11: return ((dfuncB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]);
	    case 12: return ((dfuncC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]);
	    case 13: return ((dfuncD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]);
	    case 14: return ((dfuncE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]);
	    case 15: return ((dfuncF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]);
	}
    return 0.0;
}

double double_cdecl_func(intptr_t i, intptr_t * op, long len) {
    
	switch(len) {
	    case 0: return ((cddfunc0)i)();
	    case 1: return ((cddfunc1)i)(op[0]);
	    case 2: return ((cddfunc2)i)(op[0],op[1]);
	    case 3: return ((cddfunc3)i)(op[0],op[1],op[2]);
	    case 4: return ((cddfunc4)i)(op[0],op[1],op[2],op[3]);
	    case 5: return ((cddfunc5)i)(op[0],op[1],op[2],op[3],op[4]);
	    case 6: return ((cddfunc6)i)(op[0],op[1],op[2],op[3],op[4],op[5]);
	    case 7: return ((cddfunc7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]);
	    case 8: return ((cddfunc8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]);
	    case 9: return ((cddfunc9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]);
	    case 10: return ((cddfuncA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]);
	    case 11: return ((cddfuncB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]);
	    case 12: return ((cddfuncC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]);
	    case 13: return ((cddfuncD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]);
	    case 14: return ((cddfuncE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]);
	    case 15: return ((cddfuncF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]);
	}
    return 0.0;
}

void call_std_proc(intptr_t i, intptr_t * op, long len) {
    
	switch(len) {
	    case 0: ((proc0)i)(); return;
	    case 1: ((proc1)i)(op[0]); return;
	    case 2: ((proc2)i)(op[0],op[1]); return;
	    case 3: ((proc3)i)(op[0],op[1],op[2]); return;
	    case 4: ((proc4)i)(op[0],op[1],op[2],op[3]); return;
	    case 5: ((proc5)i)(op[0],op[1],op[2],op[3],op[4]); return;
	    case 6: ((proc6)i)(op[0],op[1],op[2],op[3],op[4],op[5]); return;
	    case 7: ((proc7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]); return;
	    case 8: ((proc8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]); return;
	    case 9: ((proc9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]); return;
	    case 10: ((procA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]); return;
	    case 11: ((procB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]); return;
	    case 12: ((procC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]); return;
	    case 13: ((procD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]); return;
	    case 14: ((procE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]); return;
	    case 15: ((procF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]); return;
	}
}

intptr_t call_std_func(intptr_t i, intptr_t * op, long len) {
    
	switch(len) {
	    case 0: return ((func0)i)();
	    case 1: return ((func1)i)(op[0]);
	    case 2: return ((func2)i)(op[0],op[1]);
	    case 3: return ((func3)i)(op[0],op[1],op[2]);
	    case 4: return ((func4)i)(op[0],op[1],op[2],op[3]);
	    case 5: return ((func5)i)(op[0],op[1],op[2],op[3],op[4]);
	    case 6: return ((func6)i)(op[0],op[1],op[2],op[3],op[4],op[5]);
	    case 7: return ((func7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]);
	    case 8: return ((func8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]);
	    case 9: return ((func9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]);
	    case 10: return ((funcA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]);
	    case 11: return ((funcB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]);
	    case 12: return ((funcC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]);
	    case 13: return ((funcD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]);
	    case 14: return ((funcE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]);
	    case 15: return ((funcF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]);
	}
    return 0;
}

void call_cdecl_proc(intptr_t i, intptr_t * op, long len) {
    
	switch(len) {
	    case 0: ((cdproc0)i)(); return;
	    case 1: ((cdproc1)i)(op[0]); return;
	    case 2: ((cdproc2)i)(op[0],op[1]); return;
	    case 3: ((cdproc3)i)(op[0],op[1],op[2]); return;
	    case 4: ((cdproc4)i)(op[0],op[1],op[2],op[3]); return;
	    case 5: ((cdproc5)i)(op[0],op[1],op[2],op[3],op[4]); return;
	    case 6: ((cdproc6)i)(op[0],op[1],op[2],op[3],op[4],op[5]); return;
	    case 7: ((cdproc7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]); return;
	    case 8: ((cdproc8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]); return;
	    case 9: ((cdproc9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]); return;
	    case 10: ((cdprocA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]); return;
	    case 11: ((cdprocB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]); return;
	    case 12: ((cdprocC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]); return;
	    case 13: ((cdprocD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]); return;
	    case 14: ((cdprocE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]); return;
	    case 15: ((cdprocF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]); return;
	}
}

intptr_t call_cdecl_func(intptr_t i, intptr_t * op, long len) {
	switch(len) {
	    case 0: return ((cdfunc0)i)();
	    case 1: return ((cdfunc1)i)(op[0]);
	    case 2: return ((cdfunc2)i)(op[0],op[1]);
	    case 3: return ((cdfunc3)i)(op[0],op[1],op[2]);
	    case 4: return ((cdfunc4)i)(op[0],op[1],op[2],op[3]);
	    case 5: return ((cdfunc5)i)(op[0],op[1],op[2],op[3],op[4]);
	    case 6: return ((cdfunc6)i)(op[0],op[1],op[2],op[3],op[4],op[5]);
	    case 7: return ((cdfunc7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]);
	    case 8: return ((cdfunc8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]);
	    case 9: return ((cdfunc9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]);
	    case 10: return ((cdfuncA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]);
	    case 11: return ((cdfuncB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]);
	    case 12: return ((cdfuncC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]);
	    case 13: return ((cdfuncD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]);
	    case 14: return ((cdfuncE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]);
	    case 15: return ((cdfuncF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]);
	}
    return 0;
}

#else
// 64-bit Call-C

int64_t icall_x86_64( intptr_t func, double* xmm, int64_t *r, int args ){
	
	switch( args ){
		case 0: return ((int64_t (*)())func)();
		case 1: return ((int64_t (*)())func)( r[0] );
		case 2: return ((int64_t (*)())func)( r[0], r[1] );
		case 3: return ((int64_t (*)())func)( r[0], r[1], r[2] );
		case 4: return ((int64_t (*)())func)( r[0], r[1], r[2], r[3] );
		case 5: return ((int64_t (*)())func)( r[0], r[1], r[2], r[3], r[4] );
		default:
			return ((int64_t (*)())func)(
				r[0], r[1], r[2], r[3], r[4], 
				r[5], r[6], r[7], r[8], r[9],
				r[10], r[11], r[12], r[13], 
				r[14], r[15], r[16],
				xmm[0], xmm[1], xmm[2], xmm[3], xmm[4] );
	}
}

double dcall_x86_64( intptr_t func, double* xmm, int64_t *r ){
	return ((double (*)())func)( xmm[0], xmm[1], xmm[2], xmm[3], xmm[4],
			r[0], r[1], r[2], r[3], r[4], 
			r[5], r[6], r[7], r[8], r[9],
			r[10], r[11], r[12], r[13], 
			r[14], r[15], r[16]);
}

float fcall_x86_64( intptr_t func, double* xmm, int64_t *r ){
	return ((float (*)())func)( xmm[0], xmm[1], xmm[2], xmm[3], xmm[4],
			r[0], r[1], r[2], r[3], r[4], 
			r[5], r[6], r[7], r[8], r[9],
			r[10], r[11], r[12], r[13], 
			r[14], r[15], r[16]);
}

union xmm_param {
	double d;
	float f;
};
#endif

#if INTPTR_MAX == INT64_MAX
/* The x86-64 calling convention uses 6 registers for the first 6 integer
 * parameters.  After that, parameters are pushed on the stack.  Also,
 * the first 5 floating point parameters are put into floating point
 * registers.  Either type of argument can start overflowing onto the
 * stack, so we have to track the number of args we're putting onto the
 * stack separately from what goes into the registers.
 */
 
/* Pushes the variable //arg// on to our argument stack, but not the
 * runtime stack, as an integer. */
#define PUSH_INT_ARG \
if( arg_i < 6 ){ \
	arg_op[arg_i++] = arg; \
	++int_args; \
} \
else{ \
	arg_op[arg_stack++] = arg; \
}

#else
#define PUSH_INT_ARG arg_op[arg_i++] = arg;
#endif

// Pull off types and their values from the sequences passed in from left to right.
// This goes into the function call as (first-pushed, second-pushed, ..., last-pushed)..
// If using standard calling conventions, this means that the last-pushed value will
// be first when values are pushed onto the runtime stack.  This is what we want.
inline static object_ptr type_stack_init(s1_ptr type_list) {
	return type_list->base + 1;
}
inline static object type_stack_pop(object_ptr * this_type_stack) {
	//*next_size_ptr++
	return *(*this_type_stack)++;
}
#define value_stack_init type_stack_init
#define  value_stack_pop type_stack_pop


object call_c(int func, object proc_ad, object arg_list)
/* Call a WIN32 or Linux C function in a DLL or shared library. 
   Alternatively, call a machine-code routine at a given address. */
{
	s1_ptr arg_list_ptr, arg_size_ptr;
	object_ptr next_arg_ptr, next_size_ptr;
	object next_arg, next_size;
	int64_t iresult, i;
	double_arg dbl_arg;
	double dresult = 0;
	float_arg flt_arg;
	float fresult = 0;
	uintptr_t size;
	intptr_t proc_index;
	int cdecl_call;
	intptr_t long_proc_address;
	uintptr_t return_type;
	char NameBuff[100];
	uint64_t arg;

	intptr_t arg_op[16];
	intptr_t arg_len;
	intptr_t arg_i = 0;
#if INTPTR_MAX == INT64_MAX
	/* The x86-64 calling convention requires us to keep the first
	 * few floating point values separate from the ints.  And the
	 * floating point params could start overflowing onto the stack
	 * before we have enough ints to go there.
	 */
	union xmm_param dbl_op[5];
	intptr_t xmm_i = 0;
	intptr_t arg_stack = 6;
	int int_args = 0;
#endif
	int is_double, is_float;
	
	// Setup and Check for Errors
	proc_index = get_pos_int("c_proc/c_func", proc_ad); 
	if (proc_index >= (uintptr_t)c_routine_next) {
		snprintf(TempBuff, TEMP_SIZE, "c_proc/c_func: bad routine number (%" PRIdPTR ")", proc_index);
		RTFatal(TempBuff);
	}
	
	long_proc_address = (intptr_t)(c_routine[proc_index].address);
#if defined(EWINDOWS)
	cdecl_call = c_routine[proc_index].convention;
#else
	cdecl_call = 1;
#endif
	if (IS_ATOM(arg_list)) {
		RTFatal("c_proc/c_func: argument list must be a sequence");
	}
	
	arg_list_ptr = SEQ_PTR(arg_list);
	next_arg_ptr = value_stack_init(arg_list_ptr);
	
	// only look at length of arg size sequence for now
	arg_size_ptr = c_routine[proc_index].arg_size;
	
	next_size_ptr = type_stack_init(arg_size_ptr);
	
	return_type = c_routine[proc_index].return_size; // will be INT

	arg_len = arg_list_ptr->length;
	
	if ( (func && return_type == 0) || (!func && return_type != 0) ) {
		if (c_routine[proc_index].name->length < 100)
			MakeCString(NameBuff, MAKE_SEQ(c_routine[proc_index].name), c_routine[proc_index].name->length );
		else
			NameBuff[0] = '\0';
		snprintf(TempBuff, TEMP_SIZE, func ? "%s does not return a value" :
								 "%s returns a value",
								 NameBuff);
		RTFatal(TempBuff);
	}
		
	if (arg_list_ptr->length != arg_size_ptr->length) {
		if (c_routine[proc_index].name->length < 100)
			MakeCString(NameBuff, MAKE_SEQ(c_routine[proc_index].name), c_routine[proc_index].name->length );
		else
			NameBuff[0] = '\0';
		snprintf(TempBuff, TEMP_SIZE, "C routine %s() needs %d argument%s, not %d",
						  NameBuff,
						  arg_size_ptr->length,
						  (arg_size_ptr->length == 1) ? "" : "s",
						  arg_list_ptr->length);
		RTFatal(TempBuff);
	}
	
	// Push the Arguments
	
	for (i = 1; i <= arg_list_ptr->length; i++) {
	

		next_arg = value_stack_pop(&next_arg_ptr);
		next_size = type_stack_pop(&next_size_ptr);
		
		if (IS_ATOM_INT(next_size))
			size = INT_VAL(next_size);
		else if (IS_ATOM(next_size))
			size = (uintptr_t)DBL_PTR(next_size)->dbl;
		else 
			RTFatal("This C routine was defined using an invalid argument type");

		if (size == C_DOUBLE || size == C_FLOAT) {
			/* push 8-byte double or 4-byte float */
			if (IS_ATOM_INT(next_arg))
				dbl_arg.dbl = (double)next_arg;
			else if (IS_ATOM(next_arg))
				dbl_arg.dbl = DBL_PTR(next_arg)->dbl;
			else { 
				RTFatal("arguments to C routines must be atoms");
			}

			if (size == C_DOUBLE) {
				#if INTPTR_MAX == INT32_MAX
					arg_op[arg_i++] = dbl_arg.ints[1];
					arg_op[arg_i++] = dbl_arg.ints[0];
				#elif INTPTR_MAX == INT64_MAX
					if( xmm_i < 5 ){
						dbl_op[xmm_i++].d = dbl_arg.dbl;
					}
					else{
						arg_op[arg_stack++] = dbl_arg.int64;
					}
					int_args += 6;
				#endif
				
			}
			else {
				/* C_FLOAT */
				flt_arg.flt = (float)dbl_arg.dbl;
				#if INTPTR_MAX == INT32_MAX
					arg_op[arg_i++] = (uintptr_t)flt_arg.int32;
				#elif INTPTR_MAX == INT64_MAX
					if( xmm_i < 5 ){
						dbl_op[xmm_i++].f = flt_arg.flt;
					}
					else{
						arg_op[arg_stack++] = flt_arg.int32;
					}
					int_args += 6;
				#endif
			}
		}
		else if( size == C_POINTER ){
			if (IS_ATOM_INT(next_arg)) {
				arg = next_arg;
				PUSH_INT_ARG
			}
			else if (IS_ATOM(next_arg)) {
				// atoms are rounded to integers
				
				arg = (uint64_t)(uintptr_t)DBL_PTR(next_arg)->dbl; //correct
				// if it's a -ve f.p. number, Watcom converts it to long and
				// then to unsigned long. This is exactly what we want.
				// Works with the others too. 
				PUSH_INT_ARG
			}
		}
		else if( size == C_LONGLONG ){
			if (IS_ATOM_INT(next_arg)) {
				arg = next_arg;
				PUSH_INT_ARG
			}
			else if (IS_ATOM(next_arg)) {
				// atoms are rounded to integers
				
				arg = (uint64_t)DBL_PTR(next_arg)->dbl; //correct
				// if it's a -ve f.p. number, Watcom converts it to long and
				// then to unsigned long. This is exactly what we want.
				// Works with the others too. 
				PUSH_INT_ARG
			}
		}
		else {
			/* push ptr size integer */
			if (size >= E_INTEGER) {
				if (IS_ATOM_INT(next_arg)) {
					if (size == E_SEQUENCE)
						RTFatal("passing an integer where a sequence is required");
				}
				else {
					if (IS_SEQUENCE(next_arg)) {
						if (size != E_SEQUENCE && size != E_OBJECT)
							RTFatal("passing a sequence where an atom is required");
					}
					else {
						if (size == E_SEQUENCE)
							RTFatal("passing an atom where a sequence is required");
					}
					RefDS(next_arg);
				}
				arg = (uintptr_t) next_arg;
				PUSH_INT_ARG
			} 
			else if (IS_ATOM_INT(next_arg)) {
				arg = (uintptr_t) next_arg;
				PUSH_INT_ARG
			}
			else if (IS_ATOM(next_arg)) {
				// atoms are rounded to integers
				
				arg = (uintptr_t)DBL_PTR(next_arg)->dbl; //correct
				// if it's a -ve f.p. number, Watcom converts it to long and
				// then to unsigned long. This is exactly what we want.
				// Works with the others too. 
				PUSH_INT_ARG
			}
			else {
				RTFatal("arguments to C routines must be atoms");
			}
		}
	}    

	// Make the Call
	
	is_double = (return_type == C_DOUBLE);
	is_float = (return_type == C_FLOAT);

#if INTPTR_MAX == INT32_MAX
	if (func) {
		if (cdecl_call && is_double) {
			dresult = double_cdecl_func(long_proc_address, arg_op, arg_len);
		} else if (cdecl_call && is_float) {
			fresult = float_cdecl_func(long_proc_address, arg_op, arg_len);
		} else if (is_double) {
			dresult = double_std_func(long_proc_address, arg_op, arg_len);
		} else if (is_float) {
			fresult = float_std_func(long_proc_address, arg_op, arg_len);
		} else if (cdecl_call) {
			iresult = call_cdecl_func(long_proc_address, arg_op, arg_len);
		} else {
			iresult = call_std_func(long_proc_address, arg_op, arg_len);
		}
	} else {
		if (cdecl_call) {
			call_cdecl_proc(long_proc_address, arg_op, arg_len);
			iresult = 0;
		} else {
			call_std_proc(long_proc_address, arg_op, arg_len);
			iresult = 0;
		}
	}
#else
	if( is_double ) dresult = dcall_x86_64( long_proc_address, (double*)dbl_op, arg_op );
	if( is_float  ) fresult = fcall_x86_64( long_proc_address, (double*)dbl_op, arg_op );
	else            iresult = icall_x86_64( long_proc_address, (double*)dbl_op, arg_op, int_args );
#endif
	switch( return_type ){
		case C_DOUBLE:
			return NewDouble(dresult);
		case C_FLOAT:
			return NewDouble((eudouble)fresult);
		case C_POINTER:
			if ((uintptr_t)iresult <= (uintptr_t)MAXINT) {
				return iresult;
			}
			else {
				// signed integer result
				if (return_type >= E_INTEGER ||
					(iresult >= MININT && iresult <= MAXINT)) {
					return iresult;
				}
				else
					return NewDouble((double)iresult);
			}
		case C_LONGLONG:
			if ((long long int)iresult <= (long long int)MAXINT) {
				return iresult;
			}
			else{
				return NewDouble((eudouble)(intptr_t)iresult);
			}
		case C_INT:
			if ((intptr_t)(int)iresult <= MAXINT) {
				return (int)iresult;
			}
			else{
				return NewDouble((eudouble)(int)iresult);
			}
		case C_UINT:
			if ((uintptr_t)(unsigned int)iresult <= (unsigned int)MAXINT) {
				return (unsigned int)iresult;
			}
			else{
				return NewDouble((eudouble)(unsigned int)iresult);
			}
		case C_LONG:
			if ((intptr_t)(long)iresult <= MAXINT) {
				return (long)iresult;
			}
			else{
				return NewDouble((eudouble)(long)iresult);
			}
		case C_ULONG:
			if ((uintptr_t)(unsigned long int)iresult <= (unsigned long int)MAXINT) {
				return iresult;
			}
			else{
				return NewDouble((eudouble)(unsigned long int)iresult);
			}
		case C_UCHAR:
			return (unsigned char) iresult;
		case C_CHAR:
			return (char) iresult;
		case C_SHORT:
			return (short) iresult;
		case C_USHORT:
			return (unsigned short) iresult;
		case E_INTEGER:
		case E_ATOM:
		case E_SEQUENCE:
		case E_OBJECT:
			return iresult;
		case 0:
			// void procedure
			return 0;
			
		default:
			return 0; // unknown function return type
	}
}

#endif // EMINGW
