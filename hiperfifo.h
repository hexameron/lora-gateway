#include <curl/curl.h>

void curlInit();
void curlPush();
void curlClean();
void curlQueue( CURL *easy_handle );
