/*
 * RPC interface for connecting to nzbget
 */

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include <xmlrpc-c/base.h>
#include <xmlrpc-c/client.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "usenet.h"

#define NAME       "XML-RPC C Auth Client"
#define VERSION    "1.0"
#define SERVER_URL "http://nzbget:tegbzn6789@localhost:6789/xmlrpc"
#define METHOD_NAME "version"
#define USENET_NZBGET_SCAN_METHOD "scan"
#define USENET_NZBGET_LISTFILE_METHOD "listfiles"
#define USENET_NZBGET_LISTGROUPS_METHOD "listgroups"
#define USENET_NZBGET_HISTORY_METHOD "history"
#define USENET_NZBGET_EDITQUEUE_METHOD "editqueue"

#define USENET_NZBGET_NUM_GROUPS 10

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
	(list)->_u_std_fname = NULL

#define USENET_NZBGET_COPY_ELEMENT(element, value)				\
	(element) = (char*) malloc(strlen((const char*) (value)) +1);	\
	strcpy((element), (const char*) (value))

#define die_if_fault_occurred(envp)										\
    if ((envp)->fault_occurred) {										\
        USENET_LOG_MESSAGE_ARGS("XML-RPC Fault: %s (%d)",				\
								(envp)->fault_string, (envp)->fault_code); \
		raise(SIGINT);													\
		return USENET_ERROR;											\
    }

#define check_for_errors(envp)											\
    if ((envp)->fault_occurred) {										\
        USENET_LOG_MESSAGE_ARGS("XML-RPC Fault: %s (%d)",				\
								(envp)->fault_string, (envp)->fault_code); \
		goto clean_up;													\
    }

static int _nzb_populate_flist(xmlrpc_env* env, xmlrpc_value* resultp, struct usenet_nzb_filellist* f_list, int ix);
static int _nzb_populate_flist2(xmlNodePtr member, struct usenet_nzb_filellist* f_list);

int usenet_update_nzb_list(void)
{
    xmlrpc_env env;
	xmlrpc_client* client;
    xmlrpc_value * resultp;
    char* version = NULL;

    /* Initialize our error-handling environment. */
    xmlrpc_env_init(&env);

    /* Start up our XML-RPC client library. */
	xmlrpc_client_setup_global_const(&env);
	die_if_fault_occurred(&env);

	xmlrpc_client_create(&env, XMLRPC_CLIENT_NO_FLAGS, NAME, VERSION, NULL, 0, &client);
    die_if_fault_occurred(&env);

    USENET_LOG_MESSAGE_ARGS("Making XMLRPC call to server url %s method %s "
	   "to request version number ", SERVER_URL, METHOD_NAME);

    /* Make the remote procedure call */
	xmlrpc_client_call2f(&env, client, SERVER_URL, METHOD_NAME, &resultp, "(n)");
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
	xmlrpc_client_destroy(client);

	xmlrpc_client_teardown_global_const();

    return USENET_SUCCESS;
}

int usenet_nzb_scan(void)
{
    xmlrpc_env env;
	xmlrpc_client* client;
    xmlrpc_value * resultp;

    /* Initialize our error-handling environment. */
    xmlrpc_env_init(&env);

	xmlrpc_client_setup_global_const(&env);
	die_if_fault_occurred(&env);

    /* Start up our XML-RPC client library. */
	USENET_LOG_MESSAGE("initialising rpc client");
	xmlrpc_client_create(&env, XMLRPC_CLIENT_NO_FLAGS, NAME, VERSION, NULL, 0, &client);
    die_if_fault_occurred(&env);

    USENET_LOG_MESSAGE_ARGS("Making XMLRPC call to server url %s method %s "
	   "to update the list", SERVER_URL, USENET_NZBGET_SCAN_METHOD);

    /* Make the remote procedure call */
	xmlrpc_client_call2f(&env, client, SERVER_URL, USENET_NZBGET_SCAN_METHOD, &resultp, "(n)");
    die_if_fault_occurred(&env);

	USENET_LOG_MESSAGE("nzbget updated list");

    /* Dispose of our result value. */
    xmlrpc_DECREF(resultp);

    /* Clean up our error-handling environment. */
    xmlrpc_env_clean(&env);

    /* Shutdown our XML-RPC client library. */
	xmlrpc_client_destroy(client);

	xmlrpc_client_teardown_global_const();

    return USENET_SUCCESS;
}

