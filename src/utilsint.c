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
#include <time.h>
#include <errno.h>
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

#define USENET_CHECK_FILE_EXT(fname)									\
	(((strstr((fname), "mkv") || strstr((fname), "avi") || strstr((fname), "wmv")) && !strstr((fname), "sample"))? 1 : 0)

static inline __attribute__ ((always_inline)) const char* _usenet_utils_get_ext(const char* fname);
static const int _usenet_utils_rename_helper(struct  usenet_nzb_filellist* list, const char* file_path);

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
	USENET_GET_SETTING_INT(svr_wait_time);
	USENET_GET_SETTING_INT(nzb_fsize_threshold);
    return USENET_SUCCESS;
}

int usenet_utils_destroy_config(struct gapi_login* login)
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
		_lspid = -1;
	}

	/* close directory */
	closedir(_proc_dir);

	return _lspid;
}

size_t usenet_utils_count_blanks(const char* message)
{
	size_t _blank_spc = 0;
	int _i = 0;

	if(message == NULL)
		return _blank_spc;

	while(message[_i] != '\0') {
		if(message[_i] == USENET_BLANKSPACE_CHAR)
			_blank_spc++;
		_i++;
	}

	/*
	 * Return full length of the string if number of
	 * no blank spaces are present.
	 *
	 * This needs to be refined.
	 */
	return _blank_spc == 0 ? _i : _blank_spc;
}

/* Remove any parenthesis  new lines etc */
int usenet_utils_remove_chars(char* str, size_t len)
{
	int _i = 0, _j = 0;
	char* _c_pos = NULL, *_end_pos = NULL;
	char** _i_pos = NULL, **_pos = NULL, **_ins_pos = NULL;
	size_t _byte_loss_cnt = 0;

	/* create an array of char* to store the positions */
	_i_pos = (char**) calloc(sizeof(char*), len);

	/* set the end position of the string */
	_end_pos = str + sizeof(char)*len;

	/* initialise all to NULL and record the positions */
	for(_i = 0; _i < len; _i++) {
		_i_pos[_i] = NULL;

		_c_pos = str + _i*sizeof(char);

		/*
		 * Check for characters which are not allowed and
		 * record their positions
		 */
		switch(*_c_pos) {
		case USENET_SPACE_CHAR:
			/*
			 * space character is only checked at leading and
			 * trailing ends.
			 */
			if((_i == 0 || _i == len - 1) && _i_pos[_j] == NULL) {
				_i_pos[_j] = _c_pos;

				/* increment loss byte count */
				_byte_loss_cnt++;
			}
			break;
		case USENET_NEW_LINE_CHAR:
		case USENET_ARRAY_CHAR_BEGIN:
		case USENET_ARRAY_CHAR_END:
		case USENET_DOUBLEQ_CHAR:
		case USENET_BACKSLASH_CHAR:

			/* increment loss byte count */
			_byte_loss_cnt++;

			/*
			 * we only set the pointer on non continuous
			 * invalid characters. with the exception when
			 * _j = 0 at the start of the check.
			 */
			if(_i_pos[_j] != NULL)
				break;

			_i_pos[_j] = _c_pos;
			break;
		default:
			if(_i_pos[_j] != NULL) {
				_i_pos[++_j] = _c_pos;
				_j++;
			}
		}
	}

	/* go through the recorded position and adjust the new array */

	for(_i = 0, _pos = _i_pos;
		_i < _j+1;
		_i++, _pos++) {

		/*
		 * First we increment the array position the next, so that a
		 * difference in byte count can be obtainted between what the
		 * current element points to and the next.
		 */
		_ins_pos = _pos;
		_pos++;

		/*
		 * If the element is pointing to a NULL pointer, that means we
		 * no further adjustment and we are probably end of the line.
		 * Therefore we add a NULL pointer until the end and break.
		 */
		if(*_pos == NULL) {
			memset(*_ins_pos - 2*sizeof(char), 0, _end_pos - *_ins_pos);
			break;
		}

		if(*_pos - _i_pos[_i] <= 0)
			continue;

		/*
		 * Next element of the array points to the last continuous illegal character
		 * therefore we add another byte to what it points to get the next pointer with
		 * valid character.
		 */
		memmove(*_ins_pos, *_pos, _end_pos - *_pos);

	}

	free(_i_pos);
	_i_pos = NULL;

	return USENET_SUCCESS;
}

/*
 * Get the time difference in creation file creation time and now
 */
int usenet_utils_time_diff(const char* file)
{
	time_t _now;
	struct stat _buf;

	if(file == NULL)
		return USENET_ERROR;


	/* get file stat */
	if(stat(file, &_buf)) {
		USENET_LOG_MESSAGE_ARGS("unable to get stat file, errorno: %i", errno);
		return USENET_ERROR;
	}

	/* check if the file is a regular file */
	if(!S_ISREG(_buf.st_mode)) {
		USENET_LOG_MESSAGE("file is not a regular file");
		return USENET_ERROR;
	}

	time(&_now);
	return (int) difftime(_now, _buf.st_mtime);
}

/*
 * Replace space character with an underscore
 */
int usenet_utils_stdardise_file_name(char* file_name)
{
	int _i = 0;

	if(file_name == NULL)
		return USENET_ERROR;

	for(_i = 0; _i < strlen(file_name); _i++) {
		if(file_name[_i] == USENET_SPACE_CHAR)
			file_name[_i] = USENET_USCORE_CHAR;
	}

	return USENET_SUCCESS;
}

