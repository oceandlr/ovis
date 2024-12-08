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

static char *stream;
static struct ldmsd_plugin *myself;

#define TURNAROUND 1
#define SAMP "dynamic_stream_sampler"
#define DYN_DEFAULT_XPRT "sock"
#define DYN_DEFAULT_AUTH "munge"
#define SETUP_FEEDBACK "SETUP_FEEDBACK"
#define TEARDOWN_FEEDBACK "TEARDOWN_FEEDBACK"
#define CMD_STREAM_BASE "cmd_stream"
#define CMD_KEY "cmd"
#define PRDCRNAME_KEY "prdcrname"
#define LIST_KEY "list"
#define DYNSTREAM_KEY "stream"
#define BUFLEN 2048
#define BUFLENm1 2047
#define LDMSD_CONTROLLER_FMT "ldmsd_controller -h %s -p %s -x %s -a %s"
#define PRDCR_ADD_INTERVAL 20000000
#define PRDCR_ADD_FMT "prdcr_add host=%s xprt=%s port=%s interval=%d type=active name=%s"
#define PRDCR_SUBSCRIBE_FMT "prdcr_subscribe regex=^%s$ stream=%s"
#define PRDCR_START_FMT "prdcr_start name=%s"
#define PRDCR_UNSUBSCRIBE_FMT "prdcr_unsubscribe regex=^%s$ stream=%s"
#define PRDCR_STOP_FMT "prdcr_stop name=%s"
#define PRDCR_DEL_FMT "prdcr_del name=%s"

//START HERE....next have to add xprt and auth to the parsing

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

static int propogate_feedback(const char* cmd, const char* dest,
                              const char* port, const char* upstreamcmdstream,
                              const char* dyn_stream, const char* prdcrname,
                              char* list){

        char* xprt = DYN_DEFAULT_XPRT;
        char* auth = DYN_DEFAULT_AUTH;
        jbuf_t jb;
        ldms_t ldms = NULL;
        int rc = 0;

        // For either SETUP_FEEDBACK or TEARDOWN_FEEDBACK need all upstreaminfo:
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
        jb = jbuf_append_attr(jb, CMD_KEY, "\"%s\",", cmd);
        if (!jb) goto out_1;
        jb = jbuf_append_attr(jb, DYNSTREAM_KEY, "\"%s\",", dyn_stream);
        if (!jb) goto out_1;
        jb = jbuf_append_attr(jb, PRDCRNAME_KEY, "\"%s\",", prdcrname);
        if (!jb) goto out_1;
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
        msglog(LDMSD_LERROR, SAMP " Cannot build '%s' message\n", cmd);
        rc = -1;
        goto out;

 out:
        if (jb) jbuf_free(jb);

        return rc;
}


