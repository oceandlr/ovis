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

static char *stream;
static struct ldmsd_plugin *myself;
static ldmsd_msg_log_f msglog;
static base_data_t base;


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

static int propogate_feedback(int cmdidx,
                              const char* dest, const char* port,
                              const char* xprt, const char* auth,
                              const char* upstreamcmdstream,
                              const char* dyn_stream, const char* prdcrname,
                              const char* list,
                              const char* query, const char* uuid,
                              const char* argstring){

        jbuf_t jb;
        ldms_t ldms = NULL;
        int rc = 0;

        // For SETUP_FEEDBACK, QUERY_DB, and TEARDOWN_FEEDBACK need all upstreaminfo:
        // upstreamcmdstream name, upstreamhost, upstream auth, upstream xprt
        // these are not needed for the last one in the line

        if (!list){
                msglog(LDMSD_LDEBUG, SAMP
                       " Nothing to propogate - that can be ok. Returning.\n");
                return 0;
        }

        //build the message. same for either cmd case
        //(will have been checked before get to here)
        jb = jbuf_new();
        if (!jb) goto out_1;
        jb = jbuf_append_str(jb, "{");
        if (!jb) goto out_1;
        jb = jbuf_append_attr(jb, CMD_KEY, "\"%s\",", squeries[cmdidx].cmd);
        if (!jb) goto out_1;
        jb = jbuf_append_attr(jb, DYNSTREAM_KEY, "\"%s\",", dyn_stream);
        if (!jb) goto out_1;
        if (prdcrname){
                jb = jbuf_append_attr(jb, PRDCRNAME_KEY, "\"%s\",", prdcrname);
                if (!jb) goto out_1;
        }
        if (query){
                jb = jbuf_append_attr(jb, QUERY_KEY, "\"%s\",", query);
                if (!jb) goto out_1;
        }
        if (uuid){
                jb = jbuf_append_attr(jb, UUID_KEY, "\"%s\",", uuid);
                if (!jb) goto out_1;
        }
        if (argstring){
                jb = jbuf_append_attr(jb, ARG_STR_KEY, "\"%s\",", argstring);
                if (!jb) goto out_1;
        }
        jb = jbuf_append_attr(jb, LIST_KEY, "\"%s\"", list);
        if (!jb) goto out_1;
        jb = jbuf_append_str(jb, "}}"); if (!jb) goto out_1;

        //set up the connectionn
        ldms = ldms_xprt_new_with_auth(xprt, NULL, auth, NULL);
        if (!ldms) {
                rc = errno;
                msglog(LDMSD_LERROR, SAMP
                       " Failed to create the LDMS transport endpoint\n");
                goto out;
        }
        rc = ldms_xprt_connect_by_name(ldms, dest, port, NULL, NULL);
        if (rc) {
                msglog(LDMSD_LERROR, SAMP " Error %d connecting to peer\n", rc);
                goto out;
        }

        // if SETUP_FEEDBACK, tell dest on cmd to listen to the new stream.
        // if QUERY_FEEDBACK, tell dest on cmd the message
        // if TEARDOWN_FEEDBACK, tell dest on cmd to teardown the new stream
        // info.
        rc = ldmsd_stream_publish(ldms, upstreamcmdstream, LDMSD_STREAM_JSON,
                                  jb->buf, jb->cursor+1);
        if (rc){
                msglog(LDMSD_LERROR, SAMP " Error %d publishing to '%s'\n",
                       rc, upstreamcmdstream);
                goto out;

        }

        msglog(LDMSD_LDEBUG, SAMP " After publishing '%s'\n", jb->buf);

        goto out;

 out_1:
        msglog(LDMSD_LERROR, SAMP " Cannot build '%s' message\n",
               squeries[cmdidx].cmd);
        rc = -1;
        goto out;

 out:
        if (jb) jbuf_free(jb);

        return rc;
}


