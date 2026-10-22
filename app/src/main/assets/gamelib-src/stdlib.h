// Free-standing stdlib.h for the on-device game code build; prototypes resolve against bionic.
#ifndef _SW_SHIM_STDLIB_H
#define _SW_SHIM_STDLIB_H
#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647

void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
void abort(void) __attribute__((noreturn));
void exit(int) __attribute__((noreturn));
void _Exit(int) __attribute__((noreturn));
int atexit(void (*)(void));
int atoi(const char *);
long atol(const char *);
long long atoll(const char *);
double atof(const char *);
long strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
double strtod(const char *, char **);
int abs(int);
long labs(long);
long long llabs(long long);
int rand(void);
void srand(unsigned);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
void *bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));
char *getenv(const char *);
int setenv(const char *, const char *, int);
int unsetenv(const char *);
int system(const char *);

#endif
