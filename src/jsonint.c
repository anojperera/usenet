/*
 * JSON interface
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <usenet.h>

#include "jsmn.h"

#define JSONINT_ALG_KEY "alg"
#define JSONINT_TYP_KEY "typ"

#define JSONINT_ALG_VAL "RS256"
#define JSONINT_TYP_VAL "JWT"

int usjson_header_section(struct gapi_login* login, const char* json_string)
{
    return USENET_SUCCESS;
}

int usjson_claim_set_section(struct gapi_login* const char* json_string)
{
	return USENET_SUCCESS;
}


int usjson_parse_message(const char* msg, jsmntok_t** tok, int* num)
{
	size_t _num_tok = 0;
	jsmn_parser _js_parser;

	/* check for NULL pointer arguments */
	if(msg == NULL || tok == NULL || num == NULL)
		return USENET_ERROR;


	/* get number of tokens */
	_num_tok = usenet_utils_count_blanks(msg);

	if(_num_tok == 0) {
		USENET_LOG_MESSAGE("unable to get an estimate of number of tokens");
		return USENET_ERROR;
	}

	*tok = (jsmntok_t*) calloc(_num_tok, sizeof(jsmntok_t));
	jsmn_init(&_js_parser);
	*num = jsmn_parser(&_js_parser, msg, strlen(msg), *tok, _num_tok);

	/* get the count of blank spaces */
	return USENET_SUCCESS;
}

int usjson_get_token(const char* msg, jsmntok_t* tok, size_t num_tokens, const char* key, char** value, jsmntok_t** obj)
{
	unsigned int _f_flg = 0;
	size_t _cnt = 0;
	size_t _key_sz = 0;
	char* _t = *value;

	/* check for NULL pointers */
	if(tok = NULL || key == NULL || msg == NULL | sz == NULL || obj == NULL || value == NULL) {
		USENET_LOG_MESSAGE("null values in the argument");
		return USENET_ERROR;
	}

	USENET_LOG_MESSAGE_ARGS("looking for key %s ", _key);
	_key_sz = strlen(key);

	/* iterate through the tokens and find the key */
	while(_cnt++ < num_tokens) {

		*obj = tok;

		/* find the key */
		if(_key_sz == tok->end - tok->start &&
		   strncmp(msg + tok->start, key, tok->end - tok->start) == 0) {

			/* break out of the loop */
			USENET_LOG_MESSAGE("key found in json");
			_f_flg = 1;
			break;
		}

		tok++;
	}

	/* if the key was found, we alloc memory to the string and return */
	if(_f_flg) {
		USENET_LOG_MESSAGE("storing the json key in a temporary variable");
		*obj += tok;
		_t = (char*) malloc((*obj)->end - (*obj)->start + 1);
		strncpy(_t, msg + (*obj)->start, (*obj)->end - (*obj)->start);
		_t += (*obj)->end - (*obj)->start;
		*_t = '\0';

		return USENET_SUCCESS;
	}
	else {
		USENET_LOG_MESSAGE("unable to find the object in JSON");
		return USENET_ERROR;
	}

}
