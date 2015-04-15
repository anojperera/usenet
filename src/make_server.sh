#!/bin/bash

cwd=`pwd`
parent=$(dirname $cwd )
grand_parent=$(dirname $parent )
thor_include_folder="thor/inc/"
thor_lib_folder="thor/bin/libcomm.a"
glist_lib_folder="g_list/bin/libalist.a"

include_folder="/include/"
include_path="$parent$include_folder"
thor_inc_path="$grand_parent/$thor_include_folder"
jsmn_inc_path="$parent/external/jsmn/"

thor_lib_path="$grand_parent/$thor_lib_folder"
glist_lib_path="$grand_parent/$glist_lib_folder"

# Make server
gcc -g -Wall -O0 -o ../bin/server userver.c utilsint.c \
	 $thor_lib_path $glist_lib_path \
	-I$include_path -I$jsmn_inc_path -I/usr/include/libxml2/ -I$thor_inc_path \
	-lm -lpthread -lconfig -lcurl -lxml2 -lssl -lcrypto

exit 0
