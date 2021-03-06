/*
 * Implementation of the usenet control client. When this starts, it looks for the server
 * and start sending messages. This client shuold also be abale to
 * shut the client machine down, when instructed by the server
 */
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <gqueue.h>
#include "usenet.h"
#include "thcon.h"
#include "jsmn.h"

#define USENET_CLIENT_MSG_SZ 256
#define USENET_CLIENT_MSG_PULSE_GAP 5
#define USENET_CLIENT_NZBGET_CLIENT "nzbget"

#define USENET_CLIENT_PROGRESS_MAX 40

/* struct to encapsulate server component */
struct uclient
{
	int _progress_flg;											/* scp copy progress flag */
	int _log_fd;												/* log file descriptor */
	volatile sig_atomic_t _init_flg;							/* flag to indicate initialised struct */

	/*
	 * This flag is set to send a pulse to the server.
	 * If the server doesn't respond in the next two cycles,
	 * the client shall quit.
	 */
	volatile sig_atomic_t _pulse_sent;
	volatile sig_atomic_t _ini_wait_flg;						/* flag for the initial wait */
	volatile unsigned int _act_ix;								/* index for the action to be taken */
	volatile unsigned int _probe_nzb_flg;						/* flag to indicate probe nzbget */
	volatile int _act_nzb_id;									/* store the NZB ID here to prevent rename interupted */

	time_t _cp_prog_time;										/* last recorded progress time */

	pid_t _child_pid;											/* child process ID */
	pid_t _nzbget_pid;											/* nzbget process ID */

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
static int _progress_callback(void* self, float progress);

static inline __attribute__ ((always_inline)) int _default_response(struct uclient* client, struct usenet_message* msg);
static inline __attribute__ ((always_inline)) int _send_pulse(struct uclient* client);

static int _msg_handler(struct uclient* cli, struct usenet_message* msg);
static int _action_json(struct uclient* cli, const char* json_msg);
static int _echo_daemon_check_to_parent(struct uclient* cli);
static int _echo_update_list(struct uclient* cli);
static int _echo_scp_complete(struct uclient* cli);
static int _echo_scp_done(struct uclient* cli);

static int _handle_unknown_message(struct uclient* cli, struct usenet_message* msg);
static int _terminate_helper(struct uclient* cli, const char* msg, jsmntok_t* tok);
static int _terminate_client(struct uclient* cli, pid_t child);
static int _check_nzb_list(struct uclient* cli);
static int _copy_file(struct uclient* cli, struct usenet_nzb_filellist* list);

static int _progress_handler(struct uclient* cli, struct usenet_message* msg, jsmntok_t* tok);
static int _create_log_file(struct uclient* cli);
static int _set_scp_progress_flg(struct uclient* cli);

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
		if(!(++_cnt > client._login.svr_wait_time) && _cnt != 1) {
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
	cli->_log_fd = -1;

	memset(cli, 0, sizeof(struct uclient));

	/* initialise config object */
	if(usenet_utils_load_config(&cli->_login) != USENET_SUCCESS) {
		USENET_LOG_MESSAGE("unable to read the configuration object");
		return USENET_ERROR;
	}

	/* Create the log file and log it */
	_create_log_file(cli);

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
	cli->_probe_nzb_flg = 0;
	cli->_child_pid = -1;
	cli->_nzbget_pid = -1;
	cli->_act_nzb_id = 0;
	cli->_progress_flg = 0;

	/* set the scp progress flag accordingly */
	_set_scp_progress_flg(cli);

	/*
	 * Initialise the progress clock to now.
	 * this shall be checked every time the callback handler is called.
	 * only after a diff will the progress update sent to the server.
	 */
	time(&cli->_cp_prog_time);

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
	if(sz <= 0)
		return USENET_SUCCESS;

	USENET_LOG_MESSAGE("message received from server");

	_client = (struct uclient*) self;
	usenet_message_init(&_msg);
	usenet_unserialise_message(data, sz, &_msg);


	/* we receive some thing therefore pulse flag is reset */
	_client->_pulse_sent = USENET_PULSE_RESET;


	/*
	 * if a request instruction was sent, acknowledge
	 * to the server.
	 */
	_msg_handler(_client, &_msg);
	USENET_DESTROY_MESSAGE_BUFFER(&_msg);

	return 0;
}

int pulse_client(struct uclient* cli)
{
	/*
	 * We only send a pulse every USENET_CLIENT_MSG_PULSE_GAP
	 */
	if(++cli->_pulse_counter < USENET_CLIENT_MSG_PULSE_GAP)
		return 0;

	/* terminate the child process if running */
	_terminate_client(cli, -1);

	/* if probe flag was set, update server to broadcast */
	if(cli->_probe_nzb_flg)
		_echo_update_list(cli);

	/* if nzbget child process exists */
	if(cli->_nzbget_pid > 0)
		_check_nzb_list(cli);

	/* reset counter back to 0 to start timer again */
	cli->_pulse_counter = 0;

	/* send pulse to server */
	_send_pulse(cli);

	/*
	 * Check if the server has responded to the previous.
	 * If haven't raise signal to terminate.
	 */
	if(cli->_pulse_sent & USENET_PULSE_SENT) {
		USENET_LOG_MESSAGE("no response from server, raising SIGINT");
		/* raise(SIGINT); */
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
	void* _data = NULL;
	size_t _size = 0;

	msg->ins = USENET_REQUEST_RESPONSE;
	usenet_serialise_message(msg, &_data, &_size);

	USENET_LOG_MESSAGE("sending default response");
	thcon_send_info(&client->_connection, _data, _size);

	/*
	 * after successfully initialised, set the init flg
	 * to send a pulse to the server.
	 */
	client->_init_flg = 1;
	free(_data);
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
			_action_json(cli, msg->msg_body);
		}
		break;
	default:
		USENET_LOG_MESSAGE("unkown request received, handling message..");
		_handle_unknown_message(cli, msg);
	}