static int turnaround(char* dest, const char* port, const char* dyn_stream)
{

        char* xprt = DYN_DEFAULT_XPRT;
        char* auth = DYN_DEFAULT_AUTH;
        char* teststr = "This is a test return";
        ldms_t ldms = NULL;
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

        //tell the dest on cmd to listen to the new stream
        rc = ldmsd_stream_publish(ldms, dyn_stream, LDMSD_STREAM_STRING,
                                  teststr, strlen(teststr)+1);
        if (rc){
                msglog(LDMSD_LERROR,
                       SAMP " Error %d publishing to '%s'\n", rc, dyn_stream);
                goto out;

        }

        msglog(LDMSD_LDEBUG, SAMP " After publishing '%s'\n", teststr);
        goto out;

 out:
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


static int parse_feedback_message(const char* msg, int msg_len,
                                  char** dynstream_e, char** prdcrname_e,
                                  char** myhost_e, char** myport_e,
                                  char** upstreamhost_e, char** upstreamport_e,
                                  char** upstreamcmdstream_e,
                                  char** sendon_e)
{

        char *buff = NULL;
        char *temp = NULL;

        char *dynstream = NULL;
        char *prdcrname = NULL;
        char *myhost = NULL;
        char *myport = NULL;
        char *upstreamport = NULL;
        char *upstreamhost = NULL;
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

        // parse the data for SETUP_FEEDBACK OR TEARDOWN_FEEDBACK (same for bot)
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
        //prdcrname
        ent = json_value_find(jdoc, PRDCRNAME_KEY);
        if (!ent){
                msglog(LDMSD_LERROR, SAMP " No " PRDCRNAME_KEY " in message\n");
                rc = -1;
                goto bad_params;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR,
                       SAMP " Error: " PRDCRNAME_KEY " must be a string\n");
                goto bad_params;
        }
        prdcrname = strdup(ent->value.str_->str);
        if (!prdcrname){
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
                        // i don't cane about my own cmdstream
                        // not checking for too many fields
                }
        }

        if (!myhost || (strlen(myhost) == 0) ||
            !myport || (strlen(myport) == 0)){
                msglog(LDMSD_LERROR, SAMP
                       " Error: Bad msg params myhost = '%s' myport = '%s'\n",
                       myhost, myport);
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
                        // upstreamport is a char
                        upstreamport = strdup(tok);
                        upstreamcmdstream = strdup(saveptr);
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
        *prdcrname_e = prdcrname;
        *myhost_e = myhost;
        *myport_e = myport;
        *upstreamhost_e = upstreamhost;
        *upstreamport_e = upstreamport;
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
        if (buff) free(buff);
        if (temp) free(temp);
        if (dynlist) free(dynlist);

        if (jp) json_parser_free(jp);
        if (jdoc) json_entity_free(jdoc);

        msglog(LDMSD_LINFO,
               SAMP " my host = '%s' myport = '%s' dynstream = '%s'"
               " prdcrname = '%s' "
               "upstream host = '%s' upstream port = '%s' sendon list = '%s'\n",
               *myhost_e, *myport_e, *dynstream_e, *prdcrname_e,
               *upstreamhost_e, *upstreamport_e, *sendon_e);

        msglog(LDMSD_LDEBUG,
               SAMP " completed parse_feedback_message returning %d\n", rc);

        //it will be the callers responsibility to free the arguments
        return rc;

}


