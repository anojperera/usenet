/*
 * Header file for all interaces
 *Sun Jan 25 12:52:01 GMT 2015
 */

#ifndef _USENET_H_
#define _USENET_H_

#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <libconfig.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "jsmn.h"

#define USENET_SUCCESS 0
#define USENET_ERROR -1
#define USENET_ARG_ERROR -2
#define USENET_EXT_LIB_ERROR -3

#define USENET_CMD_BUFF_SZ 1
#define USENET_JSON_BUFF_SZ 151
#define USENET_LOG_MESSAGE_SZ 256
#define USENET_PROC_FILE_BUFF_SZ 512
#define USENET_PROC_NAME_BUFF_SZ 256
#define USENET_NZB_FILESTAT 2032

#define USENET_PULSE_SENT 1
#define USENET_PULSE_RESET 0

#define USENET_NEW_LINE_CHAR 10
#define USENET_ARRAY_CHAR_BEGIN 91
#define USENET_BACKSLASH_CHAR 92
#define USENET_ARRAY_CHAR_END 93
#define USENET_SPACE_CHAR 32
#define USENET_DOUBLEQ_CHAR 34
#define USENET_PLUS_CHAR 43
#define USENET_USCORE_CHAR 95
#define USENET_FULLSTOP_CHAR 46

#define USENET_BLANKSPACE_CHAR USENET_SPACE_CHAR

#define USENET_JSON_FN_HEADER "rpc"
#define USENET_JSON_ARG_HEADER "args"

#define USENET_JSON_FN_1 "usenet_complete"
#define USENET_JSON_FN_2 "usenet_update_list"

#define USENET_NZB_SUCCESS "SUCCESS/UNPACK"

#define USENET_NZBGET_XMLRESPONSE_VALUE "value"
#define USENET_NZBGET_XMLRESPONSE_ARRAY "array"
#define USENET_NZBGET_XMLRESPONSE_MEMBER "member"
#define USENET_NZBGET_XMLRESPONSE_NAME "name"

struct gapi_login
{
    const char* p12_path;			/* path for the p12 cert */
    const char* iss;				/* email address of the service account */
    const char* scope;				/* API scope */
    const char* aud;				/* Descriptor of intended target assertion */
    const char* sub;				/* Email address of the user */
    const char* alg;				/* loging algorythm */
    const char* typ;				/* JSON token typ */
	const char* server_name;		/* server name */
	const char* server_port;		/* server port */
	const char* nzburl;				/* search url */
	const char* mac_addr;			/* mac address of the client */
	const char* ssh_user;			/* ssh user name */
	const char* rsa_public_key;		/* public key path */
	const char* rsa_private_key;	/* private key path */
	const char* ssh_port;			/* ssh port number */

	int scan_freq;					/* frequency scan the instructions */
    int exp;						/* expiry time since unix start */
    int iat;						/* start time since unix start time */
	int svr_wait_time;				/* default server wait time */
	int nzb_fsize_threshold;		/* file size tolerance */

	config_t _config;
};

/* Message structure */
struct usenet_message
{
	unsigned char ins __attribute__ ((aligned (USENET_CMD_BUFF_SZ)));
	char msg_body[USENET_JSON_BUFF_SZ];
};

/* Usenet string array */
struct usenet_str_arr
{
	char** _arr;
	size_t _sz;
};

/* struct for getting the nzb file state */
struct usenet_nzb_filellist
{
	int _nzb_id;
	int _file_size;
	int _remaining_size;
	int _active_downloads;

	char* _nzb_file_name;
	char* _nzb_name;
	char* _dest_dir;
	char* _final_dir;
	char* _status;

	char* _u_std_fname;
	char* _u_r_fpath;									/* full path of the renamed file */
};

/*
 * Helper macros for free the elements.
 */
#define USENET_FILELIST_FREE(item)				\
	if((item)->_nzb_file_name)					\
		free((item)->_nzb_file_name);			\
	if((item)->_nzb_name)						\
		free((item)->_nzb_name);				\
	if((item)->_dest_dir)						\
		free((item)->_dest_dir);				\
	if((item)->_final_dir)						\
		free((item)->_final_dir);				\
	if((item)->_status)							\
		free((item)->_status);					\
	if((item)->_u_std_fname)					\
		free((item)->_u_std_fname);				\
	if((item)->_u_r_fpath)						\
		free((item)->_u_r_fpath);				\
	(item)->_nzb_file_name = NULL;				\
	(item)->_nzb_name = NULL;					\
	(item)->_dest_dir = NULL;					\
	(item)->_final_dir = NULL;					\
	(item)->_status = NULL;						\
	(item)->_u_std_fname = NULL;				\
	(item)->_u_r_fpath = NULL

/* initialise the elements */
#define USENET_NZBGET_INIT_LIST(list)			\
	(list)->_nzb_id = 0;						\
	(list)->_file_size = 0;						\
	(list)->_remaining_size = 0;				\
	(list)->_active_downloads = 0;				\
	(list)->_nzb_file_name = NULL;				\
	(list)->_nzb_name = NULL;					\
	(list)->_dest_dir = NULL;					\
	(list)->_final_dir = NULL;					\
	(list)->_status = NULL;						\
	(list)->_u_std_fname = NULL;				\
	(list)->_u_r_fpath = NULL

