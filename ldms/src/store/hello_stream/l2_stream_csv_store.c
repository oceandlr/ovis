/**
 * Copyright (c) 2019-2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2019-2020 Open Grid Computing, Inc. All rights reserved.
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
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY out OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <ctype.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <linux/limits.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <ovis_json/ovis_json.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_stream.h"


/**
 * TODO: At a minimum, get the header and always print in the same order
 * LATER: need to support mulitple schema. Can we get an end so we know to release that one?
 * NOTE: assume the json will be something like: {foo:1, bar:2, zed-data:[{count:1, name:xyz},{count:2, name:abc}], zed2:[{time:0, count:2}]}}
 * In the writeout, Singletons that will be included for each entry and lists that will have the same fields per entry
 */

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif


#define PNAME "l2_stream_csv_store"

static ldmsd_msg_log_f msglog;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)


//TODO: need to be able to handle multiple different streams 
static char* root_path;
static char* container;
static char* schema;
FILE* streamfile;
static char* streamfile_name;
static pthread_mutex_t cfg_lock;
static pthread_mutex_t store_lock;
//Well known that the written order will be singletonkeys followed by listentry keys
static int nkey = 0;
static int nsingleton = 0; // number of singletons in a line
static char** singletonkey = NULL;
static int nmulti = 0; //number of lists in a line. list can only have dicts and all dicts in a list must have same keys
struct listdict {
        char* listkey;
        int ndict; // there are a variable number of dicts in the list. all dicts in a list must have same keys
        char** dictkey;
};
static struct listdict *multikey = NULL;
static int validheader = 0;
static int buffer = 0;

static void _clear_key_info(){
        int i, j;

        validheader = 0;
        for (i = 0; i < nsingleton; i++){
                if (singletonkey[i]) free(singletonkey[i]);
        }
        if (singletonkey) free(singletonkey);
        singletonkey = NULL;
        nsingleton = 0;

        for (i = 0; i < nmulti; i++){
                if (multikey[i].listkey) free(multikey[i].listkey);
                multikey[i].listkey = NULL;
                
                for (j = 0; j < multikey[i].ndict; j++){
                        if (multikey[i].dictkey[j]) free (multikey[i].dictkey[j]);
                        multikey[i].dictkey[j] = NULL;
                }
                multikey[i].ndict = 0;
        }
        if (multikey) free(multikey);
        multikey = NULL;
        nmulti = 0;
}


static int _parse_json_list(json_entity_t e, int idx){
        //this should only be list of dicts
        json_entity_t li, di;
        int countdi;
        int i;

        //for getting the header, we only care about the first li
        //        for (li = json_item_first(e); li; li = json_item_next(li)){
        li = json_item_first(e);
        if (li->type != JSON_DICT_VALUE){
                msglog(LDMSD_LERROR, PNAME ": list can only have dict entries\n");
                return -1;
        }
        //        msglog(LDMSD_LDEBUG, PNAME ": %d item in list with type %s\n",
        //               countli, json_type_name(li->type));
                
        //parse the dict. process once to fill and 2nd time to fill
        for (i = 0; i < 2; i++){
                countdi = 0;
                for (di = json_attr_first(li); di; di = json_attr_next(di)){
                        //only allowed singleton entries
                        msglog(LDMSD_LDEBUG, PNAME ": list %d dict item %d = %s\n",
                               idx, countdi, di->value.attr_->name->value.str_->str);
                        if (i == 1){
                                multikey[idx].dictkey[countdi] = strdup(di->value.attr_->name->value.str_->str);
                        }
                        countdi++;
                }
                if (i == 0){
                        multikey[idx].dictkey = (char**) calloc(countdi, sizeof(char*));
                        if (!multikey[idx].dictkey){
                                //TODO: memory error
                                return -1;
                        }
                        multikey[idx].ndict = countdi;
                }
        }
        return 0;

out:
        return -1;
};