static int turnaround(char* dest, const char* port,
                      const char* xprt, const char* auth,
                      const char* dyn_stream)
{

        char* teststr = "This is a test return";
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

        //sending to the prev hardwired guy
        jb = jbuf_new();
        if (!jb) goto out;
        jb = jbuf_append_str(jb, "{");
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, CMD_KEY, "\"%s\",", "foo");
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, DYNSTREAM_KEY, "\"%s\",", "bar");
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, PRDCRNAME_KEY, "\"%s\",", "zed");
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, LIST_KEY, "\"%s\"", "wugga");
        if (!jb) goto out;
        jb = jbuf_append_str(jb, "}}"); if (!jb) goto out;
        if (0){
                rc = ldmsd_stream_publish(ldms, dyn_stream, LDMSD_STREAM_STRING,
                                          teststr, strlen(teststr)+1);
        } else {
                rc = ldmsd_stream_publish(ldms, dyn_stream, LDMSD_STREAM_JSON,
                                  jb->buf, jb->cursor+1);
        }
        if (rc){
                msglog(LDMSD_LERROR,
                       SAMP " Error %d publishing to '%s'\n", rc, dyn_stream);
                goto out;
        }

        msglog(LDMSD_LDEBUG, SAMP " After publishing '%s'\n", teststr);
        goto out;

 out:
        //TODO: do I need to close any ldms thing here
        if (jb) jbuf_free(jb);
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
                rc = 0;
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
                                            char** argstring_e)
{
        json_parser_t jp = NULL;
        json_entity_t jdoc = NULL;
        json_entity_t ent = NULL;
        char *buff = NULL;
        char *query = NULL;
        char *uuid = NULL;
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
                goto uuidparse;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR,
                       SAMP " Error: " ARG_STR_KEY " must be a string\n");
                goto uuidparse;
        }
        argstring = strdup(ent->value.str_->str);
        if (!argstring){
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

        if (buff) free(buff);
        if (jp) json_parser_free(jp);
        if (jdoc) json_entity_free(jdoc);

        //ignoring bad return, since the args wont be looked at until the query
        //return NULLs for bad fields
        //caller has responsibility to free

        msglog(LDMSD_LDEBUG,
               SAMP " completed parse_feedback_message_for_query returning %d\n",
               rc);

        return rc;

}

