/*
 * Interface for fetching the nezb's and issueing an RPC call to
 * to nzbget to initiate a download.
 */
#define _XOPEN_SOURCE
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <curl/curl.h>
#include <errno.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <time.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <gqueue.h>

#include "usenet.h"

#define USENET_SEARCH_BUFF_SZ 256
#define USENET_URL_BUFF_SZ 1024

#define USENET_HEX_CODE "0123456789abcdef"

#define USENET_DEFAULT_URL "nzbget.xml"
#define USENET_DEFAULT_ENCODE "UTF8"
#define USENET_ELEMENT_TRACE "item"
#define USENET_ELEMENT_PARENT "channel"
#define USENET_ELEMENT_ENCLOSURE "enclosure"
#define USENET_ELEMENT_DESCRIPTION "description"
#define USENET_ELEMENT_PUBDATE "pubDate"
#define USENET_ELEMENT_URL "url"

#define USENET_DESCRIPTION_SIZE 512
#define USENET_DESCRIPTION_TOKEN "<br />"
#define USENET_SIZE_TOKEN " "
#define USENET_SIZE_GB "GB"
#define USENET_MB_CONV 1024.0
#define USENET_ITEMS_TOP 5
#define USENET_DOWNLOAD_FILE "download.nzb"
#define USENET_FILE_EXT ".nzb"
#define USENET_FILE_EXT_SZ 4
#define USENET_URL_BEGIN "http://"
#define USENET_DEFAULT_SAVE_PATH "/home/pyrus/Downloads/nzb/"


static const char* USENET_URL = "http://nzbclub.com/nzbrss.aspx?q=";

struct search_content
{
    char* _memory;
    size_t _size;
    const char* _search_key;
};

/*
 * Struct for wrapping a nzb item.
 * The parser shall produce an array of this.
 */
struct nzb_item
{
    char* _pub_date;
    char* _description;
    char* _link;
    const char* _alias;
    unsigned int _sz;
    int _time_since_today;
};

char use_search_key[USENET_SEARCH_BUFF_SZ];

static int _exec_search(const char* search_key, struct search_content* content);
static int _parse_document(const char* search, xmlDocPtr* doc, const struct search_content* content, struct nzb_item** items, unsigned int* sz);
static unsigned int _get_size(const char* description);
static int _get_usenet_item(const struct nzb_item* items, unsigned int sz, const struct nzb_item** sel_item);

static unsigned int _write_content_callback(void* contents, size_t size, size_t nmemb, void* userp);
static int _sort_ascending(struct nzb_item* items, unsigned int sz);
static void _queue_delete_helper(void* item);
static int _download_content(char* url, struct search_content* content, int (*callback)(CURLcode, void*));
static int _write_file_callback(CURLcode result, void* content);

int usenet_nzb_search_and_get(const char* nzb_desc, const char* s_url)
{
    int _stat = 0;
    struct search_content _content;
    struct search_content _download;
    xmlDocPtr _doc = NULL;
    struct nzb_item* _items = NULL;
    const struct nzb_item* _sel_item = NULL;
    unsigned int _entry_count = 0;
    unsigned int _i = 0;

	/* check for argument */
	if(nzb_desc == NULL)
		return USENET_ERROR;

	/* if search url has been set pass to local variable */
	if(s_url != NULL)
		USENET_URL = s_url;

    memset(use_search_key, 0, USENET_SEARCH_BUFF_SZ);
    memset((void*) &_content, 0, sizeof(struct search_content));
    memset((void*) &_download, 0, sizeof(struct search_content));


    /* copy to local variable */
	USENET_LOG_MESSAGE("copying argument to local variable");
	strncpy(use_search_key, nzb_desc, USENET_SEARCH_BUFF_SZ-1);
	use_search_key[USENET_SEARCH_BUFF_SZ-1] = '\0';

	/* set the search key */
	_download._search_key = use_search_key;


    if(use_search_key[0] == 0) {
	    USENET_LOG_MESSAGE("No search key defined, bailing out");
	    return USENET_ERROR;
	}

    USENET_LOG_MESSAGE("contacting usenet search");
    _stat = _exec_search(use_search_key, &_content);

    if(!_stat) {
	    xmlInitParser();
	    if(!_parse_document(use_search_key, &_doc, &_content, &_items, &_entry_count)) {
		    USENET_LOG_MESSAGE_ARGS("Number of results: %i", _entry_count);

		    /* display the most appropriate item */
		    _get_usenet_item(_items, _entry_count, &_sel_item);

		    if(_sel_item != NULL) {
				USENET_LOG_MESSAGE_ARGS("nzbget selected item: %s", _sel_item->_description);
			    _download_content(_sel_item->_link, &_download, _write_file_callback);
			}
			else {
				USENET_LOG_MESSAGE("unable to get the search item");
			}
		}
	}


    /* free memory */
    if(_items != NULL) {
	    for(_i=0; _i < _entry_count; _i++) {
		    if(_items[_i]._pub_date)
				free(_items[_i]._pub_date);

		    if(_items[_i]._description)
				free(_items[_i]._description);

		    if(_items[_i]._link)
				free(_items[_i]._link);
		}
	    free(_items);
	}

    if(_content._memory)
		free(_content._memory);

    if(_doc != NULL)
		xmlFreeDoc(_doc);

    /* Shutdown libxml */
    xmlCleanupParser();

    return 0;
}

