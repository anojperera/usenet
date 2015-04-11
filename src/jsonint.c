/*
 * JSON interface
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <usenet.h>

#include "usenet.h"
#include "jsmn.h"

#define JSONINT_ALG_KEY "alg"
#define JSONINT_TYP_KEY "typ"

#define JSONINT_ALG_VAL "RS256"
#define JSONINT_TYP_VAL "JWT"

int usjson_header_section(struct gapi_login* login, const char* json_string)
{
    return USENET_SUCCESS;
}

int usjson_claim_set_section(struct gapi_login* login, const char* json_string)
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

	_num_tok *= 2;

	*tok = (jsmntok_t*) calloc(_num_tok, sizeof(jsmntok_t));
	jsmn_init(&_js_parser);
	*num = jsmn_parse(&_js_parser, msg, strlen(msg), *tok, _num_tok);

	if(*num < 0)
		return USENET_ERROR;
	else
		return USENET_SUCCESS;
}

int usjson_get_token(const char* msg, jsmntok_t* tok, size_t num_tokens, const char* key, char** value, jsmntok_t** obj)
{
	unsigned int _f_flg = 0;
	size_t _cnt = 0;
	size_t _key_sz = 0;
	char* _t = NULL;

	/* check for NULL pointers */
	if(msg == NULL || tok == NULL || key == NULL || obj == NULL) {
		USENET_LOG_MESSAGE("null values in the argument");
		return USENET_ERROR;
	}

	USENET_LOG_MESSAGE_ARGS("looking for key %s ", key);
	_key_sz = strlen(key);

	/* iterate through the tokens and find the key */
	while(_cnt++ < num_tokens) {

		/* find the key */
		if(_key_sz == tok->end - tok->start &&
		   strncmp(msg + tok->start, key, tok->end - tok->start) == 0) {

			/* break out of the loop */
			USENET_LOG_MESSAGE("key found in json");
			_f_flg = 1;
			tok++;
			*obj = tok;
			break;
		}

		tok++;
	}

	/* if the key was found, we alloc memory to the string and return */
	if(_f_flg && !value) {
		USENET_LOG_MESSAGE("JSON token found, returning token in obj para");
		return USENET_SUCCESS;
	}
	else if(_f_flg) {
		USENET_LOG_MESSAGE("storing the json key in a temporary variable");
		_t = *value;
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

/*
 * Returns an array of string pointed by the token, this will only work if the token is an array
 * and types in the array are premitive types.
 */
int usjson_get_token_arr_as_str(const char* msg, jsmntok_t* tok, struct usenet_str_arr* str_arr)
{
	int _i = 0;
	int _sz = 0;

	/* NULL check arguments */
	if(msg == NULL || tok == NULL || str_arr == NULL) {
		USENET_LOG_MESSAGE("arguments passed contains NULL pointers");
		return USENET_ERROR;
	}

	/* check type of the json token */
	USENET_LOG_MESSAGE("checking if the token is a json array");
	if(tok->type != JSMN_ARRAY) {
		USENET_LOG_MESSAGE("json token is not an array");
		return USENET_ERROR;
	}

	/*  create a char array */
	USENET_LOG_MESSAGE("going through the json array and copying to the char array");

	_sz = tok->end - tok->start;
	str_arr->_arr = (char**) calloc(sizeof(char*), tok->size);
	for(_i = 0; _i < tok->size; _i++) {
		str_arr->_arr[_i] = (char*) malloc((_sz + 1) * sizeof(char));
		memset(str_arr->_arr[_i], 0, _sz+1);
		strncpy(str_arr->_arr[_i], msg + tok->start, _sz);

		/* remove other characters */
		USENET_LOG_MESSAGE("cleaning illegal characters in array");
		usenet_utils_remove_chars(str_arr->_arr[_i], _sz);
		USENET_LOG_MESSAGE_ARGS("cleaned array: %s", str_arr->_arr[_i]);
	}

	str_arr->_sz = tok->size;
	return USENET_SUCCESS;
}
