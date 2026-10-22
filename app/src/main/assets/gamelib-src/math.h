// Free-standing math.h for the on-device game code build (runtime/simd.h's estimate helpers and
// friends); prototypes resolve against bionic (libm is in the process already).
#ifndef _SW_SHIM_MATH_H
#define _SW_SHIM_MATH_H

#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962
#define M_E 2.7182818284590452354
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_SQRT2 1.41421356237309504880

#define HUGE_VALF __builtin_huge_valf()
#define HUGE_VAL __builtin_huge_val()
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")

#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_NORMAL 4
#define FP_SUBNORMAL 5
#define FP_ZERO 3

double fabs(double);
float fabsf(float);
double sqrt(double);
float sqrtf(float);
double floor(double);
float floorf(float);
double ceil(double);
float ceilf(float);
double round(double);
float roundf(float);
long lround(double);
long lroundf(float);
double trunc(double);
float truncf(float);
double nearbyint(double);
float nearbyintf(float);
double rint(double);
float rintf(float);
double copysign(double, double);
float copysignf(float, float);
double fmin(double, double);
float fminf(float, float);
double fmax(double, double);
float fmaxf(float, float);
double fma(double, double, double);
float fmaf(float, float, float);
double fmod(double, double);
float fmodf(float, float);
double pow(double, double);
float powf(float, float);
double exp(double);
float expf(float);
double exp2(double);
float exp2f(float);
double log(double);
float logf(float);
double log2(double);
float log2f(float);
double sin(double);
float sinf(float);
double cos(double);
float cosf(float);
double tan(double);
float tanf(float);
double atan(double);
float atanf(float);
double atan2(double, double);
float atan2f(float, float);
double asin(double);
float asinf(float);
double acos(double);
float acosf(float);
double sinh(double);
float sinhf(float);
double cosh(double);
float coshf(float);
double tanh(double);
float tanhf(float);

float frexpf(float, int *);
double frexp(double, int *);
float ldexpf(float, int);
double ldexp(double, int);
float modff(float, float *);
double modf(double, double *);

#endif