/* Execute and get the content of the url */
static int _exec_search(const char* search_key, struct search_content* content)
{
    CURL* _curl = NULL;
    CURLcode _res;
    int _i = 0;
    int _rt_val = 0;

    char _url[USENET_URL_BUFF_SZ];

    USENET_LOG_MESSAGE("constructing search parameter");

    /* add basic memory allocation for the content struct */
    content->_memory = (char*) malloc(sizeof(char));
    content->_size = 0;


    memset(_url, 0, USENET_URL_BUFF_SZ);
    strncpy(_url, USENET_URL, USENET_URL_BUFF_SZ-1);
    _url[USENET_URL_BUFF_SZ-1] = '\0';

    strncat(_url, search_key, strlen(search_key));
    for(_i=0; _i<strlen(_url); _i++)
	{
	    /* If space character was found replace with + */
	    if(_url[_i] == USENET_SPACE_CHAR)
			_url[_i] = USENET_PLUS_CHAR;
	}

    USENET_LOG_MESSAGE("initialising curl easy...");
    curl_global_init(CURL_GLOBAL_ALL);

    /* initialise curl */
    _curl = curl_easy_init();
    if(_curl == NULL)
	{
	    USENET_LOG_MESSAGE("Unable to initialise CURL");
	    curl_global_cleanup();
	    return -1;
	}

    USENET_LOG_MESSAGE("setting curl parameters");
    curl_easy_setopt(_curl, CURLOPT_URL, _url);

    curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, _write_content_callback);
    curl_easy_setopt(_curl, CURLOPT_WRITEDATA, (void*) content);

    /* some server don't like performing requests without a user agent */
    curl_easy_setopt(_curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    /* handle url redirect */
    curl_easy_setopt(_curl, CURLOPT_FOLLOWLOCATION, 1L);

    /* perform the request, res will return the return code */
    USENET_LOG_MESSAGE_ARGS("executing search on %s...", _url);
    _res = curl_easy_perform(_curl);
    if(_res != CURLE_OK)
	{
	    USENET_LOG_MESSAGE_ARGS("easy perform failed %s", curl_easy_strerror(_res));
	    _rt_val = -1;
	}

    USENET_LOG_MESSAGE("search successful!");
    curl_easy_cleanup(_curl);

    USENET_LOG_MESSAGE("curl cleaned up");
    curl_global_cleanup();
    return _rt_val;
}


/* write callback method */
static unsigned int _write_content_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t _act_size = size * nmemb;

    /* cast user pointer */
    struct search_content* _content = (struct search_content*) userp;

    /* reallocate memory */
    _content->_memory = realloc(_content->_memory, _content->_size + _act_size + 1);
    if(_content->_memory == NULL)
	{
	    /* failure allocating memory bail here */
	    USENET_LOG_MESSAGE("Unable to allocate content memory..");
	    return 0;
	}

    /* copy content into the buffer */
    memcpy(&_content->_memory[_content->_size], contents, _act_size);
    _content->_size += _act_size;
    _content->_memory[_content->_size] = 0;

    return _act_size;
}

