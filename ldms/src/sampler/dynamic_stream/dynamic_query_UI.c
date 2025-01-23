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
#include <time.h>
#include <ovis_json/ovis_json.h>
#include <ovis_util/util.h>

#define LDMS_PUBLISH_CMD "/home/gentile/Work/Build/OVIS-4.4.4/sbin/ldmsd_stream_publish -x sock -p 52001 -s cmd_stream52001 -t json -a munge -h localhost "
//output file to poll upon
#define FILEBASE "/tmp/dynamicquery_uuid_"
//ldms to which to publish and which one will write out
#define MY_RESPONDER "52001"
#define MY_STREAM "dynamicbar"

/*
 * Tis is a hack.
 * This executable will be called by a popen.
 * It will take args and pack them up into json and call and ldmsd_publish
 * It will then poll on a file, waiting for the return data which it will then
 * output as the return.
 *
 * The file will need a unique name, for now this code will use a basic stem
 * postpended with a random number and we will have to hope that it does not
 * collide.
 *
 * The file will be written by the dynamic callback on L0. Since there may be
 * multiples, this code will also have to send who should do the writeout.
 * Will need to discover what port that LDMSD is on. Hack for now in the
 * dynamic_stream_sampler -- keeping myport as a variable and passing myport
 * in the query. NOTE that this is still a problem, since different streams may
 * have different endpoints. Need to know stream and endpoint for writeout.
 */




void cleanup(){

}

void signal_handler(int signum){
        printf("In signal handler\n");
        cleanup();
        exit(signum);
}



static int makeQuery(const char* uuid, const char* jsonfname,
                     const char* qu, const char* argstring){

        jbuf_t jb;
        char cmd[MAXBUF];
        char* temp;
        char* newtemp;
        int rc;
        int i,j;

        jb = jbuf_new();
        if (!jb) goto out;
        jb = jbuf_append_str(jb, "{");
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, CMD_KEY, "\"%s\",", QUERY_DB);
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, QUERY_KEY, "\"%s\",", qu);
        if (!jb) goto out;
        //HACK --- this will be the filename for now
        jb = jbuf_append_attr(jb, UUID_KEY, "\"%s\",", uuid);
        if (!jb) goto out;
        //HACK --- port id to tell which callback should write the file
        jb = jbuf_append_attr(jb, RESPONDER_KEY, "\"%s\",", MY_RESPONDER);
        //HACK -- stream to tell which callback should write the file
        jb = jbuf_append_attr(jb, RESPONSE_STREAM_KEY, "\"%s\",", MY_STREAM);
        if (!jb) goto out;
        //HACK -- may combine these
        jb = jbuf_append_attr(jb, STREAM_KEY, "\"%s\",", MY_STREAM);
        //HACK
        jb = jbuf_append_attr(jb, PRDCRNAME_KEY, "\"%s\",", "zedprdcr");
        if (!jb) goto out;
        jb = jbuf_append_attr(jb, LIST_KEY, "\"%s\"", "localhost@52001@cmd_stream52001:localhost@52002@cmd_stream52002:localhost@52003@cmd_stream52003");
        if (!jb) goto out;
        if (argstring != NULL){
                jb = jbuf_append_str(jb, ",");
                if (!jb) goto out;
                jb = jbuf_append_attr(jb, ARG_STR_KEY, "\"%s\"", argstring);
                if (!jb) goto out;
        }
        jb = jbuf_append_str(jb, "}}");
        if (!jb) goto out;

        //HACK -- ldms_publish takes the data as a file
        //everywhere with a quote within needs to be replaced by a backslash
        //quote for echo to work
        temp = strdup(jb->buf);
        newtemp = strdup(temp);
        i = 0;
        j = 0;
        while (temp[i] != '\0') {
                if (temp[i] == '"') {
                        newtemp[j++] = '\\';
                        newtemp[j++] = '"';
                } else {
                        newtemp[j++] = temp[i];
                }
                i++;
        }
        newtemp[j] = '\0';

        snprintf(cmd, sizeof(cmd), "echo \"%s\" >> %s\n",
                newtemp, jsonfname);
        //        printf("building the json file executing '%s'\n", cmd);
        system(cmd);

        snprintf(cmd, sizeof(cmd), "%s -f %s\n",
                 LDMS_PUBLISH_CMD, jsonfname);
        //        printf("publishing calling '%s'\n", cmd);
        system(cmd);


 out:

        if (!jb){
                //                printf("Can't build jbuf\n");
                rc = -1;
        } else {
                rc = 0;
        }
        if (jb)
                free(jb);
        if (temp)
                free(temp);
        if (newtemp)
                free(newtemp);

        return rc;
};


int main(int argc, char **argv){

        char fname[1024];
        char fbase[1024];
        char jsonfname[1024];
        char cmd[1024];
        int r;
        int rc;


        signal(SIGCHLD, SIG_IGN);
        signal(SIGINT, signal_handler);
        signal(SIGHUP, signal_handler);

        if ((argc != 2) && (argc != 3)){
                printf("Usage ./dynamic_query_UI <QUERY_1> (optional)<ARGSTRING>\n");
                exit (-1);
        }

        srand(time(NULL));
        r = rand();
        snprintf(fbase, sizeof(fbase), "%s%d", FILEBASE, r);

        snprintf(fname, sizeof(fname), "%s%s", fbase, ".out");
        snprintf(cmd, sizeof(cmd), "rm %s", fname);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "touch %s", fname);
        //        printf("Creating empty output file executing '%s'\n", cmd);
        system(cmd);
        //        printf("File created '%s'\n", fname);

        snprintf(jsonfname, sizeof(jsonfname), "%s%s", fbase, "_json.out");
        snprintf(cmd, sizeof(cmd), "rm %s", jsonfname);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "touch %s", jsonfname);
        //        printf("Creating empty output file executing '%s'\n", cmd);
        system(cmd);
        //        printf("File created '%s'\n", jsonfname);

        rc = makeQuery(fname, jsonfname, argv[1], (argc == 3? argv[2]: NULL));
        //        printf("After making query\n");

        // For now, print fname to STDOUT
        printf("UUID: %s\n", fname);


        //delete the file
        printf("not yet removing files '%s' and '%s'\n", fname, jsonfname);
        //        snprintf(cmd, sizeof(cmd), "rm %s", jsonfname);
        //        system(cmd);
        //        snprintf(cmd, sizeof(cmd), "rm %s", fname);
        //        system(cmd);

        return rc;
}
