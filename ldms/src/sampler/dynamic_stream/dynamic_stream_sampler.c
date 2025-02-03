/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2024 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2024 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <time.h>
#include <pthread.h>
#include <strings.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <ovis_json/ovis_json.h>
#include <assert.h>
#include <sched.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_stream.h"
#include "../sampler_base.h"
#include "dynamic_query_defs.h"

#define TURNAROUND 1
#define SAMP "dynamic_stream_sampler"
#define DYN_DEFAULT_XPRT "sock"
#define DYN_DEFAULT_AUTH "munge"

#define LDMSD_CONTROLLER_FMT "ldmsd_controller -h %s -p %s -x %s -a %s"
#define PRDCR_ADD_INTERVAL 20000000
#define PRDCR_ADD_FMT "prdcr_add host=%s xprt=%s port=%s interval=%d type=active name=%s"
#define PRDCR_SUBSCRIBE_FMT "prdcr_subscribe regex=^%s$ stream=%s"
#define PRDCR_START_FMT "prdcr_start name=%s"
#define PRDCR_UNSUBSCRIBE_FMT "prdcr_unsubscribe regex=^%s$ stream=%s"
#define PRDCR_STOP_FMT "prdcr_stop name=%s"
#define PRDCR_DEL_FMT "prdcr_del name=%s"

//this is the stream that I listen on and has to be unique. using
//it as a UUID for identifying myself, endpoints of dynamic streams, etc.
static char *stream = NULL;
static char *myUUID = NULL;
static struct ldmsd_plugin *myself;
static ldmsd_msg_log_f msglog;
static base_data_t base;
//TODO: not keeping track of the dynamic streams, but will need to if
//I need to clean them up in resilience scenarios. ALSO will need
//if I need to track the endpoints of dynamic streams


static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP " stream=<stream>\n" \
                BASE_CONFIG_USAGE \
		"     stream        Stream name to which the"\
                " dynamic_stream_sampler will subscribe. Defaults to " \
                CMD_STREAM_BASE \
                "\n";
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return NULL;
}

static int sample(struct ldmsd_sampler *self)
{
	return 0;
}


void printHostInfo(ldmsd_msg_log_f msglog, struct HostInfo* hi,
                   const char* str)
{
        msglog(LDMSD_LDEBUG, SAMP " '%s' HostInfo: host '%s' port '%s' "
               "xprt '%s' auth '%s' stream '%s' end %d\n",
               str, hi->host, hi->port, hi->xprt, hi->auth,
               hi->stream, hi->end);

        return;
}


void initHostInfo(struct HostInfo* hi)
{
        hi->host = NULL;
        hi->port = NULL;
        hi->xprt = NULL;
        hi->auth = NULL;
        hi->stream = NULL;
        hi->end = 0;
}


void freeHostInfo(struct HostInfo* hi)
{
        if (hi->host) free(hi->host);
        if (hi->port) free(hi->port);
        if (hi->xprt) free(hi->xprt);
        if (hi->auth) free(hi->auth);
        if (hi->stream) free(hi->stream);
        initHostInfo(hi);
}


int validHostInfo(struct HostInfo* hi)
{
        if (!hi->host || (strlen(hi->host) == 0) ||
            !hi->port || (strlen(hi->port) == 0) ||
            !hi->stream || (strlen(hi->stream) == 0) ||
            !hi->xprt || (strlen(hi->xprt) == 0) ||
            !hi->auth || (strlen(hi->auth) == 0)){
                return 0;
        } else {
                return 1;
        }
}


static int propogate_feedback(int cmdidx, const char* msg, int msg_len,
                              struct HostInfo* myhi, struct HostInfo* uphi)
{
        ldms_t ldms = NULL;
        int rc = 0;

        // For SETUP_FEEDBACK, QUERY_DB, and TEARDOWN_FEEDBACK need all upstreaminfo:
        // upstreamcmdstream name, upstreamhost, upstream auth, upstream xprt
        // these are not needed for the last one in the line

        if (myhi->end){
                msglog(LDMSD_LDEBUG, SAMP
                       " Nothing to propogate - that can be ok. Returning.\n");
                return 0;
        }

        //set up the connectionn
        msglog(LDMSD_LDEBUG, SAMP " setting up the connection to be able to publish\n");
        ldms = ldms_xprt_new_with_auth(uphi->xprt, NULL, uphi->auth, NULL);
        if (!ldms) {
                rc = errno;
                msglog(LDMSD_LERROR, SAMP
                       " Failed to create the LDMS transport endpoint\n");
                goto out;
        }
        rc = ldms_xprt_connect_by_name(ldms, uphi->host, uphi->port, NULL, NULL);
        if (rc) {
                msglog(LDMSD_LERROR, SAMP " Error %d connecting to peer\n", rc);
                goto out;
        }

        // if SETUP_FEEDBACK, tell dest on cmd to listen to the new stream.
        // if QUERY_FEEDBACK, tell dest on cmd the message
        // if TEARDOWN_FEEDBACK, tell dest on cmd to teardown the new stream
        // info.
        msglog(LDMSD_LDEBUG, SAMP " about to publish\n");
        rc = ldmsd_stream_publish(ldms, uphi->stream, LDMSD_STREAM_JSON,
                                  msg, msg_len);
        if (rc){
                msglog(LDMSD_LERROR, SAMP " Error %d publishing to '%s'\n",
                       rc, uphi->stream);
                goto out;

        }

        msglog(LDMSD_LDEBUG, SAMP " After publishing '%s'\n", msg);

        goto out;

 out_1:
        msglog(LDMSD_LERROR, SAMP " Cannot build '%s' message\n",
               DSCommands[cmdidx]);
        rc = -1;
        goto out;

