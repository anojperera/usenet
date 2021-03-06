/*
 * This is a basic implementation of xmlrpc using libxml2 and curl.
 * xmlrpc library has int limitations which doesn't suit the needs
 * of this project.
 */

#define _XOPEN_SOURCE
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <curl/curl.h>
#include <errno.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "usenet.h"

#define USENET_XMLRPC_SERVER_URL "http://127.0.0.1:6789/xmlrpc"
#define USENET_XMLRPC_USERNAME_PASS "nzbget:tegbzn6789"
#define USENET_XMLRPC_HEADER_BUF_SZ 256

#define USENET_XMLRPC_USERAGENT "libcurl-agent/1.0"
#define USENET_XMLRPC_HEADER1 "Content-Type: text/xml"
#define USENET_XMLRPC_HEADER2 "Content-length: %i"
#define USENET_XMLRPC_CURL_TIMEOUT 5L


/* buffer to hold the data returned from the server */
struct uxmlrpc_buffer
{
	char* _buffer;
	size_t _size;
};

/* Helper method for constructing a xmldocument with parameters */
static int _create_xml_para(const char* method, char** paras, size_t size, xmlDocPtr* xmldoc);

/* parse the buffer into a xmldoc */
static int _parse_xml_char(struct uxmlrpc_buffer* rpc_buff, xmlDocPtr* xmldoc);

/* callback for loading the return contents from the curl */
static unsigned int _write_content_callback(void* contents, size_t size, size_t nmemb, void* userp);

/* main rpc call */
int usenet_uxmlrpc_call(const char* method_name, char** paras, size_t size, xmlDocPtr* res)
{
	int _stat = CURLE_OK, _ret = USENET_SUCCESS;
	int _xml_sz = 0;												/* size of the serailsie xml data */
	xmlChar* _xml_mem = NULL;										/* serialise xml data */

	char _hbuf[USENET_XMLRPC_HEADER_BUF_SZ] = {0};

	CURL* _curl = NULL;
	struct curl_slist* _hlist = NULL;								/* header lsit for rpc call */
	xmlDocPtr _req_xmldoc = NULL;

	struct uxmlrpc_buffer _cbuf;									/* content buffer */

	/* Create xml parameter */
	if(_create_xml_para(method_name, paras, size, &_req_xmldoc) != USENET_SUCCESS)
		return USENET_ERROR;

	/* serialise the xml and get the size */
	xmlDocDumpFormatMemory(_req_xmldoc, &_xml_mem, &_xml_sz, 0);

	sprintf(_hbuf, USENET_XMLRPC_HEADER2, _xml_sz);

	/* initialise the content buffer */
	_cbuf._buffer = (char*) malloc(sizeof(char));
	_cbuf._size = 0;

	curl_global_init(CURL_GLOBAL_ALL);

	/* initialise curl */
	USENET_LOG_MESSAGE_ARGS("initialising curl for the rpc call: %s", method_name);
	_curl = curl_easy_init();
	if(_curl == NULL) {
		USENET_LOG_MESSAGE("failed to initialise curl for rpc call");
		curl_global_cleanup();
		return USENET_ERROR;
	}

	/* setup headers */
	USENET_LOG_MESSAGE("setting up headers");
	_hlist = curl_slist_append(_hlist, USENET_XMLRPC_HEADER1);
	_hlist = curl_slist_append(_hlist, _hbuf);

	/* set http basic authentication */
	curl_easy_setopt(_curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
	curl_easy_setopt(_curl, CURLOPT_USERPWD, USENET_XMLRPC_USERNAME_PASS);

	/*
	 * Set curl options for url, writing data, callback and
	 * and header list.
	 */
	curl_easy_setopt(_curl, CURLOPT_POST, 1L);
	curl_easy_setopt(_curl, CURLOPT_URL, USENET_XMLRPC_SERVER_URL);
	curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, _hlist);
	curl_easy_setopt(_curl, CURLOPT_USERAGENT, USENET_XMLRPC_USERAGENT);
	curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, _write_content_callback);

	/* set time out */
	curl_easy_setopt(_curl, CURLOPT_TIMEOUT, USENET_XMLRPC_CURL_TIMEOUT);

	/* set post fields */
	curl_easy_setopt(_curl, CURLOPT_POSTFIELDS, (char*) _xml_mem);


	curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &_cbuf);

	/* post the action */
	USENET_LOG_MESSAGE_ARGS("performing curl operation on method: %s", method_name);
	_stat = curl_easy_perform(_curl);

	if(_stat != CURLE_OK) {
		USENET_LOG_MESSAGE("rpc call was not successful, therefore no parsed xml document is available");
		_ret = USENET_ERROR;
	}
	else {
		/* since the call is sucessfull parse the document and send back to the caller */
		USENET_LOG_MESSAGE_ARGS("rpc call %s, was successful", method_name);
		_parse_xml_char(&_cbuf, res);
	}

	/* free content buffer */
	USENET_LOG_MESSAGE("cleaning up used memory");
	if(_cbuf._size > 0 || _cbuf._buffer != NULL) {
		free(_cbuf._buffer);
		_cbuf._buffer = NULL;
		_cbuf._size = 0;
	}
	if(_hlist)
		curl_slist_free_all(_hlist);

	if(_req_xmldoc)
		xmlFreeDoc(_req_xmldoc);

	USENET_LOG_MESSAGE("cleaning up curl after rpc call");
	curl_easy_cleanup(_curl);

	if(_xml_mem)
		xmlFree(_xml_mem);

	return _ret;
}

