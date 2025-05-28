#!/bin/bash

CC='gcc'
CFLAGS='-Wall -Wextra -std=c99 -pedantic -fsanitize=address'
LDFLAGS='-lncurses'

set -xe

$CC $CFLAGS -o editor main.c $LDFLAGS