 out:

        if (ldms)
                ldms_xprt_close(ldms);
        ldms = NULL;

        return rc;
}

static int printJSONattrs(json_entity_t e)
{
        json_entity_t di;

        for (di = json_attr_first(e); di; di = json_attr_next(di)){
                msglog(LDMSD_LDEBUG, "attr='%s'\n",
                       di->value.attr_->name->value.str_->str);
        }


        return 0;
}

static int turnaround(char* dest, const char* port,
                      const char* xprt, const char* auth,
                      const char* dyn_stream)
{

        ldms_t ldms = NULL;
        jbuf_t jb;
        int rc = 0;

        //set up the connection
        ldms = ldms_xprt_new_with_auth(xprt, NULL, auth, NULL);
        if (!ldms) {
                rc = errno;
                msglog(LDMSD_LERROR,
                       SAMP " Failed to create the LDMS transport endpoint\n");
                goto out;
        }
        rc = ldms_xprt_connect_by_name(ldms, dest, port, NULL, NULL);
        if (rc) {
                msglog(LDMSD_LERROR, SAMP " Error %d connecting to peer\n", rc);
                goto out;
        }

        //sending to the prev hardwired guy (in args).
        //This is not a message that is intended to be processed.
        jb = jbuf_new();
        if (!jb) goto out;
        jb = jbuf_append_str(jb, "{");
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, CMD_KEY, "\"%s\",", "foo");
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, STREAM_KEY, "\"%s\",", dyn_stream);
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, PRDCRNAME_KEY, "\"%s\",", "zed");
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, LIST_KEY, "\"%s\"", "wugga");
        if (!jb) goto out;
        jb = jbuf_append_str(jb, "}}"); if (!jb) goto out;

        rc = ldmsd_stream_publish(ldms, dyn_stream, LDMSD_STREAM_JSON,
                                  jb->buf, jb->cursor+1);
        if (rc){
                msglog(LDMSD_LERROR,
                       SAMP " Error %d publishing to '%s'\n", rc, dyn_stream);
                goto out;
        }

        msglog(LDMSD_LDEBUG, SAMP " After publishing turnaround '%s'\n", jb->buf);
        goto out;


 out:
        if (ldms)
                ldms_xprt_close(ldms);
        ldms = NULL;
        if (jb)
                jbuf_free(jb);
        jb = NULL;

        return rc;

}

static int dynamic_message_handler(const char* stream,
                                   const char* msg, size_t msg_len)
{

        //parse the message to see if it is a response and if I am
        //the responder. If so, I have to write it out to the querier

        json_parser_t jp = NULL;
        json_entity_t jdoc = NULL;
        json_entity_t ent = NULL;
        char *buff = NULL;
        char *luuid = NULL;
        char *lresp = NULL;
        char cmd[1024];
        int resp_int = 0;
        int rc = 0;


        jp = json_parser_new(0);
        if (!jp){
                rc = errno;
                msglog(LDMSD_LERROR, SAMP " read() error: %d\n", errno);
                goto out;
        }
        buff = strdup(msg);
        if (!buff){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto out;
        }
        rc = json_parse_buffer(jp, buff, msg_len, &jdoc);
        if (rc) {
                msglog(LDMSD_LERROR, SAMP " JSON parse failed: %d\n", rc);
                goto out;
        }

        //what is this message about?
        ent = json_value_find(jdoc, CMD_KEY);
        if (!ent){
                msglog(LDMSD_LINFO, SAMP " No " CMD_KEY " in message."
                       " No further actions on this message\n");
                goto out;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR, SAMP " Error: " CMD_KEY " must be a string\n");
                goto out;
        }
        //if CMD is NOT a query response....
        if (strcmp(ent->value.str_->str, QUERY_RESPONSE)){
                msglog(LDMSD_LINFO, SAMP " No actions to handle command '%s'."
                       " This may or may not be ok.\n",
                       ent->value.str_->str);
                goto out;
        }

        //if I am the responder and this was received on the right stream,
        //then print the response to the UUID

        //responder
        ent = json_value_find(jdoc, RESPONDER_KEY);
        if (!ent){
                msglog(LDMSD_LINFO, SAMP " No " RESPONDER_KEY " in message\n");
                rc = -1;
                goto out;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR, SAMP " Error: " RESPONDER_KEY " must be a string\n");
                goto out;
        }
        if (strcmp(ent->value.str_->str, myUUID)){
                msglog(LDMSD_LINFO, SAMP "I '%s' am not the responder\n", myUUID);
                goto out;
        }

        //stream
        ent = json_value_find(jdoc, RESPONSE_STREAM_KEY);
        if (!ent){
                msglog(LDMSD_LINFO, " No " RESPONSE_STREAM_KEY " in message\n");
                rc = -1;
                goto out;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR, " Error: " RESPONSE_STREAM_KEY " must be a string\n");
                goto out;
        }

        if (strcmp(ent->value.str_->str, stream)){
                msglog(LDMSD_LINFO, SAMP "I '%s' am not the responder\n", myUUID);
                goto out;
        }

        //I am the responder

        //uuid
        ent = json_value_find(jdoc, UUID_KEY);
        if (!ent){
                msglog(LDMSD_LERROR, SAMP " No " UUID_KEY " in message\n");
                rc = -1;
                goto out;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR, SAMP " Error: " UUID_KEY " must be a string\n");
                goto out;
        }

        luuid = strdup(ent->value.str_->str);
        if (!luuid){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto out;
        }

        //response
        ent = json_value_find(jdoc, RESPONSE_KEY);
        if (!ent){
                msglog(LDMSD_LINFO, SAMP " No " RESPONSE_KEY " in message\n");
                rc = -1;
                goto out;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR, SAMP " Error: " RESPONSE_KEY " must be a string\n");
                goto out;
        }

        lresp = strdup(ent->value.str_->str);
        if (!lresp){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto out;
        }

         msglog(LDMSD_LINFO, " I am the responder and SHOULD BE PRINTING '%s' to '%s'\n",
               lresp, luuid);

        snprintf(cmd, sizeof(cmd), "echo \"%s\" >> %s\n",
                 lresp, luuid);
        system(cmd);

        msglog(LDMSD_LINFO, " After printing to '%s'\n", luuid);


 out:

        if (luuid) free(luuid);
        if (lresp) free(lresp);
        if (buff) free(buff);
        if (jp) json_parser_free(jp);
        if (jdoc) json_entity_free(jdoc);
        rc = 0;

        return rc;

}


