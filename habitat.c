#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdio.h>      // Standard input/output definitions
#include <string.h>     // String function definitions
#include <unistd.h>     // UNIX standard function definitions
#include <fcntl.h>      // File control definitions
#include <errno.h>      // Error number definitions
#include <termios.h>    // POSIX terminal control definitions
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <dirent.h>
#include <math.h>
#include <time.h>
#include <curl/curl.h>

#include "utils.h"
#include "global.h"
#include "hiperfifo.h"

size_t habitat_write_data( void *buffer, size_t size, size_t nmemb, void *userp ) {
	// LogMessage("%s\n", (char *)buffer);
	return size * nmemb;
}

void hash_to_hex( unsigned char *hash, char *line ) {
	int idx;

	for ( idx = 0; idx < 32; idx++ )
	{
		sprintf( &( line[idx * 2] ), "%02x", hash[idx] );
	}
	line[64] = '\0';

	// LogMessage(line);
}

void UploadTelemetryPacket( char *Telemetry ) {
	CURL *curl;

	if ( !Config.EnableHabitat ) {
		return;
	}

	if ( strlen( Telemetry ) > 120 ) {
		return;
	}

	/* get a curl handle */
	curl = curl_easy_init();
	if ( curl ) {
#if 0
		char url[200];
		char base64_data[1000];
		size_t base64_length;
		SHA256_CTX ctx;
		unsigned char hash[32];
		char doc_id[68];
		char json[1000], now[32];
		struct curl_slist *headers = NULL;
		time_t rawtime;
		struct tm *tm;

		// Get formatted timestamp
		time( &rawtime );
		tm = gmtime( &rawtime );
		strftime( now, sizeof( now ), "%Y-%0m-%0dT%H:%M:%SZ", tm );

		// So that the response to the curl PUT doesn't mess up my finely crafted display!
		curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, habitat_write_data );

		// Set the timeout
		curl_easy_setopt( curl, CURLOPT_TIMEOUT, 20 );

		// Avoid curl library bug that happens if above timeout occurs (sigh)
		curl_easy_setopt( curl, CURLOPT_NOSIGNAL, 1 );

		// Convert sentence to base64
		base64_encode( Telemetry, strlen( Telemetry ), &base64_length, base64_data );
		base64_data[base64_length] = '\0';

		// Take SHA256 hash of the base64 version and express as hex.  This will be the document ID
		sha256_init( &ctx );
		sha256_update( &ctx, (uint8_t *)base64_data, base64_length );
		sha256_final( &ctx, (uint8_t *)hash );
		hash_to_hex( hash, doc_id );

		// Create json with the base64 data in hex, the tracker callsign and the current timestamp
		snprintf( json, sizeof( json ),
				  "{\"data\": {\"_raw\": \"%s\"},\"receivers\": {\"%s\": {\"time_created\": \"%s\",\"time_uploaded\": \"%s\"}}}",
				  base64_data,
				  Config.Tracker,
				  now,
				  now );
		// printf("\njson:%s\n",json);

		// Set the URL that is about to receive our PUT
		snprintf( url, sizeof( url ), "http://habitat.habhub.org/habitat/_design/payload_telemetry/_update/add_listener/%s", doc_id );

		// printf("\nurl :%s\n",url);
		// Set the headers
		headers = NULL;
		headers = curl_slist_append( headers, "Accept: application/json" );
		headers = curl_slist_append( headers, "Content-Type: application/json" );
		headers = curl_slist_append( headers, "charsets: utf-8" );

		// PUT to http://habitat.habhub.org/habitat/_design/payload_telemetry/_update/add_listener/<doc_id>
		//	with content-type application/json
		curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
		curl_easy_setopt( curl, CURLOPT_URL, url );
		curl_easy_setopt( curl, CURLOPT_CUSTOMREQUEST, "PUT" );
		curl_easy_setopt( curl, CURLOPT_COPYPOSTFIELDS, json );
#else
		char post[256];

		curl_easy_setopt( curl, CURLOPT_TIMEOUT, 20 );
		curl_easy_setopt( curl, CURLOPT_NOSIGNAL, 1 );
		curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, habitat_write_data );
		curl_easy_setopt( curl, CURLOPT_URL, "http://habitat.habhub.org/transition/payload_telemetry" );
		snprintf( post, sizeof(post), "callsign=%s&string=%s&string_type=ascii&metadata={}", Config.Tracker, Telemetry );
		curl_easy_setopt( curl, CURLOPT_COPYPOSTFIELDS, post );
#endif
		// cleanup later
		curlQueue( curl );
	}

}