int usenet_nzb_get_filelist(struct usenet_nzb_filellist** f_list, size_t* num)
{
	int _i = 0;
    xmlrpc_env env;
	xmlrpc_client* client;
    xmlrpc_value* resultp = NULL;

	if(f_list == NULL)
		return USENET_ERROR;

    /* Initialize our error-handling environment. */
    xmlrpc_env_init(&env);

	xmlrpc_client_setup_global_const(&env);
	die_if_fault_occurred(&env);

    /* Start up our XML-RPC client library. */
	USENET_LOG_MESSAGE("initialising rpc client");
	xmlrpc_client_create(&env, XMLRPC_CLIENT_NO_FLAGS, NAME, VERSION, NULL, 0, &client);
    die_if_fault_occurred(&env);

    USENET_LOG_MESSAGE_ARGS("Making XMLRPC call to server url %s method %s "
	   "to get list", SERVER_URL, USENET_NZBGET_LISTGROUPS_METHOD);

    /* Make the remote procedure call */
	xmlrpc_client_call2f(&env,
						 client,
						 SERVER_URL,
						 USENET_NZBGET_LISTGROUPS_METHOD,
						 &resultp,
						 "(i)",
						 (xmlrpc_int32) USENET_NZBGET_NUM_GROUPS);

    die_if_fault_occurred(&env);

	USENET_LOG_MESSAGE("nzbget getting list");

	*num = xmlrpc_array_size(&env, resultp);
	USENET_LOG_MESSAGE_ARGS("nzbget return a list of %i", (*num));
	if(!(*num) > 0) {
		USENET_LOG_MESSAGE("cleaning up as the list is less than 0");
		goto clean_up;
	}

	/* create an array to hold the structs */
	*f_list = (struct usenet_nzb_filellist*) calloc( *num, sizeof(struct usenet_nzb_filellist));
	USENET_LOG_MESSAGE("polulating file list");
	for(_i = 0; _i < (*num); _i++) {
		_nzb_populate_flist(&env, resultp, f_list[_i], _i);
	}

clean_up:
    /* Dispose of our result value. */
    xmlrpc_DECREF(resultp);

    /* Clean up our error-handling environment. */
    xmlrpc_env_clean(&env);

    /* Shutdown our XML-RPC client library. */
	xmlrpc_client_destroy(client);

	xmlrpc_client_teardown_global_const();
    return USENET_SUCCESS;
}

int usenet_nzb_get_history(struct usenet_nzb_filellist** f_list, size_t* num)
{
	int _stat = USENET_SUCCESS, _count = 0, _ix = 0;
	xmlDocPtr _xmldoc = NULL;
	xmlNodePtr _root_node = NULL;
	xmlNodePtr _child_node = NULL;
	xmlNodePtr _sibling_node = NULL;

	char* _rpc_args[] = {"True"};

	_stat = usenet_uxmlrpc_call(USENET_NZBGET_HISTORY_METHOD, _rpc_args, 1, &_xmldoc);
	if(_stat != USENET_SUCCESS) {
		return USENET_ERROR;
	}

	/* parse xml */
	/* get root node */
	_root_node = xmlDocGetRootElement(_xmldoc);
	if(_root_node == NULL) {
		USENET_LOG_MESSAGE("unable to get root node");
		goto cleanup;
	}

	USENET_LOG_MESSAGE("getting a count of array returned by rpc response");

	/*
	 * _sibling_node pointer is set to the first value node found.
	 */
	usenet_uxmlrpc_get_node_count(_root_node, USENET_NZBGET_XMLRESPONSE_VALUE, &_count, &_sibling_node);
	if(_count <= 0) {
		USENET_LOG_MESSAGE_ARGS("rpc response array returned %i", _count);
		goto cleanup;
	}

	/* alloc memmory to the list and populate it */
	*num = _count;
	*f_list = (struct usenet_nzb_filellist*) calloc(sizeof(struct usenet_nzb_filellist), _count);

	/* iterate over the members and get set the values */
	USENET_LOG_MESSAGE("iterating over the members");
	while(_sibling_node) {
		_child_node = xmlFirstElementChild(_sibling_node);

		/* initialise the list */
		USENET_NZBGET_INIT_LIST(&(*f_list)[_ix]);

		while(_child_node) {

			/*
			 * if the node is a member call the method and break
			 * from inner loop.
			 */
			if(strcmp((char*) _child_node->name, USENET_NZBGET_XMLRESPONSE_MEMBER) == 0) {
				_nzb_populate_flist2(_child_node, &(*f_list)[_ix]);
				_child_node = xmlNextElementSibling(_child_node);
			}
			else {
				_child_node = xmlFirstElementChild(_child_node);
			}
		}

		_sibling_node = xmlNextElementSibling(_sibling_node);
		_ix++;
	}

	USENET_LOG_MESSAGE("history list loaded successfully");

cleanup:
	/* free xmldoc */
	if(_xmldoc != NULL)
		xmlFreeDoc(_xmldoc);
    return USENET_SUCCESS;
}