static int parse_feedback_message_for_sendon(const char* msg, int msg_len,
                                  char** dynstream_e,
                                  char** myhost_e, char** myport_e,
                                  char** myxprt_e, char** myauth_e,
                                  char** upstreamhost_e, char** upstreamport_e,
                                  char** upstreamxprt_e, char** upstreamauth_e,
                                  char** upstreamcmdstream_e,
                                  char** sendon_e)
{

        char *buff = NULL;
        char *temp = NULL;

        char *dynstream = NULL;

        char *myhost = NULL;
        char *myport = NULL;
        char *myxprt = NULL;
        char *myauth = NULL;
        char *mycmdstream = NULL;

        char *upstreamport = NULL;
        char *upstreamhost = NULL;
        char *upstreamxprt = NULL;
        char *upstreamauth = NULL;
        char *upstreamcmdstream = NULL;
        char *sendon = NULL;

        char *dynlist = NULL;
        char *mydata = NULL;
        char *upstreamdata = NULL;
        char *saveptr = NULL;
        char *tok = NULL;

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
        ent = json_value_find(jdoc, DYNSTREAM_KEY);
        if (!ent){
                msglog(LDMSD_LERROR, SAMP " No " DYNSTREAM_KEY " in message\n");
                rc = -1;
                goto bad_params;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR,
                       SAMP " Error: " DYNSTREAM_KEY " must be a string\n");
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

        //parse the list
        mydata = strtok_r(dynlist, ":", &saveptr);
        if (!mydata){
                msglog(LDMSD_LERROR,
                       SAMP " No myhost information in message\n");
                rc = -1;
                goto bad_params;
        }
        if (!saveptr || (strlen(saveptr) == 0)){
                msglog(LDMSD_LDEBUG, SAMP " No upstream info and that is ok\n");
                sendon = NULL;
                rc = 0;
                goto good_params;
        }
        temp = strdup(saveptr);
        if (!temp){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto bad;
        }

        //split mydata
        tok = strtok_r(mydata, "@", &saveptr);
        if (tok != NULL){
                myhost = strdup(tok);
                tok = strtok_r(NULL, "@", &saveptr);
                if (tok != NULL){
                        myport = strdup(tok);
                        tok = strtok_r(NULL, "@", &saveptr);
                        if (tok != NULL){
                                mycmdstream = strdup(tok);
                                tok = strtok_r(NULL, "@", &saveptr);
                                if (tok != NULL){
                                        myxprt = strdup(tok);
                                        myauth = strdup(saveptr);
                                } else {
                                        myxprt = strdup(DYN_DEFAULT_XPRT);
                                        myauth = strdup(DYN_DEFAULT_AUTH);
                                }
                        }
                }
        }

        if (!myhost || (strlen(myhost) == 0) ||
            !myport || (strlen(myport) == 0) ||
            !mycmdstream || (strlen(mycmdstream) == 0)){
                msglog(LDMSD_LERROR, SAMP " Error: Bad msg params"
                       " myhost = '%s' myport = '%s' mycmdstream = '%s'\n",
                       myhost, myport, mycmdstream);
                rc = -1;
                goto bad_params;
        }

        sendon = strdup(temp);
        if (!sendon){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto bad;
        }

        //split upstreamdata. It might be ok if this doesn't exist
        upstreamdata = strtok_r(temp, ":", &saveptr);
        if (upstreamdata != NULL){
                //split upstreamdata
               tok = strtok_r(upstreamdata, "@", &saveptr);
                if (tok != NULL){
                        upstreamhost = strdup(tok);
                        tok = strtok_r(NULL, "@", &saveptr);
                        if (tok != NULL){
                                upstreamport = strdup(tok);
                                tok = strtok_r(NULL, "@", &saveptr);
                                if (tok != NULL){
                                        upstreamcmdstream = strdup(tok);
                                        tok = strtok_r(NULL, "@", &saveptr);
                                        if (tok != NULL){
                                                upstreamxprt = strdup(tok);
                                                upstreamauth = strdup(saveptr);
                                        } else {
                                                upstreamxprt =
                                                        strdup(DYN_DEFAULT_XPRT);
                                                upstreamauth =
                                                        strdup(DYN_DEFAULT_AUTH);
                                        }
                                }
                        }
                }
        }

        if (!upstreamhost || (strlen(upstreamhost) == 0) ||
            !upstreamport || (strlen(upstreamport) == 0) ||
            !upstreamcmdstream || (strlen(upstreamcmdstream) == 0)){
                msglog(LDMSD_LDEBUG, SAMP
                       " Error: Bad msg params upstreamhost = '%s'"
                       " upstreamport = '%s' upcmd = '%s'\n",
                       upstreamhost, upstreamport, upstreamcmdstream);
                rc = -1;
                goto bad_params;
        } else {
                rc = 0;
                goto good_params;
        }


 good_params:

        *dynstream_e = dynstream;
        *myhost_e = myhost;
        *myport_e = myport;
        *myxprt_e = myxprt;
        *myauth_e = myauth;
        *upstreamhost_e = upstreamhost;
        *upstreamport_e = upstreamport;
        *upstreamxprt_e = upstreamxprt;
        *upstreamauth_e = upstreamauth;
        *upstreamcmdstream_e = upstreamcmdstream;
        *sendon_e = sendon;
        rc = 0;

        goto out;


 bad:
        //if get here, some form of bad parameters to act on. rc will be set
 bad_params:
        //if get here, some form of bad parsing. rc will get set

        //freeing sendon will be a sign of badness along with error code return.
        if (*sendon_e){
                free(*sendon_e);
                *sendon_e = NULL;
        }

 out:
        if (mycmdstream) free(mycmdstream); //don't need this
        mycmdstream = NULL;

        if (buff) free(buff);
        if (temp) free(temp);
        if (dynlist) free(dynlist);

        if (jp) json_parser_free(jp);
        if (jdoc) json_entity_free(jdoc);

        msglog(LDMSD_LDEBUG,
               SAMP " completed parse_feedback_message_for_sendon returning %d\n", rc);

        //it will be the callers responsibility to free the arguments
        return rc;

}


