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
#include "dynamic_query.h"
#include <ovis_json/ovis_json.h>
#include <ovis_util/util.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_stream.h"

static char *host = NULL;
static char *port = NULL;
static char *xprt = "sock";
static char *auth = "none";
static char *stream = NULL;
static const int auth_opt_max = AUTH_OPT_MAX;
static ldmsd_stream_type_t typ = LDMSD_STREAM_JSON;
static ldms_t ldms = NULL;
static int SocketFD = -1;
static int ConnectFD = -1;


static struct option long_opts[] = {
	{"host",     required_argument, 0,  'h' },
	{"port",     required_argument, 0,  'p' },
	{"stream",   required_argument, 0,  's' },
	{"xprt",     required_argument, 0,  'x' },
	{"auth",     required_argument, 0,  'a' },
	{"auth_arg", required_argument, 0,  'A' },
	{0,          0,                 0,  0 }
};

void usage(int argc, char **argv) __attribute__((noreturn));
void usage(int argc, char **argv){
	printf("usage: %s -x <xprt> -h <host> -p <port> "
	       "-s <stream-name> "
	       "-a <auth> -A <auth-opt> \n",
	       argv[0]);
	exit(1);
}

static const char *short_opts = "h:p:s:x:a:A:";


void cleanup(){
        printf("In cleanup\n");
        close(SocketFD);
        close(ConnectFD);
        if (ldms)
                ldms_xprt_close(ldms);
        ldms = NULL;

}


void signal_handler(int signum){
        printf("In signal handler\n");
        cleanup();
        exit(signum);
}


jbuf_t execResultsQuery(int qu, char* uuid, char *argstring){

        int rc = 0;
        int len = 0;
        jbuf_t jb = NULL;
        FILE *mf = NULL;
        char cmdbuf[MAXBUF];
        char lbuf[MAXBUF];
        char* s = NULL;

        // this will execute a query on the database (in another thread?)
        // should this be a callback on the result being obtained?

        len = snprintf(cmdbuf, sizeof(cmdbuf),"%s%s%s",
                       queries[qu].qstring,
                       (argstring == NULL? "" : " "),
                       (argstring == NULL? "" : argstring));
        printf("in exec: Should be doing query %d '%s'\n",
               qu, cmdbuf);

        mf = popen(cmdbuf, "r");
        if (!mf){
                printf("in exec: popen file ptr == NULL\n");
                rc = ENOENT;
                goto out;
        }

        //for now, single line return only
        s = fgets(lbuf, sizeof(lbuf), mf);
        if (!s){
                printf("in exec: error reading output of popen\n");
                rc = ENOENT;
                goto out;
        }
        //TODO/FIXME: check output of fgets
         //Get rid of trailing whitespace and newlines
        len = strlen(lbuf);
        while (len &&
               (isspace(lbuf[len-1]) || lbuf[len-1] == '\n')) {
                len--;
        }

        if (!len){
                printf("in exec: Empty return!\n");
                rc = -1;
                goto out;
        }
        lbuf[len] = '\0';

        printf("Building the jbuf\n");

        jb = jbuf_new();
        if (!jb) goto out;
        jb = jbuf_append_str(jb, "{");
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, RESPONSE_KEY, "\"%s\",", lbuf);
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, UUID_KEY, "\"%s\"", uuid);
        if (!jb) goto out;
        jb = jbuf_append_str(jb, "}}");
        if (!jb) goto out;

        if (!jb) {
                printf("Warning --- jb is null\n");
        }

        printf("Will be sending jbuf '%s'\n", jb->buf);
 out:

        if (mf) pclose(mf);
        mf = NULL;

        return jb;

}

int parseJSONQuery(char* msg_buf, int* qu, char**uuid, char**argstring){
        //expects to get a message in json format
        //{QUERY_KEY:"foo", UUID_KEY:"bar", ARGS_STR_KEY "a b c"}

        json_parser_t jp = NULL;
        json_entity_t jdoc = NULL;
        json_entity_t ent = NULL;

        char* luuid = NULL;
        char* largstr = NULL;
        int lqu = -1;
        int k;
        int rc;

        printf("Should be parsing the jbuf\n");

        jp = json_parser_new(0);
        if (!jp){
                rc = errno;
                printf(" read() error: %d\n", errno);
                goto out;
        }
        rc = json_parse_buffer(jp, msg_buf, strlen(msg_buf), &jdoc);
        if (rc) {
                printf(" JSON parse failed: %d\n", rc);
                goto out;
        }

        // which query
        ent = json_value_find(jdoc, QUERY_KEY);
        if (!ent){
                printf(" No " QUERY_KEY " in message\n");
                rc = -1;
                goto out;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                printf(" Error: " QUERY_KEY " must be a string\n");
                goto out;
        }

        for (k = 0; k < NUM_QUERIES; k++){
                if (!strcmp(queries[k].qkey, ent->value.str_->str)){
                        lqu = k;
                        break;
                }
        }
        if (lqu == -1){
                printf("The query string is invalid '%s'\n",
                       ent->value.str_->str);
                rc = -1;
                goto out;
        }

        //uuid
        ent = json_value_find(jdoc, UUID_KEY);
        if (!ent){
                printf(" No " UUID_KEY " in message\n");
                rc = -1;
                goto out;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                printf(" Error: " UUID_KEY " must be a string\n");
                goto out;
        }

        luuid = strdup(ent->value.str_->str);
        if (!luuid){
                rc = ENOMEM;
                printf(" Out of memory\n");
                goto out;
        }

        //argstr
        ent = json_value_find(jdoc, ARG_STR_KEY);
        if (!ent){
                printf(" No " ARG_STR_KEY " in message. Could be ok.\n");
                rc = 0;
                goto out;
        }
        if (ent->type != JSON_STRING_VALUE){
                rc = EINVAL;
                printf(" Error: " ARG_STR_KEY " must be a string\n");
                goto out;
        }

        largstr = strdup(ent->value.str_->str);
        if (!largstr){
                rc = ENOMEM;
                printf(" Out of memory\n");
                goto out;
        }

 out:
        if (jp) json_parser_free(jp);
        if (jdoc) json_entity_free(jdoc);

        *argstring = largstr;
        *uuid = luuid;
        *qu = lqu;

        return rc;

}