static int dynamic_stream_recv_cb(ldmsd_stream_client_t c, void *ctxt,
                                  ldmsd_stream_type_t stream_type,
                                  const char *msg, size_t msg_len,
                                  json_entity_t entity)
{
	int rc = 0;

        // Placeholder function for receiving data on the feedback channel.
        // This may end up being removed.

        switch (stream_type) {
        case LDMSD_STREAM_JSON:
                msglog(LDMSD_LDEBUG,
                       "dynamic stream: '%s', stream_type: %s, msg: \"%s\","
                       " msg_len: %d, entity: %p\n",
                       ldmsd_stream_client_name(c), "JSON",
                       msg, msg_len, entity);

                //this is a TEST
                if (0){
                        printJSONattrs(entity);
                }

                rc = dynamic_message_handler(ldmsd_stream_client_name(c),
                                             msg, msg_len);

                goto out;
                break;
	case LDMSD_STREAM_STRING:
                msglog(LDMSD_LDEBUG,
                       "dynamic stream: '%s', stream_type: %s, msg: \"%s\", "
                       " msg_len: %d, entity: %p\n",
                       ldmsd_stream_client_name(c), "STRING",
                       msg, msg_len, entity);
                rc = 0;
                goto out;
	break;
        }

 out:

        msglog(LDMSD_LDEBUG, SAMP " completed dynamic_stream_recv_cb returning %d\n", rc);
        return rc;

}

static int parse_feedback_message_for_setup_teardown(const char* msg, int msg_len,
                                                     char** prdcrname_e)
{
        json_parser_t jp = NULL;
        json_entity_t jdoc = NULL;
        json_entity_t ent = NULL;
        char *buff = NULL;
        char *prdcrname = NULL;
        int rc = 0;

        jp = json_parser_new(0);
        if (!jp){
                rc = errno;
                msglog(LDMSD_LERROR, SAMP " read() error: %d\n", errno);
                goto out;
        }
        buff = strdup(msg);
        if (!buff){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto out;
        }
        rc = json_parse_buffer(jp, buff, msg_len, &jdoc);
        if (rc) {
                msglog(LDMSD_LERROR, SAMP " JSON parse failed: %d\n", rc);
                goto out;
        }

        //prdcrname
        ent = json_value_find(jdoc, PRDCRNAME_KEY);
        if (!ent){
                msglog(LDMSD_LERROR, SAMP " No " PRDCRNAME_KEY " in message\n");
                rc = -1;
                goto out;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR,
                       SAMP " Error: " PRDCRNAME_KEY " must be a string\n");
                goto out;
        }
        prdcrname = strdup(ent->value.str_->str);
        if (!prdcrname){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto out;
        }

 out:

        //caller has to free
        *prdcrname_e = prdcrname;

        if (buff) free(buff);
        if (jp) json_parser_free(jp);
        if (jdoc) json_entity_free(jdoc);

        msglog(LDMSD_LDEBUG,
               SAMP " completed parse_feedback_message_for_setup_teardown"
               " returning %d\n", rc);

        return rc;

}