static int _get_header_from_data(json_entity_t e){

        json_entity_t a;
	jbuf_t jb; //this is a test
        int isingleton, imulti;
	int i, j, rc;

	//TODO: will have to put in a bunch of robustnesss checks
	//TODO: check for thread safety

        _clear_key_info();
        
	// process to build the header.
        msglog(LDMSD_LDEBUG, PNAME ":starting first pass\n");
        i = 0;
	for (a = json_attr_first(e); a; a = json_attr_next(a)){
                json_attr_t attr = a->value.attr_;
                msglog(LDMSD_LDEBUG, PNAME ": get_header_from_data: parsing attr %d '%s' with value type %s\n",
                       i, attr->name->value.str_->str, json_type_name(attr->value->type));
		switch (attr->value->type){
		case JSON_LIST_VALUE:
                        //TODO: think the json will keep the list name from being the same
                        //note there is no way to check that all the dicts are the same, except by checking every line.
                        //instead will take the first one that we get and will subbsequently ask for those values
                        nmulti++;
                        break;
		case JSON_DICT_VALUE:
                        msglog(LDMSD_LERROR, PNAME ": not handling type JSON_DICT_VALUE in header\n");
                        goto out;
                        break;
		case JSON_NULL_VALUE:
                        //treat like a singleton
                        nsingleton++;
                        break;
		case JSON_ATTR_VALUE:
                        msglog(LDMSD_LERROR, PNAME ": should not have ATTR type now\n");
                        goto out;
                        break;
		default:
                        //it's a singleton
                        nsingleton++;
                        break;
		}
                i++;
	}
        msglog(LDMSD_LDEBUG, PNAME ":completed first pass\n");
        
	if ((nsingleton == 0) && (nmulti == 0)){
		msglog(LDMSD_LERROR, PNAME ": no keys for header. Waiting for next one\n");
                goto out;
	}
	
        singletonkey = (char**) calloc(nsingleton, sizeof(char*));
        multikey = (struct listdict*) calloc(nmulti, sizeof(struct listdict));
	if ((nsingleton && !singletonkey) || (nmulti&& !multikey)){
                goto out;
	}

	// process again to fill. will parse dicts here
        msglog(LDMSD_LDEBUG, PNAME ":starting second pass\n");
        isingleton = 0;
        imulti = 0;
        i = 0;
	for (a = json_attr_first(e); a; a = json_attr_next(a)){
                json_attr_t attr = a->value.attr_;
                msglog(LDMSD_LDEBUG, PNAME ": get_header_from_data: parsing attr %d '%s' with value type %s\n",
                       i, attr->name->value.str_->str, json_type_name(attr->value->type));
		switch (attr->value->type){
		case JSON_LIST_VALUE:
                        multikey[imulti].listkey = strdup(attr->name->value.str_->str);
                        rc = _parse_json_list(attr->value, imulti++);
                        if (rc) goto out;
                        break;
		case JSON_DICT_VALUE:
                        msglog(LDMSD_LERROR, PNAME ": not handling type JSON_DICT_VALUE in header\n");
                        goto out;
                        break;
		case JSON_ATTR_VALUE:
                        msglog(LDMSD_LERROR, PNAME ": should not have ATTR type now\n");
                        goto out;
                        break;
		case JSON_NULL_VALUE:                        
		default:
                        //it's a singleton
                        singletonkey[isingleton++] = strdup(attr->name->value.str_->str);
                        break;
		}
        }

        msglog(LDMSD_LDEBUG, PNAME ": printing header\n");
        //order will be order of singletons and order of dict. repeat dicts will be separate entries in the csv
        for (i = 0; i < nsingleton; i++){
                msglog(LDMSD_LDEBUG, "<%s>\n", singletonkey[i]);
                fprintf(streamfile, "%s", singletonkey[i]);
                if (i < (nsingleton -1)){
                        fprintf(streamfile, ",");
                }
        }
        if ((nsingleton > 0) && (nmulti > 0) && (multikey[0].ndict > 0)) fprintf(streamfile, ",");

        for (i = 0; i < nmulti; i++){
                for (j = 0; j < multikey[i].ndict; j++){ 
                        msglog(LDMSD_LDEBUG, "<%s:%s>\n", multikey[i].listkey, multikey[i].dictkey[j]);
                        fprintf(streamfile, "%s:%s", multikey[i].listkey, multikey[i].dictkey[j]);                
                        if (j < (multikey[i].ndict-1)){
                                fprintf(streamfile, ",");
                        }
                }
                if (i < nmulti-1){
                        fprintf(streamfile, ",");
                }
        }
        fprintf(streamfile, "\n");
        fflush(streamfile);
        fsync(fileno(streamfile));
        validheader = 1;
                
        return 0;
        
out:
        msglog(LDMSD_LDEBUG, PNAME ":header build failed - return -1\n");        
        
	_clear_key_info();
	msglog(LDMSD_LDEBUG, PNAME ": returning from get_header_from_data\n");

	return -1;
}



