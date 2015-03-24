/*
 * Methods for general utils
 */

#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <math.h>
#include <sys/types.h>
#include <dirent.h>

#include <libconfig.h>
#include "usenet.h"

#define USENET_SETTINGS_FILE "../config/usenet.cfg"
#define USENET_PROC_PATH "/proc"

#define USENET_GET_SETTING_STRING(name)								\
    if((_setting = config_lookup(&login->_config, #name)) != NULL)	\
		(login->name)  = config_setting_get_string(_setting)
#define USENET_GET_SETTING_INT(name)								\
    if((_setting = config_lookup(&login->_config, #name)) != NULL)	\
		(login->name)  = config_setting_get_int(_setting)

/* load configuration settings from file */
int usenet_utils_load_config(struct gapi_login* login)
{
    config_setting_t* _setting = NULL;

    /* check for arguments */
    if(login == NULL)
		return USENET_ARG_ERROR;

    /* initialise login struct */
    memset(login, 0, sizeof(struct gapi_login));

    /* initialise config object */
    config_init(&login->_config);

    /* read file and load the settings */
    if(config_read_file(&login->_config, USENET_SETTINGS_FILE) != CONFIG_TRUE) {
		USENET_LOG_MESSAGE(config_error_text(&login->_config));
		config_destroy(&login->_config);
		return USENET_EXT_LIB_ERROR;
    }

    /* load the settings into the struct and return to user */
    USENET_GET_SETTING_STRING(p12_path);
    USENET_GET_SETTING_STRING(iss);
    USENET_GET_SETTING_STRING(aud);
    USENET_GET_SETTING_STRING(sub);
    USENET_GET_SETTING_STRING(scope);
    USENET_GET_SETTING_STRING(alg);
    USENET_GET_SETTING_STRING(typ);
	USENET_GET_SETTING_STRING(server_name);
	USENET_GET_SETTING_STRING(server_port);
	USENET_GET_SETTING_STRING(nzburl);
	USENET_GET_SETTING_STRING(mac_addr);
	USENET_GET_SETTING_INT(scan_freq);
    return USENET_SUCCESS;
}

int usenet_destroy_config(struct gapi_login* login)
{
	config_destroy(&login->_config);

	/* set every thing to NULL */
	memset(login, 0, sizeof(struct gapi_login));
	return USENET_SUCCESS;
}

/* encode base64 */
int base64encode(const char* message, char** buffer)
{
    BIO *bio, *b64;
    FILE* stream;
    int encodedSize = 4*ceil((double)strlen(message)/3);
    *buffer = (char *)malloc(encodedSize+1);

    stream = fmemopen(*buffer, encodedSize+1, "w");
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new_fp(stream, BIO_NOCLOSE);
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, message, strlen(message));
    /* BIO_flush(bio); */
    BIO_free_all(bio);
    fclose(stream);

    return USENET_SUCCESS;
}

/* Initialise the message struct */
int usenet_message_init(struct usenet_message* msg)
{
	if(msg == NULL)
		return USENET_ERROR;

	memset(msg, 0, sizeof(struct usenet_message));
	return USENET_SUCCESS;
}

/* Send a request instruction to client */
int usenet_message_request_instruct(struct usenet_message* msg)
{
	/* set instruction to command */
	if(msg == NULL)
		return USENET_ERROR;

	msg->ins |= USENET_REQUEST_COMMAND;
	return USENET_SUCCESS;
}

/* Send default response acknowledging the command */
int usenent_message_response_instruct(struct usenet_message* msg)
{
	if(msg == NULL)
		return USENET_ERROR;

	msg->ins = 0;
	return USENET_SUCCESS;
}

int usenet_read_file(const char* path, char** buff, size_t* sz)
{
	int _fd = 0;
	char* _err_str = NULL;
	off_t _size = 0;
	struct stat _fstat = {};
	int _err = USENET_SUCCESS;

	/* check arguments */
	if(!path || !buff || !sz)
		return USENET_ERROR;

	*sz = 0;
	*buff = NULL;

	USENET_LOG_MESSAGE_ARGS("opening file: %s", path);
	_fd = open(path, O_RDONLY);

	if(_fd == -1) {
		/* error have occured, get error string */
		_err = errno;
		_err_str = strerror(errno);
		USENET_LOG_MESSAGE(_err_str);
		return USENET_ERROR;
	}

	/* get file size */
	_size = lseek(_fd, 0, SEEK_SET);
	if(fstat(_fd, &_fstat)) {
		/* error have occured, get error string */
		_err = errno;
		_err_str = strerror(errno);
		USENET_LOG_MESSAGE(_err_str);
		goto clean_up;
	}

	/* get file size */
	_size = _fstat.st_size;
	if(_size <= 0) {
		_err = USENET_ERROR;
		USENET_LOG_MESSAGE("files size is less than 0, nothing to read");
		goto clean_up;
	}

	/* create buffer */
	*buff = (char*) malloc(sizeof(char) * (size_t) (_size+1));
	*sz = read(_fd, *buff, (size_t) _size);
	if(*sz < 0) {

		/* error have occured, get error string */
		_err = errno;
		_err_str = strerror(errno);
		USENET_LOG_MESSAGE(_err_str);
		goto clean_up;
	}

	/* add NULL pointer to NULL terminate the buffer */
	(*buff)[*sz] = '\0';

clean_up:
	USENET_LOG_MESSAGE_ARGS("closing file: %s", path);
	close(_fd);
	return _err;
}

/* find the process with the name */
pid_t usenet_find_process(const char* pname)
{
	DIR* _proc_dir = NULL;
	FILE* _stat_fp = NULL;
	struct dirent* _dir_ent = NULL;
	long _lpid = -1, _lspid = -1;								/* pid of process */
	char _stat_file[USENET_PROC_FILE_BUFF_SZ] = {0};

	char _pname[USENET_PROC_NAME_BUFF_SZ] = {0};


	if(pname == NULL)
		return USENET_ERROR;

	USENET_LOG_MESSAGE_ARGS("finding if process %s exists", pname);

	/* open proc file system */
	_proc_dir = opendir(USENET_PROC_PATH);
	if(_proc_dir == NULL) {
		USENET_LOG_MESSAGE("unable to open the proc file system");
		return USENET_ERROR;
	}

	/* iterate through the directorys and find the process name pname */
	while((_dir_ent = readdir(_proc_dir))) {

		/*
		 * Initialise the temporary variables passed to the open file method.
		 */
		_lspid = -1;
		memset(_stat_file, 0, USENET_PROC_FILE_BUFF_SZ);

		/*
		 * Get convert the directory name to long value.
		 * We excpect the names to have process id.
		 */
		_lpid = atol(_dir_ent->d_name);

		/* continue looking if its not a process value */
		if(_lpid < 0)
			continue;

		/* construct the stat file path for the pid */
		sprintf(_stat_file, "%s/%ld/%s", USENET_PROC_PATH, _lpid, "stat");

		_stat_fp = fopen(_stat_file, "r");
		if(_stat_fp == NULL)
			continue;

		/* load the stat files parameters in to the variables */
		if(fscanf(_stat_fp, "%ld (%[^)])", &_lspid, _pname) != 2)
			USENET_LOG_MESSAGE_ARGS("unable to get the details from %s", _stat_file);

		fclose(_stat_fp);

		/* If the pid is found return */
		if(strcmp(_pname, pname) == 0)
			break;

		memset(_pname, 0, USENET_PROC_NAME_BUFF_SZ);
	}

	/* close directory */
	closedir(_proc_dir);

	return _lspid;
}