static int parse_feedback_message_for_query(const char* msg, int msg_len,
                                            char** query_e, char** uuid_e,
                                            char** responder_e,
                                            char** querier_e,
                                            char** argstring_e)
{
        json_parser_t jp = NULL;
        json_entity_t jdoc = NULL;
        json_entity_t ent = NULL;
        char *buff = NULL;
        char *query = NULL;
        char *uuid = NULL;
        char *responder = NULL;
        char *querier = NULL;
        char *argstring = NULL;
        int rc = 0;


        jp = json_parser_new(0);
        if (!jp){
                rc = errno;
                msglog(LDMSD_LERROR, SAMP " read() error: %d\n", errno);
                goto out;
        }
        buff = strdup(msg);
        if (!buff){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto out;
        }
        rc = json_parse_buffer(jp, buff, msg_len, &jdoc);
        if (rc) {
                msglog(LDMSD_LERROR, SAMP " JSON parse failed: %d\n", rc);
                goto out;
        }

        //TODO: See if there is an iterator where I can just pack up all
        //the other fields, since they arent actually used
        ent = json_value_find(jdoc, QUERY_KEY);
        if (!ent){
                msglog(LDMSD_LERROR, SAMP " No " QUERY_KEY " in message\n");
                rc = -1;
                goto argparse;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR,
                       SAMP " Error: " QUERY_KEY " must be a string\n");
                goto argparse;
        }
        query = strdup(ent->value.str_->str);
        if (!query){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto out;
        }

 argparse:
        ent = json_value_find(jdoc, ARG_STR_KEY);
        if (!ent){
                msglog(LDMSD_LERROR, SAMP " No " ARG_STR_KEY " in message\n");
                rc = -1;
                goto responderparse;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR,
                       SAMP " Error: " ARG_STR_KEY " must be a string\n");
                goto responderparse;
        }
        argstring = strdup(ent->value.str_->str);
        if (!argstring){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto out;
        }

 responderparse:
        ent = json_value_find(jdoc, RESPONDER_KEY);
        if (!ent){
                msglog(LDMSD_LERROR, SAMP " No " RESPONDER_KEY " in message\n");
                rc = -1;
                goto querierparse;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR,
                       SAMP " Error: " RESPONDER_KEY " must be a string\n");
                goto querierparse;
        }
        responder = strdup(ent->value.str_->str);
        if (!responder){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto out;
        }

 querierparse:
        ent = json_value_find(jdoc, QUERIER_KEY);
        if (!ent){
                msglog(LDMSD_LERROR, SAMP " No " QUERIER_KEY " in message\n");
                rc = -1;
                goto uuidparse;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR,
                       SAMP " Error: " QUERIER_KEY " must be a string\n");
                goto uuidparse;
        }
        querier = strdup(ent->value.str_->str);
        if (!querier){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto out;
        }

 uuidparse:
        ent = json_value_find(jdoc, UUID_KEY);
        if (!ent){
                msglog(LDMSD_LERROR, SAMP " No " UUID_KEY " in message\n");
                rc = -1;
                goto out;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR,
                       SAMP " Error: " UUID_KEY " must be a string\n");
                goto out;
        }
        uuid = strdup(ent->value.str_->str);
        if (!uuid){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto out;
        }


 out:
        *query_e = query;
        *argstring_e = argstring;
        *uuid_e = uuid;
        *responder_e = responder;
        *querier_e = querier;

        if (buff) free(buff);
        if (jp) json_parser_free(jp);
        if (jdoc) json_entity_free(jdoc);

        //ignoring bad return, since the args wont be looked at until the query
        //return NULLs for bad fields
        //caller has responsibility to free

        msglog(LDMSD_LDEBUG,
               SAMP " completed parse_feedback_message_for_query"
               " returning %d\n",
               rc);

        return rc;

}

static int parse_string_for_HostInfo(char* str, char* matchUUID,
                                     struct HostInfo* hi,
                                     char** rest)
{

        char* mydata = NULL;
        char* mydatacp = NULL;
        char* outersaveptr = NULL;
        char *saveptr = NULL;
        char *tok = NULL;

        int found = 0;
        int rc = 0;

        msglog(LDMSD_LDEBUG, SAMP " looking for '%s in '%s\n", matchUUID, str);

        mydata = strtok_r(str, ":", &outersaveptr);
        do {
                if (mydata == NULL){
                        break;
                }

                mydatacp = strdup(mydata);
                msglog(LDMSD_LDEBUG,
                       SAMP " checking mydata = '%s' rest = '%s'\n",
                       mydatacp, outersaveptr);

                //split mydata
                tok = strtok_r(mydatacp, "@", &saveptr);
                if (tok){
                        hi->host = strdup(tok);
                        //                        msglog(LDMSD_LDEBUG, SAMP "\t myhost='%s'\n", hi->host);
                        tok = strtok_r(NULL, "@", &saveptr);
                        if (tok){
                                hi->port = strdup(tok);
                                //                                msglog(LDMSD_LDEBUG, SAMP "\t myport='%s'\n", hi->port);
                                tok = strtok_r(NULL, "@", &saveptr);
                                if (tok){
                                        hi->stream = strdup(tok);
                                        //                                        msglog(LDMSD_LDEBUG, SAMP "\t mystream='%s'\n", hi->stream);
                                        tok = strtok_r(NULL, "@", &saveptr);
                                        if (tok){
                                                hi->xprt = strdup(tok);
                                                hi->auth = strdup(saveptr);
                                        } else {
                                                //default
                                                hi->xprt = strdup("sock");
                                                hi->auth = strdup("munge");
                                        }
                                }
                        }
                }

                if (mydatacp) {
                        free(mydatacp);
                        mydatacp = NULL;
                }

                if (!validHostInfo(hi)){
                        msglog(LDMSD_LERROR, SAMP "Bad data for my HostInfo"
                               " too few my fields\n");
                        rc = -1;
                        break;
                }

                //                printHostInfo(msglog, hi, " extracted: ");

                //if I'm given something to match, see if it matches
                //if not, then just return the first one
                if (matchUUID){
                        if (!strcmp(hi->stream, matchUUID)){
                                found = 1;
                                //                                msglog(LDMSD_LDEBUG, SAMP " found '%s' so breaking\n",
                                //                                       matchUUID);
                                break;
                        }
                } else {
                        found = 1;
                        //                        msglog(LDMSD_LDEBUG, SAMP " looking for first item, so breaking\n",
                        //                                       matchUUID);
                        break;
                }

                freeHostInfo(hi);

                mydata = strtok_r(NULL, ":", &outersaveptr);
        } while (mydata);

        //        msglog(LDMSD_LDEBUG, SAMP "broken: mydata = '%s' rest = '%s'\n", mydata,
        //outersaveptr);

        if (!found){
                msglog(LDMSD_LDEBUG, SAMP " I '%s' am not in the list - bad\n",
                       myUUID);
                rc = -1;
                goto out;
        } else {
                msglog(LDMSD_LDEBUG, SAMP " I '%s' am in the list - good\n",
                       myUUID);
        }

        if (!outersaveptr || !strlen(outersaveptr)){
                msglog(LDMSD_LDEBUG, SAMP " no upstream data -- "
                       "I'm the end of the line and that is ok.\n");
                hi->end = 1;
                *rest = NULL;
        } else {
                msglog(LDMSD_LDEBUG, SAMP " I am not the end\n");
                hi->end = 0;
                *rest = strdup(outersaveptr);
        }

 out:
        if (mydatacp) free(mydatacp);

        if (rc)
                freeHostInfo(hi);

        printf("Returing from parse_string_for_Host_Info\n");
        return rc;

}


