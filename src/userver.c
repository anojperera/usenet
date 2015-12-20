/*
 * Server for controlling the other machine for downloading nzbs.
 * This sends magic packet to wake the machine with mac address
 * and waits for the client to send the confirmation signal.
 * Subsequently,
 */

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "usenet.h"
#include "thcon.h"

#define USENET_SERVER_MSG_SZ 256
#define USENET_SERVER_JSON_SZ 151

#define USENET_SERVER_JSON_FN_HEADER USENET_JSON_FN_HEADER
#define USENET_SERVER_JSON_ARG_HEADER USENET_JSON_ARG_HEADER
#define USENET_SERVER_JSON_FN_NAME "usenet_nzb_search_and_get"
#define USENET_SERVER_JSON_PATH "../resource/req.json"

/* struct to encapsulate server component */
struct userver
{
	int active_sock;										/* active socket */
	const char* _server_name;
	const char* _server_port;

	struct gapi_login _login;
	thcon _connection;

	int _active_sock;										/* active connection socket */
	unsigned int _act_ix;									/* index for action */
	volatile sig_atomic_t _conn_flg;						/* flag to indicate connection was made */
	volatile sig_atomic_t _accept_flg;						/* flag to indicate handshake was made */
};

static int _data_receive_callback(void* self, void* data, size_t sz);
static int _conn_made(void* self, void* conn);
static int _conn_closed(void* self, void* conn, int socket);
static int _initialise_contact(struct userver* svr);
static void _signal_hanlder(int signal);							/* signal handler */
static int _msg_handler(struct userver* svr, struct usenet_message* msg);

static inline __attribute__ ((always_inline)) int _send_function_req(struct userver* svr, struct usenet_message* msg);
static inline __attribute__ ((always_inline)) int _send_reset_req(struct userver* svr, struct usenet_message* msg);
static inline __attribute__ ((always_inline)) int _reset_ix_conn_flg(struct userver* svr);

/* Methods for constructing jsons */
static int _msg_get_nzb(const char* nzb, struct usenet_message* msg);

/* starts  the server */
int init_server(struct userver* svr);
int stop_server(struct userver* svr);



struct userver server;												/* server */
volatile sig_atomic_t term_sig = 1;									/* signal */
int main(int argc, char** argv)
{
	int sec_cnt = 0;												/* second counter */
	struct usenet_message _msg = {0};

	if(init_server(&server) == USENET_ERROR)
		return USENET_ERROR;

	signal(SIGINT, _signal_hanlder);
	while(term_sig) {

		if(sec_cnt++ % server._login.scan_freq) {
			_initialise_contact(&server);
		}

		/* send client a new function request if the file was updated */
		if(usenet_utils_time_diff(USENET_SERVER_JSON_PATH) == server._login.scan_freq) {
			thcon_wol_device(&server._connection, server._login.mac_addr);
			USENET_LOG_MESSAGE("request json changed, sending request to client to reset index");

			/* reset the index and action flg*/
			usenet_message_init(&_msg);
			_send_reset_req(&server, &_msg);
		}

		sleep(1);
	}

	/* stop the server */
	stop_server(&server);
	USENET_LOG_MESSAGE("server stopped");

	/* stop the server */
	USENET_LOG_MESSAGE("good bye");
    return 0;
}


/* initialise method */
int init_server(struct userver* svr)
{
	int status = 0;
	svr->_server_name = NULL;
	svr->_server_port = NULL;

	memset(svr, 0, sizeof(struct userver));

	/* initialise config object */
	if(usenet_utils_load_config(&svr->_login) != USENET_SUCCESS) {
		USENET_LOG_MESSAGE("unable to read the configuration object");
		return USENET_ERROR;
	}

	/* set the server port and name to local */
	USENET_LOG_MESSAGE("setting server port and name to local from config file");
	svr->_server_name = svr->_login.server_name;
	svr->_server_port = svr->_login.server_port;
	svr->_active_sock = 0;


	/* launch connection object as a server */
	USENET_LOG_MESSAGE("initiating connection object..");
	status = thcon_init(&svr->_connection, thcon_mode_server);


	thcon_set_subnet(&svr->_connection, "255.255.255.255");

	/* start client */
	USENET_LOG_MESSAGE_ARGS("sending magic packet to device: %s", svr->_login.mac_addr);
	status = thcon_wol_device(&svr->_connection, svr->_login.mac_addr);
	if(status == -1)
		USENET_LOG_MESSAGE_ARGS("errors occured while sending magic packet to %s", svr->_login.mac_addr);
	else
		USENET_LOG_MESSAGE("magic packet sent");

	if(status) {
		USENET_LOG_MESSAGE("unable to initialise the connection object");
		return USENET_ERROR;
	}

	/* set port and server */
	thcon_set_server_name(&svr->_connection, svr->_server_name);
	thcon_set_port_name(&svr->_connection, svr->_server_port);

	/* assign callbacks */
	USENET_LOG_MESSAGE("setting callbacks to the connection object");
	thcon_set_conmade_callback(&svr->_connection, _conn_made);
	thcon_set_closed_callback(&svr->_connection, _conn_closed);
	thcon_set_recv_callback(&svr->_connection, _data_receive_callback);

	thcon_set_ext_obj(&svr->_connection, svr);

	/* start the server */
	USENET_LOG_MESSAGE("connection initialised, starting service");
	status = thcon_start(&svr->_connection);

	/* initialise the connection atomic variable */
	svr->_conn_flg = 0;
	svr->_active_sock = 0;
	svr->_accept_flg = 0;
	svr->_act_ix = 0;

	if(status != 0) {
		USENET_LOG_MESSAGE("server not started successfully");
		return USENET_ERROR;
	}

	return USENET_SUCCESS;
}

