/*
 * Header file for all interaces
 *Sun Jan 25 12:52:01 GMT 2015
 */

#ifndef _USENET_H_
#define _USENET_H_

#include <stdlib.h>
#include <time.h>
#include <libconfig.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>

#define USENET_SUCCESS 0
#define USENET_ERROR -1
#define USENET_ARG_ERROR -2
#define USENET_EXT_LIB_ERROR -3

#define USENET_CMD_BUFF_SZ 1
#define USENET_JSON_BUFF_SZ 151
#define USENET_LOG_MESSAGE_SZ 256
#define USENET_PROC_FILE_BUFF_SZ 512
#define USENET_PROC_NAME_BUFF_SZ 256


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
	int scan_freq;					/* frequency scan the instructions */
    int exp;						/* expiry time since unix start */
    int iat;						/* start time since unix start time */
	config_t _config;
};

/* Message structure */
struct usenet_message
{
	unsigned char ins __attribute__ ((aligned (USENET_CMD_BUFF_SZ)));
	char msg_body[USENET_JSON_BUFF_SZ];
};

int usenet_utils_load_config(struct gapi_login* login);
int usenet_destroy_config(struct gapi_login* login);

/* message struc handler methods */
int usenet_message_init(struct usenet_message* msg);								/* Initialise the message struct */
int usenet_message_request_instruct(struct usenet_message* msg);				/* Send a request instruction to client */
int usenent_message_response_instruct(struct usenet_message* msg);				/* Response to request */
int usenet_nzb_search_and_get(const char* nzb_desc, const char* s_url);			/* search get and issue rpc call to nzbget */
int usenet_read_file(const char* path, char** buff, size_t* sz);					/* Read contents of a file pointed by file path */

pid_t usenet_find_process(const char* pname);									/* find process id */

#define USENET_REQUEST_RESPONSE 0x00
#define USENET_REQUEST_RESPONSE_PENDING 0x01
#define USENET_REQUEST_RESET 0x02
#define USENET_REQUEST_COMMAND 0x03
#define USENET_REQUEST_DOWNLOAD 0x04
#define USENET_REQUEST_FUNCTION 0x05

/* helper method for logging */
static inline __attribute__ ((always_inline)) void _usenet_log_message(const char* msg)
{
	struct tm* _time;
	int _len = 0;
	char _buff[USENET_LOG_MESSAGE_SZ] = {};

	/* Get current time */
	time_t _now = time(NULL);

	_time = gmtime(&_now);

	_len = strftime (_buff, USENET_LOG_MESSAGE_SZ, "[%Y-%m-%dT%H:%M:%S] ", _time);

	/* format the rest of the message */
	sprintf(_buff+_len, "%s", msg);

	/* print to file */
    fprintf(stdout, "%s - %s line %i\n", _buff, __FILE__, __LINE__);
	return;
}

/* helper method for loggin with arguments (not strict inlined) */
static inline void _usenet_log_message_args(const char* msg, ...)
{
	/* format the message */
	va_list _list;

	char _buff[USENET_LOG_MESSAGE_SZ] = {};
	va_start(_list, msg);
	vsprintf(_buff, msg, _list);
	va_end(_list);
	_usenet_log_message(_buff);

	return;
}
#define USENET_LOG_MESSAGE(msg)					\
	_usenet_log_message(msg)
#define USENET_LOG_MESSAGE_ARGS(msg, ...)		\
	_usenet_log_message_args(msg, __VA_ARGS__)
#define USENET_LOG_MESSAGE_WITH_INT(msg, val)	\
    fprintf(stdout, "[%s %s] %s%i - %s line %i\n", __DATE__, __TIME__, (msg), (val), __FILE__, __LINE__)
#define USENET_LOG_MESSAGE_WITH_STR(msg, val)	\
    fprintf(stdout, "[%s %s] %s%s - %s line %i\n", __DATE__, __TIME__, (msg), (val), __FILE__, __LINE__)

#endif /* _USENET_H_ */