/* parse xml document */
static int _parse_document(const char* search, xmlDocPtr* doc, const struct search_content* content, struct nzb_item** items, unsigned int* sz)
{
    xmlNodePtr _root_node = NULL;
    xmlNodePtr _child = NULL;
    xmlNodePtr _s_child = NULL;
    int _rt_val = 0;
    unsigned int _queue_count = 0;
    unsigned int _queue_counter = 0;
    gqueue _queue;

    struct nzb_item* _item = NULL;
    struct nzb_item* _items = NULL;
    void* _data = NULL;

    const char* _prop = NULL;
    xmlChar* _url = NULL;

    time_t  _now;
    struct tm _tm;
    double _pub_days = 0.0;
    char* _time_stat = NULL;
    time(&_now);

    *doc = xmlReadMemory(content->_memory, content->_size, USENET_DEFAULT_URL, NULL, 0);
    if(*doc == NULL)
	{
	    USENET_LOG_MESSAGE("Errors occured while parsing");
	    return USENET_ERROR;
	}


    /* get root node */
    _root_node = xmlDocGetRootElement(*doc);
    if(_root_node == NULL)
	{
	    USENET_LOG_MESSAGE("Unable to get root node");
	    return -1;
	}

    _child = xmlFirstElementChild(_root_node);
    if(_child == NULL)
	{
	    USENET_LOG_MESSAGE("Unable to get the first child");
	    return -1;
	}

    /* count item elements */
    USENET_LOG_MESSAGE("counting result entries...");
    gqueue_new(&_queue, _queue_delete_helper);

    while(_child)
	{
	    if(strcmp((char*) _child->name, USENET_ELEMENT_PARENT) == 0)
			_child = xmlFirstElementChild(_child);
	    else if(strcmp((char*) _child->name, USENET_ELEMENT_TRACE) == 0)
		{
		    (*sz)++;
		    /* dive into child */
		    _s_child = xmlFirstElementChild(_child);
		    if(_s_child != NULL)
			{
			    _item = (struct nzb_item*) malloc(sizeof(struct nzb_item));

			    /* initialise items */
			    memset(_item, 0, sizeof(struct nzb_item));
			    _item->_alias = search;
			    _item->_time_since_today = 0;
			}


		    while(_s_child)
			{
			    if(strcmp((const char*) _s_child->name, USENET_ELEMENT_DESCRIPTION) == 0)
				{
				    _item->_description = (char*) xmlNodeGetContent(_s_child);
				    _item->_sz = _get_size(_item->_description);
				}
			    else if(strcmp((const char*) _s_child->name, USENET_ELEMENT_ENCLOSURE) == 0)
				{
				    _prop = USENET_ELEMENT_URL;
				    _url = xmlGetProp(_s_child, (const xmlChar*) _prop);
				    _item->_link = (_url? (char*) _url : NULL);
				}
			    else if(strcmp((const char*) _s_child->name, USENET_ELEMENT_PUBDATE) == 0)
				{
				    _item->_pub_date = (char*) xmlNodeGetContent(_s_child);
				    _time_stat = strptime(_item->_pub_date, "%a, %d %b %Y %H:%M:%S", &_tm);
				}

			    /* if the _tm struct is not NULL, we calculate days since published */
			    if(_time_stat != NULL)
				{
				    _pub_days = difftime(_now, mktime(&_tm));
				    _item->_time_since_today = (int) (_pub_days / (60*60*24));
				    _time_stat = NULL;
				    _pub_days = 0.0;
				}

			    _s_child = xmlNextElementSibling(_s_child);
			}

		    /* push element into the list and set to NULL */
		    if(_item)
			{
			    gqueue_in(&_queue, _item);
			    _item = NULL;
			}

		    _child = xmlNextElementSibling(_child);
		}
	    else
			_child = xmlNextElementSibling(_child);
	}

    _queue_count = gqueue_count(&_queue);
    while(_queue_count > 0)
	{
	    if(_items == NULL)
			_items = (struct nzb_item*) calloc(_queue_count, sizeof(struct nzb_item));

	    gqueue_out(&_queue, &_data);
	    if(_data != NULL)
		{
		    _items[_queue_counter]._pub_date = ((struct nzb_item*) _data)->_pub_date;
		    _items[_queue_counter]._description = ((struct nzb_item*) _data)->_description;
		    _items[_queue_counter]._link = ((struct nzb_item*) _data)->_link;
		    _items[_queue_counter]._sz = ((struct nzb_item*) _data)->_sz;
		    _items[_queue_counter]._alias = ((struct nzb_item*) _data)->_alias;
		    _items[_queue_counter]._time_since_today = ((struct nzb_item*) _data)->_time_since_today;
		    _queue_counter++;
		    free(_data);
		}
	    _queue_count = gqueue_count(&_queue);
	}

    /* sort items */
    _sort_ascending(_items, _queue_counter);

    *items = _items;
    *sz = _queue_counter;

    /* delete queue */
    gqueue_delete(&_queue);
    return _rt_val;
}

