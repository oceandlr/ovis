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
#ifndef __DYNAMIC_QUERY_DEFS_H_
#define __DYNAMIC_QUERY_DEFS_H_

#define MAXBUF 2048
// commands to the sampler
#define SETUP_FEEDBACK "SETUP_FEEDBACK"
#define TEARDOWN_FEEDBACK "TEARDOWN_FEEDBACK"
#define QUERY_DB "QUERY_DB"
// fields in/about a command
#define CMD_STREAM_BASE "cmd_stream"
#define CMD_KEY "cmd"
#define PRDCRNAME_KEY "prdcrname"
#define LIST_KEY "list"
//combine these next two
#define STREAM_KEY "stream"
// headers in a DB query
#define QUERY_KEY "query_key"
#define RESPONSE_KEY "response_key"
#define RESPONSE_STREAM_KEY "dynstream_key"
#define RESPONDER_KEY "responder_key"
#define UUID_KEY "uuid_key"
#define ARG_STR_KEY "argstr_key"

#define QUERYDB_CLIENT_EXE "/home/gentile/Work/Build/OVIS-4.4.4/sbin/dynamic_query_client"

// query options --- note that the sampler doesnt use/check any of the query info
#define NUM_SQUERIES 3
#define NUM_QUERIES 3

struct Sampler_Query {
        char cmd[48];
        int stream;
        int prdcrname;
        int list;
        int query;
        int args;
        int uuid;
};

struct Sampler_Query squeries[3] = {{ SETUP_FEEDBACK, 1, 1, 1, 0, 0, 0},
                                    {TEARDOWN_FEEDBACK, 1, 1, 1, 0, 0, 0},
                                    {QUERY_DB, 1, 0, 1, 1, 2, 1}
};



struct Db_Query {
	char qkey[48];
	char qstring[100];
	int nargs; //currently unused
};

struct Db_Query queries[3] = {{ "QUERY_1", "/home/gentile/Work/Build/streams/fakedbcall.sh", 2},
                              { "QUERY_2", "echo \"hello\"", 0},
                              { "QUERY_3", "echo \"junk\"", 0}
};


#endif