static int call_ldmsd_controller(const char* cmd, const char* dynstream,
                                 const char* prdcrname,
                                 const char* myhost, const char* myport,
                                 const char* upstreamhost,
                                 const char* upstreamport)
{

        char* xprt = DYN_DEFAULT_XPRT;
        char* auth = DYN_DEFAULT_AUTH;
        int rc = 0;

        //FIXME are there return values to system?
        char teststring[BUFLEN];

        if (!upstreamhost || !upstreamport){
                msglog(LDMSD_LDEBUG, SAMP " No prdcr to add/remove"
                       " and that can be ok. Returning\n");
                return 0;
        }

        msglog(LDMSD_LINFO,
               SAMP " Issuing commands to ldmsd_controller for '%s'\n", cmd);

        if (!strcmp(cmd, SETUP_FEEDBACK)){
                //SETUP_FEEDBACK needs myhost, myport, myxprt, myauth, but not mystream, and it needs upstream host, xprt, and port
                //PROPOGATE --- FOR EITHER SETUP_FEEDBACK or TEARDOWN_FEEDBACK need all the upstream info
                //MEANS WHEN UNPACKING NEVER NEED MYSTREAM (and FIRST one never needs it) AND NEED EVERYTHING OR NOTHING FOR UPSTREAM
                //FINAL ONE DOESNT NEED AN UPSTREAM OR A MYSTREAM FOR THIS, BUT IT HAS AN UPSTREAMNAME USED IN FOR THE PREVIOUS ONE.
                //MEANS THE FIRST ONE DOESNT NEED MYSTREAM, BUT IT DOES EXIST SINCE IT IS USED FOR THE FIRST CONNECTION
                rc = snprintf(teststring, BUFLENm1,
                              "echo \"" PRDCR_ADD_FMT "\" | "
                              LDMSD_CONTROLLER_FMT,
                              upstreamhost, xprt, upstreamport,
                              PRDCR_ADD_INTERVAL, prdcrname,
                              myhost, myport, xprt, auth);
                msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
                //FIXME: is there a time to wait?
                system(teststring);

                rc = snprintf(teststring, BUFLENm1,
                              "echo \"" PRDCR_SUBSCRIBE_FMT "\" | "
                              LDMSD_CONTROLLER_FMT,
                              prdcrname, dynstream, myhost, myport, xprt, auth);
                msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
                //FIXME: is there a time to wait?
                system(teststring);

                rc = snprintf(teststring, BUFLENm1,
                              "echo \"" PRDCR_START_FMT "\" | "
                              LDMSD_CONTROLLER_FMT,
                              prdcrname, myhost, myport, xprt, auth);
                msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
                //FIXME: is there a time to wait?
                system(teststring);

                rc = 0;

        } else if (!strcmp(cmd, TEARDOWN_FEEDBACK)){
                //TEARDOWN_FEEDBACK needs myhost, myport, myxprt, myauth, but not mystream, and it needs nothing from the upstream
                //PROPOGATE --- FOR EITHER SETUP_FEEDBACK or TEARDOWN_FEEDBACK need all the upstream info
                //MEANS THAT WHEN UNPACKING NEVER NEED MYSTREAM (and FIRST ONE NEVER NEEDS it) AND NEED EVERYTING OR NOTHING FOR UPSTREAM
                //FINAL ONE DOESNT NEED ANYTHING FOR THIS AND NO FIELDS ARE USED HERE, BUT THEY ARE ALL USED IN PROPOGATE
                //MEANS THE FIRST ONE DOESNT NEED MYSTREAM, BUT IT DOES EXIST SINCE IT IS USED FOR THE FIRST CONNECTION

                //TODO: doublecheck order
                rc = snprintf(teststring, BUFLENm1,
                              "echo \"" PRDCR_UNSUBSCRIBE_FMT "\" | "
                              LDMSD_CONTROLLER_FMT,
                              prdcrname, dynstream, myhost, myport, xprt, auth);
                msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
                //FIXME: is there a time to wait?
                system(teststring);

                rc = snprintf(teststring, BUFLENm1,
                              "echo \"" PRDCR_STOP_FMT "\" | "
                              LDMSD_CONTROLLER_FMT,
                              prdcrname, myhost, myport, xprt, auth);
                msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
                //FIXME: is there a time to wait?
                system(teststring);

                msglog(LDMSD_LCRITICAL,
                       SAMP " Need to debug prdcr_del before it can be issued."
                       " Not executing it\n");
                //                rc = snprintf(teststring, BUFLENm1,
                //                "echo \"" PRDCR_DEL_FMT "\" | "
                //                LDMSD_CONTROLLER_FMT,
                //                              prdcrname, myhost, myport, xprt, auth);
                //                msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
                //                //FIXME: is there a time to wait?
                //                system(teststring);

                rc = 0;

        } else {
                msglog(LDMSD_LCRITICAL, SAMP " unknown cmd '%s'\n", cmd);
                rc = -1;
        }

        return rc;
}


