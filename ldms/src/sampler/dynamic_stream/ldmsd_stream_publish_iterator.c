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

//We will want this to mimic executing a query on the database and then sending
//down the pipe at regular intervals
// keeping in the turnaround in dynamic_stream_sampler but then call this
// ./ldmsd_stream_publish_iterator -x sock -p 52003 -s dynamicbar -t json -a munge -f thisisatest.txt -h localhost -i 5000000 -r 5

#define MSG_KEY "msg_key"

static struct option long_opts[] = {
	{"host",     required_argument, 0,  'h' },
	{"port",     required_argument, 0,  'p' },
	{"file",     required_argument, 0,  'f' },
	{"stream",   required_argument, 0,  's' },
	{"type",     required_argument, 0,  't' },
	{"xprt",     required_argument, 0,  'x' },
	{"auth",     required_argument, 0,  'a' },
	{"auth_arg", required_argument, 0,  'A' },
	{"repeat",   required_argument, 0,  'r' },
	{"interval", required_argument, 0,  'i' },
	{0,          0,                 0,  0 }
};

void usage(int argc, char **argv) __attribute__((noreturn));
void usage(int argc, char **argv)
{
	printf("usage: %s -x <xprt> -h <host> -p <port> "
	       "-s <stream-name> -t <stream-type> "
	       "-f <file> -a <auth> -A <auth-opt> "
	       "-r <count> -i <microsec> \n",
	       argv[0]);
	exit(1);
}

static const char *short_opts = "h:p:f:s:t:x:a:A:lr:i:nN";

#define AUTH_OPT_MAX 128

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
	FILE *file;
	const char *stream_type = "string";
	ldmsd_stream_type_t typ = LDMSD_STREAM_STRING;
	int repeat = 0;
	unsigned interval = 0;
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
		case 'f':
			filename = strdup(optarg);
			if (!filename) {
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
		default:
			usage(argc, argv);
		}
	}
	if (!host || !port || !stream)
		usage(argc, argv);

	if (filename) {
		file = fopen(filename, "r");
		if (!file) {
			perror(filename);
			exit(1);
		}
	} else {
		if (repeat || interval) {
			printf("%s: To use -r or -i, -f FILE must also be used.\n",argv[0]);
			exit(1);
		}
		file = stdin;
	}
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
		char line_buffer[4096];
		char *s;
		if (k)
			rewind(file);
		while (0 != (s = fgets(line_buffer, sizeof(line_buffer)-1, file))) {
                  if (typ == LDMSD_STREAM_STRING){
			ldmsd_stream_publish(ldms, stream, typ, s, strlen(s)+1);
                  } else {
                    jbuf_t jb;

                    int len = strlen(s);
                    printf("s before = <%s> len=%d\n", s, len);
                    //Get rid of trailing whitespace and newlines
                    while (len &&
                           (isspace(s[len-1]) || s[len-1] == '\n')) {
                      len--;
                    }
                    s[len] = '\0';
                    len = strlen(s);
                    printf("s after = <%s> len=%d\n", s, len);

                    jb = jbuf_new();
                    if (!jb) goto out_1;
                    jb = jbuf_append_str(jb, "{");
                    if (!jb) goto out_1;
                    jb = jbuf_append_attr(jb, MSG_KEY, "\"%s\"", s);
                    //                    jb = jbuf_append_attr(jb, MSG_KEY, "\"%s\"", "foo"); TIS WORKS
                    if (!jb) goto out_1;
                    jb = jbuf_append_str(jb, "}}");
                    if (!jb) goto out_1;

                    printf("Should be publishing '%s'\n", jb->buf);

                    rc = ldmsd_stream_publish(ldms, stream, typ, jb->buf, jb->cursor+1);

                    if (jb) jbuf_free(jb);
                    if (rc) goto out_1;

                    continue;

                  out_1:
                    printf("Error building or publishing json message\n");

                  }
                }
		if (k)
			printf("loop: %d finished.\n", k);
		usleep(interval);
	}
	ldms_xprt_close(ldms);
	return rc;
}
