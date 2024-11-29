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

#define SAMP "dynamic_stream_sampler"
#define SETUP_FEEDBACK "SETUP_FEEDBACK"
#define CMD_STREAM_BASE "cmd_stream"
static ldmsd_msg_log_f msglog;
static base_data_t base;


static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP " stream=<stream>\n" \
                BASE_CONFIG_USAGE \
		"     stream        Stream name to which the dynamic_stream_sampler will subscribe. Defaults to " \
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


static int propogate_feedback(char* dest, const char* port, const char* upstreamcmdstream, const char* dyn_stream, char* list){

        char* xprt = "sock";
        char* auth = "munge";
        jbuf_t jb;
        ldms_t ldms = NULL;
        int rc = 0;

        if (!list){
                msglog(LDMSD_LDEBUG, SAMP " Nothing to propogate and that can be ok. Returning.\n");
                return 0;
        }

        //mst send on a different cmdstream to not block

        //build the message.
        msglog(LDMSD_LDEBUG, SAMP " building jbuf\n");
        jb = jbuf_new(); if (!jb) goto out_1;
        jb = jbuf_append_str(jb, "{"); if (!jb) goto out_1;
        jb = jbuf_append_attr(jb, "cmd", "\"%s\",", SETUP_FEEDBACK); if (!jb) goto out_1;
        jb = jbuf_append_attr(jb, "stream", "\"%s\",", dyn_stream); if (!jb) goto out_1;
        jb = jbuf_append_attr(jb, "list", "\"%s\"", list); if (!jb) goto out_1;
        jb = jbuf_append_str(jb, "}}"); if (!jb) goto out_1;
        msglog(LDMSD_LDEBUG, SAMP " done building jbuf\n");


        //set up the connection
        ldms = ldms_xprt_new_with_auth(xprt, NULL, auth, NULL);
        if (!ldms) {
                rc = errno;
                msglog(LDMSD_LERROR, SAMP " Failed to create the LDMS transport endpoint\n");
                goto out;
        }
        rc = ldms_xprt_connect_by_name(ldms, dest, port, NULL, NULL);
        if (rc) {
                msglog(LDMSD_LERROR, SAMP " Error %d connecting to peer\n", rc);
                goto out;
        }

        //tell the dest on cmd to listen to the new stream
        rc = ldmsd_stream_publish(ldms, upstreamcmdstream, LDMSD_STREAM_JSON, jb->buf, jb->cursor+1);
        if (rc){
                msglog(LDMSD_LERROR, SAMP " Error %d publishing to '%s'\n", rc, upstreamcmdstream);
                goto out;

        }

        msglog(LDMSD_LDEBUG, SAMP " After publishing '%s'\n", jb->buf);

        goto out;

 out_1:
        msglog(LDMSD_LERROR, SAMP " Cannot build SETUP_FEEDBACK message\n");
        rc = -1;
        goto out;

 out:
        if (jb) jbuf_free(jb);

        return rc;


}

static int dynamic_stream_recv_cb(ldmsd_stream_client_t c, void *ctxt,
                                  ldmsd_stream_type_t stream_type,
                                  const char *msg, size_t msg_len,
                                  json_entity_t entity)
{
	int rc = 0;

        //this is a placeholder function for when I recieve data on the feedback channel. This may end up being removed.

        switch (stream_type) {
        case LDMSD_STREAM_JSON:
                msglog(LDMSD_LDEBUG, "dynamic stream: '%s', stream_type: %s, msg: \"%s\", msg_len: %d, entity: %p\n",
                       ldmsd_stream_client_name(c), "JSON", msg, msg_len, entity);
                rc = 0;
                goto out;
                break;
	case LDMSD_STREAM_STRING:
                msglog(LDMSD_LDEBUG, "dynamic stream: '%s', stream_type: %s, msg: \"%s\", msg_len: %d, entity: %p\n",
                       ldmsd_stream_client_name(c), "STRING", msg, msg_len, entity);
                rc = 0;
                goto out;
	break;
        }

 out:
        return rc;

}


