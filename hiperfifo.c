/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) 1998 - 2016, Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.haxx.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ***************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include "hiperfifo.h"

/* Global information, common to all connections */ 

  CURLM *multi;
  int running;
  int pending;

void curlInit()
{
	curl_global_init(CURL_GLOBAL_ALL);
	multi = curl_multi_init();
	running = 0;
	pending = 0;
}

/* Die if we get a bad CURLMcode somewhere */
void mcode_or_die(const char *where, CURLMcode code)
{
    const char *s;
    CURLMcode rc = code;

    if(CURLM_OK == code)
        return;

    if (CURLM_CALL_MULTI_PERFORM == code)
        rc = curl_multi_perform(multi, &running);
			
    switch (rc) {
      case     CURLM_BAD_HANDLE:         s="CURLM_BAD_HANDLE";         break;
      case     CURLM_BAD_EASY_HANDLE:    s="CURLM_BAD_EASY_HANDLE";    break;
      case     CURLM_OUT_OF_MEMORY:      s="CURLM_OUT_OF_MEMORY";      break;
      case     CURLM_INTERNAL_ERROR:     s="CURLM_INTERNAL_ERROR";     break;
      case     CURLM_UNKNOWN_OPTION:     s="CURLM_UNKNOWN_OPTION";     break;
      case     CURLM_LAST:               s="CURLM_LAST";               break;
      default:                           s="CURLM_unknown";            break;

      case     CURLM_BAD_SOCKET:
      case     CURLM_CALL_MULTI_PERFORM:
      case     CURLM_OK:
                        return;
    }

    printf("Curl fatal error:%s,%s\n",where,s);
    exit(code);
}

/* Check for completed transfers, and remove their easy handles */
void check_multi_info()
{
  CURLMsg *msg;
  int msgs_left;
  CURL *easy;
  CURLcode res;

  while((msg = curl_multi_info_read(multi, &msgs_left))) {
    if(msg->msg == CURLMSG_DONE) {
      easy = msg->easy_handle;
      res = msg->data.result;
      mcode_or_die("check_multi_info: curl_multi_socket_action", res);

      curl_multi_remove_handle(multi, easy);
      curl_easy_cleanup(easy);
    }
  }
}


/* Add a new easy handle to the global curl_multi */
void curlQueue(CURL *easy_handle )
{
  CURLMcode rc;

  if(!easy_handle)
	return;

  // drop messeges if the queue is blocked
  if(running > 64) {
	curl_easy_cleanup(easy_handle);
	return;
  }

  rc = curl_multi_add_handle(multi, easy_handle);
  mcode_or_die("curlQueue: curl_multi_add_handle", rc);
  pending++;
}

void curlPush()
{
	CURLMcode rc;

	// clear completed handles
	check_multi_info();

	if (!pending)
		return;

	// start new handles
	rc = curl_multi_perform(multi, &running);
	mcode_or_die("curlPush: curl_multi_perform", rc);
	pending = 0;
}


/* this, of course, won't get called since the only way to stop this program is
	     via ctrl-C, but it is here to show how cleanup /would/ be done. */
void curlClean()
{
  curl_multi_cleanup(multi);
  curl_global_cleanup();
}
