#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define FOO "echo \"prdcr_add host=%s xprt=%s port=%s interval=20000000 type=active name=%s\""
#define BAR "ldmsd_controller -h %s -p %s -x %s -a %s"

struct HostInfo{
  char* host;
  char* port;
  char* xprt;
  char* auth;
  char* stream;
  int end;
};

void printHostInfo(struct HostInfo* hi){
  printf("HostInfo: host '%s' port '%s' xprt '%s' auth '%s' stream '%s' end %d\n",
         hi->host, hi->port, hi->xprt, hi->auth, hi->stream, hi->end);

  return;
};

void initHostInfo(struct HostInfo* hi){
  hi->host = NULL;
  hi->port = NULL;
  hi->xprt = NULL;
  hi->auth = NULL;
  hi->stream = NULL;
  hi->end = 0;
};

void freeHostInfo(struct HostInfo* hi){
  if (hi->host) free(hi->host);
  if (hi->port) free(hi->port);
  if (hi->xprt) free(hi->xprt);
  if (hi->auth) free(hi->auth);
  if (hi->stream) free(hi->stream);
  initHostInfo(hi);
};

int validHostInfo(struct HostInfo* hi){
  if (!hi->host || (strlen(hi->host) == 0) ||
      !hi->port || (strlen(hi->port) == 0) ||
      !hi->stream || (strlen(hi->stream) == 0) ||
      !hi->xprt || (strlen(hi->xprt) == 0) ||
      !hi->auth || (strlen(hi->auth) == 0)){
    return 0;
  } else {
    return 1;
  }
}


int v1(char* str);
int v2(char* str, char* match, struct HostInfo* hi, char** rest);

int testfct(char **str){
  char buff[256];
  char abc[] = "abc";
  char zed[] = "zed";

  snprintf(buff, 255, FOO " | " BAR, abc, abc, abc, abc,
           zed, zed, zed, zed);
  printf("<%s>\n", buff);

  *str = strdup("this is a test");
  return 0;
}


// NOTE: have to set up each on to listen on something of a different

int main(int argc, char* argv[]){
  /*
  int len;
  char *ch;

  //  char *msg = "this is the msg \n";
  char *msg = "thisisthemsg";

  len = strlen(msg);
  printf("'%s' len=%d\n", msg, len);
  //get rid of trailing white space and newlines
  ch = strdup(msg);
  while (len && (isspace(ch[len-1]) || ch[len-1] == '\n')){
    printf("decrementing %d '%c'\n", len, ch[len-1]);
    len--;
  }

  if (!len){
    printf("Empty String!!\n");
  } else {
    printf("replacing %d '%c'\n",len, ch[len]);
    ch[len] =  '\0';
  }
   //do we want to get rid of leading spaces??
  printf("new = '%s'\n", ch);
  free(ch);
  */

  //  char str[]="L0@52001@:L2@52002@cmd2";
  //  char str[]="L0@52001@:L2@52002@cmd2:";
  //  char str[]="L2@52002@cmd2:";
  //  char str[]="L0@52001:L2@cmd2";
  //char str[]="L0@52001:L2@52002@cmd2:L3@52003@cmd3:L4@52004@cmd4"; //BAD
  char str[]="L0@52001@cmd1:L2@52002@cmd2:L3@52003@cmd3:L4@52004@cmd4";
  //  char str[]="L1@52001:L2@52002@cmd2";
  //  char str[]="L1@52001@cmd1";
  //  char str[]="L1@52001";
  //  char str[]="L0:L2@52002@cmd2";

  struct HostInfo myhi;
  struct HostInfo uphi;
  char *mydata = NULL;
  char *junk = NULL;
  int rc;

  if (0){
    rc = testfct(&mydata);
    printf("str return ='%s'\n", mydata);
    free(mydata);
    mydata = NULL;
  }

  initHostInfo(&myhi);
  initHostInfo(&uphi);

  rc = v2(str, argv[1], &myhi, &mydata);
  if (rc){
    printf("Invalid myHostInfo '%d'\n", rc);
  } else {
    if (!myhi.end){
      rc = v2(mydata, NULL, &uphi, &junk);
      if (rc){
        printf("Invalid upHostInfo '%d'\n", rc);
      }
    }
  }
  printHostInfo(&myhi);
  printHostInfo(&uphi);

  if (mydata) free(mydata);
  if (junk) free(junk);

  freeHostInfo(&myhi);
  freeHostInfo(&uphi);

  return 0;
};

