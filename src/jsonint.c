/*
 * JSON interface
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <usenet.h>
#include <sys/time.h>

#include "usenet.h"
#include "jsmn.h"

#define JSONINT_EXP_EXTRA_TIME 300
#define JSONINT_HEADER_SIZE 512

#define JSONINT_ALG_KEY alg
#define JSONINT_TYP_KEY typ

#define JSONINT_ISS_KEY iss
#define JSONINT_SCOPE_KEY scope
#define JSONINT_AUD_KEY aud
#define JSONINT_EXP_KEY exp
#define JSONINT_IAT_KEY iat

#define JSONINT_AS_STRING(name)					\
	#name


/* Helper methods to get various time values */
static inline __attribute__ ((always_inline)) int _usjson_get_exp_time(void);
static inline __attribute__ ((always_inline)) int _usjson_get_iat_time(void);
static inline __attribute__ ((always_inline)) int _usjson_copy_to_str(const char* msg, int* start, int end, char** _str);

int _usjson_header_section(struct gapi_login* login, char** json_string, size_t* size);
int _usjson_claim_set_section(struct gapi_login* login, char** json_string, size_t* size);

int usjson_prepare_jwt(struct gapi_login* login, char** jwt, size_t* size)
{
	size_t _header_sz = 0, _claim_sz = 0;
	char* _header = NULL;
	char* _claim = NULL;

	USENET_LOG_MESSAGE("preparing JWT");

	_header = (char*) malloc(sizeof(char) * JSONINT_HEADER_SIZE);
	_claim = (char*) malloc(sizeof(char) * JSONINT_HEADER_SIZE);

	/* get header and claim set */
	_usjson_header_section(login, &_header, &_header_sz);
	_usjson_claim_set_section(login, &_claim, &_claim_sz);

	/* if either header or claim set is not populated we return error */
	if(!_header_sz > 0 || !_claim_sz > 0) {
		USENET_LOG_MESSAGE("unable to create JWT");
		return USENET_ERROR;
	}

	free(_header);
	free(_claim);
	return USENET_SUCCESS;
}

int _usjson_header_section(struct gapi_login* login, char** json_string, size_t* size)
{
	USENET_LOG_MESSAGE("preparing header section of JWT");
	*size = sprintf(*json_string,
					"{\"%s\":\"%s\",\"%s\":\"%s\"}",
					JSONINT_AS_STRING(JSONINT_ALG_KEY),
					login->JSONINT_ALG_KEY,
					JSONINT_AS_STRING(JSONINT_TYP_KEY),
					login->JSONINT_TYP_KEY);

    return USENET_SUCCESS;
}

int _usjson_claim_set_section(struct gapi_login* login, char** json_string, size_t* size)
{
	int _exp_time = 0, _iat_time = 0;

	USENET_LOG_MESSAGE("preparing claimset of JWT");

	/* Helper methods to get expiry and initialial assertion time */
	_exp_time = _usjson_get_exp_time();
	_iat_time = _usjson_get_iat_time();

	*size = sprintf(*json_string, "{\"%s\":\"%s\",\"%s\":\"%s\",\"%s\":\"%s\",\"%s\":%i,\"%s\":%i}",
					JSONINT_AS_STRING(JSONINT_ISS_KEY),
					login->JSONINT_ISS_KEY,
					JSONINT_AS_STRING(JSONINT_SCOPE_KEY),
					login->JSONINT_SCOPE_KEY,
					JSONINT_AS_STRING(JSONINT_AUD_KEY),
					login->JSONINT_AUD_KEY,
					JSONINT_AS_STRING(JSONINT_EXP_KEY),
					_exp_time,
					JSONINT_AS_STRING(JSONINT_IAT_KEY),
					_iat_time);

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
		*value = (char*) malloc((*obj)->end - (*obj)->start + 1);
		strncpy(*value, msg + (*obj)->start, (*obj)->end - (*obj)->start);
		_t = *value;
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
		_usjson_copy_to_str(msg, &tok->start, tok->end, &str_arr->_arr[_i]);

		/* remove other characters */
		USENET_LOG_MESSAGE("cleaning illegal characters in array");
		usenet_utils_remove_chars(&str_arr->_arr[_i], strlen(str_arr->_arr[_i]));
		USENET_LOG_MESSAGE_ARGS("cleaned array: %s", str_arr->_arr[_i]);
	}

	str_arr->_sz = tok->size;
	return USENET_SUCCESS;
}

static inline __attribute__ ((always_inline)) int _usjson_copy_to_str(const char* msg, int* start, int end, char** _str)
{
	const char* _end;
	const char* _start;

	/*
	 * Variable to control the return start position.
	 * If a comma was found, we increment to the next position
	 * if not reset to zero.
	 * Its initialised to 2 by default.
	 */
	int _extra = 2;

	/* find the end position of the string */
	_start = msg + *start;
	_end = strchr(_start, USENET_COMMA_CHAR);

	if(_end == NULL) {
		_end = msg + *start + end;
		_extra = 0;
	}

	/* need to filter the characters */
	strncpy(*_str, _start, _end - _start);
	(*_str)[(_end - _start)] = '\0';
	*start = _end - msg + _extra;

	return USENET_SUCCESS;
}


static inline __attribute__ ((always_inline)) int _usjson_get_exp_time(void)
{
	return _usjson_get_iat_time() + JSONINT_EXP_EXTRA_TIME;
}

static inline __attribute__ ((always_inline)) int _usjson_get_iat_time(void)
{
	struct timeval _tm;
	gettimeofday(&_tm, NULL);

	return _tm.tv_sec;
}


/* #include <openssl/evp.h> */
/*  #include <openssl/rsa.h> */

/*  EVP_PKEY_CTX *ctx; */
/*  /\* md is a SHA-256 digest in this example. *\/ */
/*  unsigned char *md, *sig; */
/*  size_t mdlen = 32, siglen; */
/*  EVP_PKEY *signing_key; */

/*  /\* */
/*   * NB: assumes signing_key and md are set up before the next */
/*   * step. signing_key must be an RSA private key and md must */
/*   * point to the SHA-256 digest to be signed. */
/*   *\/ */
/*  ctx = EVP_PKEY_CTX_new(signing_key, NULL /\* no engine *\/); */
/*  if (!ctx) */
/*         /\* Error occurred *\/ */
/*  if (EVP_PKEY_sign_init(ctx) <= 0) */
/*         /\* Error *\/ */
/*  if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) <= 0) */
/*         /\* Error *\/ */
/*  if (EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha256()) <= 0) */
/*         /\* Error *\/ */

/*  /\* Determine buffer length *\/ */
/*  if (EVP_PKEY_sign(ctx, NULL, &siglen, md, mdlen) <= 0) */
/*         /\* Error *\/ */

/*  sig = OPENSSL_malloc(siglen); */

/*  if (!sig) */
/*         /\* malloc failure *\/ */

/*  if (EVP_PKEY_sign(ctx, sig, &siglen, md, mdlen) <= 0) */
/*         /\* Error *\/ */

/*  /\* Signature is siglen bytes written to buffer sig *\/ */
