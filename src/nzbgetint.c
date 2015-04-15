/*
 * RPC interface for connecting to nzbget
 */

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include <xmlrpc-c/base.h>
#include <xmlrpc-c/client.h>
#include "usenet.h"

#define NAME       "XML-RPC C Auth Client"
#define VERSION    "1.0"
#define SERVER_URL "http://nzbget:tegbzn6789@localhost:6789/xmlrpc"
#define METHOD_NAME "version"
#define USENET_NZBGET_SCAN_METHOD "scan"
#define USENET_NZBGET_LISTFILE_METHOD "listfiles"

int usenet_update_nzb_list(void);
int usenet_nzb_scan(void);

static void die_if_fault_occurred(xmlrpc_env * const envp)
{
    if (envp->fault_occurred) {
        fprintf(stderr, "XML-RPC Fault: %s (%d)\n",
                envp->fault_string, envp->fault_code);
		raise(SIGINT);
        exit(1);
    }
}

int usenet_update_nzb_list(void)
{
    xmlrpc_env env;
    xmlrpc_value * resultp;
    char* version = NULL;

    /* Initialize our error-handling environment. */
    xmlrpc_env_init(&env);

    /* Start up our XML-RPC client library. */
    xmlrpc_client_init2(&env, XMLRPC_CLIENT_NO_FLAGS, NAME, VERSION, NULL, 0);
    die_if_fault_occurred(&env);

    USENET_LOG_MESSAGE_ARGS("Making XMLRPC call to server url %s method %s "
	   "to request version number ", SERVER_URL, METHOD_NAME);

    /* Make the remote procedure call */
    resultp = xmlrpc_client_call(&env, SERVER_URL, METHOD_NAME, "(n)");
    die_if_fault_occurred(&env);

    /* Get our sum and print it out. */
    xmlrpc_read_string(&env, resultp, (const char**) &version);
    die_if_fault_occurred(&env);

    if(version != NULL)
	{
		USENET_LOG_MESSAGE_ARGS("nzbget version: %s", version);
	    free(version);
	}

    /* Dispose of our result value. */
    xmlrpc_DECREF(resultp);

    /* Clean up our error-handling environment. */
    xmlrpc_env_clean(&env);

    /* Shutdown our XML-RPC client library. */
    xmlrpc_client_cleanup();
    return 0;
}

int usenet_nzb_scan(void)
{
    xmlrpc_env env;
    xmlrpc_value * resultp;

    /* Initialize our error-handling environment. */
    xmlrpc_env_init(&env);

    /* Start up our XML-RPC client library. */
	USENET_LOG_MESSAGE("initialising rpc client");
    xmlrpc_client_init2(&env, XMLRPC_CLIENT_NO_FLAGS, NAME, VERSION, NULL, 0);
    die_if_fault_occurred(&env);

    USENET_LOG_MESSAGE_ARGS("Making XMLRPC call to server url %s method %s "
	   "to update the list", SERVER_URL, USENET_NZBGET_SCAN_METHOD);

    /* Make the remote procedure call */
	resultp = xmlrpc_client_call(&env, SERVER_URL, USENET_NZBGET_SCAN_METHOD, "(n)");
    die_if_fault_occurred(&env);

	USENET_LOG_MESSAGE("nzbget updated list");

    /* Dispose of our result value. */
    xmlrpc_DECREF(resultp);

    /* Clean up our error-handling environment. */
    xmlrpc_env_clean(&env);

    /* Shutdown our XML-RPC client library. */
    xmlrpc_client_cleanup();
    return USENET_SUCCESS;
}

int usenet_nzb_get_filelist(struct usenet_nzb_filellist** f_list)
{
	int _arr_sz = 0, _i = 0;
    xmlrpc_env env;
    xmlrpc_value* resultp = NULL;
	xmlrpc_value* _arr_val = NULL;
	xmlrpc_value* _struct_val = NULL;
	struct usenet_nzb_filellist* _list = NULL;

	if(f_list == NULL)
		return USENET_ERROR;

    /* Initialize our error-handling environment. */
    xmlrpc_env_init(&env);

    /* Start up our XML-RPC client library. */
	USENET_LOG_MESSAGE("initialising rpc client");
    xmlrpc_client_init2(&env, XMLRPC_CLIENT_NO_FLAGS, NAME, VERSION, NULL, 0);
    die_if_fault_occurred(&env);

    USENET_LOG_MESSAGE_ARGS("Making XMLRPC call to server url %s method %s "
	   "to update the list", SERVER_URL, USENET_NZBGET_LISTFILE_METHOD);

    /* Make the remote procedure call */
	resultp = xmlrpc_client_call(&env,
								 SERVER_URL,
								 USENET_NZBGET_LISTFILE_METHOD,
								 "(iii)",
								 (xmlrpc_int32) 0,
								 (xmlrpc_int32) 0,
								 (xmlrpc_int32) 0);

    die_if_fault_occurred(&env);

	USENET_LOG_MESSAGE("nzbget getting update list");

	_arr_sz = xmlrpc_array_size(env, resultp);
	USENET_LOG_MESSAGE_ARGS("nzbget return a list of %i", _arr_sz);
	if(_arr_sz > 0) {
		USENET_LOG_MESSAGE("cleaning up as the list is less than 0");
		goto clean_up;
	}

	/* create an array to hold the structs */
	*f_list = (struct usenet_nzb_filellist*) calloc( _arr_sz, sizeof(struct usenet_nzb_filellist));
	_list = *f_list;
	for(_i = 0; _i < _arr_sz; _i++) {

		/* get the item */
		xmlrpc_array_read_item(env, resultp, _i, &_arr_val);

		/* get ID */
		xmlrpc_struct_read_value(env, _arr_val, "ID", &_struct_val);
		xmlrpc_read_int(env, _struct_val, (xmlrpc_int*) &_list->_id);
		xmlrpc_DECREF(_struct_val);

		/* get nzb id */
		xmlrpc_struct_read_value(env, _arr_val, "NZBID", &_struct_val);
		xmlrpc_read_int(env, _struct_val, (xmlrpc_int*) &_list->_nzb_id);
		xmlrpc_DECREF(_struct_val);

		/* get nzb file name */
		xmlrpc_read_string(env, _arr_val, "NZBFilename", &_struct_val);
		xmlrpc_read_int(env, _struct_val, &_list->_nzb_file_name);
		xmlrpc_DECREF(_struct_val);

		/* decrement reference count */
		xmlrpc_DECREF(_arr_val);
		_list++;
	}

clean_up:
    /* Dispose of our result value. */
    xmlrpc_DECREF(resultp);

    /* Clean up our error-handling environment. */
    xmlrpc_env_clean(&env);

    /* Shutdown our XML-RPC client library. */
    xmlrpc_client_cleanup();
    return USENET_SUCCESS;
}
