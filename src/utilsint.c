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
#include <libssh2.h>
#include "thcon.h"
#include "usenet.h"

#define USENET_SETTINGS_FILE "../config/usenet.cfg"
#define USENET_PROC_PATH "/proc"
#define USENET_DESTINATION_PATH_SZ 512
#define USENET_SCP_BUF_SZ 1024

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
	USENET_GET_SETTING_STRING(ssh_user);
	USENET_GET_SETTING_STRING(rsa_public_key);
	USENET_GET_SETTING_STRING(rsa_private_key);
	USENET_GET_SETTING_STRING(ssh_port);
	USENET_GET_SETTING_STRING(destination_folder);
	USENET_GET_SETTING_STRING(log_to_file);
	USENET_GET_SETTING_STRING(log_file_path);
	USENET_GET_SETTING_STRING(scp_progress);
	USENET_GET_SETTING_INT(scan_freq);
	USENET_GET_SETTING_INT(svr_wait_time);
	USENET_GET_SETTING_INT(nzb_fsize_threshold);
	USENET_GET_SETTING_INT(progress_update_interval);

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

/* Initialise with size */
int usenet_message_init_with_sz(struct usenet_message* msg, size_t sz)
{
	if(msg == NULL)
		return USENET_ERROR;

	memset(msg, 0, sizeof(struct usenet_message));

	/* Check the difference in size */
	if(sz < (USENET_CMD_BUFF_SZ + USENET_SIZE_BUFF_SZ))
		return USENET_ERROR;

	/* create the message and initialise the buffer */
	USENET_CREATE_MESSAGE(msg, sz - (USENET_CMD_BUFF_SZ + USENET_SIZE_BUFF_SZ));

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
	struct stat _fstat = {0};
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
int usenet_utils_remove_chars(char** str, size_t len)
{
	int _cnt = 0;
	char* _str_cp = NULL;									/* copy of the string */
	char* _ptr_s = NULL, *_ptr_d = NULL;

	/* copy the string to local */
	_str_cp = (char*) malloc(sizeof(char) * (len + 1));
	memset(_str_cp, 0, len + 1);
	strncpy(_str_cp, *str, len);

	/* set the source pointer to the copy of the string */
	_ptr_s = _str_cp;
	_ptr_d = *str;

	/* copy the legal characters to the destination */
	do {
		switch(*_ptr_s) {
		default:
		case USENET_SPACE_CHAR:
			/*
			 * if its not the first position or the last
			 * copy the space character.
			 */
			if(_cnt != 0 || _cnt != len-1) {
				*_ptr_d = *_ptr_s;
				_ptr_d++;
			}
			break;
		case USENET_NEW_LINE_CHAR:
        case USENET_ARRAY_CHAR_BEGIN:
        case USENET_ARRAY_CHAR_END:
        case USENET_DOUBLEQ_CHAR:
        case USENET_BACKSLASH_CHAR:
		case USENET_OBJECTOP_CHAR:
		case USENET_OBJECTCL_CHAR:
			break;
		}
		_ptr_s++;
		_cnt++;
	} while(*_ptr_s != '\0');

	/*
	 * Free the original and set the new pointer
	 * to the old position.
	 */
	free(_str_cp);
	*_ptr_d = '\0';
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

	int _ret = USENET_ERROR;
	DIR* _nzb_dir = NULL;
	struct dirent* _dir_ent = NULL;

	struct stat _sbuf = {0};

	/* open the download directory */
	_nzb_dir = opendir(list->_dest_dir);
	if(_nzb_dir == NULL) {
		USENET_LOG_MESSAGE_ARGS("unable to open the directory: %s, exiting rename process.", list->_dest_dir);

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
			_ret = USENET_SUCCESS;
			break;
		}

		if(_nfname)
			free(_nfname);

		_nfname = NULL;
	}

	/* close the directory */
	closedir(_nzb_dir);

	if(_nfname && _ret == USENET_SUCCESS) {

		USENET_LOG_MESSAGE_ARGS("file found %s, with %dMB, list size %dMB", _nfname, USENET_CONV_MB(_sbuf.st_size), list->_file_size);
		_ret = _usenet_utils_rename_helper(list, _nfname);
	}

	if(_nfname) {
		free(_nfname);
		_nfname = NULL;
	}

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

/* Create destination path */
int usenet_utils_create_destinatin_path(struct gapi_login* config, struct usenet_nzb_filellist* list, char** dest, size_t* dest_sz)
{
	char _buf[USENET_DESTINATION_PATH_SZ] = {0};
	char _fname_buf[USENET_DESTINATION_PATH_SZ] = {0};

	const char* _fname = list->_u_std_fname;
	const char* _ext = _usenet_utils_get_ext(list->_u_r_fpath);

	char* _end_pos = (char*) _fname + strlen(_fname);
	char* _itr = _end_pos;

	USENET_LOG_MESSAGE("constructing destination path");
	/* find the first occurence of an under score from the end */
	do {
		if(*_itr == USENET_USCORE_CHAR) {
			_end_pos = _itr;
			break;
		}

		_itr--;
	}while(_itr != _fname);

	/* if _end_pos is still NULL we need to exit here */
	if(_end_pos == NULL) {
		USENET_LOG_MESSAGE("unable to construct the destination folder name from file name");
		return USENET_ERROR;
	}

	/* copy the memory of the file name  without season and episode numbers */
	memcpy(_fname_buf, _fname, _end_pos - _fname);

	/* create the full path */
	sprintf(_buf, "%s%s/%s%s",
			config->destination_folder,
			_fname_buf,
			_fname,
			_ext);

	/* get the actual size of the string and copy the file */
	*dest_sz = strlen(_buf);
	*dest = (char*) malloc(sizeof(char) * ((*dest_sz) + 1));
	strcpy(*dest, _buf);

	return USENET_SUCCESS;
}

/* scp the file from source to the destination */
int usenet_utils_scp_file(struct gapi_login* config,
						  const char* source,
						  const char* target,
						  int (*prog)(void*, float),
						  void* ext_obj)
{
	thcon _thcon;											/* connection object */
	int _sock = -1, _fd = -1, _nread = 0, _rc = 0, _prog = 0;
	int _ret = USENET_ERROR;
	char _buf[USENET_SCP_BUF_SZ] = {0};						/* read buffer */
	char* _w_ptr = NULL;									/* pointer to the buffer writing to channel */

	struct stat _fstat = {0};

    LIBSSH2_SESSION *_session = NULL;
    LIBSSH2_CHANNEL *_channel = NULL;

	/* iniialise the connection object */
	USENET_LOG_MESSAGE("creating connection object");
	thcon_init(&_thcon, thcon_mode_client);

	/* set destination and port address */
	thcon_set_server_name(&_thcon, config->server_name);
	thcon_set_port_name(&_thcon, config->ssh_port);

	/* create a raw socket */
	_sock = thcon_create_raw_sock(&_thcon);
	if(_sock <= 0) {
		USENET_LOG_MESSAGE("unable to create a raw socket for the scp");
		goto cleanup;
	}

	/* create a new ssh session */
	USENET_LOG_MESSAGE("initialising ssh session");
	_session = libssh2_session_init();
	if(!_session) {
		USENET_LOG_MESSAGE("unable to create ssh session");
		goto cleanup;
	}

	/* start a handshake */
	USENET_LOG_MESSAGE("ssh handshake");
	if(libssh2_session_handshake(_session, _sock)) {
		USENET_LOG_MESSAGE("ssh handshake failed");
		goto cleanup;
	}

	/* authenticate using public key */
	USENET_LOG_MESSAGE("authenticating using public/private");
	if(libssh2_userauth_publickey_fromfile(_session,
										   config->ssh_user,
										   config->rsa_public_key,
										   config->rsa_private_key,
										   NULL)) {
		USENET_LOG_MESSAGE("unable to authenticate connection");
		goto cleanup;
	}

	USENET_LOG_MESSAGE("ssh connection authenticated successfully");

	/* open file and get stats */
	USENET_LOG_MESSAGE("openning source file and getting stats");
	_fd = open(source, O_RDONLY);

	if(_fd < 1) {
		USENET_LOG_MESSAGE_ARGS("unable to open the file %s", source);
		goto cleanup;
	}

	fstat(_fd, &_fstat);
	USENET_LOG_MESSAGE_ARGS("creating channel for target %s", target);
	_channel = libssh2_scp_send(_session,
								target,
								_fstat.st_mode & 0777,
								(unsigned long) _fstat.st_size);
	/* errors have occured */
	if(!_channel) {
		USENET_LOG_MESSAGE("errors occured while creating a channel");
		goto cleanup;
	}

	/* read chunk at a time and write to the channel */
	do {
		_nread = read(_fd, &_buf, USENET_SCP_BUF_SZ);

		/* break from loop if the read size less or equal to zero */
		if(_nread <= 0)
			break;

		_w_ptr = _buf;

		/* handle progress */
		_prog += _nread;

		do {

			/* write all of the read bytes until done */
			_rc = libssh2_channel_write(_channel, _w_ptr, _nread);
			if(_rc < 0) {
				USENET_LOG_MESSAGE("errors occured writing to channel");
				goto cleanup;
			}

			/*
			 * increment write position of the buffer and decrement the number of
			 * bytes written from the total read.
			 */
			_w_ptr += _rc;
			_nread -= _rc;

		} while(_nread);

		/* if the callback function is supplied with this we call to indicate progress */
		if(prog != NULL)
			prog(ext_obj,  (float) ((float) _prog / (float) _fstat.st_size));

	}while(1);

	/* send ending characters */
	libssh2_channel_send_eof(_channel);

	libssh2_channel_wait_eof(_channel);

	libssh2_channel_wait_closed(_channel);

	_ret = USENET_SUCCESS;

cleanup:

	USENET_LOG_MESSAGE("ssh cleanup...");

	/* free the channel */
	if(_channel)
		libssh2_channel_free(_channel);
	_channel = NULL;

	/* close the open file descriptor */
	if(_fd > 0)
		close(_fd);

	/* close the session */
	if(_session) {
		libssh2_session_disconnect(_session, "ssh session shutdown");
		libssh2_session_free(_session);
	}

	/* close descriptor if was open */
	if(_sock > 0)
		close(_sock);


	/* delete connection object */
	thcon_delete(&_thcon);
	return _ret;
}

/* Serialise the message into buffer */
int usenet_serialise_message(struct usenet_message* msg, data** buff, size_t* sz)
{
	/* get the buffer size */
	*sz += USENET_CMD_BUFF_SZ + USENET_SIZE_BUFF_SZ + USENET_GET_MSG_SIZE(msg);

	*buff = malloc(sizeof(char) * (*sz));

	/* pack the buffer with message contents */
	memcpy(*buff, msg->ins, USENET_CMD_BUFF_SZ);
	memcpy(*buff + USENET_CMD_BUFF_SZ, msg->size, USENET_SIZE_BUFF_SZ);
	memcpy(*buff + USENET_CMD_BUFF_SZ + USENET_SIZE_BUFF_SZ, msg->msg_body, msg->size);

	/* return the total size */
	return USENET_SUCCESS;
}

/* unserialise the buffer */
int usenet_unserialise_message(const data* buff, const size_t sz, struct usenet_message* msg)
{
	size_t _buff_sz = 0;

	/* check buffer size */
	if(sz < (USENET_CMD_BUFF_SZ + USENET_SIZE_BUFF_SZ))
		return USENET_ERROR;

	/* copy the bytes to the message struct */
	memcpy(&msg->ins, buff, USENET_CMD_BUFF_SZ);
	memcpy(&msg->size, buff + USENET_CMD_BUFF_SZ, USENET_SIZE_BUFF_SZ);

	msg->msg_body = (char*) malloc(sizeof(char) * (sz - (USENET_CMD_BUFF_SZ + USENET_SIZE_BUFF_SZ)));
	memcpy(msg->msg_body, buff + USENET_CMD_BUFF_SZ + USENET_SIZE_BUFF_SZ, msg->size);

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

	/*
	 * We only do a rename if the original and the new paths are different.
	 * If they are the same we return success and continue with scp
	 */
	if(strcmp(file_path, _rfname) != 0 && rename(file_path, _rfname)) {
		/* errors have occured */
		USENET_LOG_MESSAGE_ARGS("errors occured while renaming the file with error %i - %s", errno, strerror(errno));
		_ret = USENET_ERROR;
	}

	/* set the new file name pointer to the list item */
	list->_u_r_fpath = _rfname;

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