	return 0;
}

static int _action_json(struct uclient* cli, const char* json_msg)
{
	int _ret = USENET_SUCCESS, _i = 0, _num_tok = 0;
	jsmntok_t* _json_tok = NULL, *_json_args = NULL;
	struct usenet_str_arr _str_arr = {0};

	/* parse the json message */
	do {
		if(usjson_parse_message(json_msg, &_json_tok, &_num_tok) == USENET_ERROR) {

			USENET_LOG_MESSAGE("unable to parse json message");

			_json_tok = NULL;
			_ret = USENET_ERROR;
			break;
		}
		else {
			USENET_LOG_MESSAGE("JSON parser succeeded");
		}

		/* get token */
		if(usjson_get_token(json_msg, _json_tok, _num_tok, "args", NULL, &_json_args) == USENET_ERROR) {
			USENET_LOG_MESSAGE("unable to get token");
			_ret = USENET_ERROR;
			break;
		}

		/* get the array into a buffer */
		if(usjson_get_token_arr_as_str(json_msg, _json_args, &_str_arr) == USENET_ERROR) {
			USENET_LOG_MESSAGE("unable to get the arg array for the rpc call");
			_ret = USENET_ERROR;
			break;
		}

		break;
	} while(0);

	/* clean the memory taken by token */
	if(_json_tok != NULL)
		free(_json_tok);

	/* get the NZBs and free the array */
	for(_i = 0; _i < _str_arr._sz; _i++) {
		if(_str_arr._arr[_i]) {
			usenet_nzb_search_and_get(_str_arr._arr[_i], NULL);
			free(_str_arr._arr[_i]);
			_str_arr._arr[_i] = NULL;
		}
	}

	if(_str_arr._arr != NULL)
		free(_str_arr._arr);
	_str_arr._arr = NULL;

	if(_ret == USENET_ERROR)
		return _ret;

	cli->_nzbget_pid = usenet_find_process(USENET_CLIENT_NZBGET_CLIENT);

	if(cli->_nzbget_pid < 0) {
		USENET_LOG_MESSAGE("process not initialised, spawning nzbget");

		/* fork the process here, call system to spawn nzbget */
		cli->_child_pid = fork();
		if(cli->_child_pid == 0) {

			/* this is the child process therefore spawn nzbget */
			USENET_LOG_MESSAGE("starting nzbget as a deamon");
			system("nzbget -D");

			_echo_daemon_check_to_parent(cli);
			_echo_update_list(cli);

			/* raise signal to terminate self */
			USENET_LOG_MESSAGE("stopping client");
			stop_client(&client);
			raise(SIGINT);
			exit(0);
		}
	}
	else {
		USENET_LOG_MESSAGE_ARGS("process found with pid %i", cli->_nzbget_pid);
		_echo_update_list(cli);
	}

	return _ret;
}

