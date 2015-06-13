#!/bin/bash

cwd=`pwd`
parent=$(dirname $cwd )
grand_parent=$(dirname $parent )
thor_include_folder="thor/inc/"
thor_lib_folder="thor/bin/"

include_folder="/include/"
include_path="$parent$include_folder"
thor_inc_path="$grand_parent/$thor_include_folder"
thor_lib_path="$grand_parent/$thor_lib_folder"
jsmn_inc_path="$parent/external/jsmn/"


gcc -g -Wall -O0 -o ../bin/client uclient.c utilsint.c jsonint.c unzbget.c nzbgetint.c uxmlrpc.c $jsmn_inc_path/jsmn.c \
	-I$include_path -I/usr/include/libxml2/ -I$thor_inc_path -I$jsmn_inc_path \
	-L$thor_lib_path -Wl,-rpath=$thor_lib_path \
	-lcomm -lalist -lm -lconfig -lxmlrpc_util -lxmlrpc_client -lxmlrpc -lcurl -lxml2 -lssh2 -lssl -lcrypto -lpthread

exit 0
