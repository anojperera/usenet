/*
 * Implementation of the usenet control client. When this starts, it looks for the server
 * and start sending messages. This client shuold also be abale to
 * shut the client machine down, when instructed by the server
 */
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <gqueue.h>
#include "usenet.h"
#include "thcon.h"

#define USENET_CLIENT_MSG_SZ 256
#define USENET_CLIENT_MSG_PULSE_GAP 5
#define USENET_CLIENT_NZBGET_CLIENT "nzbget"

/* struct to encapsulate server component */
struct uclient
{
	volatile sig_atomic_t _init_flg;							/* flag to indicate initialised struct */

	/*
	 * This flag is set to send a pulse to the server.
	 * If the server doesn't respond in the next two cycles,
	 * the client shall quit.
	 */
	volatile sig_atomic_t _pulse_sent;
	volatile sig_atomic_t _ini_wait_flg;						/* flag for the initial wait */
	unsigned int _act_ix;										/* index for the action to be taken */

	int _pulse_counter;											/* pulse counter */
	const char* _server_name;									/* server name */
	const char* _server_port;									/* port name */

	struct gapi_login _login;									/* struct containing login settings */
	pthread_t _thread;											/* thread */
	pthread_mutex_t _mutex;										/* queue mutex */
	thcon _connection;											/* connection object */
};

static int _data_receive_callback(void* self, void* data, size_t sz);
static void _signal_hanlder(int signal);
static inline __attribute__ ((always_inline)) int _default_response(struct uclient* client, struct usenet_message* msg);
static int _msg_handler(struct uclient* cli, struct usenet_message* msg);
static int _action_json(const char* json_msg);

/* static void* _thread_handler(void* obj); */

/* starts  the server */
int init_client(struct uclient* cli);
int stop_client(struct uclient* cli);

int pulse_client(struct uclient* cli);


struct uclient client;											/* client object */
volatile sig_atomic_t term_sig = 1;

int main(int argc, char** argv)
{
	int _cnt = 0;
	if(init_client(&client) == USENET_ERROR)
		return USENET_ERROR;

	signal(SIGINT, _signal_hanlder);
	while(term_sig) {
		if(client._init_flg ^ client._ini_wait_flg)
			pulse_client(&client);

		sleep(1);
		if(!(_cnt++ % client._login.svr_wait_time) && _cnt != 1) {
			client._ini_wait_flg ^= 1;
			_cnt = 0;
		}
	}

	/* stop the server */
	stop_client(&client);
	USENET_LOG_MESSAGE("client stopped");

	USENET_LOG_MESSAGE("good bye");
    return 0;
}

/* initialise method */
int init_client(struct uclient* cli)
{
	int status = 0;
	cli->_server_name = NULL;
	cli->_server_port = NULL;

	memset(cli, 0, sizeof(struct uclient));

	/* initialise config object */
	if(usenet_utils_load_config(&cli->_login) != USENET_SUCCESS) {
		USENET_LOG_MESSAGE("unable to read the configuration object");
		return USENET_ERROR;
	}

	/* set the server port and name to local */
	USENET_LOG_MESSAGE("setting server port and name to local from config file");
	cli->_server_name = cli->_login.server_name;
	cli->_server_port = cli->_login.server_port;

	/* launch connection object as a server */
	USENET_LOG_MESSAGE("initiating connection object..");
	status = thcon_init(&cli->_connection, thcon_mode_client);

	if(status) {
		USENET_LOG_MESSAGE("unable to initialise the connection object");
		return USENET_ERROR;
	}

	/* set port and server */
	thcon_set_server_name(&cli->_connection, cli->_server_name);
	thcon_set_port_name(&cli->_connection, cli->_server_port);

	/* assign callbacks */
	USENET_LOG_MESSAGE("setting callbacks to the connection object");
	thcon_set_recv_callback(&cli->_connection, _data_receive_callback);

	thcon_set_ext_obj(&cli->_connection, cli);

	/* start the server */
	USENET_LOG_MESSAGE("connection initialised, starting service");
	status = thcon_start(&cli->_connection);

	if(status != 0) {
		USENET_LOG_MESSAGE("server not started successfully");
		return USENET_ERROR;
	}

	cli->_pulse_sent = 0;
	cli->_init_flg = 0;
	cli->_pulse_counter = 0;
	cli->_act_ix = 0;

	/* initialise thread */

	return USENET_SUCCESS;
}


int stop_client(struct uclient* svr)
{
	thcon_stop(&svr->_connection);
	return USENET_SUCCESS;
}


