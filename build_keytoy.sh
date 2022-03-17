#!/bin/bash

CFLAGS="-I/usr/local/include -I../../../ -I/usr/local/include/libdrm -L/usr/local/lib"
DEFS="-D_POSIX_C_SOURCE=200809L"
OPTS="-Wall -Wextra -pedantic -O3 -flto -ffast-math"
SRCS="main.c platforms/keytoy.c core/*.c scenes/*.c shaders/*.c tests/*.c"
LIBS="-lm -lgbm -lepoxy -ldrm"

cd renderer && clang -o ../Viewer $CFLAGS $DEFS $OPTS $SRCS $LIBS && cd ..