/*
 * This method removes the item from the history list.
 */
int usenet_nzb_delete_item_from_history(int* ids, size_t num)
{
	int _i = 0;
    xmlrpc_env env;
	xmlrpc_client* client;
    xmlrpc_value* resultp, *_ids,  *_id;


    /* Initialize our error-handling environment. */
    xmlrpc_env_init(&env);

	xmlrpc_client_setup_global_const(&env);
	die_if_fault_occurred(&env);

    /* Start up our XML-RPC client library. */
	USENET_LOG_MESSAGE("initialising rpc client");
	xmlrpc_client_create(&env, XMLRPC_CLIENT_NO_FLAGS, NAME, VERSION, NULL, 0, &client);
    die_if_fault_occurred(&env);

    USENET_LOG_MESSAGE_ARGS("Making XMLRPC call to server url %s method %s "
	   "delete item from list", SERVER_URL, USENET_NZBGET_EDITQUEUE_METHOD);

	/* make the array and append new items */
	_ids = xmlrpc_array_new(&env);
	die_if_fault_occurred(&env);

	for(_i = 0; _i < num; _i++) {
		_id = xmlrpc_int_new(&env, ids[_i]);

		xmlrpc_array_append_item(&env, _ids, _id);
		USENET_LOG_MESSAGE_ARGS("adding id for deletion %i", ids[_i]);
		die_if_fault_occurred(&env);
		xmlrpc_DECREF(_id);
	}

    /* Make the remote procedure call */
	xmlrpc_client_call2f(&env, client, SERVER_URL, USENET_NZBGET_EDITQUEUE_METHOD, &resultp,
						 "(sisA)", "HistoryDelete", 0, "", _ids);
    die_if_fault_occurred(&env);

	USENET_LOG_MESSAGE("nzbget item removed from list successfully");

	/* decrease reference */
	xmlrpc_DECREF(_ids);

    /* Dispose of our result value. */
    xmlrpc_DECREF(resultp);

    /* Clean up our error-handling environment. */
    xmlrpc_env_clean(&env);

    /* Shutdown our XML-RPC client library. */
	xmlrpc_client_destroy(client);

	xmlrpc_client_teardown_global_const();

    return USENET_SUCCESS;
}


/*
 * This fuction populates the file list struct.
 * Very inefficent as it duplicates memory like a dog.
 */
