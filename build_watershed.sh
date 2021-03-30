#!/bin/sh
perl -n conf_decl.pl conf_spec.txt > conf_decl.c
perl -n conf_parse.pl conf_spec.txt > conf_parse.c
perl -n conf_default.pl conf_spec.txt > default.conf
gcc `sdl2-config --cflags --libs` -o watershed watershed.c
