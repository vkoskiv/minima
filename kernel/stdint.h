/* Exact integral types.  */

/* Signed.  */

/* There is some amount of overlap with <sys/types.h> as known by inet code */
#ifndef __int8_t_defined
# define __int8_t_defined
typedef signed char                int8_t;
typedef short int                int16_t;
typedef int                        int32_t;
# if __WORDSIZE == 64
typedef long int                int64_t;
# else
__extension__
typedef long long int                int64_t;
# endif
#endif

/* Unsigned.  */
typedef unsigned char                uint8_t;
typedef unsigned short int        uint16_t;
#ifndef __uint32_t_defined
typedef unsigned int                uint32_t;
# define __uint32_t_defined
#endif
#if __WORDSIZE == 64
typedef unsigned long int        uint64_t;
#else
__extension__
typedef unsigned long long int        uint64_t;
#endif


/* Small types.  */

/* Signed.  */
typedef signed char                int_least8_t;
typedef short int                int_least16_t;
typedef int                        int_least32_t;
#if __WORDSIZE == 64
typedef long int                int_least64_t;
#else
__extension__
typedef long long int                int_least64_t;
#endif

/* Unsigned.  */
typedef unsigned char                uint_least8_t;
typedef unsigned short int        uint_least16_t;
typedef unsigned int                uint_least32_t;
#if __WORDSIZE == 64
typedef unsigned long int        uint_least64_t;
#else
__extension__
typedef unsigned long long int        uint_least64_t;
#endif


/* Fast types.  */

/* Signed.  */
typedef signed char                int_fast8_t;
#if __WORDSIZE == 64
typedef long int                int_fast16_t;
typedef long int                int_fast32_t;
typedef long int                int_fast64_t;
#else
typedef int                        int_fast16_t;
typedef int                        int_fast32_t;
__extension__
typedef long long int                int_fast64_t;
#endif

/* Unsigned.  */
typedef unsigned char                uint_fast8_t;
#if __WORDSIZE == 64
typedef unsigned long int        uint_fast16_t;
typedef unsigned long int        uint_fast32_t;
typedef unsigned long int        uint_fast64_t;
#else
typedef unsigned int                uint_fast16_t;
typedef unsigned int                uint_fast32_t;
__extension__
typedef unsigned long long int        uint_fast64_t;
#endif