static int parse_setup_feedback_message(const char* msg, int msg_len, char** dynstream_e, char** myhost_e, char** myport_e,
                                        char** upstreamhost_e, char** upstreamport_e, char** upstreamcmdstream_e,
                                        char** sendon_e){

        char *buff = NULL;
        char *temp = NULL;

        char *dynstream = NULL;
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

        //parse the data for command SETUP_FEEDBACK
        //parsing will catch if this is json
        //NOTE: that there are corner cases that will still slip through...
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
        ent = json_value_find(jdoc, "stream");
        if (!ent){
                msglog(LDMSD_LERROR, SAMP " No stream in message\n");
                rc = -1;
                goto bad_params;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR, SAMP " Error: 'stream' must be a string\n");
                goto bad_params;
        }
        dynstream = strdup(ent->value.str_->str);
        if (!dynstream){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto bad;
        }

        ent = json_value_find(jdoc, "list");
        if (!ent){
                msglog(LDMSD_LERROR, SAMP " No list in message\n");
                rc = -1;
                goto bad_params;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                msglog(LDMSD_LERROR, SAMP " Error: 'list' must be a string\n");
                goto bad_params;
        }
        dynlist = strdup(ent->value.str_->str);
        if (!dynlist){
                rc = ENOMEM;
                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                goto bad;
        }

        //parse the list
        msglog(LDMSD_LDEBUG, SAMP " orig='%s'\n", dynlist);
        mydata = strtok_r(dynlist, ":", &saveptr);
        if (!mydata){
                msglog(LDMSD_LERROR, SAMP " No myhost information in message\n");
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

        msglog(LDMSD_LDEBUG, SAMP " mydata='%s' rest='%s'\n", mydata, saveptr);

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

        if (!myhost || (strlen(myhost) == 0) || !myport || (strlen(myport) == 0)){
                msglog(LDMSD_LERROR,
                       SAMP " Error: Bad msg params myhost = '%s' myport = '%s'\n",
                       myhost, myport);
                rc = -1;
                goto bad_params;
        }
        msglog(LDMSD_LDEBUG, SAMP " myhost = '%s' myport = '%s'\n",
               myhost, myport);

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
                       " Error: Bad msg params upstreamhost = '%s' upstreamport = '%s' upcmd = '%s'\n",
                       upstreamhost, upstreamport, upstreamcmdstream);
                rc = -1;
                goto bad_params;
        } else {
                msglog(LDMSD_LDEBUG, SAMP " upstreamhost = '%s' upstreamport = '%s' upcmd = '%s'\n",
                       upstreamhost, upstreamport, upstreamcmdstream);
                rc = 0;
                goto good_params;
        }


 good_params:
        *dynstream_e = dynstream;
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

        //freeing sendon will be the sign of badness along with error code return.
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
               SAMP " my host = '%s' myport = '%s' dynstream = '%s' upstream host = '%s' upstream port = '%s' sendon list = '%s'\n",
               *myhost_e, myport_e, *dynstream_e,  *upstreamhost_e, *upstreamport_e, *sendon_e);

        msglog(LDMSD_LDEBUG, SAMP " completed parse_setup_feedback_message returning %d\n", rc);

        //it will be the callers responsibility to free the arguments
        return rc;

}