static int parse_feedback_message_for_HostInfos(const char* msg, int msg_len,
                                                char** dynstream_e,
                                                struct HostInfo* myhi,
                                                struct HostInfo* uphi)
{

        char *buff = NULL;
        char *mydata = NULL;
        char *junk = NULL;
        char *dynstream = NULL;
        char *dynlist = NULL;

        json_parser_t jp = NULL;
        json_entity_t jdoc = NULL;
        json_entity_t ent = NULL;

        int rc = 0;

        // parse the common data commands (note that SETUP and TEARDOWN need the same)
        // parsing will catch if this is json
        // NOTE: that there are corner cases that will still slip through...
        jp = json_parser_new(0);
        if (!jp){
                rc = errno;
                msglog(LDMSD_LERROR, SAMP " read() error: %d\n", errno);
                goto bad;
        }
        buff = strdup(msg);
        if (!buff){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto bad;
        }
        rc = json_parse_buffer(jp, buff, msg_len, &jdoc);
        if (rc) {
                msglog(LDMSD_LERROR, SAMP " JSON parse failed: %d\n", rc);
                goto bad;
        }

        // dynamic stream name
        ent = json_value_find(jdoc, STREAM_KEY);
        if (!ent){
                msglog(LDMSD_LERROR, SAMP " No " STREAM_KEY " in message\n");
                rc = -1;
                goto bad_params;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR,
                       SAMP " Error: " STREAM_KEY " must be a string\n");
                goto bad_params;
        }
        dynstream = strdup(ent->value.str_->str);
        if (!dynstream){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto bad;
        }

        //list
        ent = json_value_find(jdoc, LIST_KEY);
        if (!ent){
                msglog(LDMSD_LERROR, SAMP " No " LIST_KEY " in message\n");
                rc = -1;
                goto bad_params;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR,
                       SAMP " Error: " LIST_KEY " must be a string\n");
                goto bad_params;
        }
        dynlist = strdup(ent->value.str_->str);
        if (!dynlist){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto bad;
        }

        //Extract the info from the list but don't change it.
        //That way we don't have to rebuild the message each time.


        //look for me
        rc = parse_string_for_HostInfo(dynlist, myUUID, myhi, &mydata);
        if (rc) {
                msglog(LDMSD_LINFO,
                       SAMP "Invalid HostInfo or not in list. Both bad.\n");
                goto bad;
        }
        printHostInfo(msglog, myhi, "My host: ");
        if (!myhi->end){
                //look for upstream
                rc = parse_string_for_HostInfo(mydata, NULL, uphi, &junk);
                if (rc){
                        msglog(LDMSD_LERROR, SAMP "Invalid upHostInfo or "
                               " not in list. Both bad.'%d'\n",
                               rc);
                        goto bad;
                }
        }
        printHostInfo(msglog, uphi, "Up host: ");
        *dynstream_e = dynstream;

 bad:
        //if get here, some form of bad parameters to act on. rc will be set
 bad_params:
        //if get here, some form of bad parsing. rc will get set

        if (dynlist) free(dynlist);
        if (buff) free(buff);
        if (mydata) free(mydata);
        if (junk) free(junk);

        if (jp) json_parser_free(jp);
        if (jdoc) json_entity_free(jdoc);

        msglog(LDMSD_LDEBUG,
               SAMP " completed parse_feedback_message_for_HostInfos returning %d\n", rc);

        //it will be the callers responsibility to free the arguments if they are good
        return rc;

}


static int call_ldmsd_controller(int cmdidx, const char* dynstream,
                                 const char* prdcrname,
                                 struct HostInfo* myhi,
                                 struct HostInfo* uphi)
{
        int rc = 0;

        //FIXME are there return values to system?
        char teststring[MAXBUF];


        if (!uphi || !uphi->host || !uphi->port){
                msglog(LDMSD_LDEBUG, SAMP " No prdcr to add/remove"
                       " and that can be ok. Returning\n");
                return 0;
        }

        msglog(LDMSD_LINFO,
               SAMP " Issuing commands to ldmsd_controller for '%s'\n",
               DSCommands[cmdidx]);


