#include <stdint.h>
char *url_encode( char *str );
void UpdatePayloadLOG( char * payload );
void base64_encode( const unsigned char *data, size_t input_length, size_t *output_length, char *encoded_data );

typedef struct {
	uint8_t data[64];
	uint32_t datalen;
	uint32_t bitlen[2];
	uint32_t state[8];
} SHA256_CTX;

#define LOG_TELEM    ( 1 << 0 )
#define LOG_RADIO    ( 1 << 2 )