/* main method for handling received data */
static int _data_receive_callback(void* self, void* data, size_t sz)
{
	struct uclient* _client = NULL;
	struct usenet_message _msg;

	/*
	 * If the size is less than 0 or the size is greater
	 * than the message size we exit
	 */
	if(sz <= 0 || sz > THORNIFIX_MSG_BUFF_SZ)
		return USENET_SUCCESS;

	USENET_LOG_MESSAGE("message received from server");

	_client = (struct uclient*) self;
	usenet_message_init(&_msg);
	memcpy(&_msg, data, sz);

	/* we receive some thing therefore pulse flag is reset */
	_client->_pulse_sent = USENET_PULSE_RESET;


	/*
	 * if a request instruction was sent, acknowledge
	 * to the server.
	 */
	_msg_handler(_client, &_msg);

	return 0;
}

int pulse_client(struct uclient* cli)
{
	struct usenet_message _msg;

	/*
	 * We only send a pulse every USENET_CLIENT_MSG_PULSE_GAP
	 */
	if(++cli->_pulse_counter < USENET_CLIENT_MSG_PULSE_GAP)
		return 0;

	/* reset counter back to 0 to start timer again */
	cli->_pulse_counter = 0;

	memset(&_msg, 0, sizeof(struct usenet_message));
	_msg.ins = USENET_REQUEST_PULSE;
	sprintf(_msg.msg_body, "%s", "working");

	/* get pointer to the connection object */
	thcon* _con = &cli->_connection;

	USENET_LOG_MESSAGE("sending server pulse");
	thcon_send_info(_con, (void*) &_msg, sizeof(struct usenet_message));

	/*
	 * Check if the server has responded to the previous.
	 * If haven't raise signal to terminate.
	 */
	if(cli->_pulse_sent & USENET_PULSE_SENT) {
		USENET_LOG_MESSAGE("no response from server, raising SIGINT");
		raise(SIGINT);
	}

	cli->_ini_wait_flg = 0;
	cli->_pulse_sent = USENET_PULSE_SENT;
	return 0;
}

static void _signal_hanlder(int signal)
{
	USENET_LOG_MESSAGE("stopping client");
	term_sig = 0;
}

/* Default reponse */
static int _default_response(struct uclient* client, struct usenet_message* msg)
{
	msg->ins = 0;
	thcon_send_info(&client->_connection, msg, sizeof(struct usenet_message));

	/*
	 * after successfully initialised, set the init flg
	 * to send a pulse to the server.
	 */
	client->_init_flg = 1;
	return 0;
}

static int _msg_handler(struct uclient* cli, struct usenet_message* msg)
{

	if(msg->ins == USENET_REQUEST_RESET) {
		USENET_LOG_MESSAGE("request reset complied");
		_default_response(cli, msg);
		cli->_act_ix = 0;
	}

	if(msg->ins == USENET_REQUEST_PULSE) {
		USENET_LOG_MESSAGE("server responded to pulse");

		/* return here as we no loger need to process the message */
		return USENET_SUCCESS;
	}

	switch(cli->_act_ix) {
	case 0:
		/* if the index is zero look for the messages request command */
		if(msg->ins == USENET_REQUEST_COMMAND) {
			USENET_LOG_MESSAGE("request command received, sending response");
			_default_response(cli, msg);
			cli->_act_ix++;
		}
		break;
	case 1:
		if(msg->ins == USENET_REQUEST_FUNCTION) {
			USENET_LOG_MESSAGE_ARGS("json rpc request received, message body: %s", msg->msg_body);
			_default_response(cli, msg);
			cli->_act_ix++;

			/* look for process */
			_action_json(msg->msg_body);
		}
		break;
	default:
		USENET_LOG_MESSAGE("unkown request received");
	}

	return 0;
}

static int _action_json(const char* json_msg)
{
	pid_t _nzbget_pid = -1;
	pid_t _f_pid = -1;

	_nzbget_pid = usenet_find_process(USENET_CLIENT_NZBGET_CLIENT);
	if(_nzbget_pid < 0) {
		USENET_LOG_MESSAGE("process not initialised, spawning nzbget");

		/* fork the process here, call system to spawn nzbget */
		_f_pid = fork();
		if(_f_pid == 0) {

			/* this is the child process therefore spawn nzbget */
			USENET_LOG_MESSAGE("starting nzbget as a deamon");
			system("nzbget -D");

			/* raise signal to terminate self */
			raise(SIGINT);
		}
	}
	else
		USENET_LOG_MESSAGE_ARGS("process found with pid %i", _nzbget_pid);



	return USENET_SUCCESS;
}
