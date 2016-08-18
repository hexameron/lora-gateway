#include <curl/curl.h>

struct curl_slist *slist_headers;

void curlInit();
void curlPush();
void curlClean();
void curlQueue( CURL *easy_handle );