static int _echo_daemon_check_to_parent(struct uclient* cli)
{
	struct usenet_message _msg;
	void* _data = NULL;
	size_t _size = 0;

	usenet_message_init(&_msg);
	_msg.ins = USENET_REQUEST_BROADCAST;
	_msg.msg_body = (char*) malloc(sizeof(char)*USENET_JSON_BUFF_SZ);
	_msg.size += USENET_JSON_BUFF_SZ;
	memset(_msg.msg_body, 0, sizeof(char)*USENET_JSON_BUFF_SZ);

	snprintf(_msg.msg_body,
			 USENET_JSON_BUFF_SZ,
			 "{\"%s\": \"%s\", \"%s\": []}",
			 USENET_JSON_FN_HEADER, USENET_JSON_FN_1, USENET_JSON_ARG_HEADER);

	USENET_LOG_MESSAGE("serialise message");
	usenet_serialise_message(&_msg, &_data, &_size);

	USENET_LOG_MESSAGE("broadcasting message to parent to indicate it I am complete");
	thcon_send_info(&cli->_connection, _data, _size);

	free(_data);
	USENET_DESTROY_MESSAGE_BUFFER(&_msg);
	return USENET_SUCCESS;
}


static int _handle_unknown_message(struct uclient* cli, struct usenet_message* msg)
{
	int _num = 0;
	jsmntok_t* _tok = NULL, *_rpc_tok = NULL, *_arg_tok = NULL;
	char* _rpc_val = NULL, *_arg_val = NULL;


	int _ret = USENET_SUCCESS;

	/* parse the message */
	USENET_LOG_MESSAGE("parsing unknown message");

	if(usjson_parse_message(msg->msg_body, &_tok, &_num) != USENET_SUCCESS)
		return USENET_ERROR;

	/* get token */
	USENET_LOG_MESSAGE("inspecting remote procedure call");
	if(usjson_get_token(msg->msg_body, _tok, _num, USENET_JSON_FN_HEADER, &_rpc_val, &_rpc_tok) != USENET_SUCCESS) {
		_ret = USENET_ERROR;
		USENET_LOG_MESSAGE("json parser error");
		goto clean_up;
	}

	/* compare the function call */
	if(strcmp(_rpc_val, USENET_JSON_FN_1) == 0) {
		USENET_LOG_MESSAGE("echo message received from child process, nzbget is launched successfully");
		cli->_probe_nzb_flg = 1;

		/* get the pid of newly spawned nzbget instance */
		cli->_nzbget_pid = usenet_find_process(USENET_CLIENT_NZBGET_CLIENT);
	}
	else if(strcmp(_rpc_val, USENET_JSON_FN_2) == 0) {
		USENET_LOG_MESSAGE("echo message received to update the nzbget list");
		usenet_nzb_scan();
	}
	else if(strcmp(_rpc_val, USENET_JSON_FN_3) == 0) {
		/*
		 * The child process is indicating the scp operation is complete.
		 * Get the array value.
		 */
		USENET_LOG_MESSAGE("process complete message recieved");
		if(usjson_get_token(msg->msg_body, _tok, _num, USENET_JSON_ARG_HEADER, &_arg_val, &_arg_tok) != USENET_SUCCESS) {
			_ret = USENET_ERROR;
			USENET_LOG_MESSAGE("unable to parse json to get the array value");
			goto clean_up;
		}

		USENET_LOG_MESSAGE("attempting to terminate the process");
		if(_arg_val) {
			/*
			 * Call the helper method to parse the array and
			 * kill the process with process id
			 */
			_terminate_helper(cli, msg->msg_body, _arg_tok);

			/* reset the index */
			cli->_act_nzb_id = 0;
			free(_arg_val);
		}

		_arg_tok = NULL;
		_arg_val = NULL;

	}
	else if(strcmp(_rpc_val, USENET_JSON_FN_4) == 0) {

		if(usjson_get_token(msg->msg_body, _tok, _num, USENET_JSON_ARG_HEADER, &_arg_val, &_arg_tok) != USENET_SUCCESS) {
			_ret = USENET_ERROR;
			goto clean_up;
		}

		if(_arg_val) {

			/* write the progress to the screen or log */
			_progress_handler(cli, msg, _arg_tok);

			free(_arg_val);
			_arg_val = NULL;
		}
	}


clean_up:
	USENET_LOG_MESSAGE("cleaning up the allocated memory");
	/* free allocated resources */
	if(_tok)
		free(_tok);

	if(_rpc_val)
		free(_rpc_val);

	return _ret;
}

