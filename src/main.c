#include <stdlib.h>
#include <stdio.h>
#include "usenet.h"

int main(int argc, char** argv)
{
    if(argc > 2) {
		usenet_nzb_search_and_get(argv[1], argv[2]);
	}
	else if(argc > 1) {
		usenet_nzb_search_and_get(argv[1], NULL);
	}
    return 0;
}
