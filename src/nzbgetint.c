/*
 * RPC interface for connecting to nzbget
 */

#include <stdlib.h>
#include <stdio.h>

#include <xmlrpc-c/base.h>
#include <xmlrpc-c/client.h>

#define NAME       "XML-RPC C Auth Client"
#define VERSION    "1.0"
#define SERVER_URL "http://nzbget:tegbzn6789@localhost:6789/xmlrpc"
#define METHOD_NAME "version"

int update_list(void);

static void die_if_fault_occurred(xmlrpc_env * const envP)
{
    if (envP->fault_occurred) {
        fprintf(stderr, "XML-RPC Fault: %s (%d)\n",
                envP->fault_string, envP->fault_code);
        exit(1);
    }
}

int update_list(void)
{
    xmlrpc_env env;
    xmlrpc_value * resultP;
    char* version = NULL;
    
    /* Initialize our error-handling environment. */
    xmlrpc_env_init(&env);

    /* Start up our XML-RPC client library. */
    xmlrpc_client_init2(&env, XMLRPC_CLIENT_NO_FLAGS, NAME, VERSION, NULL, 0);
    die_if_fault_occurred(&env);

    printf("Making XMLRPC call to server url %s method %s "
	   "to request version number ", SERVER_URL, METHOD_NAME);

    /* Make the remote procedure call */
    resultP = xmlrpc_client_call(&env, SERVER_URL, METHOD_NAME, "(n)");
    die_if_fault_occurred(&env);
    
    /* Get our sum and print it out. */
    xmlrpc_read_string(&env, resultP, (const char**) &version);
    die_if_fault_occurred(&env);    

    if(version != NULL)
	{
	    printf("%s\n", version);
	    free(version);
	}
    
    /* Dispose of our result value. */
    xmlrpc_DECREF(resultP);

    /* Clean up our error-handling environment. */
    xmlrpc_env_clean(&env);
    
    /* Shutdown our XML-RPC client library. */
    xmlrpc_client_cleanup();
    return 0;    
}