static int stream_cb(ldmsd_stream_client_t c, void *ctxt,
		     ldmsd_stream_type_t stream_type,
		     const char *msg, size_t msg_len,
		     json_entity_t e) {


	msglog(LDMSD_LDEBUG, PNAME ": Calling stream_cb. msg '%s'\n", msg);
	pthread_mutex_lock(&store_lock);

	int rc = 0;
	int i = 0;
	
	if (!streamfile){
		msglog(LDMSD_LERROR, PNAME ": Cannot insert values for '%s': file is NULL\n",
		       streamfile);
		rc = EPERM;
		goto out;
	}
	
	// msg will be populated. if the type was json, entity will also be populated.
	if (stream_type == LDMSD_STREAM_STRING){
		fprintf(streamfile, "%s\n", msg);
		if (!buffer){
			fflush(streamfile);
			fsync(fileno(streamfile));
		}
	} else if (stream_type == LDMSD_STREAM_JSON){
		if (!e){
			msglog(LDMSD_LERROR, PNAME ": why is entity NULL?\n");
			rc = EINVAL;
			goto out;
		}

		if (e->type != JSON_DICT_VALUE) {
			msglog(LDMSD_LERROR, PNAME ": Expected a dictionary object, not a %s.\n",
			       json_type_name(e->type));
			rc = EINVAL;
			goto out;
		}

                if (!validheader){
                        rc = _get_header_from_data(e);
                        if (rc != 0) {
                                msglog(LDMSD_LDEBUG, PNAME ": error processing header from data <%d>\n", rc);
                                goto out;
                        }
                }
        }
                
#if 0

                
                msglog(LDMSD_LDEBUG, PNAME ": numkeys %d\n", numkeys);
                for (i = 0; i < numkeys; i++){
                        //how many attr in this entity?
                        json_entity_t en = json_value_find(e, keyarray[i]);
                        if (en == NULL){
                                msglog(LDMSD_LDEBUG, PNAME ": NULL return from find for key <%s>\n",
                                       keyarray[i]);
                                //print nothing
                        } else {
                                msglog(LDMSD_LDEBUG, PNAME ": processing key '%d' type '%s'\n",
                                       i, json_type_name(en->type));
                                switch (en->type) {
                                case JSON_INT_VALUE:
                                        fprintf(streamfile, "%ld", en->value.int_);
                                        break;
                                case JSON_BOOL_VALUE:
                                        if (en->value.bool_)
                                                fprintf(streamfile, "%s", "true");
                                        else
                                                fprintf(streamfile, "%s", "false");
                                        break;
                                case JSON_FLOAT_VALUE:
                                        fprintf(streamfile, "%f", en->value.double_);
                                        break;
                                case JSON_STRING_VALUE:
                                        fprintf(streamfile, "\"%s\"", en->value.str_->str);
                                        break;
                                case JSON_LIST_VALUE:
                                        //how to handle this?
                                        //print_list(jb, e);
                                        fprintf(streamfile, "LDMS_LIST_PRINTME");
                                        break;
                                case JSON_DICT_VALUE:
                                        //how to handle this?
                                        //print_dict(jb, e);
                                        fprintf(streamfile, "LDMS_DICT_PRINTME");
                                        break;
                                case JSON_NULL_VALUE:
                                        //how to handle this?
                                        fprintf(streamfile, "null");
                                        break;
                                case JSON_ATTR_VALUE:
                                        msglog(LDMSD_LERROR, PNAME ": Value should not be ATTR type\n");
                                        fprintf(streamfile, "LDMS_UNKNOWN");
                                        break;
                                default:
                                        msglog(LDMSD_LDEBUG, PNAME, ": cannot process JSON type '%s'\n",
                                               json_type_name(en->type));
                                        fprintf(streamfile, "UNKNOWN");
                                }
                        }
                        if (i < (numkeys-1)){
                                fprintf(streamfile, ",");
                        }
                }
                fprintf(streamfile, "\n");
                if (!buffer){
                        fflush(streamfile);
                        fsync(fileno(streamfile));
                }
        } else {
                msglog(LDMSD_LERROR, PNAME ": unknown stream type\n");
		rc = EINVAL;
		goto out;
	}
#endif
#if 1
        rc = -1;
#endif

out:

	pthread_mutex_unlock(&store_lock);
	return rc;
}