static int call_ldmsd_controller(const char* dynstream, const char* myhost, const char* myport,
                                 const char* upstreamhost, const char* upstreamport){

        int rc = 0;
        char* prdcrname = "zed";

        //echo “prdcr_add name=L1 …” | Ldmsd_controller -h L0 -x rdma –p 412 –a munge
        //echo “prdcr_subscribe name=L1 stream=foo_f …” | Ldmsd_controller -h L0 -x rdma –p 412 –a munge
        //echo “prdcr_start name=L1 …” | Ldmsd_controller -h L0 -x rdma –p 412 –a munge

        //TODO... WRITE THIS THIS IS JUST A TEST
        //FIXME need to have in the message the xprt and the auth, because we cannot get them from the args in the cb
        //FIXME are there return values to system?
        char teststring[2048];

        if (!upstreamhost || !upstreamport){
                msglog(LDMSD_LDEBUG, SAMP " No prdcr to add and that can be ok. Returning\n");
                return 0;
        }

        msglog(LDMSD_LCRITICAL, SAMP " should be issuing commands to ldmsd_controller, but they are not complete yet\n");
        rc = snprintf(teststring, 2047, "echo \"prdcr_add host=%s xprt=sock port=%s interval=2000000 type=active name=%s\" | ldmsd_controller -h %s -p %s -x sock -a munge", upstreamhost, upstreamport, prdcrname, myhost, myport);
        msglog(LDMSD_LDEBUG, SAMP " issuing '%s'\n", teststring);
        //FIXME: is there a time to wait?
        system(teststring);
        //START HERE....
        //        rc = snprintf(teststring, 2047, "echo \"prdcr_subscribe stream=%s host=%s xprt=sock port=%s interval=2000000 type=active name=zed\" | ldmsd_controller -h %s -p %s -x sock -a munge", myhost, myport, upstreamhost, upstreamport);
        //        system(teststring);


        rc = 0;
        return rc;
}


