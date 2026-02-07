#!/bin/sh

cc -I$(pg_config --includedir-server) -o muggin_test muggin.c muggin_test.c && ./muggin_test