/* Kills the child process, child argument takes priority */
static int _terminate_client(struct uclient* cli, pid_t child)
{
	if(child > 0) {
		USENET_LOG_MESSAGE_ARGS("terminating child process: %i", child);
		kill(child, SIGKILL);
	}
	else if(cli->_child_pid > 0) {
		USENET_LOG_MESSAGE_ARGS("terminating child process: %i", cli->_child_pid);
		kill(cli->_child_pid, SIGKILL);
		cli->_child_pid = -1;
	}

	return USENET_SUCCESS;
}

static int _echo_update_list(struct uclient* cli)
{
	struct usenet_message _msg;
	void* _data = NULL;
	size_t _size = 0;

	usenet_message_init(&_msg);
	_msg.ins = USENET_REQUEST_BROADCAST;

	_msg.msg_body = (char*) malloc(sizeof(char) * USENET_JSON_BUFF_SZ);
	memset(_msg.msg_body, 0, sizeof(char) * USENET_JSON_BUFF_SZ);
	snprintf(_msg.msg_body,
			 USENET_JSON_BUFF_SZ,
			 "{\"%s\": \"%s\", \"%s\": []}",
			 USENET_JSON_FN_HEADER, USENET_JSON_FN_2, USENET_JSON_ARG_HEADER);

	/* serialise the message */
	USENET_LOG_MESSAGE("serialising the message");
	usenet_serialise_message(&_msg, &_data, &_size);

	USENET_LOG_MESSAGE("broadcasting message update list");
	thcon_send_info(&cli->_connection, _data, _size);

	/*
	 * we turn the probe flag off as we don't want nzbget to
	 * scan continuously.
	 */
	cli->_probe_nzb_flg = 0;

	free(_data);
	USENET_DESTROY_MESSAGE_BUFFER(&_msg);
	return USENET_SUCCESS;
}

static inline __attribute__ ((always_inline)) int _send_pulse(struct uclient* client)
{
	struct usenet_message _msg = {0};
	thcon* _con = NULL;
	void* _data = NULL;
	size_t _size = 0;

	usenet_message_init(&_msg);
	_msg.ins = USENET_REQUEST_PULSE;
	_msg.msg_body = (char*) malloc(sizeof(char) * USENET_JSON_BUFF_SZ);
	memset(_msg.msg_body, 0, sizeof(char) * USENET_JSON_BUFF_SZ);

	snprintf(_msg.msg_body, sizeof(char) * USENET_JSON_BUFF_SZ, "%s", "alive");

	/* serialise the buffer */
	USENET_LOG_MESSAGE("serialising the message");
	usenet_serialise_message(&_msg, &_data, &_size);

	/* get pointer to the connection object */
	_con = &client->_connection;

	USENET_LOG_MESSAGE("sending server pulse");
	thcon_send_info(_con, _data, _size);

	free(_data);
	USENET_DESTROY_MESSAGE_BUFFER(&_msg);
	return USENET_SUCCESS;
}

