#!/bin/bash

CC='gcc'
CFLAGS='-Wall -Wextra -std=c99 -pedantic -fsanitize=address'
LDFLAGS='-lncursesw'

set -xe

$CC $CFLAGS -o editor main.c $LDFLAGS
