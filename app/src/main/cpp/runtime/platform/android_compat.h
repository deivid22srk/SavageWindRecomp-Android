// What bionic already has (strlcpy/strlcat since API 26, arc4random always): nothing to shim.
// Included via runtime/platform.h when __ANDROID__ is defined. (linux_compat.h is glibc-specific:
// its __GLIBC_PREREQ guards don't exist on bionic.)
#pragma once
#include <stdint.h>