static int feedback_handler(const char* cmd, const char* msg, int msg_len)
{
        int rc = 0;
        int holdrc = 0;
        //responsible for freeing all of these:
        char *dynstream = NULL;
        char *prdcrname = NULL;
        char *myhost = NULL;
        char *myport = NULL;
        char *upstreamhost = NULL;
        char *upstreamport = NULL;
        char *upstreamcmdstream = NULL;
        char *sendon = NULL;

        ldmsd_stream_client_t client = NULL;

        // the host and port info will be used for ldmsd controller
        rc = parse_feedback_message(msg, msg_len, &dynstream, &prdcrname,
                                    &myhost, &myport,
                                    &upstreamhost, &upstreamport,
                                    &upstreamcmdstream, &sendon);
        if (rc != 0){
                msglog(LDMSD_LDEBUG, SAMP
                       " Error parsing message. No further actions on %s\n",
                       cmd);
                goto out;
        }

        if (!upstreamhost || !upstreamport || !upstreamcmdstream || !sendon){
                if (TURNAROUND){ //FIXME --- this is a temporary hack.
                        // 4)  the extreme end, send a test message back down.
                        // NOTE: you can also call ldmsd_stream_publish on the
                        // next to last L on the dynamic stream
                        system("sleep 20");
                        msglog(LDMSD_LINFO, SAMP " End of the line."
                               " Testing sending a message back down\n");
                        turnaround("localhost", "52002", dynstream);
                } else {
                        msglog(LDMSD_LDEBUG, SAMP " Nothing to act upon. "
                               " No further actions on SETUP_FEEDBACK");
                }
                goto out;
        }

        msglog(LDMSD_LINFO,
               SAMP " my host = '%s' myport = '%s' dynstream = '%s'"
               " prdcrname = '%s'"
               " upstream host = '%s' upstream port = '%s' upstreamcmd = '%s'"
               " sendon list = '%s'\n",
               myhost, myport, dynstream, prdcrname,
               upstreamhost, upstreamport, upstreamcmdstream, sendon);

        // 1) use ldmsd_controller to tell this daemon on myhost myport
        // to subscribe to stream dynstream from upstreamhost.
        // OR if teardown, to tear down
        rc = call_ldmsd_controller(cmd, dynstream, prdcrname,
                                   myhost, myport, upstreamhost, upstreamport);
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
        // FIXME TODO This may end up being removed at some points
        if (!strcmp(cmd, SETUP_FEEDBACK)){
                msglog(LDMSD_LINFO, SAMP " subscribing to stream '%s'\n",
                       dynstream);
                client = ldmsd_stream_subscribe(dynstream, dynamic_stream_recv_cb,
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
        } else if (!strcmp(cmd, TEARDOWN_FEEDBACK)){
                msglog(LDMSD_LCRITICAL, SAMP
                       " will be closing stream '%s' BUT its not written yet\n",
                       dynstream);
        }
 prop:

        // 3) have stripped off my daemon and send the message to upstream
        // so that it can do the same up the stream
        rc = propogate_feedback(cmd, upstreamhost, upstreamport,
                                upstreamcmdstream, dynstream,
                                prdcrname, sendon);
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
        if (upstreamhost) free(upstreamhost);
        if (upstreamport) free(upstreamport);
        if (upstreamcmdstream) free(upstreamcmdstream);
        if (sendon) free(sendon);

        msglog(LDMSD_LDEBUG,
               SAMP " completed setup_feedback returning %d\n", rc);

        return rc;
}



static int cmd_recv_cb(ldmsd_stream_client_t c, void *ctxt,
			 ldmsd_stream_type_t stream_type,
			 const char *msg, size_t msg_len,
			 json_entity_t entity)
{
	int rc = 0;
        int len;
        char *dynstream = NULL;
        char *cmd = NULL;
        char *buff = NULL;
	const char *type = "UNKNOWN";
        json_parser_t jp = NULL;
        json_entity_t jdoc = NULL;
        json_entity_t ent = NULL;

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
                if (ent){
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

                        if (!strcmp(cmd, SETUP_FEEDBACK) ||
                            !strcmp(cmd, TEARDOWN_FEEDBACK)){
                                rc = feedback_handler(cmd, msg, msg_len);
                                if (rc != 0)
                                        msglog(LDMSD_LERROR, SAMP
                                               " '%s' error=%d\n", cmd, rc);
                        } else {
                                msglog(LDMSD_LERROR,
                                       SAMP " Error: invalid cmd '%s'\n", cmd);
                                rc = -1;
                                goto out;
                        }
                } else {
                        msglog(LDMSD_LERROR,
                               SAMP "Error: missing cmd in msg '%s'\n", buff);
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
        if (dynstream) free(dynstream);
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