/*
 * Queries the nzbget list and once which are finished
 * can be renamed and scp to the server.
 */
static int _check_nzb_list(struct uclient* cli)
{
	int _i = 0;
	size_t _list_sz = 0;
	int _s_flg = 0;													/* flag to indicate success of failure */
	struct usenet_nzb_filellist* _list = NULL;

	USENET_LOG_MESSAGE_ARGS("found nzbget with pid %i, getting history", cli->_nzbget_pid);

	/* call the interface method for getting a list */
	USENET_LOG_MESSAGE("getting history list");
	usenet_nzb_get_history(&_list, &_list_sz);

	/* if list size is zero let the server we are done */
	if(_list_sz == 0)
		_echo_scp_done(cli);

	/* iterate through the list and action */
	for(_i = 0; _i < _list_sz; _i++) {

		/* do stuff here */
		if(!_list[_i]._nzb_name || !_list[_i]._status)
			continue;

		/* create standard name field and copy to it */
		usenet_utils_append_std_fname(&_list[_i]);

		USENET_LOG_MESSAGE_ARGS("nzb file name: %s, and status: %s", _list[_i]._u_std_fname, _list[_i]._status);

		/*
		 * If the download was not sucessful or the the nzb id being processed is same as
		 * this one we continue with the next one.
		 */
		_s_flg = strcmp(_list[_i]._status, USENET_NZB_SUCCESS);
		if(_s_flg || cli->_act_nzb_id != 0) {
			if(_s_flg)
				usenet_nzb_delete_item_from_history(&_list[_i]._nzb_id, 1);
			continue;
		}

		if(cli->_act_nzb_id == 0)
			cli->_act_nzb_id = _list[_i]._nzb_id;

		/*
		 * Rename and copy the file in a forked process.
		 * If not delete the file in the next round
		 */
		if(usenet_utils_rename_file(&_list[_i], cli->_login.nzb_fsize_threshold) == USENET_SUCCESS)
			_copy_file(cli, &_list[_i]);
		else {
			usenet_nzb_delete_item_from_history(&_list[_i]._nzb_id, 1);
			cli->_act_nzb_id = 0;
		}

	}

	/* free the list and return */
	USENET_LOG_MESSAGE("free file list");
	for(_i = 0; _i < _list_sz; _i++) {
		USENET_FILELIST_FREE(&_list[_i]);
	}
	free(_list);

	return USENET_SUCCESS;
}

/*
 * Method for copying the file to the remote destination.
 * This uses a forked process and send message to the remote server on completion.
 */
static int _copy_file(struct uclient* cli, struct usenet_nzb_filellist* list)
{
	int _stat = USENET_SUCCESS;
	char* _fname = NULL;
	size_t _len = 0;

	/* fork the process */
	pid_t _pid = 0;

	/* fork the process */
	USENET_LOG_MESSAGE("forking the process to copy the file to the remote server");
	_pid = fork();

	/* if this is the parent process exit here */
	if(_pid != 0)
		return USENET_SUCCESS;

	/* construct the destination path */
	_stat = usenet_utils_create_destinatin_path(&cli->_login, list, &_fname, &_len);
	if(_stat == USENET_ERROR) {
		USENET_LOG_MESSAGE("errors occured while creating destination path");
		goto cleanup;
	}

	/*
	 * SCP the file if it was successful, remove from list.
	 */
	if(usenet_utils_scp_file(&cli->_login,
							 list->_u_r_fpath,
							 _fname,
							 (cli->_progress_flg? _progress_callback : NULL),
							 (void*) cli) == USENET_SUCCESS)
		usenet_nzb_delete_item_from_history(&list->_nzb_id, 1);

	/* echo the message to the server to indicate complete */
	_echo_scp_complete(cli);

cleanup:
	/* exit(0); */
	return USENET_SUCCESS;
}

/*
 * Indicate to the server that the scp operation is complete.
 * this is relayed back to the client.
 */
