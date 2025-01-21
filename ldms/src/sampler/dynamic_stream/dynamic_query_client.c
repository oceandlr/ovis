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

static struct sockaddr_in stSockAddr;
static int Res = -1;
static int SocketFD = -1;

void cleanup(){

}

void signal_handler(int signum){
        printf("In signal handler\n");
        cleanup();
        exit(signum);
}

static int setupSocket(){
        struct hostent *he;
        struct in_addr **addr_list;


        if ((he = gethostbyname("localhost")) == NULL) {  // get the host info
                printf("Error: failed gethostbyname(%s)\n", "localhost");
                return -1;
        }

        addr_list = (struct in_addr **)he->h_addr_list;
        if (addr_list[0] == NULL) {
                printf("Error: failed addr_list[0]\n");
                return -1;
        }

        SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (-1 == SocketFD) {
                printf("Error: cannot create socket");
                return -1;

        }

        memset(&stSockAddr, 0, sizeof(stSockAddr));

        stSockAddr.sin_family = AF_INET;
        stSockAddr.sin_port = htons(DYNAMIC_SERVICE_PORT);
        Res = inet_pton(AF_INET, inet_ntoa(*addr_list[0]), &stSockAddr.sin_addr);

        if (0 > Res) {
                printf("error: first parameter is not a valid address family");
                close(SocketFD);
                SocketFD = -1;
                return -1;
        }  else if (0 == Res) {
                printf("char string (second parameter does not contain valid ipaddress)");
                close(SocketFD);
                SocketFD = -1;
                return -1;
        }

        if (-1 == connect(SocketFD, (struct sockaddr *)&stSockAddr, sizeof(stSockAddr))) {
                printf("connect failed");
                close(SocketFD);
                SocketFD = -1;
                return -1;
        }

        return 0;

}


static int makeQuery(const char* qu, const char* uuid,
                     const char* responder, const char* dynstream,
                     const char* argstring){

        jbuf_t jb;
        char sendBuff[MAXBUF];
        int rc;

        if (SocketFD == -1){
                return -1;
        }

        jb = jbuf_new();
        if (!jb) goto out;
        jb = jbuf_append_str(jb, "{");
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, QUERY_KEY, "\"%s\",", qu);
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, UUID_KEY, "\"%s\",", uuid);
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, RESPONDER_KEY, "\"%s\",", responder);
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, RESPONSE_STREAM_KEY, "\"%s\"", dynstream);
        if (!jb) goto out;
        if (argstring != NULL){
                jb = jbuf_append_str(jb, ",");
                if (!jb) goto out;
                jb = jbuf_append_attr(jb, ARG_STR_KEY, "\"%s\"", argstring);
                if (!jb) goto out;
        }
        jb = jbuf_append_str(jb, "}}");
        if (!jb) goto out;

        snprintf(sendBuff, sizeof(sendBuff), "%s", jb->buf);

        /* perform write operations ... */
        printf("Sending %s\n", sendBuff);
        rc = write(SocketFD, sendBuff, strlen(sendBuff));

 out:
        if (!jb){
                printf("Can't build jbuf\n");
                rc = -1;
        } else {
                rc = 0;
        }

        return rc;
};


int main(int argc, char **argv){

        int rc;

        signal(SIGCHLD, SIG_IGN);
        signal(SIGINT, signal_handler);
        signal(SIGHUP, signal_handler);

        if ((argc != 5) && (argc != 6)){
                printf("Usage ./dynamic_query_client <QUERY_1> <UUID> <RESPONDER> <DYNSTREAM> (optional)<ARGSTRING>\n");
                exit (-1);
        }

        rc = setupSocket();
        if (rc){
                printf("can't setup socket\n");
                exit(-1);
        }

        rc = makeQuery(argv[1], argv[2], argv[3], argv[4], (argc == 6? NULL: argv[5]));

        return rc;
}
