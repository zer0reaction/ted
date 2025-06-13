#!/bin/bash

# debug build
CC='gcc'
CFLAGS='-Wall -Wextra -Wno-unused-function -std=c99 -pedantic -fsanitize=address'
LDFLAGS=''

# static release build
# CC='gcc'
# CFLAGS='-Wall -Wextra -std=c99 -pedantic'
# LDFLAGS='-static'

set -xe

$CC $CFLAGS -o ted ted.c $LDFLAGS
