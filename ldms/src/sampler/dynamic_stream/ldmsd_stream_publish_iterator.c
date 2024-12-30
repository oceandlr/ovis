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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include <getopt.h>
#include <semaphore.h>
#include <pthread.h>
#include <ovis_json/ovis_json.h>
#include <ovis_util/util.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_stream.h"
//#include "../ldmsd_request.h"
//#include "../ldmsd_stream.h"

// We will want this to mimic executing a query on the database and then sending
// down the pipe at regular intervals
// keeping in the turnaround in dynamic_stream_sampler but then call this
// ./ldmsd_stream_publish_iterator -x sock -p 52003 -s dynamicbar -a munge -h localhost -i 5000000 -r 5

#define MSG_KEY "msg_key"

struct Db_Query {
        char qkey[10];
        char qstring[100];
};

struct Db_Query queries[3] = {{ "QUERY_1", "do this thing 1"},
                              { "QUERY_2", "do this thing 2"},
                              { "QUERY_3", "do this thing 3"}
};
#define NUM_QUERIES 3


static struct option long_opts[] = {
	{"host",     required_argument, 0,  'h' },
	{"port",     required_argument, 0,  'p' },
	{"file",     required_argument, 0,  'f' },
	{"stream",   required_argument, 0,  's' },
	{"xprt",     required_argument, 0,  'x' },
	{"auth",     required_argument, 0,  'a' },
	{"auth_arg", required_argument, 0,  'A' },
	{"repeat",   required_argument, 0,  'r' },
	{"interval", required_argument, 0,  'i' },
        {"query",    required_argument, 0,  'q' },
	{0,          0,                 0,  0 }
};

void usage(int argc, char **argv) __attribute__((noreturn));
void usage(int argc, char **argv)
{
	printf("usage: %s -x <xprt> -h <host> -p <port> "
	       "-s <stream-name> "
	       "-a <auth> -A <auth-opt> "
	       "-r <count> -i <microsec> -q <query>\n",
	       argv[0]);
	exit(1);
}

static const char *short_opts = "h:p:s:x:a:A:r:i:q:";

#define AUTH_OPT_MAX 128


jbuf_t execResultsQuery(int qu)
{
        int rc = 0;
        int len;
        jbuf_t jb;
        char s[100] = "This is the result";

        // this will execute a query on the database (in another thread?)
        printf("Should be doing query %d '%s'\n", qu, queries[qu].qstring);

        // will need to know how to match up queries to generate
        // values and queries to get their results
        // should this be a callback on the result being obtained?

        len = strlen(s);
        //Get rid of trailing whitespace and newlines
        while (len &&
               (isspace(s[len-1]) || s[len-1] == '\n')) {
                len--;
        }
        s[len] = '\0';

        printf("Should be building the jbuf\n");

        jb = jbuf_new();
        if (!jb) goto out;
        jb = jbuf_append_str(jb, "{");
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, MSG_KEY, "\"%s\"", s);
        if (!jb) goto out;
        jb = jbuf_append_str(jb, "}}");
        if (!jb) goto out;

        printf("Should be publishing '%s'\n", jb->buf);

 out:

        return jb;

}


int main(int argc, char **argv)
{
	char *host = NULL;
	char *port = NULL;
	char *xprt = "sock";
	char *filename = NULL;
	char *stream = NULL;
	int opt, opt_idx;
	char *lval, *rval;
	char *auth = "none";
	struct attr_value_list *auth_opt = NULL;
	const int auth_opt_max = AUTH_OPT_MAX;
	const char *stream_type = "string";
	ldmsd_stream_type_t typ = LDMSD_STREAM_STRING;
	int repeat = 0;
	unsigned interval = 0;
        int qu = -1;
        int k;


	auth_opt = av_new(auth_opt_max);
	if (!auth_opt) {
		perror("could not allocate auth options");
		exit(1);
	}

	while ((opt = getopt_long(argc, argv,
				  short_opts, long_opts,
				  &opt_idx)) > 0) {
		switch (opt) {
		case 'h':
			host = strdup(optarg);
			if (!host) {
				printf("ERROR: out of memory\n");
				exit(1);
			}
			break;
		case 'p':
			port = strdup(optarg);
			if (!port) {
				printf("ERROR: out of memory\n");
				exit(1);
			}
			break;
		case 'x':
			xprt = strdup(optarg);
			if (!xprt) {
				printf("ERROR: out of memory\n");
				exit(1);
			}
			break;
		case 'a':
			auth = strdup(optarg);
			if (!auth) {
				printf("ERROR: out of memory\n");
				exit(1);
			}
			break;
		case 'A':
			lval = strtok(optarg, "=");
			rval = strtok(NULL, "");
			if (!lval || !rval) {
				printf("ERROR: Expecting -A name=value");
				exit(1);
			}
			if (auth_opt->count == auth_opt->size) {
				printf("ERROR: Too many auth options");
				exit(1);
			}
			auth_opt->list[auth_opt->count].name = lval;
			auth_opt->list[auth_opt->count].value = rval;
			auth_opt->count++;
			break;
		case 's':
			stream = strdup(optarg);
			if (!stream) {
				printf("ERROR: out of memory\n");
				exit(1);
			}
			break;
		case 't':
			if (0 == strcmp("json", optarg)) {
				stream_type = "json";
				typ = LDMSD_STREAM_JSON;
			} else if (0 == strcmp("string", optarg)) {
				stream_type = "string";
				typ = LDMSD_STREAM_STRING;
			} else {
				printf("The type argument must be 'json' or 'string'\n");
				usage(argc, argv);
			}
			break;
		case 'r':
			repeat = atoi(optarg);
			break;
		case 'i':
			interval = (unsigned)atoi(optarg);
			break;
                case 'q':
                        for (k = 0; k < NUM_QUERIES; k++){
                                if (!strcmp(queries[k].qkey, optarg)){
                                        qu = k;
                                        break;
                                }
                        }
                        if (qu == -1){
                                printf("The query string is invalid '%s'\n",
                                       optarg);
                                usage(argc,argv);
                        }
			break;
		default:
			usage(argc, argv);
		}
	}
	if (!host || !port || !stream || (qu == -1))
		usage(argc, argv);

	if (!repeat)
		repeat = 1;

	int rc;
	ldms_t ldms = NULL;
        ldms = ldms_xprt_new_with_auth(xprt, NULL, auth, NULL);
        if (!ldms) {
          rc = errno;
          printf("Failed to create the LDMS transport endpoint.\n");
          return rc;
        }
        rc = ldms_xprt_connect_by_name(ldms, host, port, NULL, NULL);
        if (rc){
          printf("Error %d connecting to peer\n", rc);
          return rc;
        }

	for (k = 0; k < repeat; k++) {
                jbuf_t jb;

                jb = execResultsQuery(qu);
                if (jb == NULL){
                        printf("jb is null\n");
                        goto out_2;
                }

                rc = ldmsd_stream_publish(ldms, stream, typ,
                                          jb->buf, jb->cursor+1);
                if (rc) goto out_1;

                if (jb)
                        jbuf_free(jb);
        out_2:
                if (k)
			printf("loop: %d finished.\n", k);
		usleep(interval);

                continue;

        out_1:

                printf("Error building or publishing json message\n");

                if (jb)
                        jbuf_free(jb);
                break;

	}
	ldms_xprt_close(ldms);
	return rc;
}
