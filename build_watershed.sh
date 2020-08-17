#!/bin/sh
perl -n conf_decl.pl conf_spec.txt > conf_decl.c
perl -n conf_parse.pl conf_spec.txt > conf_parse.c
perl -n conf_default.pl conf_spec.txt > default.conf
gcc -I /Library/Frameworks/SDL2.framework/Headers -framework SDL2 -o watershed watershed.c
