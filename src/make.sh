#!/bin/bash

# Usenet program compile script
gcc -g -Wall -O0 -o ../bin/usenet main.c unzbget.c nzbgetint.c \
	-I/home/pyrus/Prog/C++/usenet/include/ -I/usr/include/libxml2/ \
	-lxmlrpc_util -lxmlrpc_client -lxmlrpc -lxml2 -lcurl -lalist
exit 0