int usenet_uxmlrpc_get_node_count(xmlNodePtr root_node, const char* key, int* count, xmlNodePtr* node)
{
	int _val_flg = 0;
	xmlNodePtr _child_node = NULL;

	/* find the value node */
	_child_node = xmlFirstElementChild(root_node);
	do {

		/* check for value */
		/* set val flag to true and find get the value node */
		if(strcmp((char*) _child_node->name, key) == 0)
			_val_flg += 1;

		/* break out of the loop */
		if(_val_flg > 1)
			break;

		_child_node = xmlFirstElementChild(_child_node);

	}while(_child_node != NULL);


	*node = _child_node;

	/* get count */
	while(_child_node) {
		(*count) += 1;
		_child_node = xmlNextElementSibling(_child_node);
	}

	return USENET_SUCCESS;
}

int usenet_uxmlrpc_get_member(xmlNodePtr member_node, const char* name, char** value)
{
	xmlNodePtr _child_node = NULL;
	xmlChar* _name_val = NULL;

	/* initialise to NULL */
	*value = NULL;

	if(strcmp((char*) member_node->name, "member") != 0) {
		USENET_LOG_MESSAGE("node is not a member");
		return USENET_ERROR;
	}

	_child_node = xmlFirstElementChild(member_node);
	if(_child_node == NULL)
		return USENET_ERROR;

	if(strcmp((char*) _child_node->name, "name") != 0)
		return USENET_ERROR;

	/* if the element is the name, compare the field */
	_name_val = xmlNodeGetContent(_child_node);
	if(_name_val == NULL)
		return USENET_ERROR;

	/* if the name doesn't match clean up and exit */
	if(strcmp((char*) _name_val, name) != 0)
		goto cleanup;

	/* get the next sibling */
	if(!(_child_node = xmlNextElementSibling(_child_node)) &&
	   strcmp((char*) _child_node->name, USENET_NZBGET_XMLRESPONSE_VALUE))
		goto cleanup;

	_child_node = xmlFirstElementChild(_child_node);
	if(!_child_node)
		goto cleanup;

	/* get the contents of field */
	*value = (char*) xmlNodeGetContent(_child_node);

cleanup:
	if(_name_val)
		xmlFree(_name_val);

	return USENET_SUCCESS;
}

static unsigned int _write_content_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t _act_size = size * nmemb;

    /* cast user pointer */
    struct uxmlrpc_buffer* _content = (struct uxmlrpc_buffer*) userp;

    /* reallocate memory */
    _content->_buffer = realloc(_content->_buffer, _content->_size + _act_size + 1);
    if(_content->_buffer == NULL)
	{
	    /* failure allocating memory bail here */
	    USENET_LOG_MESSAGE("Unable to allocate content memory..");
	    return 0;
	}

    /* copy content into the buffer */
    memcpy(&_content->_buffer[_content->_size], contents, _act_size);
    _content->_size += _act_size;
    _content->_buffer[_content->_size] = 0;

    return _act_size;
}

static int _create_xml_para(const char* method, char** paras, size_t size, xmlDocPtr* xmldoc)
{
	size_t _i = 0;
	xmlNodePtr _root_node = NULL;
	xmlNodePtr _paras_node = NULL;
	xmlNodePtr _para_node = NULL;

	if(size <= 0 || method == NULL) {
		USENET_LOG_MESSAGE("parameters set to NULL or size is incorrect");
		return USENET_ERROR;
	}

	/* creates the document */
	*xmldoc = xmlNewDoc(BAD_CAST "1.0");

	/* create root node */
	_root_node = xmlNewNode(NULL, BAD_CAST "methodCall");
	xmlDocSetRootElement(*xmldoc, _root_node);


	/* create method name */
	xmlNewChild(_root_node, NULL, BAD_CAST "methodName", (xmlChar*) method);


	/* create parameters node */
	_paras_node = xmlNewChild(_root_node, NULL, BAD_CAST "params", NULL);

	/* iterate over the parameter list and add to the parameters */
	for(_i = 0; _i < size; _i++) {
		_para_node = xmlNewChild(_paras_node, NULL, BAD_CAST "param", NULL);
		xmlNewChild(_para_node, NULL, BAD_CAST "value", (xmlChar*) paras[_i]);
	}

	return USENET_SUCCESS;

}

/* parse the memory into a xml document */
static int _parse_xml_char(struct uxmlrpc_buffer* rpc_buff, xmlDocPtr* xmldoc)
{
	/* initialise the parser */
	xmlInitParser();

	*xmldoc = xmlReadMemory(rpc_buff->_buffer,
							rpc_buff->_size,
							"rpc_method.xml",
							NULL,
							XML_PARSE_RECOVER | XML_PARSE_HUGE);

	/* clean up the global memory consumed internally */
	xmlCleanupParser();

	return USENET_SUCCESS;
}