int usenet_utils_load_config(struct gapi_login* login);
int usenet_utils_destroy_config(struct gapi_login* login);

#define usenet_destroy_config usenet_utils_destroy_config

/* message struc handler methods */
int usenet_message_init(struct usenet_message* msg);								/* Initialise the message struct */
int usenet_message_request_instruct(struct usenet_message* msg);				/* Send a request instruction to client */
int usenent_message_response_instruct(struct usenet_message* msg);				/* Response to request */
int usenet_nzb_search_and_get(const char* nzb_desc, const char* s_url);			/* search get and issue rpc call to nzbget */
int usenet_read_file(const char* path, char** buff, size_t* sz);					/* Read contents of a file pointed by file path */

pid_t usenet_find_process(const char* pname);									/* find process id */

/*
 * Count the number of blank spaces in a given string,
 * the string is expected to be NULL terminated.
 */
size_t usenet_utils_count_blanks(const char* message);

/*
 * Utility method for removing unwanted characters.
 */
int usenet_utils_remove_chars(char* str, size_t len);

/*
 * Get the access time difference between present time.
 */
int usenet_utils_time_diff(const char* file);


int usenet_utils_append_std_fname(struct usenet_nzb_filellist* list);

/*
 * Standardise file name by replacing space characters
 * with an underscore.
 */
int usenet_utils_stdardise_file_name(char* file_name);

/*
 * Rename file to the new path
 */
int usenet_utils_rename_file(struct usenet_nzb_filellist* list, int threshold);

/*
 * Escape blank spaces of a filename/ file path
 */
int usenet_utils_escape_blanks(char* fname, size_t sz);

/*
 * Construct new file name
 */
int usenet_utils_cons_new_fname(const char* dir, const char* fname, char** nbuf, size_t* sz);


/*
 * JSON Parser helper methods
 */
int usjson_parse_message(const char* msg, jsmntok_t** tok, int* num);
int usjson_get_token(const char* msg, jsmntok_t* tok, size_t num_tokens, const char* key, char** value, jsmntok_t** obj);
int usjson_get_token_arr_as_str(const char* msg, jsmntok_t* tok, struct usenet_str_arr* str_arr);

/*
 * nzbget methods
 */
int usenet_update_nzb_list(void);
int usenet_nzb_scan(void);
int usenet_nzb_get_filelist(struct usenet_nzb_filellist** f_list, size_t* num);
int usenet_nzb_get_history(struct usenet_nzb_filellist** f_list, size_t* num);
int usenet_nzb_delete_item_from_history(int* ids, size_t num);


/*
 * xml rpc methods
 */
int usenet_uxmlrpc_call(const char* method_name, char** paras, size_t size, xmlDocPtr* res);
int usenet_uxmlrpc_get_node_count(xmlNodePtr root_node, const char* key, int* count, xmlNodePtr* node);
int usenet_uxmlrpc_get_member(xmlNodePtr member_node, const char* name, char** value);

#define USENET_REQUEST_RESPONSE 0x00
#define USENET_REQUEST_RESPONSE_PENDING 0x01
#define USENET_REQUEST_RESET 0x02
#define USENET_REQUEST_COMMAND 0x03
#define USENET_REQUEST_DOWNLOAD 0x04
#define USENET_REQUEST_FUNCTION 0x05
#define USENET_REQUEST_PULSE 0x06
#define USENET_REQUEST_BROADCAST 0x07

/* helper method for logging */
static inline __attribute__ ((always_inline)) void _usenet_log_message(const char* msg, const char* fname, int line)
{
	struct tm* _time;
	int _len = 0;
	char _buff[USENET_LOG_MESSAGE_SZ] = {0};

	/* Get current time */
	time_t _now = time(NULL);

	_time = gmtime(&_now);

	_len = strftime (_buff, USENET_LOG_MESSAGE_SZ, "[%Y-%m-%dT%H:%M:%S] ", _time);

	/* format the rest of the message */
	sprintf(_buff+_len, "[%d] %s", getpid(), msg);

	/* print to file */
    fprintf(stdout, "%s - %s line %i\n", _buff, fname, line);
	return;
}

/* helper method for loggin with arguments (not strict inlined) */
static inline void _usenet_log_message_args(const char* msg, const char* fname, int line, ...)
{
	/* format the message */
	va_list _list;

	char _buff[USENET_LOG_MESSAGE_SZ] = {};
	va_start(_list, line);
	vsprintf(_buff, msg, _list);
	va_end(_list);
	_usenet_log_message(_buff, fname, line);

	return;
}

#define USENET_LOG_MESSAGE(msg)					\
	_usenet_log_message(msg, __FILE__, __LINE__)
#define USENET_LOG_MESSAGE_ARGS(msg, ...)		\
	_usenet_log_message_args(msg, __FILE__, __LINE__, __VA_ARGS__)


#define USENET_CONV_MB(sz)						\
	sz / (1000 * 1000)
#endif /* _USENET_H_ */