static int setup_feedback(const char* msg, int msg_len){

        int rc = 0;
        int holdrc = 0;
        //responsible for freeing all of these:
        char *dynstream = NULL;
        char *myhost = NULL;
        char *myport = NULL;
        char *upstreamhost = NULL;
        char *upstreamport = NULL;
        char *upstreamcmdstream = NULL;
        char *sendon = NULL;

        ldmsd_stream_client_t client = NULL;

        // the host and port info will be used for ldmsd controller
        rc = parse_setup_feedback_message(msg, msg_len, &dynstream, &myhost, &myport, &upstreamhost,
                                          &upstreamport, &upstreamcmdstream, &sendon);
        if (rc != 0){
                msglog(LDMSD_LDEBUG, SAMP " Error parsing message. No further actions on SETUP_FEEDBACK\n");
                goto out;
        }

        if (!upstreamhost || !upstreamport || !upstreamcmdstream || !sendon){
                msglog(LDMSD_LDEBUG, SAMP " Nothing to act on.  No further actions on SETUP_FEEDBACK\n");
                goto out;
        }

        msglog(LDMSD_LINFO,
               SAMP " my host = '%s' myport = '%s' dynstream = '%s' upstream host = '%s' upstream port = '%s' upstreamcmd = '%s' sendon list = '%s'\n",
               myhost, myport, dynstream, upstreamhost, upstreamport, upstreamcmdstream, sendon);
        //1) use ldmsd_controller to tell this daemon on myhost myport to subscribe to stream dynstream from upstreamhost.
        //FIXME --- this is not written and it needs oter data sent to it
        rc = call_ldmsd_controller(dynstream, myhost, myport, upstreamhost, upstreamport);
        if (rc != 0){
                //will not setup a feedback for this, if I cannot call ldmsd_controller to listen to the dynamic stream
                //but will still try to pass the message on to the next one -- does this make sense?
                msglog(LDMSD_LERROR, SAMP " Error calling ldmsd controller..No cleanup yet. Will still try to propogate\n");
                holdrc = rc;
                goto prop;
        }

        //2) set up a callback for what to do when I receive a message on foo_fb (which I will get from upstream).
        //                      This may end up being removed at some points
        //FIXME: TEMP Don't need this for the moment....
        msglog(LDMSD_LERROR, SAMP " should be subscribing to stream '%s' as a possible test, but not doing for now\n", dynstream);
        /*
          msglog(LDMSD_LINFO, SAMP " subscribing to stream '%s'\n", dynstream);
          client = ldmsd_stream_subscribe(dynstream, dynamic_stream_recv_cb, myself);
          if (!client){
          msglog(LDMSD_LERROR,
          SAMP " cannot subscribe to stream '%s' (might be duplicate, so continuing)\n",
          dynstream);
          } else {
          msglog(LDMSD_LINFO,
          SAMP " subscribed to stream '%s')\n",
          dynstream);
          }
        */

 prop:

        //3) have stripped off my daemon and send the message to upstream so that it can do the same up the stream
        rc = propogate_feedback(upstreamhost, upstreamport, upstreamcmdstream, dynstream, sendon);
        if (rc)
                msglog(LDMSD_LERROR, SAMP " cannot propogate feedback w/Error case \n");
        msglog(LDMSD_LERROR, SAMP " after propogate_feedback\n");

        if (holdrc){
                rc = holdrc;
                msglog(LDMSD_LERROR, SAMP, "re-establishing error code to %d before returning\n", rc);
        }

        //4) TODO: at the extreme end, send a test message back down.
        //TODO

 out:

        if (dynstream) free(dynstream);
        if (myhost) free(myhost);
        if (myport) free(myport);
        if (upstreamhost) free(upstreamhost);
        if (upstreamport) free(upstreamport);
        if (upstreamcmdstream) free(upstreamcmdstream);
        if (sendon) free(sendon);

        msglog(LDMSD_LDEBUG, SAMP " completed setup_feedback returning %d\n", rc);

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
                /* For now, only accepting a message that is in the form
                   "{"cmd":"SETUP_FEEDBACK", "stream":"foo_fb", "list":"L1@52001:L2@52002@cmd2:L3@52003@cmd3..."
                   this will:
                   1) use ldmsd_controller to tell this Aggregator to subscribe to stream foo_fb from L1
                   2) set up a callback for what to do when I receive a message on foo_fb (which I will get from upstream).
                      This may end up being removed at some points.TODO: will that end in blocking???
                   3) strip off L1 and send the message to L2 on cmd2 so that it can do the same up the stream
                   (have to have separate name due to blocking)
                   4) at the extreme end, send a test message back down. TODO: need to see if this will block
                */
                msglog(LDMSD_LDEBUG, "stream: '%s', stream_type: %s, msg: \"%s\", msg_len: %d, entity: %p\n",
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
                        msglog(LDMSD_LERROR, SAMP " JSON parse failed: %d\n", rc);
                        goto out;
                }
                ent = json_value_find(jdoc, "cmd");
                if (ent){
                        if (ent->type != JSON_STRING_VALUE){
                                rc = EINVAL;
                                msglog(LDMSD_LERROR, SAMP " Error: 'cmd' must be a string\n");
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
                        while (len && (isspace(cmd[len-1]) || cmd[len-1] == '\n')) {
                                len--;
                        }

                        if (!len){
                                msglog(LDMSD_LERROR,  SAMP " Error: empty cmd!\n");
                                rc = -1;
                                goto out;
                        }
                        cmd[len] = '\0';

                        if (!strcmp(cmd, SETUP_FEEDBACK)){
                                rc = setup_feedback(msg, msg_len);
                                if (rc != 0) {
                                        msglog(LDMSD_LERROR, SAMP " could not set up feedback error=%d\n", rc);
                                        goto out;
                                }
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
                //3) strip off L1 and send the message to L1 so that it can do the same up the stream
                //4) at the extreme end, send a test message back down.

                break;
        case LDMSD_STREAM_STRING:
                type = "STRING";
                msglog(LDMSD_LDEBUG, "stream: '%s', stream_type: %s, msg: \"%s\", msg_len: %d, entity: %p\n",
                      ldmsd_stream_client_name(c), type, msg, msg_len, entity);
                break;
        default:
                type = "UNKNOWN";
                msglog(LDMSD_LDEBUG, "stream: '%s', stream_type: %s, msg: \"%s\", msg_len: %d, entity: %p\n",
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
	ldmsd_stream_client_t client = ldmsd_stream_subscribe(stream, cmd_recv_cb, self);
        if (!client){
                msglog(LDMSD_LERROR, SAMP " cannot subscribe to stream '%s'\n", stream);
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