/*
 * Create std file name from nzb file name
 */
int usenet_utils_append_std_fname(struct usenet_nzb_filellist* list)
{
	size_t _sz = 0;

	if(list->_nzb_name == NULL || list->_u_std_fname != NULL)
		return USENET_ERROR;

	/* get the size of the file name */
	_sz = strlen(list->_nzb_name);

	/* create memory to field and copy */
	list->_u_std_fname = (char*) malloc((_sz + 1) * sizeof(char));
	strncpy(list->_u_std_fname, list->_nzb_name, _sz);
	list->_u_std_fname[_sz] = '\0';

	return usenet_utils_stdardise_file_name(list->_u_std_fname);
}

int usenet_utils_rename_file(struct usenet_nzb_filellist* list, int threshold)
{
	size_t _nfname_sz = 0;
	char* _nfname = NULL;										/* full path of the file to be changed */

	int _ret = USENET_SUCCESS;
	DIR* _nzb_dir = NULL;
	struct dirent* _dir_ent = NULL;

	struct stat _sbuf = {0};

	/* open the download directory */
	_nzb_dir = opendir(list->_dest_dir);
	if(_nzb_dir == NULL) {
		USENET_LOG_MESSAGE_ARGS("unable to open the directory: %s, exiting rename process.", list->_dest_dir);

		_ret = USENET_ERROR;
		return _ret;
	}

	while((_dir_ent = readdir(_nzb_dir))) {

		/* continue if not a regular file */
		if(_dir_ent->d_type != DT_REG)
			continue;

		/* construct full file name */
		usenet_utils_cons_new_fname(list->_dest_dir, _dir_ent->d_name, &_nfname, &_nfname_sz);

		/* get stats of the new file name */
		if(!stat(_nfname, &_sbuf) &&
		   USENET_CHECK_FILE_EXT(_nfname)) {

			/* break here and operate on the file */
			break;
		}

		if(_nfname)
			free(_nfname);

		_nfname = NULL;
	}

	/* close the directory */
	closedir(_nzb_dir);

	if(_nfname) {

		USENET_LOG_MESSAGE_ARGS("file found %s, with %dMB, list size %dMB", _nfname, USENET_CONV_MB(_sbuf.st_size), list->_file_size);
		_ret = _usenet_utils_rename_helper(list, _nfname);

		free(_nfname);
	}

	_nfname = NULL;

	return _ret;
}


int usenet_utils_escape_blanks(char* fname, size_t sz)
{
	size_t _end_off = 0;
	char* _pos = NULL;
	char* _end_pos = NULL;

	/* get end position */
	_end_off = strlen(fname);
	_end_pos = fname + _end_off + 1;

	/* iterate while charater is NOT null */
	while(*fname != '\0') {

		/*
		 * If the position is not NULL,
		 * we have found a space character, we move every thing from
		 * this position to the end
		 */
		if(_pos != NULL) {

			/* move every thing to the right */
			memmove(++fname, _pos+sizeof(char), _end_pos - (_pos+sizeof(char)));

			/* fill the blanks with escaped space */

			*_pos = USENET_BACKSLASH_CHAR;
			*(_pos + sizeof(char)) = USENET_SPACE_CHAR;
		}

		/*
		 * If space character was found we record it
		 * on the next round, we set it to NULL.
		 */
		if(*fname == USENET_SPACE_CHAR)
			_pos = fname;
		else
			_pos = NULL;

		fname++;
	}

	return USENET_SUCCESS;
}


int usenet_utils_cons_new_fname(const char* dir, const char* fname, char** nbuf, size_t* sz)
{
	*sz = strlen(dir) + strlen(fname) + 2;

	*nbuf = (char*) malloc(*sz * sizeof(char));
	sprintf(*nbuf, "%s/%s", dir, fname);

	return USENET_SUCCESS;
}

/* helper method for renaming the file */
static const int _usenet_utils_rename_helper(struct  usenet_nzb_filellist* list, const char* file_path)
{
	size_t _rfname_sz = 0;
	char* _rfname = NULL;
	const char* _ext = NULL;
	int _ret = USENET_SUCCESS;

	/* get file extension */
	_ext = _usenet_utils_get_ext(file_path);

	/* make the new file name */
	_rfname_sz = strlen(list->_dest_dir) + strlen(list->_u_std_fname) + strlen(_ext) + 1;
	_rfname = (char*) malloc((_rfname_sz + 1) * sizeof(char));
	sprintf(_rfname, "%s/%s%s", list->_dest_dir, list->_u_std_fname, _ext);

	USENET_LOG_MESSAGE_ARGS("renaming file %s to %s", file_path, _rfname);

	/* we only do a rename if a original and the new paths are different */
	if(!strcmp(file_path, _rfname) || !rename(file_path, _rfname)) {
		/* errors have occured */
		USENET_LOG_MESSAGE_ARGS("errors occured while renaming the file %s", strerror(errno));
		_ret = USENET_ERROR;
	}

	free(_rfname);
	_rfname = NULL;

	return _ret;
}

/* find the last occurance of a period character */
static inline __attribute__ ((always_inline)) const char* _usenet_utils_get_ext(const char* fname)
{
	const char* _pos = NULL;
	do {
		_pos = fname;
	} while((fname = strchr(fname+1, USENET_FULLSTOP_CHAR)));

	return _pos;
}
