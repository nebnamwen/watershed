all: watershed default.conf

watershed: watershed.c conf_decl.c conf_parse.c
	gcc `sdl2-config --cflags --libs` -o watershed watershed.c

conf_decl.c: conf_spec.txt conf_decl.pl
	perl -n conf_decl.pl conf_spec.txt > conf_decl.c

conf_parse.c: conf_spec.txt conf_parse.pl
	perl -n conf_parse.pl conf_spec.txt > conf_parse.c

default.conf: conf_spec.txt conf_default.pl
	perl -n conf_default.pl conf_spec.txt > default.conf