/* delete helper method */
static void _queue_delete_helper(void* item)
{
    struct nzb_item* _item;
    if(item == NULL)
		return;

    _item = (struct nzb_item*) item;

    /* set all elements to NULL */
    _item->_pub_date = NULL;
    _item->_description = NULL;
    _item->_link = NULL;
    _item->_alias = NULL;

    /* free memory allocated by previous */
    free(_item);

    return;
}

/* gets the size in mega bytes */
static unsigned int _get_size(const char* description)
{
    const char* _tok_end_pos = NULL;
    char _t_buff[USENET_DESCRIPTION_SIZE];
    char* _tok;
    float _file_size = 0.0;

    /* flags to indicate dimension of the file size expressed in */
    unsigned int _gb_flg = 0;

    /* check for NULL pointer. If return 0 as size */
    if(description == NULL)
		return 0;

    memset(_t_buff, 0, USENET_DESCRIPTION_SIZE);


    /* get the first html break in the string */
    _tok_end_pos = strstr(description, USENET_DESCRIPTION_TOKEN);

    if(_tok_end_pos == NULL)
		return 0;

    strncpy(_t_buff, description, (unsigned int) (_tok_end_pos - description));
    _t_buff[USENET_DESCRIPTION_SIZE-1] = '\0';

    if(strstr(_t_buff, USENET_SIZE_GB))
		_gb_flg = 1;


    /* tokenise the string */
    _tok = strtok(_t_buff, USENET_SIZE_TOKEN);
    while(_tok)
	{
	    _file_size = atof(_tok);
	    if(_file_size > 0.0)
			break;

	    _tok = strtok(NULL, USENET_SIZE_TOKEN);
	}

    /* if the size was expressed in GB we convert to MB */
    if(_gb_flg)
		_file_size *= USENET_MB_CONV;

    return (unsigned int) _file_size;
}

/* sort the array in ascending order of size */
static int _sort_ascending(struct nzb_item* items, unsigned int sz)
{
    struct nzb_item _item;
    unsigned int _i, _j;
    for(_i=0; _i<sz; _i++)
	{
	    for(_j=_i+1; _j<sz; _j++)
		{
		    if(items[_i]._sz < items[_j]._sz)
			{
			    memset((void*) &_item, 0, sizeof(struct nzb_item));
			    memcpy((void*) &_item, (void*) &items[_j], sizeof(struct nzb_item));

			    /* swap items */
			    memcpy((void*) &items[_j], (void*) &items[_i], sizeof(struct nzb_item));
			    memcpy((void*) &items[_i], (void*) &_item, sizeof(struct nzb_item));
			}
		}
	}

    return 0;
}

