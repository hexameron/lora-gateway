/* RealTime KML export of the payload data */
/* Based on https://ukhas.org.uk/using_google_earth */

#define LOG_TELEM    (1<<0)
#define LOG_KML      (1<<1)
#define LOG_RADIO    (1<<2)

void UpdatePayloadLOG(char * payload);
void UpdatePayloadKML(char * payload, unsigned int seconds, double latitude, double longitude, unsigned int altitude);