static int call_ldmsd_controller(int cmdidx, const char* dynstream,
                                 const char* prdcrname,
                                 const char* myhost, const char* myport,
                                 const char* myxprt, const char* myauth,
                                 const char* upstreamhost,
                                 const char* upstreamport,
                                 const char* upstreamxprt
                                 )
{
        int rc = 0;

        //FIXME are there return values to system?
        char teststring[MAXBUF];


        if (!upstreamhost || !upstreamport){
                msglog(LDMSD_LDEBUG, SAMP " No prdcr to add/remove"
                       " and that can be ok. Returning\n");
                return 0;
        }

        msglog(LDMSD_LINFO,
               SAMP " Issuing commands to ldmsd_controller for '%s'\n",
               squeries[cmdidx].cmd);


        switch (cmdidx){
        case 0:
                rc = snprintf(teststring, (MAXBUF-1),
                              "echo \"" PRDCR_ADD_FMT "\" | "
                              LDMSD_CONTROLLER_FMT,
                              upstreamhost, upstreamxprt, upstreamport,
                              PRDCR_ADD_INTERVAL, prdcrname,
                              myhost, myport, myxprt, myauth);
                msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
                //FIXME: is there a time to wait?
                system(teststring);

                rc = snprintf(teststring, (MAXBUF-1),
                              "echo \"" PRDCR_SUBSCRIBE_FMT "\" | "
                              LDMSD_CONTROLLER_FMT,
                              prdcrname, dynstream,
                              myhost, myport, myxprt, myauth);
                msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
                //FIXME: is there a time to wait?
                system(teststring);

                rc = snprintf(teststring, (MAXBUF-1),
                              "echo \"" PRDCR_START_FMT "\" | "
                              LDMSD_CONTROLLER_FMT,
                              prdcrname,
                              myhost, myport, myxprt, myauth);
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
                              prdcrname, dynstream,
                              myhost, myport, myxprt, myauth);
                msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
                //FIXME: is there a time to wait?
                system(teststring);

                rc = snprintf(teststring, (MAXBUF-1),
                              "echo \"" PRDCR_STOP_FMT "\" | "
                              LDMSD_CONTROLLER_FMT,
                              prdcrname,
                              myhost, myport, myxprt, myauth);
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
                                     myhost, myport, myxprt, myauth);
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

static int end_of_the_line(int cmdidx, const char* upstreamhost,
                           const char* upstreamport,
                           const char* upstreamcmdstream,
                           const char* sendon,
                           const char* dynstream)
{

        if (upstreamhost || upstreamport || upstreamcmdstream || sendon){
                //not end of the line
                return 0;
        }

        //FIXME: TEMPORARY HACK
        // 4)  the extreme end, send a test message back down.
        // NOTE: you can also call ldmsd_stream_publish on the
        // next to last L on the dynamic stream

        switch (cmdidx){
        case 0:
        case 1:
                if (TURNAROUND){
                        msglog(LDMSD_LINFO, SAMP " End of the line. Sleeping 20 and"
                               " Testing sending a message back down\n");
                        system("sleep 20");
                        turnaround("localhost", "52002",
                                   DYN_DEFAULT_XPRT, DYN_DEFAULT_AUTH,
                                   dynstream);
                }
                break;
        case 2:
                msglog(LDMSD_LINFO,
                       SAMP " Should be querying the DB,"
                       " but it is not written yet\n");
                break;
        default:
                //won't happen
                break;
        }

        return 1;


}