/* Get selected item */
static int _get_usenet_item(const struct nzb_item* items, unsigned int sz, const struct nzb_item** sel_item)
{
    unsigned int _i = 0, _j = 0;
    unsigned int _max = 0;
    /* check items */
    if(items == NULL || sz <=0)
		return -1;

    /* iterate over the selection */
    _max = (sz > USENET_ITEMS_TOP? USENET_ITEMS_TOP : sz);

    for(_i=0; _i<_max; _i++)
	{
	    if(*sel_item == NULL)
		{
		    for(_j=_i+1; _j<_max; _j++)
			{
			    if(items[_i]._time_since_today <=  items[_j]._time_since_today)
				{
				    *sel_item = &items[_i];
				    break;
				}
			}
		}
	    else
			if((*sel_item)->_time_since_today > items[_i]._time_since_today)
				*sel_item = &items[_i];
	}

    return 0;
}

static int _download_content(char* url, struct search_content* content, int (*callback)(CURLcode, void*))
{
    CURL* _curl = NULL;
    CURLcode _res;
    int _rt_val = 0;
    int _i = 0;

    /* add basic memory allocation for the content struct */
    content->_memory = (char*) malloc(sizeof(char));
    content->_size = 0;


    USENET_LOG_MESSAGE("initialising curl easy...");
    curl_global_init(CURL_GLOBAL_ALL);

    /* initialise curl */
    _curl = curl_easy_init();
    if(_curl == NULL)
	{
		USENET_LOG_MESSAGE("Unable to initialise CURL");
	    curl_global_cleanup();
	    return -1;
	}

    for(_i=0; _i<strlen(url); _i++)
	{
	    /* If space character was found replace with + */
	    if(url[_i] == USENET_SPACE_CHAR)
			url[_i] = USENET_USCORE_CHAR;
	}

    USENET_LOG_MESSAGE("setting curl parameters");
    curl_easy_setopt(_curl, CURLOPT_URL, url);

    curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, _write_content_callback);
    curl_easy_setopt(_curl, CURLOPT_WRITEDATA, (void*) content);

    /* some server don't like performing requests without a user agent */
    curl_easy_setopt(_curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    /* handle url redirect */
    curl_easy_setopt(_curl, CURLOPT_FOLLOWLOCATION, 1L);

    /* perform the request, res will return the return code */
    USENET_LOG_MESSAGE("executing search...");
    _res = curl_easy_perform(_curl);
    if(_res != CURLE_OK)
	{
	    USENET_LOG_MESSAGE_ARGS("easy perform failed %s", curl_easy_strerror(_res));
	    _rt_val = -1;
	}

    /* call the callback to indicate we are done */
    if(callback != NULL)
		callback(_res, content);

    USENET_LOG_MESSAGE("search successful!");
    curl_easy_cleanup(_curl);

    USENET_LOG_MESSAGE("curl cleaned up");
    curl_global_cleanup();


    return _rt_val;
}


static int _write_file_callback(CURLcode result, void* content)
{
    char _file_name[USENET_SEARCH_BUFF_SZ] = {0};
    struct search_content* _content;
    int _fd = 0;
    if(result != CURLE_OK || content == NULL)
		return -1;

    _content = (struct search_content*) content;

    /* copy the file name to internal buffer */
    if(_content->_search_key == NULL)
		strcpy(_file_name, USENET_DOWNLOAD_FILE);
    else
	{
	    strncat(_file_name, USENET_DEFAULT_SAVE_PATH, strlen(USENET_DEFAULT_SAVE_PATH));
	    strncat(_file_name, _content->_search_key, strlen(_content->_search_key));
	    strncat(_file_name, USENET_FILE_EXT, USENET_FILE_EXT_SZ);
	}

    USENET_LOG_MESSAGE_ARGS("opening file, %s for writing", _file_name);
    _fd = open(_file_name, O_WRONLY | O_CREAT);
    fchmod(_fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if(_fd == -1)
	{
	    goto write_file_free;
		USENET_LOG_MESSAGE("unable to open the file");
	    perror("open() error");
	}

    write(_fd, _content->_memory, _content->_size);
    close(_fd);
    USENET_LOG_MESSAGE("file written successfully");

write_file_free:
    if(_content && _content->_size > 0)
		free(_content->_memory);
    return USENET_SUCCESS;
}