static int _echo_scp_complete(struct uclient* cli)
{
	pid_t _pid;
	struct usenet_message _msg;
	void* _data = NULL;
	size_t _size = 0;

	/* get the pid */
	_pid = getpid();

	/* format the message */
	usenet_message_init(&_msg);
	_msg.ins = USENET_REQUEST_BROADCAST;
	_msg.msg_body = (char*) malloc(sizeof(char)*USENET_JSON_BUFF_SZ);
	memset(_msg.msg_body, 0, sizeof(char)*USENET_JSON_BUFF_SZ);

	snprintf(_msg.msg_body,
			 USENET_JSON_BUFF_SZ,
			 "{\"%s\": \"%s\", \"%s\": [\"%i\"]}",
			USENET_JSON_FN_HEADER,
			USENET_JSON_FN_3,
			USENET_JSON_ARG_HEADER,
			_pid);

	/* serialise the message */
	USENET_LOG_MESSAGE("serialising the message");
	usenet_serialise_message(&_msg, &_data, &_size);

	USENET_LOG_MESSAGE_ARGS("broadcasting message, %s, scp complete", _msg.msg_body);
	thcon_send_info(&cli->_connection, _data, _size);

	free(_data);
	USENET_DESTROY_MESSAGE_BUFFER(&_msg);
	return USENET_SUCCESS;
}

/*
 * Helper method for handling terminating clinet.
 * The helpper method is used to parse the array and
 * get pid's
 */
static int _terminate_helper(struct uclient* cli, const char* msg, jsmntok_t* tok)
{
	int _i = 0;
	pid_t _child_pid = -1;
	struct usenet_str_arr _str_arr = {0};

	if(usjson_get_token_arr_as_str(msg, tok, &_str_arr) == USENET_ERROR) {
		USENET_LOG_MESSAGE("unable to get the arg array for the rpc call");
		return USENET_ERROR;
	}

	for(_i = 0; _i < _str_arr._sz; _i++) {
		if(!_str_arr._arr[_i])
			continue;

		/*
		 * Since we have a valid argument, get the value and
		 * terminate the process
		 */
		_child_pid = atoi(_str_arr._arr[_i]);
		_terminate_client(cli, _child_pid);

		free(_str_arr._arr[_i]);
		_str_arr._arr[_i] = NULL;
	}

	/* reset the active NZB ID */
	cli->_act_nzb_id = 0;

	if(_str_arr._arr != NULL)
		free(_str_arr._arr);
	_str_arr._arr = NULL;

	return USENET_SUCCESS;
}

/*
 * indicate progress to the server.
 */
static int _progress_callback(void* self, float progress)
{
	struct usenet_message _msg;
	struct uclient* _self = NULL;
	void* _data = NULL;
	size_t _size = 0;
	time_t _now;

	if(self == NULL)
		return USENET_ERROR;

	/* cast the object to uclient */
	_self = (struct uclient*) self;

	/* compare the times */
	time(&_now);
	if(difftime(_now, _self->_cp_prog_time) < _self->_login.progress_update_interval)
		return USENET_SUCCESS;

	/* set the current time */
	time(&_self->_cp_prog_time);

	/* format the message */
	usenet_message_init(&_msg);
	_msg.ins = USENET_REQUEST_BROADCAST;
	_msg.msg_body = (char*) malloc(sizeof(char) * USENET_JSON_BUFF_SZ);
	memset(_msg.msg_body, 0, sizeof(char) * USENET_JSON_BUFF_SZ);

	snprintf(_msg.msg_body,
			 USENET_JSON_BUFF_SZ,
			 "{\"%s\": \"%s\", \"%s\": [\"%.1f\"]}",
			USENET_JSON_FN_HEADER,
			USENET_JSON_FN_4,
			USENET_JSON_ARG_HEADER,
			progress);

	/* serialise the buffer */
	usenet_serialise_message(&_msg, &_data, &_size);

	/* no logging is done here to minimise stdout */
	thcon_send_info(&_self->_connection, _data, _size);

	free(_data);
	USENET_DESTROY_MESSAGE_BUFFER(&_msg);
	return USENET_SUCCESS;
}

