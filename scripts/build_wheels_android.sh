#!/bin/bash
# scripts/build_wheels_android.sh (EXPERIMENTAL / in preparation)
# Builds the two Python wheels the on-device game code generator needs (unicorn, capstone) for
# Chaquopy's Python (cp313, android_arm64_v8a) via CPython 3.13's supported Android target
# (PEP 738) + crossenv, and drops them in app/src/main/python/wheels/ for the Gradle pip step.
# Not wired into the workflow yet: the base build is green without it; when it works, the
# generator runs fully on-device.
set -euo pipefail
echo "This script is not ready yet. See README 'Limitações conhecidas'."
echo "Planned pipeline:"
echo "  1. build CPython 3.13 for aarch64-linux-android29 (PEP 738, --with-build-python)"
echo "  2. pip install crossenv; create a cross virtualenv against that interpreter"
echo "  3. pip wheel unicorn==2.1.4 capstone==5.0.9 (CC=NDK clang) with android platform tags"
echo "  4. copy the wheels to app/src/main/python/wheels/"
exit 1