static int feedback_handler(int cmdidx, const char* msg, int msg_len)
{
        int rc = 0;
        int holdrc = 0;
        //responsible for freeing all of these:
        char *dynstream = NULL;
        char *prdcrname = NULL;
        char *myhost = NULL;
        char *myport = NULL;
        char *myxprt = NULL;
        char *myauth = NULL;
        char *upstreamhost = NULL;
        char *upstreamport = NULL;
        char *upstreamxprt = NULL;
        char *upstreamauth = NULL;
        char *upstreamcmdstream = NULL;
        char *sendon = NULL;
        char *query = NULL;
        char *argstring = NULL;
        char *uuid = NULL;

        ldmsd_stream_client_t client = NULL;

        // the host and port info will be used for ldmsd controller
        //ACG: can resuse this for query if prdcrname is ok to be null
        rc = parse_feedback_message_for_sendon(msg, msg_len, &dynstream,
                                    &myhost, &myport, &myxprt, &myauth,
                                    &upstreamhost, &upstreamport,
                                    &upstreamxprt, &upstreamauth,
                                    &upstreamcmdstream, &sendon);
        if (rc != 0){
                msglog(LDMSD_LDEBUG, SAMP
                       " Error parsing message for sendon. No further actions on '%s'n",
                       squeries[cmdidx].cmd);

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
                               squeries[cmdidx].cmd);
                        goto out;

                }
                break;
        case 2:
                rc = parse_feedback_message_for_query(msg, msg_len, &query,
                                                      &uuid, &argstring);
                if (rc != 0){
                        msglog(LDMSD_LDEBUG, SAMP
                               " Error parsing message for query."
                               " No further actions on '%s'\n",
                               squeries[cmdidx].cmd);
                        goto out;
                }
                break;
        default:
                //wont happen
                break;
        }

        rc = end_of_the_line(cmdidx, upstreamhost, upstreamport,
                             upstreamcmdstream, sendon,
                             dynstream);
        if (rc){
                msglog(LDMSD_LDEBUG, SAMP " Nothing to act upon. "
                       " This may be ok. No further actions on '%s'",
                       squeries[cmdidx].cmd);
                rc = 0; //because this is actually ok
                goto out;
        }

        msglog(LDMSD_LINFO,
               SAMP " myhost = '%s' myport = '%s'"
               " myxprt = '%s' myauth = '%s'"
               " dynstream = '%s'"
               " upstream host = '%s' upstream port = '%s'"
               " upstream xprt = '%s' upstream auth = '%s'"
               " upstreampcmd = '%s'"
               " sendon list = '%s'\n",
               myhost, myport, myxprt, myauth,
               dynstream,
               upstreamhost, upstreamport,
               upstreamxprt, upstreamauth,
               upstreamcmdstream, sendon);


        switch (cmdidx){
        case 0:
        case 1:
                // 1) use ldmsd_controller to tell this daemon on myhost myport
                // to subscribe to stream dynstream from upstreamhost.
                // OR if teardown, to tear down
                //ACG -- this will only be for SETUP and TEARDOWN

                rc = call_ldmsd_controller(cmdidx, dynstream, prdcrname,
                                           myhost, myport, myxprt, myauth,
                                           upstreamhost, upstreamport,
                                           upstreamxprt);
                if (rc != 0){
                        // TODO will not setup a feedback for this, if I cannot call
                        // ldmsd_controller to listen to the dynamic stream
                        // but will still try to pass the message on to the next one
                        // -- does this make sense?
                        msglog(LDMSD_LERROR, SAMP " Error calling ldmsd controller."
                               " No cleanup yet. Will still try to propogate\n");
                        holdrc = rc;
                        goto prop;
                }

                // 2) As a test, set up a callback for when receive a message
                // on the dynamic stream.
                // FIXME TODO This may end up being removed at some point
                // NOTE: the last ldmsd doesn't subscribe nor does he have a callback,
                // BUT someone can send to him and he will pass it on -
                // is this what should happen? TODO CHECK
                if (cmdidx == 0){
                        msglog(LDMSD_LINFO, SAMP " subscribing to stream '%s'\n",
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
                                       SAMP " subscribed to stream '%s')\n",
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
        rc = propogate_feedback(cmdidx, upstreamhost, upstreamport,
                                upstreamxprt, upstreamauth,
                                upstreamcmdstream, dynstream,
                                prdcrname, sendon, query, uuid, argstring);
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
        if (myhost) free(myhost);
        if (myport) free(myport);
        if (myxprt) free(myxprt);
        if (myauth) free(myauth);
        if (upstreamhost) free(upstreamhost);
        if (upstreamport) free(upstreamport);
        if (upstreamxprt) free(upstreamxprt);
        if (upstreamauth) free(upstreamauth);
        if (upstreamcmdstream) free(upstreamcmdstream);
        if (sendon) free(sendon);
        if (query) free(query);
        if (argstring) free(argstring);
        if (uuid) free(uuid);

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
                        if (!strcmp(cmd, squeries[i].cmd)){
                                found = 1;
                                rc = feedback_handler(i, msg, msg_len);
                                if (rc != 0)
                                        msglog(LDMSD_LERROR, SAMP
                                               " '%s' error=%d\n",
                                               squeries[i].cmd, rc);
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
	if (value)
		stream = strdup(value); //should be cmd_streamPORTNO
	else
		stream = strdup(CMD_STREAM_BASE);

        myself = self;
        msglog(LDMSD_LCRITICAL, SAMP " subscribing to stream '%s'\n", stream);
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