int v1(char* str){
  char *orig = NULL;
  char *temp = NULL;

  char *myhost= NULL;
  char *myport = NULL;
  char *mystream = NULL;
  char *myxprt = NULL;
  char *myauth = NULL;
  char *upstreamhost = NULL;
  char *upstreamport = NULL;
  char *upstreamxprt = NULL;
  char *upstreamauth = NULL;
  char *upstreamstream = NULL;
  char *sendon = NULL;

  char *mydata = NULL;
  char *upstreamdata = NULL;
  char *saveptr = NULL;
  char *tok = NULL;

  int rc;

  printf("ORIG '%s'\n", str);
  orig = strdup(str);
  mydata = strtok_r(str, ":", &saveptr);
  temp = strdup(saveptr);

  if (mydata != NULL) {
    printf("mydata = '%s' rest = '%s'\n", mydata, saveptr);
    //split mydata
    tok = strtok_r(mydata, "@", &saveptr);
    if (tok != NULL){
      myhost = strdup(tok);
      printf("\t myhost='%s'\n", myhost);
      tok = strtok_r(NULL, "@", &saveptr);
      if (tok!= NULL){
        myport = strdup(tok);
        printf("\t myport='%s'\n", myport);
        tok = strtok_r(NULL, "@", &saveptr);
        if (tok!= NULL){
          mystream = strdup(tok);
          printf("\t mystream='%s'\n", mystream);
          tok = strtok_r(NULL, "@", &saveptr);
          if (tok!= NULL){
            myxprt = strdup(tok);
            myauth = strdup(saveptr);
          } else {
            myxprt = strdup("sock");
            myauth = strdup("munge");
          }
        }
      }
    }
  }

  if (!myhost || (strlen(myhost) == 0) ||
      !myport || (strlen(myport) == 0) ||
      !mystream || (strlen(mystream) == 0)) { //xprt and auth have defaults
    printf("Bad data for me --- too few my fields\n");
    goto out;
  }

  printf("myhost = '%s'  len=%d myport = '%s' len=%d mystream='%s' myxprt = '%s' myauth = '%s'\n",
         myhost, strlen(myhost),
         myport, strlen(myport),
         mystream, myxprt, myauth);


  if (!temp || (strlen(temp) == 0)){
    printf("no upstream data -- I'm the end of the line and that is ok.\n");
    goto out;
  }

  sendon = strdup(temp);
  printf("pre-upstreamdata = '%s' strlen-pre=%d\n", temp, strlen(temp));
  upstreamdata = strtok_r(temp, ":", &saveptr);
  if (upstreamdata != NULL){
    printf("upstreamdata = '%s' rest = '%s'\n", upstreamdata, saveptr);
    tok = strtok_r(upstreamdata, "@", &saveptr);
    if (tok != NULL){
      upstreamhost = strdup(tok);
      tok = strtok_r(NULL, "@", &saveptr);// will have at least one of these
      if (tok != NULL){
        upstreamport = strdup(tok);
        tok = strtok_r(NULL, "@", &saveptr);
        if (tok != NULL){
          upstreamstream = strdup(tok);
          tok = strtok_r(NULL, "@", &saveptr);
          if (tok != NULL){
            upstreamxprt = strdup(tok);
            upstreamauth = strdup(saveptr);
          } else {
            upstreamxprt = strdup("sock"); //default
            upstreamauth = strdup("munge"); //default
          }
        }
      }
    }
  }

  if (!upstreamhost || (strlen(upstreamhost) == 0) ||
      !upstreamport || (strlen(upstreamport) == 0) ||
      !upstreamstream || (strlen(upstreamstream) == 0)){
    printf("Bad data upstream --- too few fields\n");
    goto out;
  }

 out:

  printf("upstreamstream = '%s' upstream host = '%s' upstream port = '%s' upstream xprt = '%s' upstream auth = '%s'\n",
         upstreamstream, upstreamhost, upstreamport, upstreamxprt, upstreamauth);
  printf("sending  on '%s'\n", sendon);

  if (orig) free(orig);
  if (temp) free(temp);
  if (myhost) free(myhost);
  if (myport) free(myport);
  if (mystream) free(mystream);
  if (myxprt) free(myxprt);
  if (myauth) free(myauth);
  if (upstreamhost) free(upstreamhost);
  if (upstreamport) free(upstreamport);
  if (upstreamstream) free(upstreamstream);
  if (upstreamxprt) free(upstreamxprt);
  if (upstreamauth) free(upstreamauth);
  if (sendon) free(sendon);

  return 0;
}

