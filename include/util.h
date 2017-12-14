
#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include <stdio.h>	/* For snprintf(3) */
#include <stdlib.h>	/* For *alloc(3) */
#include <string.h>	/* For memcpy(3) */
#include <sys/types.h>	/* For size_t */
#include <stdarg.h>	/* For va_start */
#include <stddef.h>	/* for offsetof and ptrdiff_t */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif 

extern FILE * log_file;
#define LOG_MESSAGE(...)	do { print_time(log_file); fprintf(log_file, __VA_ARGS__); fflush(log_file); } while(false)
void print_time(FILE * log_file);
#define printd printf

#ifdef _WIN32
#ifndef WIN32
#define WIN32
#endif
#endif

#ifdef	WIN32

#include <malloc.h>
#include <stdint.h>
#define	 vsnprintf	_vsnprintf
#define	 snprintf	_snprintf
#define sleep(x) Sleep(1000*x)

#ifdef _MSC_VER			/* MSVS.Net */
#ifndef __cplusplus
#define inline __inline
#endif
#define	ssize_t		SSIZE_T
//typedef	char		int8_t;
//typedef	short		int16_t;
//typedef	int		int32_t;
//typedef	unsigned char	uint8_t;
//typedef	unsigned short	uint16_t;
//typedef	unsigned int	uint32_t;
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <float.h>
#define isnan _isnan
#define finite _finite
#define copysign _copysign
#define	ilogb	_logb
#endif	/* _MSC_VER */

#else	/* !WIN32 */

#if defined(__vxworks)
#include <types/vxTypes.h>
#else	/* !defined(__vxworks) */

#include <inttypes.h>	/* C99 specifies this file */
/*
 * 1. Earlier FreeBSD version didn't have <stdint.h>,
 * but <inttypes.h> was present.
 * 2. Sun Solaris requires <alloca.h> for alloca(3),
 * but does not have <stdint.h>.
 */
#if	(!defined(__FreeBSD__) || !defined(_SYS_INTTYPES_H_))
#if	defined(sun)
#include <alloca.h>	/* For alloca(3) */
#include <ieeefp.h>	/* for finite(3) */
#elif	defined(__hpux)
#ifdef	__GNUC__
#include <alloca.h>	/* For alloca(3) */
#else	/* !__GNUC__ */
#define inline
#endif	/* __GNUC__ */
#else
#include <stdint.h>	/* SUSv2+ and C99 specify this file, for uintXX_t */
#endif	/* defined(sun) */
#endif

#endif	/* defined(__vxworks) */

#endif	/* WIN32 */

#if	__GNUC__ >= 3
#ifndef	GCC_PRINTFLIKE
#define	GCC_PRINTFLIKE(fmt,var)	__attribute__((format(printf,fmt,var)))
#endif
#else
#ifndef	GCC_PRINTFLIKE
#define	GCC_PRINTFLIKE(fmt,var)	/* nothing */
#endif
#endif

#ifndef	offsetof	/* If not defined by <stddef.h> */
#define	offsetof(s, m)	((ptrdiff_t)&(((s *)0)->m) - (ptrdiff_t)((s *)0))
#endif	/* offsetof */

#ifndef	MIN		/* Suitable for comparing primitive types (integers) */
#if defined(__GNUC__)
#define	MIN(a,b)	({ __typeof a _a = a; __typeof b _b = b;	\
	((_a)<(_b)?(_a):(_b)); })
#else	/* !__GNUC__ */
#define	MIN(a,b)	((a)<(b)?(a):(b))	/* Unsafe variant */
#endif /* __GNUC__ */
#endif	/* MIN */

#endif
