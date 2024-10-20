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
static ldmsd_msg_log_f msglog;
static base_data_t base;


static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP " stream=<stream>\n" \
                BASE_CONFIG_USAGE \
		"     stream        Stream name to which the dynamic_stream_sampler will subscribe. Defaults to 'cmd_stream'\n";
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return NULL;
}

static int sample(struct ldmsd_sampler *self)
{
	return 0;
}

static int dynamic_stream_recv_cb(ldmsd_stream_client_t c, void *ctxt,
                                  ldmsd_stream_type_t stream_type,
                                  const char *msg, size_t msg_len,
                                  json_entity_t entity)
{
	int rc = 0;
        char *buff = NULL;
        char *dyncmd = NULL;
        char *temp = NULL;
        char* orig = NULL;
        char* sendon = NULL;
        char *mydata = NULL;
        char *myhost = NULL;
        int myport = -1;
        char *upstreamdata = NULL;
        char *upstreamhost = NULL;
        int upstreamport = -1;
        char *saveptr = NULL;
	const char *type = "UNKNOWN";
        json_parser_t jp = NULL;
        json_entity_t jdoc = NULL;
        json_entity_t ent = NULL;


	switch (stream_type) {
	case LDMSD_STREAM_JSON:
		type = "JSON";
                msglog(LDMSD_LDEBUG, "dynamic stream: '%s', stream_type: %s, msg: \"%s\", msg_len: %d, entity: %p\n",
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
                                msglog(LDMSD_LERROR, SAMP " Error: 'name' must be a string\n");
                                goto out;
                        }
                        dyncmd = strdup(ent->value.str_->str);
                        if (!dyncmd){
                                rc = ENOMEM;
                                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                                goto out;
                        }
                        if (strcmp(dyncmd, "SETUP_UPSTREAM")){
                                msglog(LDMSD_LERROR, SAMP " Cannot handle command '%s'\n", dyncmd);
                                rc = -1;
                                goto out;
                        }
                        free(dyncmd);
                        dyncmd = NULL;

                        ent = json_value_find(jdoc, "list");
                        if (ent){
                                if (ent->type != JSON_STRING_VALUE){
                                        rc = EINVAL;
                                        msglog(LDMSD_LERROR, SAMP " Error: 'list' must be a string\n");
                                        goto out;
                                }
                                dyncmd = strdup(ent->value.str_->str);
                                if (!dyncmd){
                                        rc = ENOMEM;
                                        msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                                        goto out;
                                }
                                orig = strdup(dyncmd);
                                if (!orig){
                                        rc = ENOMEM;
                                        msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                                        goto out;
                                }

                                mydata = strtok_r(dyncmd, ":", &saveptr);
                                if (mydata != NULL){
                                        msglog(LDMSD_LDEBUG, SAMP " mydata='%s' rest='%s'\n", mydata, saveptr);
                                        sendon = strdup(saveptr);
                                        temp = strdup(saveptr);

                                        //split mydata
                                        myhost = strtok_r(mydata, "@", &saveptr);
                                        if (myhost != NULL){
                                                myport = atoi(saveptr); //TODO: replace with something that will check with error
                                                msglog(LDMSD_LDEBUG,
                                                       SAMP " myhost = 's' myport = '%d'\n",
                                                       myhost, myport);
                                        } else {
                                                msglog(LDMSD_LERROR,
                                                       SAMP " Error Malformed argument: mydata bad '%s'\n", mydata);
                                                goto out;
                                        }

                                        //split upstreamdata
                                        upstreamdata = strtok_r(temp, ":", &saveptr);
                                        if (upstreamdata != NULL){
                                                upstreamhost = strtok_r(upstreamdata, "@", &saveptr);
                                                if (upstreamhost != NULL){
                                                        upstreamport = atoi(saveptr); //TODO: replace
                                                } else {
                                                        msglog(LDMSD_LERROR,
                                                               SAMP " Error Malformed argument: upstreamdata bad '%s'\n",
                                                               upstreamdata);
                                                        goto out;
                                                }
                                        } else {
                                                msglog(LDMSD_LERROR,
                                                       SAMP " Error Malformed argument: upstreamdata bad '%s'\n",
                                                       upstreamdata);
                                                goto out;
                                        }
                                } else {
                                        msglog(LDMSD_LERROR,
                                               SAMP " Error Malformed argument: mydata bad '%s'\n", mydata);
                                        goto out;
                                }
                        } else {
                                msglog(LDMSD_LERROR, SAMP " Error: Malformed argument: no upstream list\n");
                                rc = -1;
                                goto out;
                        }
                } else {
                        msglog(LDMSD_LERROR, SAMP " Error: Malformed argument: no cmd list\n");
                        rc = -1;
                        goto out;
                }

		break;
	case LDMSD_STREAM_STRING:
                type = "STRING";
                msglog(LDMSD_LDEBUG, "dynamic stream: '%s', stream_type: %s, msg: \"%s\", msg_len: %d, entity: %p\n",
                       ldmsd_stream_client_name(c), type, msg, msg_len, entity);
	break;
	}

        msglog(LDMSD_LINFO, SAMP " orig string '%s'\n", orig);
        msglog(LDMSD_LINFO, SAMP " my host = '%s' myport = '%d' upstream host = '%s' upstream port = '%d'\n",
         myhost, myport, upstreamhost, upstreamport);
        msglog(LDMSD_LINFO, SAMP " sending on '%s'\n", sendon);

        //START HERE...
        //TODO: NO UNSUBSCRIBING YET...

 out:


        if (buff) free(buff);
        if (orig) free(orig);
        if (temp) free(temp);
        if (dyncmd) free(dyncmd);
        if (sendon) free(sendon);
        if (jp) json_parser_free(jp);
        if (jdoc) json_entity_free(jdoc);

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
        char *buff = NULL;
	const char *type = "UNKNOWN";
        json_parser_t jp = NULL;
        json_entity_t jdoc = NULL;
        json_entity_t ent = NULL;


	switch (stream_type) {
	case LDMSD_STREAM_JSON:
		type = "JSON";
                //For now, whatever that msg is, is a stream to which I (the sampler) will subscribe
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
                ent = json_value_find(jdoc, "name");
                if (ent){
                        if (ent->type != JSON_STRING_VALUE){
                                rc = EINVAL;
                                msglog(LDMSD_LERROR, SAMP " Error: 'name' must be a string\n");
                                goto out;
                        }
                        dynstream = strdup(ent->value.str_->str);
                        if (!dynstream){
                                rc = ENOMEM;
                                msglog(LDMSD_LERROR, SAMP " Out of memory\n");
                                goto out;
                        }
                        len = strlen(dynstream);
                        if (!len){
                                msglog(LDMSD_LERROR,
                                       SAMP " Error: invalid stream name '%s'\n", dynstream);
                                rc = -1;
                                goto out;
                        }
                        //Get rid of trailing whitespace and newlines
                        while (len && (isspace(dynstream[len-1]) || dynstream[len-1] == '\n')) {
                                len--;
                        }

                        if (!len){
                                msglog(LDMSD_LERROR,  SAMP " Error: empty stream name!\n");
                                rc = -1;
                                goto out;
                        }
                        dynstream[len] = '\0';
                        msglog(LDMSD_LINFO, SAMP " subscribing to stream '%s'\n", dynstream);
                        ldmsd_stream_client_t client =
                                ldmsd_stream_subscribe(dynstream, dynamic_stream_recv_cb, myself);
                        if (!client){
                                msglog(LDMSD_LERROR,
                                       SAMP " cannot subscribe to stream '%s' (might be duplicate)\n",
                                       dynstream);
                                rc = -1; //what happens on this -1?
                                goto out;
                        }
                } else {
                        msglog(LDMSD_LERROR, SAMP " Error: no dynamic stream name\n");
                        rc = -1;
                        goto out;
                }

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

	return rc;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl,
		  struct attr_value_list *avl)
{
	char *value;
	int rc = 0;

	value = av_value(avl, "stream");
	if (value)
		stream = strdup(value);
	else
		stream = strdup("cmd_stream");

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
