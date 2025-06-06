#!/bin/bash

CC='gcc'
CFLAGS='-Wall -Wextra -std=c99 -pedantic -fsanitize=address'
LDFLAGS='-ltinfo'

set -xe

$CC $CFLAGS -o scuffed main.c $LDFLAGS