/* log progress to the screen */
static int _progress_handler(struct uclient* cli, struct usenet_message* msg, jsmntok_t* tok)
{
	int _i = 0, _prog = 0;
	struct usenet_str_arr _str_arr = {0};
	char* _prog_disp = NULL;

	/* get the array into struct */
	if(usjson_get_token_arr_as_str(msg->msg_body, tok, &_str_arr) == USENET_ERROR) {
		return USENET_ERROR;
	}

	for(_i = 0; _i < _str_arr._sz; _i++) {
		if(!_str_arr._arr[_i])
			continue;

		/* write the progress */
		_prog = USENET_CLIENT_PROGRESS_MAX * atof(_str_arr._arr[_i]);

		/* allocate memory */
		_prog_disp = (char*) malloc(sizeof(char) * (USENET_CLIENT_PROGRESS_MAX + 2));
		memset(_prog_disp, USENET_ASSIGN_CHAR, _prog);
		sprintf(_prog_disp+_prog, ">");
		USENET_LOG_MESSAGE(_prog_disp);

		free(_prog_disp);
		free(_str_arr._arr[_i]);
		_str_arr._arr[_i] = NULL;
	}

	return USENET_SUCCESS;
}

static int _echo_scp_done(struct uclient* cli)
{
	struct usenet_message _msg;
	void* _data = NULL;
	size_t _size = 0;

	/* format the message */
	USENET_LOG_MESSAGE("copy complete to the remote server");
	usenet_message_init(&_msg);
	_msg.ins = USENET_REQUEST_BROADCAST;

	_msg.msg_body = (char*) malloc(sizeof(char) * USENET_JSON_BUFF_SZ);
	memset(_msg.msg_body, 0, sizeof(char) * USENET_JSON_BUFF_SZ);

	sprintf(_msg.msg_body,
			"{\"%s\": \"%s\", \"%s\": []}",
			USENET_JSON_FN_HEADER,
			USENET_JSON_FN_5,
			USENET_JSON_ARG_HEADER);

	/* serialise the message */
	usenet_serialise_message(&_msg, &_data, &_size);
	thcon_send_info(&cli->_connection, _data, _size);

	free(_data);
	USENET_DESTROY_MESSAGE_BUFFER(&_msg);
	return USENET_SUCCESS;
}

static int _create_log_file(struct uclient* cli)
{
	int _ret = USENET_ERROR;

	/* check if log to file is enabled */
	if(strcmp(cli->_login.log_to_file, USENET_CONFIG_YES) != 0)
		goto clean_up;

	/* open the file with read write permission */
	cli->_log_fd = open(cli->_login.log_file_path,
						O_CREAT | O_RDWR | O_APPEND);

	/* duplicate the file to the STDOUT */
	if(cli->_log_fd == -1) {
		USENET_LOG_MESSAGE_ARGS("unable to create the log file with error %s "
								"continuiing with stdout "
								"file path: %s",
								strerror(errno),
								cli->_login.log_file_path);
		goto clean_up;
	}

	/* if the file was opened succesfully, we set the file permissions */
    fchmod(cli->_log_fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	/* before duplicate flush stdout */
	fflush(stdout);

	if(dup2(cli->_log_fd, STDOUT_FILENO) == -1) {
		USENET_LOG_MESSAGE_ARGS("unable to duplicate the log file to stdout with error %s "
								"continuiing with stdout",
								strerror(errno));
		goto clean_up;
	}

	_ret = USENET_SUCCESS;
clean_up:

	if(cli->_log_fd != -1)
		close(cli->_log_fd);
	cli->_log_fd = -1;
	return _ret;

}

/*
 * This method gets the config parameter and set the client objects flag
 * to call the progress method.
 */
static int _set_scp_progress_flg(struct uclient* cli)
{
	if(strcmp(cli->_login.scp_progress, USENET_CONFIG_YES) == 0)
		cli->_progress_flg = 1;
	else
		cli->_progress_flg = 0;

	return USENET_SUCCESS;
}
