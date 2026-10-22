// Free-standing fenv.h for the on-device game code build (only pulled in through runtime/simd.h
// when the game code carries portable-C FP lowering; prototypes resolve against bionic).
#ifndef _SW_SHIM_FENV_H
#define _SW_SHIM_FENV_H

#define FE_INVALID 1
#define FE_DIVBYZERO 2
#define FE_OVERFLOW 4
#define FE_UNDERFLOW 8
#define FE_INEXACT 16
#define FE_ALL_EXCEPT 31

#define FE_TONEAREST 0
#define FE_DOWNWARD 1
#define FE_UPWARD 2
#define FE_TOWARDZERO 3

typedef int fexcept_t;
typedef struct { unsigned long long __cw; } fenv_t;

int feclearexcept(int);
int fetestexcept(int);
int feraiseexcept(int);
int fegetround(void);
int fesetround(int);
int fegetenv(fenv_t *);
int fesetenv(const fenv_t *);
int feholdexcept(fenv_t *);
int feupdateenv(const fenv_t *);

#endif