        switch (cmdidx){
        case 0:
                rc = snprintf(teststring, (MAXBUF-1),
                              "echo \"" PRDCR_ADD_FMT "\" | "
                              LDMSD_CONTROLLER_FMT,
                              uphi->host, uphi->xprt, uphi->port,
                              PRDCR_ADD_INTERVAL, prdcrname,
                              myhi->host, myhi->port, myhi->xprt,
                              myhi->auth);
                msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
                //FIXME: is there a time to wait?
                system(teststring);

                rc = snprintf(teststring, (MAXBUF-1),
                              "echo \"" PRDCR_SUBSCRIBE_FMT "\" | "
                              LDMSD_CONTROLLER_FMT,
                              prdcrname, dynstream,
                              myhi->host, myhi->port, myhi->xprt,
                              myhi->auth);
                msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
                //FIXME: is there a time to wait?
                system(teststring);

                rc = snprintf(teststring, (MAXBUF-1),
                              "echo \"" PRDCR_START_FMT "\" | "
                              LDMSD_CONTROLLER_FMT,
                              prdcrname,
                              myhi->host, myhi->port, myhi->xprt,
                              myhi->auth);
                msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
                //FIXME: is there a time to wait?
                system(teststring);

                rc = 0;
                break;
        case 1:
                //TODO: doublecheck order
                rc = snprintf(teststring, (MAXBUF-1),
                              "echo \"" PRDCR_UNSUBSCRIBE_FMT "\" | "
                              LDMSD_CONTROLLER_FMT,
                              prdcrname, dynstream, myhi->host,
                              myhi->port, myhi->xprt, myhi->auth);
                msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
                //FIXME: is there a time to wait?
                system(teststring);

                rc = snprintf(teststring, (MAXBUF-1),
                              "echo \"" PRDCR_STOP_FMT "\" | "
                              LDMSD_CONTROLLER_FMT,
                              prdcrname,
                              myhi->port, myhi->xprt, myhi->auth);
                msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
                //FIXME: is there a time to wait?
                system(teststring);

                //TODO FIXME: This is still not working right.
                if (1){
                        msglog(LDMSD_LCRITICAL, SAMP
                               " Cannot issue prdcr_del within this path, due to locks."
                               " Not executing it and looking into alternate methods.\n");
                } else {

                       FILE* mf;
                       char lbuf[2048];
                       char *s;

                       msglog(LDMSD_LCRITICAL,
                               SAMP " about to issue prdcr_del after sleep"
                               " time to check the controller via command line)\n.");
                       sleep(120);
                       rc = snprintf(teststring, (MAXBUF-1),
                                     "echo \"" PRDCR_DEL_FMT "\" | "
                                     LDMSD_CONTROLLER_FMT,
                                     prdcrname,
                                     myhi->port, myhi->xprt, myhi->auth);
                       msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n",
                              teststring);
                       //FIXME: is there a time to wait?
                       system(teststring);

                       if (0) {
                               mf = popen(teststring, "r");
                               if (!mf){
                                       msglog(LDMSD_LERROR,
                                              SAMP ": Could not execute '%s'\n",
                                              teststring);
                                       rc = ENOENT;
                                       goto out;
                               }

                               do {
                                       s = fgets(lbuf, sizeof(lbuf), mf);
                                       if (!s)
                                               break;
                                       msglog(LDMSD_LDEBUG,
                                              SAMP ": Read '%s'\n", lbuf);
                               } while (s);
                               if (mf)  pclose(mf);
                               mf = NULL;
                       }

                       msglog(LDMSD_LDEBUG,
                              SAMP ": After issuing prdcr_del\n");
                }

                rc = 0;
                break;
        default:
                //wont happen
                break;
        }

 out: //this is a placeholder, while testing pclose

        return rc;
}

static int end_of_the_line(int cmdidx, const char* dynstream)
{

        char lbuf[MAXBUF];
        int rc;

        //FIXME: TEMPORARY HACK
        // 4)  the extreme end, send a test message back down.
        // NOTE: you can also call ldmsd_stream_publish on the
        // next to last L on the dynamic stream

        switch (cmdidx){
        case 0:
        case 1:
                if (TURNAROUND){
                        msglog(LDMSD_LINFO, SAMP " End of the line. Sleeping 20 and"
                               " Testing sending a message send back down\n");
                        system("sleep 20");
                        turnaround("localhost", "52002",
                                   DYN_DEFAULT_XPRT, DYN_DEFAULT_AUTH,
                                   dynstream);
                }
                rc = 1;
                break;
        default:
                //no other cmds do anything
                rc = 0;
                break;
        }

        return rc;
}


