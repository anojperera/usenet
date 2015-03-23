/*
 * JSON interface
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <json.h>

#define JSONINT_ALG_KEY "alg"
#define JSONINT_TYP_KEY "typ"

#define JSONINT_ALG_VAL "RS256"
#define JSONINT_TYP_VAL "JWT"



int header_section(struct gapi_login* login, const char* json_string)
{
    const char* json_srl = NULL;
    
    struct json_object* _alg = NULL, *_typ = NULL;
    struct json_object* _parent = NULL;

    _parent = json_object_new_object();

    /* create string objects */
    _alg = json_object_new_string(login->alg);
    _typ = json_object_new_string(login->typ);

    /*
     * if the objects were created successfully we add to the the parent
     */
    json_object_object_add(_parent, JSONINT_ALG_KEY, _alg);
    json_object_object_add(_parent, JSONINT_TYP_KEY, _typ);

    json_string = json_object_to_json_string_ext(_parent, JSON_C_TO_STRING_PLAIN);
    
    while(json_object_put(_parent) != 1);
    
    return USENET_SUCCESS;
}

int claim_set_section(struct gapi_login* const char* json_string)
{

}