int stop_server(struct userver* svr)
{
	thcon_stop(&svr->_connection);
	return USENET_SUCCESS;
}


/* main method for handling received data */
static int _data_receive_callback(void* self, void* data, size_t sz)
{
	struct userver* _server = NULL;
	struct usenet_message _msg;


	if(sz <= 0)
		return USENET_ERROR;

	if(self == NULL)
		return USENET_ERROR;

	_server = (struct userver*) self;
	usenet_message_init(&_msg);
	usenet_unserialise_message(data, sz, &_msg);

	USENET_LOG_MESSAGE_ARGS("message received from client, action ix: %i", _server->_act_ix);
	_msg_handler(_server, &_msg);

	/* destroy the message object */
	USENET_DESTROY_MESSAGE_BUFFER(&_msg);

	return USENET_SUCCESS;
}

static int _conn_closed(void* self, void* conn, int socket)
{
	struct userver* _server = NULL;

	USENET_LOG_MESSAGE("connection closed");

	if(self == NULL)
		return USENET_ERROR;

	_server = (struct userver*) self;
	if(socket == _server->_active_sock) {
		_server->_conn_flg = 0;
		_server->_active_sock = 0;
		_server->_accept_flg = 0;
		_server->_act_ix = 0;
	}

	return USENET_SUCCESS;
}

static int _conn_made(void* self, void* conn)
{
	thcon* _conn = (thcon*) conn;
	struct userver* _svr = (struct userver*) self;
	USENET_LOG_MESSAGE("connection made, checking for existing connections");

	/*
	 * Check if a connection is made and the active socket was set.
	 * If these were set we don't want to handle any other connections
	 * therefore we exit method.
	 */
	if(_svr->_conn_flg > 0 && _svr->active_sock > 0)
		return USENET_ERROR;

	USENET_LOG_MESSAGE("no other connections are made, therefore proceeding with instructions");
	/* set the active socket to this connection */
	_svr->_active_sock = THCON_GET_ACTIVE_SOCK(_conn);

	/*
	 * On first connection we set the flag to true. If this method is called
	 * again it will not handle any processing on this connection.
	 */
	_svr->_conn_flg = 1;
	_svr->_accept_flg = 1;

	return 0;
}

/* Send message to the client */
static int _initialise_contact(struct userver* svr)
{
	void* _data = NULL;
	size_t _size = 0;
	struct usenet_message _msg;

	if(svr->_conn_flg == 0)
		return USENET_SUCCESS;

	if(svr->_active_sock <= 0)
		return USENET_SUCCESS;

	if(svr->_accept_flg <= 0)
		return USENET_SUCCESS;

	/* send message to client */
	usenet_message_init(&_msg);
	usenet_message_request_instruct(&_msg);

	/* serialise the message */
	USENET_LOG_MESSAGE("serialising message");
	usenet_serialise_message(&_msg, &_data, &_size);

	USENET_LOG_MESSAGE("sending handshake to client waiting for response");
	thcon_send_info(&svr->_connection, _data, _size);

	free(_data);

	return USENET_SUCCESS;
}

static void _signal_hanlder(int signal)
{
	USENET_LOG_MESSAGE("stopping server");
	term_sig = 0;
}