static int reopen_container(){

	int rc = 0;
	char* path = NULL;
	char* dpath = NULL;


	//already have the cfg_lock
	if (!root_path || !container || !schema) {
	     msglog(LDMSD_LERROR, PNAME ": config not called. cannot open.\n");
	     return ENOENT;
	}

	if (streamfile){ //dont reopen
		return 0;
	}

	size_t pathlen = strlen(root_path) + strlen(schema) + strlen(container) + 8;
	path = malloc(pathlen);
	if (!path){
		rc = ENOMEM;
		goto out;
	}
	if (streamfile_name)
		free(streamfile_name);
	dpath = malloc(pathlen);
	if (!dpath) {
		rc = ENOMEM;
		goto out;
	}
	sprintf(path, "%s/%s/%s", root_path, container, schema);
	sprintf(dpath, "%s/%s", root_path, container);
	streamfile_name = strdup(path);

	msglog(LDMSD_LDEBUG, PNAME ": schema '%s' will have file path '%s'\n", schema, path);

	pthread_mutex_init(&store_lock, NULL);
	pthread_mutex_lock(&store_lock);

	/* create path if not already there. */
	rc = mkdir(dpath, 0777);
	if ((rc != 0) && (errno != EEXIST)) {
		msglog(LDMSD_LERROR, PNAME ": Failure %d creating directory '%s'\n",
			 errno, dpath);
		rc = ENOENT;
		goto err1;
	}
	rc = 0;

	streamfile = fopen_perm(path, "a+", LDMSD_DEFAULT_FILE_PERM);
	if (!streamfile){
		msglog(LDMSD_LERROR, PNAME ": Error %d opening the file %s.\n",
		       errno,path);
		rc = ENOENT;
		goto err1;
	}
	pthread_mutex_unlock(&store_lock);

	goto out;

err1:
	fclose(streamfile);
	streamfile = NULL;
	free(streamfile_name);
	streamfile_name = NULL;
	pthread_mutex_unlock(&store_lock);
	pthread_mutex_destroy(&store_lock);

out:

	free(path);
	free(dpath);
	return rc;

}


/**
 * \brief Configuration
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char* s;
	int rc;

	pthread_mutex_lock(&cfg_lock);

	s = av_value(avl, "buffer");
	if (!s){
		buffer = atoi(s);
		msglog(LDMSD_LDEBUG, PNAME ": setting buffer to '%d'\n", buffer);
	}

	s = av_value(avl, "stream");
	if (!s){
		msglog(LDMSD_LDEBUG, PNAME ": missing stream in config\n");
		rc = EINVAL;
		goto out;
	} else {
		schema = strdup(s);
		msglog(LDMSD_LDEBUG, PNAME ": setting stream to '%s'\n", schema);
	}


	s = av_value(avl, "path");
	if (!s){
		msglog(LDMSD_LDEBUG, PNAME ": missing path in config\n");
		rc = EINVAL;
		goto out;
	} else {
		root_path = strdup(s);
		msglog(LDMSD_LDEBUG, PNAME ": setting root_path to '%s'\n", root_path);
	}


	s = av_value(avl, "container");
	if (!s){
		msglog(LDMSD_LDEBUG, PNAME ": missing container in config\n");
		rc = EINVAL;
		goto out;
	} else {
		container = strdup(s);
		msglog(LDMSD_LDEBUG, PNAME ": setting container to '%s'\n", container);
	}

	rc = reopen_container();
	if (rc) {
		msglog(LDMSD_LERROR, PNAME ": Error opening %s/%s/%s\n",
		       root_path, container, schema);
		rc = EINVAL;
		goto out;
	}

	msglog(LDMSD_LDEBUG, PNAME ": subscribing to stream '%s'\n", schema);
	ldmsd_stream_subscribe(schema, stream_cb, self);

out:
	pthread_mutex_unlock(&cfg_lock);

	return rc;
}

static void term(struct ldmsd_plugin *self)
{
	int i;

	if (streamfile)
		fclose(streamfile);
	streamfile = NULL;
	free(root_path);
	root_path = NULL;
	free(container);
	container = NULL;
	free(schema);
	schema = NULL;
	free(streamfile_name);
	streamfile_name = NULL;
        _clear_key_info();
	buffer = 0;

	return;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "    config name=l2_stream_csv_store path=<path> container=<container> stream=<stream> [buffer=<0/1>] \n"
		"         - Set the root path for the storage of csvs and some default parameters\n"
		"         - path       The path to the root of the csv directory\n"
		"         - container  The directory under the path\n"
		"         - schema     The stream name which will also be the file name\n"
		" 	  - buffer     0 to disable buffering, 1 to enable it with autosize (default)\n"
		;
}



static struct ldmsd_store l2_stream_csv_store = {
	.base = {
			.name = "l2_stream_csv_store",
			.type = LDMSD_PLUGIN_STORE,
			.term = term,
			.config = config,
			.usage = usage,
	},
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &l2_stream_csv_store.base;
}

static void __attribute__ ((constructor)) l2_stream_csv_store_init();
static void l2_stream_csv_store_init()
{
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) l2_stream_csv_store_fini(void);
static void l2_stream_csv_store_fini()
{
	pthread_mutex_destroy(&cfg_lock);
}