static int _nzb_populate_flist(xmlrpc_env* env, xmlrpc_value* resultp, struct usenet_nzb_filellist* f_list, int ix)
{
	xmlrpc_value* _arr_val = NULL;
	xmlrpc_value* _struct_val = NULL;
	const char* _str_val = NULL;

	USENET_NZBGET_INIT_LIST(f_list);

	/* get the item */
	xmlrpc_array_read_item(env, resultp, ix, &_arr_val);
	die_if_fault_occurred(env);

	/* get nzb id */
	xmlrpc_struct_read_value(env, _arr_val, "NZBID", &_struct_val);
	xmlrpc_read_int(env, _struct_val, (xmlrpc_int*) &f_list->_nzb_id);
	xmlrpc_DECREF(_struct_val);

	/* get nzb file name */
	xmlrpc_struct_read_value(env, _arr_val, "NZBFilename", &_struct_val);
	xmlrpc_read_string(env, _struct_val, &_str_val);
	if(_str_val) {
		USENET_NZBGET_COPY_ELEMENT(f_list->_nzb_file_name, _str_val);
		free((void*) _str_val);
		_str_val = NULL;
		xmlrpc_DECREF(_struct_val);
	}

	/* get file name */
	xmlrpc_struct_read_value(env, _arr_val, "NZBName", &_struct_val);
	xmlrpc_read_string(env, _struct_val, &_str_val);
	if(_str_val) {
		USENET_NZBGET_COPY_ELEMENT(f_list->_nzb_name, _str_val);
		free((void*) _str_val);
		_str_val = NULL;
		xmlrpc_DECREF(_struct_val);
	}


	/* Destination directory */
	xmlrpc_struct_read_value(env, _arr_val, "DestDir", &_struct_val);
	xmlrpc_read_string(env, _struct_val, &_str_val);
	if(_str_val) {
		USENET_NZBGET_COPY_ELEMENT(f_list->_dest_dir, _str_val);
		free((void*) _str_val);
		_str_val = NULL;
		xmlrpc_DECREF(_struct_val);
	}


	/* Final directory */
	xmlrpc_struct_read_value(env, _arr_val, "FinalDir", &_struct_val);
	xmlrpc_read_string(env, _struct_val, &_str_val);
	if(_str_val) {
		USENET_NZBGET_COPY_ELEMENT(f_list->_final_dir, _str_val);
		free((void*) _str_val);
		_str_val = NULL;
		xmlrpc_DECREF(_struct_val);
	}

	/* File size */
	xmlrpc_struct_read_value(env, _arr_val, "FileSizeMB", &_struct_val);
	xmlrpc_read_int(env, _struct_val, (xmlrpc_int*) &f_list->_file_size);
	xmlrpc_DECREF(_struct_val);

	/* Remaining size */
	xmlrpc_struct_find_value(env, _arr_val, "RemainingSizeMB", &_struct_val);
	if(!_struct_val)
		f_list->_remaining_size = 0;
	else {
		xmlrpc_read_int(env, _struct_val, (xmlrpc_int*) &f_list->_remaining_size);
		xmlrpc_DECREF(_struct_val);
	}

	/* Active download list */
	xmlrpc_struct_find_value(env, _arr_val, "ActiveDownloads", &_struct_val);
	if(!_struct_val)
		f_list->_active_downloads = 0;
	else {
		xmlrpc_read_int(env, _struct_val, (xmlrpc_int*) &f_list->_active_downloads);
		xmlrpc_DECREF(_struct_val);
	}

	/* Status */
	xmlrpc_struct_read_value(env, _arr_val, "Status", &_struct_val);
	xmlrpc_read_string(env, _struct_val, &_str_val);
	if(_str_val) {
		USENET_NZBGET_COPY_ELEMENT(f_list->_status, _str_val);
		free((void*) _str_val);
		_str_val = NULL;
		xmlrpc_DECREF(_struct_val);
	}

	/* decrement reference count */
	xmlrpc_DECREF(_arr_val);

	return USENET_SUCCESS;
}

static int _nzb_populate_flist2(xmlNodePtr member, struct usenet_nzb_filellist* f_list)
{
	xmlChar* _name = NULL;
	char* _value = NULL;
	xmlNodePtr _child_node = NULL;

	/* get the member name */
	_child_node = xmlFirstElementChild(member);
	if(_child_node == NULL)
		return USENET_ERROR;

	_name = xmlNodeGetContent(_child_node);
	usenet_uxmlrpc_get_member(member, (char*) _name, &_value);

	if(_value == NULL)
		goto cleanup;

	if(strcmp((char*) _name, "NZBID") == 0) {
		f_list->_nzb_id = atoi(_value);
	}
	else if(strcmp((char*) _name, "NZBFilename") == 0) {
		USENET_NZBGET_COPY_ELEMENT(f_list->_nzb_file_name,_value);
	}
	else if(strcmp((char*) _name, "NZBName") == 0) {
		USENET_NZBGET_COPY_ELEMENT(f_list->_nzb_name, _value);
	}
	else if(strcmp((char*) _name, "DestDir") == 0) {
		USENET_NZBGET_COPY_ELEMENT(f_list->_dest_dir, _value);
	}
	else if(strcmp((char*) _name, "FinalDir") == 0) {
		USENET_NZBGET_COPY_ELEMENT(f_list->_final_dir, _value);
	}
	else if(strcmp((char*) _name, "FileSizeMB") == 0) {
		f_list->_file_size = atoi(_value);
	}
	else if(strcmp((char*) _name, "RemainingSizeMB") == 0) {
		f_list->_remaining_size = atoi(_value);
	}
	else if(strcmp((char*) _name, "ActiveDownloads") == 0) {
		f_list->_active_downloads = atoi(_value);
	}
	else if(strcmp((char*) _name, "Status") == 0) {
		USENET_NZBGET_COPY_ELEMENT(f_list->_status, _value);
	}

cleanup:
	if(_value)
		free(_value);

	if(_name)
		xmlFree(_name);

	_name = NULL;
	_value = NULL;
	return USENET_SUCCESS;
}