int v2(char* str, char* matchUUID, struct HostInfo* hi, char** rest){
  char *mydata = NULL;
  char *mydatacp = NULL;
  char *outersaveptr = NULL;
  char *saveptr = NULL;
  char *tok = NULL;

  int found = 0;
  int rc = 0;

  //want to write this so we dont need to repack the message
  printf("ORIG '%s'\n", str);

  mydata = strtok_r(str, ":", &outersaveptr);
  do {
    if (mydata == NULL){
      printf("mydata == NULL. breaking.\n");
      break;
    }

    mydatacp = strdup(mydata);
    printf("checking mydata = '%s' rest = '%s'\n", mydatacp, outersaveptr);

    //split mydata
    tok = strtok_r(mydatacp, "@", &saveptr);
    if (tok != NULL){
      hi->host = strdup(tok);
      printf("\t myhost='%s'\n", hi->host);
      tok = strtok_r(NULL, "@", &saveptr);
      if (tok!= NULL){
        hi->port = strdup(tok);
        printf("\t myport='%s'\n", hi->port);
        tok = strtok_r(NULL, "@", &saveptr);
        if (tok!= NULL){
          hi->stream = strdup(tok);
          printf("\t mystream='%s'\n", hi->stream);
          tok = strtok_r(NULL, "@", &saveptr);
          if (tok!= NULL){
            hi->xprt = strdup(tok);
            hi->auth = strdup(saveptr);
          } else {
            hi->xprt = strdup("sock"); //default
            hi->auth = strdup("munge"); //default
          }
        }
      }
    }

    if (mydatacp) {
      free(mydatacp);
      mydatacp = NULL;
    }

    if (!validHostInfo(hi)){
      printf("Bad data for me --- too few my fields\n");
      rc = -1;
      break;
    }

    printHostInfo(hi);

    //if I'm given something to match, see if it matches
    //if not, then just return the first one
    if (matchUUID){
      if (!strcmp(hi->stream, matchUUID)){
        found = 1;
        printf("Found so breaking\n");
        break;
      }
    } else {
      found = 1;
      printf("Did first one, so breaking\n");
      break;
    }

    freeHostInfo(hi);

    mydata = strtok_r(NULL, ":", &outersaveptr);
  } while (mydata);

  printf("broken: mydata = '%s' rest = '%s'\n", mydata, outersaveptr);

  if (!found){
    printf("I am not in the list - bad\n");
    rc = -1;
    goto out;
  } else {
    printf("I am in the list\n");
  }

  if (!outersaveptr || !strlen(outersaveptr)){
    printf("no upstream data -- I'm the end of the line and that is ok.\n");
    hi->end = 1;
    *rest = NULL;
  } else {
    printf("I am not the end strlen=%d\n", strlen(outersaveptr));
    hi->end = 0;
    *rest = strdup(outersaveptr);
  }

 out:
  if (mydatacp) free(mydatacp);

  printf("Returing from v2\n");
  return rc;

}