static int feedback_handler(int cmdidx, const char* msg, int msg_len)
{
        int rc = 0;
        int holdrc = 0;
        //responsible for freeing all of these:
        char *dynstream = NULL;
        char *prdcrname = NULL;
        struct HostInfo myhi;
        struct HostInfo uphi;
        char *sendon = NULL;
        char *query = NULL;
        char *argstring = NULL;
        char *uuid = NULL;
        char *responder = NULL;
        char *querier = NULL;

        char lbuf[MAXBUF];
        //TODO: do I have to keep and free this?
        ldmsd_stream_client_t client = NULL;

        initHostInfo(&myhi);
        initHostInfo(&uphi);

        // the host and port info will be used for ldmsd controller
        //ACG: can resuse this for query if prdcrname is ok to be null
        rc = parse_feedback_message_for_HostInfos(msg, msg_len, &dynstream,
                                                  &myhi, &uphi);
        if (rc != 0){
                msglog(LDMSD_LDEBUG, SAMP
                       " Error parsing message for sendon. No further actions on '%s'n",
                       DSCommands[cmdidx]);

                goto out;
        }

        switch (cmdidx){
        case 0:
        case 1:
                rc = parse_feedback_message_for_setup_teardown(msg, msg_len,
                                                               &prdcrname);
                if (rc != 0){
                        msglog(LDMSD_LDEBUG, SAMP
                               " Error parsing message for setup_teardown."
                             " No further actions on '%s'\n",
                             DSCommands[cmdidx]);
                        goto out;
                }

                if (myhi.end)
                        end_of_the_line(cmdidx, dynstream);
                if (rc){
                        msglog(LDMSD_LDEBUG, SAMP " I '%s' am the end of the line."
                               " This may be ok. No further actions on '%s'",
                               myUUID, DSCommands[cmdidx]);
                        rc = 0; //because this is actually ok
                        goto out;
                }
                break;
        case 2:
                rc = parse_feedback_message_for_query(msg, msg_len, &query,
                                                      &uuid, &responder,
                                                      &querier,
                                                      &argstring);
                if (rc != 0){
                        msglog(LDMSD_LDEBUG, SAMP
                               " Error parsing message for query."
                               " No further actions on '%s'\n",
                               DSCommands[cmdidx]);
                        goto out;
                }

                //am I the querier? if so, execute the query and stop
                if (!strcmp(querier, myUUID)){
                        // ./dynamic_query_client QUERY_1 foo 52001 dynamicbar "a b c"
                        msglog(LDMSD_LDEBUG,
                               SAMP " I '%s' am the querier '%s' and will query\n",
                               myUUID, querier);
                        if (argstring){
                                rc = snprintf(lbuf, sizeof(lbuf),
                                              "%s %s %s %s %s \"%s\"",
                                              QUERYDB_CLIENT_EXE,
                                              query, uuid, responder, dynstream,
                                              argstring);
                        } else {
                                rc = snprintf(lbuf, sizeof(lbuf),
                                              "%s %s %s %s %s",
                                              QUERYDB_CLIENT_EXE,
                                              query, uuid, responder,
                                              dynstream);
                        }
                        system(lbuf);
                        msglog(LDMSD_LINFO, SAMP " After calling query '%s'.\n",
                               lbuf);
                        rc = 0;
                        goto out;
                } else {
                        msglog(LDMSD_LDEBUG,
                               SAMP " I '%s' am not the querier '%s'"
                               " and will pass the message on\n",
                               myUUID, querier);
                }
                break;
        default:
                //wont happen
                break;
        }

        printHostInfo(msglog, &myhi, " my host\n");
        printHostInfo(msglog, &uphi, " up host\n");

 controller:
        switch (cmdidx){
        case 0:
        case 1:
                // 1) use ldmsd_controller to tell this daemon on myhost myport
                // to subscribe to stream dynstream from upstreamhost.
                // OR if teardown, to tear down
                //ACG -- this will only be for SETUP and TEARDOWN

                rc = call_ldmsd_controller(cmdidx, dynstream, prdcrname,
                                           &myhi, &uphi);
                if (rc != 0){
                        // TODO will not setup a feedback for this, if I cannot
                        // call ldmsd_controller to listen to the dynamic stream
                        // but will still try to pass the message on to the next
                        // one -- does this make sense?
                        msglog(LDMSD_LERROR, SAMP " Error calling ldmsd "
                               " controller. No cleanup yet. Will still try to "
                               "propogate\n");
                        holdrc = rc;
                        goto prop;
                }

                // 2) As a test, set up a callback for when receive a message
                // on the dynamic stream.
                // FIXME TODO This may end up being removed at some point
                // NOTE: the last ldmsd doesn't subscribe nor does he have a
                // callback, BUT someone can send to him and he will pass it
                // on - is this what should happen? TODO CHECK
                if (cmdidx == 0){
                        msglog(LDMSD_LINFO,
                               SAMP " subscribing to stream '%s'\n",
                               dynstream);
                        client = ldmsd_stream_subscribe(dynstream,
                                                        dynamic_stream_recv_cb,
                                                        myself);
                        if (!client){
                                msglog(LDMSD_LERROR,
                                       SAMP " cannot subscribe to stream '%s'"
                                       " (might be duplicate, so continuing)\n",
                                       dynstream);
                        } else {
                                msglog(LDMSD_LINFO,
                                       SAMP " subscribed to stream '%s'\n",
                                       dynstream);
                        }
                } else {
                        msglog(LDMSD_LCRITICAL, SAMP
                               " will be closing stream '%s' BUT"
                               " its not written yet\n",
                               dynstream);
                }

                break;
        default:
                //do nothing
                break;
        }


 prop:

        // 3) have stripped off my daemon and send the message to upstream
        // so that it can do the same up the stream
        // ACG -- this will need to be for all, but the message will have
        //different params for FEEDBACK as opposed to QUERY
        rc = propogate_feedback(cmdidx, msg, msg_len, &myhi, &uphi);
        if (rc)
                msglog(LDMSD_LERROR, SAMP
                       " cannot propogate feedback w/Error case \n");

        msglog(LDMSD_LERROR, SAMP " after propogate_feedback\n");

        if (holdrc){
                rc = holdrc;
                msglog(LDMSD_LERROR, SAMP
                       "re-establishing error code to %d before returning\n",
                       rc);
        }


 out:

        if (dynstream) free(dynstream);
        if (prdcrname) free(prdcrname);
        freeHostInfo(&myhi);
        freeHostInfo(&uphi);
        if (query) free(query);
        if (argstring) free(argstring);
        if (uuid) free(uuid);
        if (responder) free(responder);
        if (querier) free(querier);

        msglog(LDMSD_LDEBUG,
               SAMP " completed feedback_handler returning %d\n", rc);

        return rc;
}