static int _msg_handler(struct userver* svr, struct usenet_message* msg)
{
	void* _data;
	size_t _size;

	/* check if handshake is required */
	switch(svr->_act_ix) {
	case 0:
		if(svr->_accept_flg <= 0 || msg->ins != USENET_REQUEST_RESPONSE)
			break;
		USENET_LOG_MESSAGE_ARGS("response accepted from client with ins: %x", msg->ins);
		svr->_accept_flg = 0;

		/* send a json request */
		_send_function_req(svr, msg);

		/* increment action index to the next message */
		svr->_act_ix++;
		break;
	case 1:
		if(msg->ins != USENET_REQUEST_RESPONSE)
			break;
		USENET_LOG_MESSAGE_ARGS("response accepted from client with ins: %x", msg->ins);
		svr->_act_ix++;
		break;
	case 2:
		if(msg->ins != USENET_REQUEST_RESPONSE)
			break;

		USENET_LOG_MESSAGE("responding to clients reset response with a command request");

		usenet_message_init(msg);
		usenet_message_request_instruct(msg);

		/*
		 * Reset the connection flag and action index,
		 * the message handler then go through another iteration
		 * of the action list.
		 */
		_reset_ix_conn_flg(svr);

	default:
		break;
	}

	if(msg->ins == USENET_REQUEST_PULSE)
		USENET_LOG_MESSAGE("responding to client's pulse");

	if(msg->ins == USENET_REQUEST_BROADCAST)
		USENET_LOG_MESSAGE("responding to client's broadcast request");

	/* echo back the message if its pulse or broadcast */
	if(msg->ins == USENET_REQUEST_PULSE || msg->ins == USENET_REQUEST_BROADCAST) {
		usenet_serialise_message(msg, &_data, &_size);
		thcon_send_info(&svr->_connection, _data, _size);
		USENET_LOG_MESSAGE("sent client response");

		if(_data != NULL && _size > 0)
			free(_data);
	}

	return 0;
}

/* construct message */
static int _msg_get_nzb(const char* nzb, struct usenet_message* msg)
{
	char* _buff = NULL;
	size_t _sz = 0;

	USENET_LOG_MESSAGE("constructing json rpc message");

	/* if nzb is not NULL */
	if(nzb != NULL) {
		msg->msg_body = (char*) malloc(sizeof(char) * USENET_JSON_BUFF_SZ);
		memset(msg->msg_body, 0, sizeof(char)*USENET_JSON_BUFF_SZ);
		msg->size += USENET_JSON_BUFF_SZ;

		return snprintf(msg->msg_body, USENET_JSON_BUFF_SZ,
						"{%s:%s,%s:[%s]}",
						USENET_SERVER_JSON_FN_HEADER,
						USENET_SERVER_JSON_FN_NAME,
						USENET_SERVER_JSON_ARG_HEADER,
						nzb);
	}

	/* read json from file */
	usenet_read_file(USENET_SERVER_JSON_PATH, &_buff, &_sz);

	if(_buff != NULL && _sz > 0) {
		USENET_LOG_MESSAGE("copying request json to message body");
		msg->msg_body = (char*) malloc(_sz+1);
		strncpy((char*) msg->msg_body, _buff, _sz);
		msg->msg_body[_sz] = '\0';
		msg->size += _sz;
	}
	if(_buff != NULL)
		free(_buff);

	return USENET_SUCCESS;
}

static inline __attribute__ ((always_inline)) int _send_function_req(struct userver* svr, struct usenet_message* msg)
{
	void* _data = NULL;
	size_t _size = 0;

	msg->ins = USENET_REQUEST_FUNCTION;
	_msg_get_nzb(NULL, msg);

	USENET_LOG_MESSAGE("serialising the buffer");
	usenet_serialise_message(msg, &_data, &_size);

	USENET_LOG_MESSAGE("sending message to client");
	thcon_send_info(&svr->_connection, _data, _size);

	/* once sent destroy the buffer */
	free(_data);
	return USENET_SUCCESS;
}

static inline __attribute__ ((always_inline)) int _send_reset_req(struct userver* svr, struct usenet_message* msg)
{
	void* _data = NULL;
	size_t _size = 0;

	if(svr->_conn_flg == 0)
		return USENET_SUCCESS;

	if(svr->_active_sock <= 0)
		return USENET_SUCCESS;

	if(svr->_accept_flg != 0)
		return USENET_SUCCESS;

	msg->ins = USENET_REQUEST_RESET;

	USENET_LOG_MESSAGE("serialising message");
	/* serialise the buffer */
	usenet_serialise_message(msg, &_data, &_size);

	USENET_LOG_MESSAGE("sending message to client reset request");
	thcon_send_info(&svr->_connection, _data, _size);

	free(_data);
	return USENET_SUCCESS;
}

/*
 * This method resets the action index and set the connection flag
 * back in to the accepted state.
 */
static inline __attribute__ ((always_inline)) int _reset_ix_conn_flg(struct userver* svr)
{
	svr->_act_ix = 0;
	svr->_accept_flg = 1;

	return USENET_SUCCESS;
}
