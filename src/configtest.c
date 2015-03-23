
#include <stdlib.h>
#include <stdio.h>
#include <libconfig.h>

#define CONFIG_PATH "usenet.cfg"
int main(int argc, char** argv)
{
	config_t _config;

	/* initialise configuration */
	config_init(&_config);

	if(config_read_file(&_config, CONFIG_PATH) != CONFIG_TRUE) {
		fprintf(stderr, "Errors occured: %s at line %i\n", config_error_text(&_config), config_error_line(&_config));
		return -1;
	}
	config_destroy(&_config);

    return 0;
}