static int cmd_recv_cb(ldmsd_stream_client_t c, void *ctxt,
			 ldmsd_stream_type_t stream_type,
			 const char *msg, size_t msg_len,
			 json_entity_t entity)
{

        char *cmd = NULL;
        char * buff;
	const char *type = "UNKNOWN";
        json_parser_t jp = NULL;
        json_entity_t jdoc = NULL;
        json_entity_t ent = NULL;
        int found;
        int len;
        int i;
        int rc = 0;

	switch (stream_type) {
	case LDMSD_STREAM_JSON:
		type = "JSON";
                /* Accepting messages in the form (note keywords are DEFINED)
                   {"cmd"="SETUP_FEEDBACK", "stream"="foo_fb", "prdcrname"="zed"
                     "list"="L1@52001:L2@52002@cmd2:L3@52003@cmd3..."
                   this will:
                   1) use ldmsd_controller to tell this Aggregator to subscribe
                   to stream foo_fb from L1
                   2) set up a callback for what to do when I receive a message
                   on foo_fb (which I will get from upstream).
                   This may end up being removed at some points.
                   3) strip off L1 and send the message to L2 on cmd2
                   so that it can do the same up the stream
                   (have to have separate name due to blocking)
                   4) at the extreme end, send a test message back down.

                   There is a similar "TEARDOWN_FEEDBACK" which closes the
                   stream and calls the controller to unsubscribe.

                   There is also a QUERY_DB which accepts messages in the form
                   {"cmd"="QUERY_DB", "stream"="foo_fb",
                     "list"="L1@52001:L2@52002@cmd2:L3@52003@cmd3...",
                     "query"="QUERY_1", "argstr_key"="foo bar"
                     this also passes things up the cmd channel, morphing the
                     list as it goes, but only at the end does it call the
                     dynamic client on the service....
                */
                msglog(LDMSD_LDEBUG,
                       "stream: '%s', stream_type: %s, msg: \"%s\","
                       " msg_len: %d, entity: %p\n",
                       ldmsd_stream_client_name(c), type, msg, msg_len, entity);

                jp = json_parser_new(0);
                if (!jp){
                        rc = errno;
                        msglog(LDMSD_LERROR, SAMP " read() error: %d\n", errno);
                        goto out;
                }
                buff = strdup(msg);
                if (!buff){
                        rc = ENOMEM;
                        msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                        goto out;
                }
                rc = json_parse_buffer(jp, buff, msg_len, &jdoc);
                if (rc) {
                        msglog(LDMSD_LERROR,
                               SAMP " JSON parse failed: %d\n", rc);
                        goto out;
                }
                ent = json_value_find(jdoc, CMD_KEY);
                if (!ent){
                        msglog(LDMSD_LERROR,
                               SAMP "Error: missing cmd in msg '%s'\n", buff);
                        rc = -1;
                        goto out;
                }
                if (ent->type != JSON_STRING_VALUE){
                        rc = EINVAL;
                        msglog(LDMSD_LERROR, SAMP " Error: "
                               CMD_KEY " must be a string\n");
                        goto out;
                }
                cmd = strdup(ent->value.str_->str);
                if (!cmd){
                        rc = ENOMEM;
                        msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                        goto out;
                }
                len = strlen(cmd);
                if (!len){
                        msglog(LDMSD_LERROR,
                               SAMP " Error: invalid cmd '%s'\n", cmd);
                        rc = -1;
                        goto out;
                }
                //Get rid of trailing whitespace and newlines
                while (len &&
                       (isspace(cmd[len-1]) || cmd[len-1] == '\n')) {
                        len--;
                }

                if (!len){
                        msglog(LDMSD_LERROR,
                               SAMP " Error: empty cmd!\n");
                        rc = -1;
                        goto out;
                }
                cmd[len] = '\0';

                found = 0;
                for (i = 0; i < NUM_SQUERIES; i++){
                        if (!strcmp(cmd, DSCommands[i])){
                                found = 1;
                                rc = feedback_handler(i, msg, msg_len);
                                if (rc != 0)
                                        msglog(LDMSD_LERROR, SAMP
                                               " '%s' error=%d\n",
                                               DSCommands[i], rc);
                                break;
                        }
                }
                if (!found){
                        msglog(LDMSD_LERROR,
                               SAMP " Error: invalid cmd '%s'\n", cmd);
                        rc = -1;
                        goto out;
                }

                break;
        case LDMSD_STREAM_STRING:
                type = "STRING";
                msglog(LDMSD_LDEBUG, SAMP
                       "stream: '%s', stream_type: %s,"
                       " msg: \"%s\", msg_len: %d, entity: %p\n",
                      ldmsd_stream_client_name(c), type, msg, msg_len, entity);
                break;
        default:
                type = "UNKNOWN";
                msglog(LDMSD_LDEBUG, SAMP
                       "stream: '%s', stream_type: %s,"
                       " msg: \"%s\", msg_len: %d, entity: %p\n",
                       ldmsd_stream_client_name(c), type, msg, msg_len, entity);
                break;

	}

 out:
        if (jp) json_parser_free(jp);
        if (jdoc) json_entity_free(jdoc);
        if (buff) free(buff);

        msglog(LDMSD_LDEBUG, SAMP " completed cmd_recv_cb returning %d\n", rc);

	return rc;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl,
		  struct attr_value_list *avl)
{
	char *value;
	int rc = 0;

	value = av_value(avl, "stream");
	if (value) {
		stream = strdup(value); //should be cmd_streamPORTNO
                myUUID = strdup(stream);
        } else {
                msglog(LDMSD_LERROR, SAMP " must have a stream to which it is "
                       "listening and that must be unique\n");
        }

        myself = self;
        msglog(LDMSD_LINFO, SAMP " subscribing to stream '%s'\n", stream);
        //TODO: do I have to keep and free this?
	ldmsd_stream_client_t client =
                ldmsd_stream_subscribe(stream, cmd_recv_cb, self);
        if (!client){
                msglog(LDMSD_LERROR,
                       SAMP " cannot subscribe to stream '%s'\n", stream);
                rc = -1;
        }

	return rc;
}

static void term(struct ldmsd_plugin *self)
{
        if (myUUID) free(myUUID);
        myUUID = NULL;
        if (stream) free(stream);
        stream = NULL;
        myself = NULL;
}

static struct ldmsd_sampler dynamic_stream_sampler = {
	.base = {
                 .name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &dynamic_stream_sampler.base;
}

static void __attribute__ ((constructor)) dynamic_stream_sampler_init(void)
{
}

static void __attribute__ ((destructor)) dynamic_stream_sampler_term(void)
{
}