int parseArgs(int argc, char **argv){
        int opt, opt_idx;
        char *lval, *rval;
        struct attr_value_list *auth_opt = NULL;

        int rc;

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
		default:
			usage(argc, argv);
		}
	}
	if (!host || !port || !stream )
		usage(argc, argv);

        return 0;
}


int setupLDMSD(){
        //have to do this in the thread

	int rc = 0;

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

        return rc;
}

void handleMsg(int CFD){

        char recvBuff[MAXBUF];
        int numrcv;
        jbuf_t jb;
        char* uuid = NULL;
        char* args = NULL;
        int qu;
	int rc;

        memset(recvBuff, '0', sizeof(recvBuff));
        numrcv = read(CFD, recvBuff, sizeof(recvBuff));
	recvBuff[numrcv] = '\0';
	printf("Received %s\n", recvBuff);


        rc = parseJSONQuery(recvBuff, &qu, &uuid, &args);
        if (rc){
		printf("Warning: ignoring bad query\n");
                if (uuid) free(uuid);
                if (args) free(args);
		return;
	}

        printf("Should be doing query %d '%s%s%s' uuid='%s'\n",
               qu, queries[qu].qstring,
               (args == NULL? "": " "),
               (args == NULL? "": args),
               uuid);

        jb = execResultsQuery(qu, uuid, args);
        if (jb == NULL){
                printf("jb is null. Not publishing\n");
                goto out;
        }

        printf("Setting up ldmsd connection in the thread now\n");
        rc = setupLDMSD();
        if (rc != 0){
                printf("Cannot setup LDMSD. Not publishing.\n");
                goto out;
        }

        //NOTE: had to move the connection to the thread for this not
        //to block. Can do iterations in the thread and it will work ok.
        printf("Publishing now\n");
        rc = ldmsd_stream_publish(ldms, stream, typ,
                                  jb->buf, jb->cursor+1);
        printf("After publishing\n");
        if (rc) {
                printf("Error on stream publish\n");
                goto out;
        }


 out:
        if (jb)
                jbuf_free(jb);
        if (ldms)
                ldms_xprt_close(ldms);
        ldms = NULL;
        if (uuid)
                free(uuid);
        uuid = NULL;
        if (args)
                free(args);
        args = NULL;

        printf("returning\n");
        return;
}


int main(int argc, char **argv){

        int rc;

        signal(SIGCHLD, SIG_IGN);
        signal(SIGINT, signal_handler);
        signal(SIGHUP, signal_handler);

        rc = parseArgs(argc, argv);
        if (rc != 0){
                printf("Bad args\n");
                exit(-1);
        }

	struct sockaddr_in stSockAddr;
	SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (-1 == SocketFD) {
                perror("can not create socket");
                exit(EXIT_FAILURE);
	}

        memset(&stSockAddr, NULL, sizeof(stSockAddr));

        stSockAddr.sin_family = AF_INET;
        stSockAddr.sin_port = htons(DYNAMIC_SERVICE_PORT);
	stSockAddr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (-1 == bind(SocketFD,(struct sockaddr *)&stSockAddr, sizeof(stSockAddr))) {
                perror("error bind failed");
                close(SocketFD);
                exit(EXIT_FAILURE);
        }

        if (-1 == listen(SocketFD, DYNAMIC_SERVICE_LISTEN_BACKLOG)) {
	        perror("error listen failed");
		close(SocketFD);
	        exit(EXIT_FAILURE);
        }

        for(;;) {
                ConnectFD = accept(SocketFD, NULL, NULL);
                if (ConnectFD < 0) {
	                perror("error accept failed");
                        close(SocketFD);
	                exit(EXIT_FAILURE);
                }

                int pid = fork();
		if (pid < 0)
                        perror("error on fork");
		if (pid == 0){
	                close(SocketFD);
                        handleMsg(ConnectFD);
                        printf("After handleMsg and should be exiting thread\n");
                        exit(0);
                } else {
                        close(ConnectFD);
                }
	}

 out:
        cleanup();
        return 0;
}
