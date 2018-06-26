#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <stdarg.h>
#include <curses.h>
#include <math.h>

#include <wiringPi.h>
#include <wiringPiSPI.h>

#include "hiperfifo.h"
#include "utils.h"
#include "global.h"

// RFM98
#define REG_FIFO                    0x00
#define REG_OPMODE                  0x01
#define REG_FREQ                    0x06
#define REG_FIFO_ADDR_PTR           0x0D
#define REG_FIFO_TX_BASE_AD         0x0E
#define REG_FIFO_RX_BASE_AD         0x0F
#define REG_RX_NB_BYTES             0x13
#define REG_FIFO_RX_CURRENT_ADDR    0x10
#define REG_IRQ_FLAGS               0x12
#define REG_PACKET_SNR              0x19
#define REG_PACKET_RSSI             0x1A
#define REG_CURRENT_RSSI            0x1B
#define REG_DIO_MAPPING_1           0x40
#define REG_DIO_MAPPING_2           0x41
#define REG_MODEM_CONFIG            0x1D
#define REG_MODEM_CONFIG2           0x1E
#define REG_MODEM_CONFIG3           0x26
#define REG_PAYLOAD_LENGTH          0x22
#define REG_IRQ_FLAGS_MASK          0x11
#define REG_HOP_PERIOD              0x24
#define REG_FREQ_ERROR              0x28
#define REG_DETECT_OPT              0x31
#define REG_DETECTION_THRESHOLD     0x37
#define REG_SYNC		    0x39

// sync = 0x12-default, 0x34-lorawan, 00-fossasat
#define SYNC_VAL			0x12

// FSK Settings
#define REG_BITRATEH                0x02
#define REG_BITRATEL                0x03
#define REG_DEVH                    0x04
#define REG_DEVL                    0x05
#define REG_RX_CONF                 0x0D
#define REG_RSSI_CONF               0x0E
#define REG_RSSI_FLOOR              0x10
#define REG_FSK_RSSI                0x11
#define REG_RX_BW                   0x12
#define REG_AFC_BW                  0x13
#define REG_START_AGC               0x1A
#define REG_PREAMBLE                0x1F
#define REG_SYNC_CONF               0x27
#define REG_SYNC_1                  0x28
#define REG_SYNC_2                  0x29
#define REG_CONF_CRC                0x30
#define REG_CONF_2                  0x31
#define REG_PAYLOAD_LENGTH_FSK      0x32
#define REG_FSK_IRQ1                0x3E
#define REG_FSK_IRQ2                0x3F


// MODES
#define RF96_MODE_RX_CONTINUOUS     0x85
#define RF96_MODE_SLEEP             0x80
#define RF96_MODE_STANDBY           0x81
#define RF69_MODE_RX_CONTINUOUS     0x05
#define RF69_MODE_SLEEP             0x00
#define RF69_MODE_STANDBY           0x01


#define PAYLOAD_LENGTH              255

// Modem Config 1
#define EXPLICIT_MODE               0x00
#define IMPLICIT_MODE               0x01

#define ERROR_CODING_4_5            0x02
#define ERROR_CODING_4_6            0x04
#define ERROR_CODING_4_7            0x06
#define ERROR_CODING_4_8            0x08

#define BANDWIDTH_7K8               0x00
#define BANDWIDTH_10K4              0x10
#define BANDWIDTH_15K6              0x20
#define BANDWIDTH_20K8              0x30
#define BANDWIDTH_31K25             0x40
#define BANDWIDTH_41K7              0x50
#define BANDWIDTH_62K5              0x60
#define BANDWIDTH_125K              0x70
#define BANDWIDTH_250K              0x80
#define BANDWIDTH_500K              0x90

// Modem Config 2

#define SPREADING_6                 0x60
#define SPREADING_7                 0x70
#define SPREADING_8                 0x80
#define SPREADING_9                 0x90
#define SPREADING_10                0xA0
#define SPREADING_11                0xB0
#define SPREADING_12                0xC0

#define CRC_OFF                     0x00
#define CRC_ON                      0x04

// POWER AMPLIFIER CONFIG
#define REG_PA_CONFIG               0x09
#define PA_MAX_BOOST                0x8F
#define PA_LOW_BOOST                0x81
#define PA_MED_BOOST                0x8A
#define PA_OFF_BOOST                0x00
#define RFO_MIN                     0x00

// LOW NOISE AMPLIFIER
#define REG_LNA                     0x0C
#define LNA_MAX_GAIN                0x23  // 0010 0011
#define LNA_OFF_GAIN                0x00
#define LNA_LOW_GAIN                0xC0  // 1100 0000

#define RSSI_OFFSET 50
//#define RSSI_OFFSET 164
//#define RSSI_OFFSET 157

const char *Modes[7] = {"Slow", "SSDV", "Repeat", "Turbo", "TurboX", "Call", "2FSK"};
#define MODE_FSK (6)

struct TConfig Config;
struct TPayload Payloads[16];

void writeRegister( int Channel, uint8_t reg, uint8_t val ) {
	unsigned char data[2];

	data[0] = reg | 0x80;
	data[1] = val;
	wiringPiSPIDataRW( Channel, data, 2 );
}

uint8_t readRegister( int Channel, uint8_t reg ) {
	unsigned char data[2];
	uint8_t val;

	data[0] = reg & 0x7F;
	data[1] = 0;
	wiringPiSPIDataRW( Channel, data, 2 );
	val = data[1];

	return val;
}


void LogMessage( const char *format, ... ) {
	static WINDOW *Window = NULL;
	char Buffer[200];

	if ( Window == NULL ) {
		// Window = newwin(25, 30, 0, 50);
		Window = newwin( 12, 99, 14, 0 );
		scrollok( Window, TRUE );
	}

	va_list args;
	va_start( args, format );

	vsnprintf( Buffer, 159, format, args );

	va_end( args );

	waddstr( Window, Buffer );

	wrefresh( Window );
}

void ChannelPrintf( int Channel, int row, int column, const char *format, ... ) {
	char Buffer[80];

	va_list args;
	va_start( args, format );

	vsnprintf( Buffer, 40, format, args );

	va_end( args );

	mvwaddstr( Config.LoRaDevices[Channel].Window, row, column, Buffer );

	wrefresh( Config.LoRaDevices[Channel].Window );
}

void setMode( int Channel, uint8_t newMode ) {
	switch ( newMode )
	{
	case RF69_MODE_RX_CONTINUOUS:	
	case RF96_MODE_RX_CONTINUOUS:
	case RF69_MODE_SLEEP:
	case RF96_MODE_SLEEP:
	case RF69_MODE_STANDBY:
	case RF96_MODE_STANDBY:
		break;
	default:
		return;
	}
	writeRegister( Channel, REG_OPMODE, newMode );
	// LogMessage("Channel %d changing to Mode %d\n", Channel, newMode);
	usleep( 50000 );
}


void setChipMode( int Channel, int lora ) {
	double Frequency;
	unsigned long FrequencyValue;

	// Need to be in sleep mode BEFORE changing modulation 
	if (lora) {
		setMode( Channel, RF96_MODE_SLEEP );
		setMode( Channel, RF96_MODE_SLEEP );
		setMode( Channel, RF96_MODE_SLEEP );
	} else {
		setMode( Channel, RF69_MODE_SLEEP );
		setMode( Channel, RF69_MODE_SLEEP );
		setMode( Channel, RF69_MODE_SLEEP );
	}

	if ( sscanf( Config.LoRaDevices[Channel].Frequency, "%lf", &Frequency ) ) {
		FrequencyValue = (unsigned long)( Frequency * 7110656 / 434 );
		writeRegister( Channel, REG_FREQ,   ( FrequencyValue >> 16 ) & 0xFF );
		writeRegister( Channel, REG_FREQ + 1, ( FrequencyValue >> 8 ) & 0xFF );
		writeRegister( Channel, REG_FREQ + 2, FrequencyValue & 0xFF );
	}
}

/*
 *	* Setup to receive continuously *
 */
void startReceiving( int Channel ) {
	writeRegister( Channel, REG_MODEM_CONFIG, Config.LoRaDevices[Channel].ImplicitOrExplicit
						| Config.LoRaDevices[Channel].ErrorCoding
						| Config.LoRaDevices[Channel].Bandwidth );
	writeRegister( Channel, REG_MODEM_CONFIG2, Config.LoRaDevices[Channel].SpreadingFactor | CRC_ON );
	writeRegister( Channel, REG_MODEM_CONFIG3, 0x04 | Config.LoRaDevices[Channel].LowDataRateOptimize );
													// 0x04: AGC sets LNA gain
	writeRegister( Channel, REG_DETECT_OPT, ( readRegister( Channel, REG_DETECT_OPT ) & 0xF8 )
						| ( ( Config.LoRaDevices[Channel].SpreadingFactor == SPREADING_6 ) ? 0x05 : 0x03 ) );
													// 0x05 For SF6; 0x03 otherwise
	writeRegister( Channel, REG_DETECTION_THRESHOLD, ( Config.LoRaDevices[Channel].SpreadingFactor == SPREADING_6 ) ? 0x0C : 0x0A );
													// 0x0C for SF6, 0x0A otherwise
													// 0x04: AGC sets LNA gain
	writeRegister( Channel, REG_SYNC, SYNC_VAL ); // !! breaks everything
	
	writeRegister( Channel, REG_PAYLOAD_LENGTH, Config.LoRaDevices[Channel].PayloadLength );
	writeRegister( Channel, REG_RX_NB_BYTES, Config.LoRaDevices[Channel].PayloadLength );

	writeRegister( Channel, REG_FIFO_RX_BASE_AD, 0 );
	writeRegister( Channel, REG_FIFO_ADDR_PTR, 0 );

	writeRegister( Channel, REG_PA_CONFIG, PA_OFF_BOOST );
	writeRegister( Channel, REG_LNA, LNA_MAX_GAIN );

	writeRegister( Channel, REG_IRQ_FLAGS, 0xFF );
	setMode( Channel, RF96_MODE_RX_CONTINUOUS );
}

#define FSK_DATABYTES (16)
void hab2fsk( int Channel ) {
	// 600 Hz 2FSK
	setChipMode( Channel, 0 );
	Config.LoRaDevices[Channel].packet_rssi = 0;

	writeRegister( Channel, REG_CONF_CRC,	0x00);	// fixed length, no whitening, CRC off, no addressing
	writeRegister( Channel, REG_CONF_2,	0x40);	// packet mode
	//  writeRegister(REG_BITRATEH,		0xFA);	// 32MHz / 256 * 250  => 500 Hz
	writeRegister( Channel, REG_BITRATEH,	0xD0);  // 600 Hz
	writeRegister( Channel, REG_BITRATEL,	0x35);	// - but not exactly
	writeRegister( Channel, REG_DEVL,	0x08);	// 7 * 120 Hz => 840 Hz
	writeRegister( Channel, REG_PAYLOAD_LENGTH_FSK, FSK_DATABYTES);
	writeRegister( Channel, REG_SYNC_CONF,	0x73);	// // restart,4 sync+on, preamble 5555
	writeRegister( Channel, REG_SYNC_1 + 0,	0x96);	// horus sync
	writeRegister( Channel, REG_SYNC_1 + 1,	0x69);	// horus sync
	writeRegister( Channel, REG_SYNC_1 + 2,	0x69);	// horus sync
	writeRegister( Channel, REG_SYNC_1 + 3,	0x96);	// horus sync

	writeRegister( Channel, REG_RX_BW,	0x06 );   //  8kHz filter
	writeRegister( Channel, REG_AFC_BW,	0x06 );   //  8kHz filter

	writeRegister( Channel, REG_PREAMBLE,	0xA8);
	writeRegister( Channel, REG_RX_CONF,	0x1E);

	writeRegister( Channel, REG_FSK_IRQ1, 0xFF );
	writeRegister( Channel, REG_FSK_IRQ2, 0xFF );	// Clear all interrupts
	setMode( Channel, RF69_MODE_RX_CONTINUOUS );
	writeRegister( Channel, REG_START_AGC, 0x10 ); // Trigger AGC setting
}

void setupRFM98( int Channel ) {
	if ( Config.LoRaDevices[Channel].InUse ) {
		// initialize the pins
		pinMode( Config.LoRaDevices[Channel].DIO0, INPUT );
		pinMode( Config.LoRaDevices[Channel].DIO5, INPUT );

		if ( wiringPiSPISetup( Channel, 500000 ) < 0 ) {
			fprintf( stderr, "Failed to open SPI port.  Try loading spi in raspi-config" );
			exit( 1 );
		}
		if ( Config.LoRaDevices[Channel].SpeedMode == MODE_FSK ) {
			hab2fsk( Channel );
			return;
		}
		// LoRa mode
		setChipMode( Channel, 1 );
		startReceiving( Channel );
	}
}

double FrequencyError( int Channel, bool retune ) {
	double Offset;
	int32_t Frequency, Temp;

	Temp = (int32_t)readRegister( Channel, REG_FREQ_ERROR ) & 7;
	Temp <<= 8L;
	Temp += (int32_t)readRegister( Channel, REG_FREQ_ERROR + 1 );
	Temp <<= 8L;
	Temp += (int32_t)readRegister( Channel, REG_FREQ_ERROR + 2 );

	if ( readRegister( Channel, REG_FREQ_ERROR ) & 8 ) {
		Temp = Temp - 524288;
	}
	Offset = -( (double)Temp * ( 1 << 24 ) / 32000000.0 ) * ( Config.LoRaDevices[Channel].Reference / 500000.0 );

	// Avoid unnecessary changes
	if ( retune && (( Offset > +900.0 ) || ( Offset < -900.0 )) ) {
		Temp = (int32_t)readRegister( Channel, REG_FREQ );
		Temp <<= 8;
		Temp += (int32_t)readRegister( Channel, REG_FREQ + 1 );
		Temp <<= 8;
		Temp += (int32_t)readRegister( Channel, REG_FREQ + 2 );

		// Frequency is not updated until AFTER rfm98 receives next packet.
		Frequency = Temp + (int32_t)( 0.005 * Offset );
		writeRegister( Channel, REG_FREQ,   ( Frequency >> 16 ) & 0xff );
		writeRegister( Channel, REG_FREQ + 1, ( Frequency >> 8 ) & 0xff );
		writeRegister( Channel, REG_FREQ + 2, ( Frequency >> 0 ) & 0xff );
	}
	return Offset;
}

void ProcessCallingMessage( int Channel, char *Message ) {
	char Payload[16];
	double Frequency;
	int ImplicitOrExplicit, ErrorCoding, Bandwidth, SpreadingFactor, LowDataRateOptimize;
	int freqSet;

	if ( sscanf( Message, "%15[^,],%lf,%d,%d,%d,%d,%d",
				 Payload,
				 &Frequency,
				 &ImplicitOrExplicit,
				 &ErrorCoding,
				 &Bandwidth,
				 &SpreadingFactor,
				 &LowDataRateOptimize ) == 7 ) {
		setMode( Channel, RF96_MODE_SLEEP );

		Frequency -= FrequencyError( Channel, false );
		freqSet = (int)( Frequency * 7110656 / 434 );
		writeRegister( Channel, REG_FREQ,   ( freqSet >> 16 ) & 0xff );
		writeRegister( Channel, REG_FREQ + 1, ( freqSet >> 8 ) & 0xff );
		writeRegister( Channel, REG_FREQ + 2, ( freqSet ) & 0xff );

		writeRegister( Channel, REG_MODEM_CONFIG, ImplicitOrExplicit | ErrorCoding | Bandwidth );
		writeRegister( Channel, REG_MODEM_CONFIG2, SpreadingFactor | CRC_ON );
		writeRegister( Channel, REG_MODEM_CONFIG3, 0x04 | LowDataRateOptimize );
		writeRegister( Channel, REG_DETECT_OPT, ( readRegister( Channel, REG_DETECT_OPT ) & 0xF8 )
					   | ( ( SpreadingFactor == SPREADING_6 ) ? 0x05 : 0x03 ) );
		writeRegister( Channel, REG_DETECTION_THRESHOLD, ( SpreadingFactor == SPREADING_6 ) ? 0x0C : 0x0A );
		Config.LoRaDevices[Channel].Reference = Bandwidth;

		setMode( Channel, RF96_MODE_RX_CONTINUOUS );
	}
}


/* Critical section - concurrent use for wiringPi interrupts */
int receiveMessage( int Channel, char *message ) {
	int i, currentAddr, x;
	unsigned char data[257];
	int snr, rssi, Bytes;

	x = snr = rssi = Bytes = 0;

	if ( Config.LoRaDevices[Channel].SpeedMode != MODE_FSK ) {
		rssi = (int)(uint8_t)readRegister( Channel, REG_PACKET_RSSI ) - RSSI_OFFSET;
		x = readRegister( Channel, REG_IRQ_FLAGS );
		writeRegister( Channel, REG_IRQ_FLAGS, 0x40 );

		Config.LoRaDevices[Channel].packet_rssi = rssi;

		// check for payload crc fail
		if ( ( x & 0x20 ) == 0x20 ) {
		writeRegister( Channel, REG_IRQ_FLAGS, 0xFF );
		Config.LoRaDevices[Channel].BadCRCCount++;
		Config.LoRaDevices[Channel].freq_offset = (int)FrequencyError( Channel, false );
		return 0;
		}

		currentAddr = readRegister( Channel, REG_FIFO_RX_CURRENT_ADDR );
		Bytes = readRegister( Channel, REG_RX_NB_BYTES );
		writeRegister( Channel, REG_FIFO_ADDR_PTR, currentAddr );
	} else { // if  (MODE_FSK)
		Bytes = FSK_DATABYTES;
		writeRegister( Channel, REG_FSK_IRQ2, 0x40 );
	}

	data[0] = REG_FIFO;
	wiringPiSPIDataRW( Channel, data, Bytes + 1 );
	for ( i = 0; i <= Bytes; i++ )
			message[i] = data[i + 1];
	message[Bytes] = '\0';

	if ( Config.LoRaDevices[Channel].SpeedMode != MODE_FSK ) {
		//  For logging, try to grab base rssi between packets
		snr = ( (int8_t)readRegister( Channel, REG_PACKET_SNR ) ) / 4;
		rssi = (int)(uint8_t)readRegister( Channel, REG_CURRENT_RSSI ) - RSSI_OFFSET;
		Config.LoRaDevices[Channel].base_rssi = rssi;
		Config.LoRaDevices[Channel].freq_offset = (int)FrequencyError( Channel,
						( Config.LoRaDevices[Channel].Bandwidth <= BANDWIDTH_41K7 ) );

		//  Clear all flags
		writeRegister( Channel, REG_IRQ_FLAGS, 0xFF );
	}
	Config.LoRaDevices[Channel].packet_snr = snr;
	return Bytes;
}
/* Critical section - concurrent use for wiringPi interrupts */


static char *decode_callsign( char *callsign, uint32_t code ) {
	char *c, s;

	*callsign = '\0';

	/* Is callsign valid? */
	if ( code > 0xF423FFFF ) {
		return( callsign );
	}

	for ( c = callsign; code; c++ )
	{
		s = code % 40;
		if ( s == 0 ) {
			*c = '-';
		} else if ( s < 11 ) {
			*c = '0' + s - 1;
		} else if ( s < 14 )                                      {
			*c = '-';
		} else { *c = 'A' + s - 14; }
		code /= 40;
	}
	*c = '\0';

	return( callsign );
}

void ConvertStringToHex( char *Target, char *Source, int Length ) {
	const char Hex[16] = "0123456789ABCDEF";
	int i;

	for ( i = 0; i < Length; i++ )
	{
		*Target++ = Hex[Source[i] >> 4];
		*Target++ = Hex[Source[i] & 0x0F];
	}

	*Target++ = '\0';
}

size_t write_data( void *buffer, size_t size, size_t nmemb, void *userp ) {
	return size * nmemb;
}

unsigned queued_images = 0;
char base64_ssdv[512*8];

void UploadMultiImages() {
	CURL *curl;
	char single[1000];  // 256 * base64 + headers
	char json[8000];  // 8 * single
	unsigned PacketIndex;
	char now[32];
	time_t rawtime;
	struct tm *tm;

	if ( !queued_images )
		return;
	curl = curl_easy_init();
	if ( !curl) {
		queued_images = 0;
		return;
	}

	time(&rawtime);
	tm = gmtime(&rawtime);
	strftime(now, sizeof(now), "%Y-%0m-%0dT%H:%M:%SZ", tm);

	strcpy(json, "{\"type\": \"packets\",\"packets\":[");
	for (PacketIndex = 0; PacketIndex < queued_images; PacketIndex++) {
		snprintf(single, sizeof(single),
			"{\"type\": \"packet\", \"packet\": \"%s\", \"encoding\": \"base64\", \"received\": \"%s\", \"receiver\": \"%s\"}%s",
				&base64_ssdv[PacketIndex*512], now, Config.Tracker, (queued_images - PacketIndex == 1) ? "" : ",");
		strcat(json, single);
	}
	strcat(json, "]}");

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist_headers); 
	curl_easy_setopt(curl, CURLOPT_URL, "http://ssdv.habhub.org/api/v0/packets");  
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
	curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, json);

	curlQueue( curl );
	queued_images = 0;
}

void UploadImagePacket( char *packet ) {
	size_t base64_length;

	base64_encode(packet, 256, &base64_length, &base64_ssdv[queued_images*512]);
	base64_ssdv[base64_length + queued_images*512] = '\0';
	if ( ++queued_images >= 8 )
		UploadMultiImages(); 
}

void ReadString( FILE *fp, char *keyword, char *Result, int Length, int NeedValue ) {
	char line[100], *token, *value;

	fseek( fp, 0, SEEK_SET );
	*Result = '\0';

	while ( fgets( line, sizeof( line ), fp ) != NULL )
	{
		token = strtok( line, "= :\t" );
		if (token && (strcasecmp( keyword, token ) == 0 )) {
			value = strtok( NULL, ":= \t\n\r" );
			strcpy( Result, value );
			return;
		}
	}

	if ( NeedValue ) {
		LogMessage( "Missing value for '%s' in configuration file\n", keyword );
		exit( 1 );
	}
}

int ReadInteger( FILE *fp, char *keyword, int NeedValue, int DefaultValue ) {
	char Temp[64];

	ReadString( fp, keyword, Temp, sizeof( Temp ), NeedValue );

	if ( Temp[0] ) {
		return atoi( Temp );
	}

	return DefaultValue;
}

int ReadBoolean( FILE *fp, char *keyword, int NeedValue, int *Result ) {
	char Temp[32];

	ReadString( fp, keyword, Temp, sizeof( Temp ), NeedValue );

	if ( *Temp ) {
		*Result = ( *Temp == '1' ) || ( *Temp == 'Y' ) || ( *Temp == 'y' ) || ( *Temp == 't' ) || ( *Temp == 'T' );
	}

	return *Temp;
}

void LoadConfigFile() {
	FILE *fp;
	char *filename = "gateway.txt";
	char Keyword[32];
	int Channel, Temp;
	char TempString[16];

	Config.EnableHabitat = 1;
	Config.EnableSSDV = 1;
	Config.ftpServer[0] = '\0';
	Config.ftpUser[0] = '\0';
	Config.ftpPassword[0] = '\0';
	Config.ftpFolder[0] = '\0';
	Config.LogLevel = 0;
	Config.myLat = 52.0;
	Config.myLon = -2.0;
	Config.myAlt = 99.0;

	if ( ( fp = fopen( filename, "r" ) ) == NULL ) {
		printf( "\nFailed to open config file %s (error %d - %s).\nPlease check that it exists and has read permission.\n", filename, errno, strerror( errno ) );
		exit( 1 );
	}

	ReadString( fp, "tracker", Config.Tracker, sizeof( Config.Tracker ), 1 );
	LogMessage( "Tracker = '%s'\n", Config.Tracker );

	ReadBoolean( fp, "EnableHabitat", 0, &Config.EnableHabitat );
	ReadBoolean( fp, "EnableSSDV", 0, &Config.EnableSSDV );

	ReadString( fp, "ftpserver", Config.ftpServer, sizeof( Config.ftpServer ), 0 );
	ReadString( fp, "ftpUser", Config.ftpUser, sizeof( Config.ftpUser ), 0 );
	ReadString( fp, "ftpPassword", Config.ftpPassword, sizeof( Config.ftpPassword ), 0 );
	ReadString( fp, "ftpFolder", Config.ftpFolder, sizeof( Config.ftpFolder ), 0 );

	Config.LogLevel = ReadInteger( fp, "LogLevel", 0, 0 );

	sprintf( Keyword, "52" );
	ReadString( fp, "Latitude", Keyword, sizeof( Keyword ), 0 );
	sscanf( Keyword, "%lf", &Config.myLat );
	sprintf( Keyword, "-2" );
	ReadString( fp, "Longitude", Keyword, sizeof( Keyword ), 0 );
	sscanf( Keyword, "%lf", &Config.myLon );
	sprintf( Keyword, "99" );
	ReadString( fp, "Altitude", Keyword, sizeof( Keyword ), 0 );
	sscanf( Keyword, "%lf", &Config.myAlt );
	LogMessage( "Location: %lf, %lf, %lf\n", Config.myLat, Config.myLon, Config.myAlt );

	for ( Channel = 0; Channel <= 1; Channel++ )
	{
		// Defaults
		Config.LoRaDevices[Channel].Frequency[0] = '\0';

		sprintf( Keyword, "frequency_%d", Channel );
		ReadString( fp, Keyword, Config.LoRaDevices[Channel].Frequency, sizeof( Config.LoRaDevices[Channel].Frequency ), 0 );
		if ( Config.LoRaDevices[Channel].Frequency[0] ) {
			Config.LoRaDevices[Channel].ImplicitOrExplicit = EXPLICIT_MODE;
			Config.LoRaDevices[Channel].ErrorCoding = ERROR_CODING_4_8;
			Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_20K8;
			Config.LoRaDevices[Channel].SpreadingFactor = SPREADING_11;
			Config.LoRaDevices[Channel].LowDataRateOptimize = 0x00;
			Config.LoRaDevices[Channel].InUse = 1;

			// DIO0 / DIO5 overrides
			sprintf( Keyword, "DIO0_%d", Channel );
			Config.LoRaDevices[Channel].DIO0 = ReadInteger( fp, Keyword, 0, Config.LoRaDevices[Channel].DIO0 );
			sprintf( Keyword, "DIO5_%d", Channel );
			Config.LoRaDevices[Channel].DIO5 = ReadInteger( fp, Keyword, 0, Config.LoRaDevices[Channel].DIO5 );

			Config.LoRaDevices[Channel].SpeedMode = 0;
			sprintf( Keyword, "mode_%d", Channel );
			Config.LoRaDevices[Channel].SpeedMode = ReadInteger( fp, Keyword, 0, 0 );
			if (Config.LoRaDevices[Channel].SpeedMode < 0)
				Config.LoRaDevices[Channel].SpeedMode = 0;
			if (Config.LoRaDevices[Channel].SpeedMode > MODE_FSK)
				Config.LoRaDevices[Channel].SpeedMode = MODE_FSK;

			LogMessage( "LoRa Channel %d DIO0=%d DIO5=%d Presets:%s on %s MHz\n", Channel, Config.LoRaDevices[Channel].DIO0
										, Config.LoRaDevices[Channel].DIO5
										, Modes[Config.LoRaDevices[Channel].SpeedMode]
										, Config.LoRaDevices[Channel].Frequency );

			Config.LoRaDevices[Channel].PayloadLength = 255;
			ChannelPrintf( Channel, 0, 1, "Channel %d %sMHz %s mode", Channel, Config.LoRaDevices[Channel].Frequency, Modes[Config.LoRaDevices[Channel].SpeedMode] );

			if ( Config.LoRaDevices[Channel].SpeedMode == MODE_FSK ) {
				//UKHAS
				continue;
			} else if ( Config.LoRaDevices[Channel].SpeedMode == 5 )    {
				// Calling mode
				Config.LoRaDevices[Channel].ImplicitOrExplicit = EXPLICIT_MODE;
				Config.LoRaDevices[Channel].ErrorCoding = ERROR_CODING_4_8;
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_41K7;
				Config.LoRaDevices[Channel].SpreadingFactor = SPREADING_11;
				Config.LoRaDevices[Channel].LowDataRateOptimize = 0;
			} else if ( Config.LoRaDevices[Channel].SpeedMode == 4 )     {
				// Turbo 868 Mode
				Config.LoRaDevices[Channel].ImplicitOrExplicit = IMPLICIT_MODE;
				Config.LoRaDevices[Channel].ErrorCoding = ERROR_CODING_4_5;
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_250K;
				Config.LoRaDevices[Channel].SpreadingFactor = SPREADING_6;
				Config.LoRaDevices[Channel].LowDataRateOptimize = 0;
			} else if ( Config.LoRaDevices[Channel].SpeedMode == 3 )     {
				// Normal mode for high speed images in 868MHz band
				Config.LoRaDevices[Channel].ImplicitOrExplicit = EXPLICIT_MODE;
				Config.LoRaDevices[Channel].ErrorCoding = ERROR_CODING_4_6;
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_250K;
				Config.LoRaDevices[Channel].SpreadingFactor = SPREADING_7;
				Config.LoRaDevices[Channel].LowDataRateOptimize = 0;
			} else if ( Config.LoRaDevices[Channel].SpeedMode == 2 )     {
				// Normal mode for repeater network
				Config.LoRaDevices[Channel].ImplicitOrExplicit = EXPLICIT_MODE;
				Config.LoRaDevices[Channel].ErrorCoding = ERROR_CODING_4_8;
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_62K5;
				Config.LoRaDevices[Channel].SpreadingFactor = SPREADING_8;
				Config.LoRaDevices[Channel].LowDataRateOptimize = 0x00;
			} else if ( Config.LoRaDevices[Channel].SpeedMode == 1 )     {
				// Normal mode for SSDV
				Config.LoRaDevices[Channel].ImplicitOrExplicit = IMPLICIT_MODE;
				Config.LoRaDevices[Channel].ErrorCoding = ERROR_CODING_4_5;
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_20K8;
				Config.LoRaDevices[Channel].SpreadingFactor = SPREADING_6;
				Config.LoRaDevices[Channel].LowDataRateOptimize = 0;
			} else
			{
				Config.LoRaDevices[Channel].ImplicitOrExplicit = EXPLICIT_MODE;
				Config.LoRaDevices[Channel].ErrorCoding = ERROR_CODING_4_8;
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_20K8;
				Config.LoRaDevices[Channel].SpreadingFactor = SPREADING_11;
				Config.LoRaDevices[Channel].LowDataRateOptimize = 0x08;
			}

			sprintf( Keyword, "sf_%d", Channel );
			Temp = ReadInteger( fp, Keyword, 0, 0 );
			if ( ( Temp >= 6 ) && ( Temp <= 12 ) ) {
				Config.LoRaDevices[Channel].SpreadingFactor = Temp << 4;
				LogMessage( "Setting SF=%d; ", Temp );
			}

			sprintf( Keyword, "bandwidth_%d", Channel );
			ReadString( fp, Keyword, TempString, sizeof( TempString ), 0 );
			if ( *TempString ) {
				LogMessage( "Setting BW=%s; ", TempString );
			}
			if ( strcmp( TempString, "7K8" ) == 0 ) {
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_7K8;
			} else if ( strcmp( TempString, "10K4" ) == 0 )       {
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_10K4;
			} else if ( strcmp( TempString, "15K6" ) == 0 )       {
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_15K6;
			} else if ( strcmp( TempString, "20K8" ) == 0 )       {
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_20K8;
			} else if ( strcmp( TempString, "31K25" ) == 0 )       {
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_31K25;
			} else if ( strcmp( TempString, "41K7" ) == 0 )       {
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_41K7;
			} else if ( strcmp( TempString, "62K5" ) == 0 )       {
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_62K5;
			} else if ( strcmp( TempString, "125K" ) == 0 )       {
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_125K;
			} else if ( strcmp( TempString, "250K" ) == 0 )       {
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_250K;
			} else if ( strcmp( TempString, "500K" ) == 0 )       {
				Config.LoRaDevices[Channel].Bandwidth = BANDWIDTH_500K;
			}

			switch ( Config.LoRaDevices[Channel].Bandwidth )
			{
			case  BANDWIDTH_7K8:    Config.LoRaDevices[Channel].Reference = 7800; break;
			case  BANDWIDTH_10K4:   Config.LoRaDevices[Channel].Reference = 10400; break;
			case  BANDWIDTH_15K6:   Config.LoRaDevices[Channel].Reference = 15600; break;
			case  BANDWIDTH_20K8:   Config.LoRaDevices[Channel].Reference = 20800; break;
			case  BANDWIDTH_31K25:  Config.LoRaDevices[Channel].Reference = 31250; break;
			case  BANDWIDTH_41K7:   Config.LoRaDevices[Channel].Reference = 41700; break;
			case  BANDWIDTH_62K5:   Config.LoRaDevices[Channel].Reference = 62500; break;
			case  BANDWIDTH_125K:   Config.LoRaDevices[Channel].Reference = 125000; break;
			case  BANDWIDTH_250K:   Config.LoRaDevices[Channel].Reference = 250000; break;
			case  BANDWIDTH_500K:   Config.LoRaDevices[Channel].Reference = 500000; break;
			}

			sprintf( Keyword, "implicit_%d", Channel );
			Temp = ReadInteger( fp, Keyword, 0, 0 );
			if ( Temp ) {
				Config.LoRaDevices[Channel].ImplicitOrExplicit = IMPLICIT_MODE;
				if ( Temp > 5 ) {
					Config.LoRaDevices[Channel].PayloadLength = Temp;
				} else {
					Temp = Config.LoRaDevices[Channel].PayloadLength;
				}
				LogMessage( "Implicit Length=%d; ", Temp );
			} else {
				// Unable to force Implicit off in Mode 1, so flag it up for the user
				LogMessage( "%s Mode; ", Config.LoRaDevices[Channel].ImplicitOrExplicit ? "Implicit" : "Explicit" );
			}

			sprintf( Keyword, "coding_%d", Channel );
			Temp = ReadInteger( fp, Keyword, 0, 0 );
			if ( ( Temp >= 5 ) && ( Temp <= 8 ) ) {
				Config.LoRaDevices[Channel].ErrorCoding = ( Temp - 4 ) << 1;
				LogMessage( "Error Coding=%d; ", Temp );
			}

			sprintf( Keyword, "lowopt_%d", Channel );
			if ( ReadBoolean( fp, Keyword, 0, &Temp ) ) {
				if ( Temp ) {
					Config.LoRaDevices[Channel].LowDataRateOptimize = 0x08;
				} else {
					Config.LoRaDevices[Channel].LowDataRateOptimize = 0x00;
				}
			}
			LogMessage( "LowDataRate=%s\n", ( Config.LoRaDevices[Channel].LowDataRateOptimize & 0x08 ) ? "ON" : "OFF" );
			LogMessage( "Sync word=%x (default 0x12)\n", SYNC_VAL );
		}
	}

	fclose( fp );
}

void LoadPayloadFile( int ID ) {
	FILE *fp;
	char filename[16];

	sprintf( filename, "payload_%d.txt", ID );

	if ( ( fp = fopen( filename, "r" ) ) != NULL ) {
		//LogMessage("Reading payload file %s\n", filename);
		ReadString( fp, "payload", Payloads[ID].Payload, sizeof( Payloads[ID].Payload ), 1 );
		LogMessage( "Payload %d = '%s'\n", ID, Payloads[ID].Payload );

		Payloads[ID].InUse = 1;

		fclose( fp );
	} else
	{
		strcpy( Payloads[ID].Payload, "Unknown" );
		Payloads[ID].InUse = 0;
	}
}

void LoadPayloadFiles( void ) {
	int ID;

	for ( ID = 0; ID < 16; ID++ )
	{
		LoadPayloadFile( ID );
	}
}

WINDOW * InitDisplay( void ) {
	WINDOW * mainwin;
	int Channel;

	/*  Initialize ncurses  */

	if ( ( mainwin = initscr() ) == NULL ) {
		fprintf( stderr, "Error initialising ncurses.\n" );
		exit( EXIT_FAILURE );
	}

	start_color();                    /*  Initialize colours  */

	init_pair( 1, COLOR_RED, COLOR_BLACK );
	init_pair( 2, COLOR_GREEN, COLOR_BLACK );

	color_set( 1, NULL );
	// bkgd(COLOR_PAIR(1));
	// attrset(COLOR_PAIR(1) | A_BOLD);

	// Title bar
	mvaddstr( 0, 10, " LoRa Habitat and SSDV Gateway by daveake " );
	refresh();

	// Windows for LoRa live data
	for ( Channel = 0; Channel <= 1; Channel++ )
	{
		Config.LoRaDevices[Channel].Window = newwin( 13, 34, 1, Channel ? 34 : 0 );
		wbkgd( Config.LoRaDevices[Channel].Window, COLOR_PAIR( 2 ) );

		// wcolor_set(Config.LoRaDevices[Channel].Window, 2, NULL);
		// waddstr(Config.LoRaDevices[Channel].Window, "WINDOW");
		// mvwaddstr(Config.LoRaDevices[Channel].Window, 0, 0, "Window");
		wrefresh( Config.LoRaDevices[Channel].Window );
	}

	curs_set( 0 );

	return mainwin;
}

void CloseDisplay( WINDOW * mainwin ) {
	/*  Clean up after ourselves  */
	delwin( mainwin );
	endwin();
	refresh();
}

int ProcessLine( int Channel, char *Line ) {
	int FieldCount;
	char *telem;

	if (strlen(Line) < 16)
		return 0;	

	if (Line[0] == '%') {
		// skip repeated packet
		// "%$one,,,*\n$$two,,,*\n"
		telem = strchr( Line, '\n' );
		if (!telem)
			return 0;
		if (strlen(telem) < 16)
			return 0;
		if (telem[1] != '$')
			return 0;
		telem += 3;
	} else {
		telem = &Line[2];
	}
	FieldCount = sscanf( telem, "%15[^,],%u,%8[^,],%lf,%lf,%u",
						 ( Config.LoRaDevices[Channel].Payload ),
						 &( Config.LoRaDevices[Channel].Counter ),
						 ( Config.LoRaDevices[Channel].Time ),
						 &( Config.LoRaDevices[Channel].Latitude ),
						 &( Config.LoRaDevices[Channel].Longitude ),
						 &( Config.LoRaDevices[Channel].Altitude ) );

	// HAB->HAB_status = FieldCount == 6;
	return ( FieldCount == 6 );
}

void DoPositionCalcs( int Channel ) {
	if (Config.LoRaDevices[Channel].Latitude > 1e6) {
		Config.LoRaDevices[Channel].Latitude *= 1.0e-7;
		Config.LoRaDevices[Channel].Longitude *= 1.0e-7;
	}
	ChannelPrintf( Channel, 2, 1, "%8.5lf, %8.5lf, %05u   ",
				   Config.LoRaDevices[Channel].Latitude,
				   Config.LoRaDevices[Channel].Longitude,
				   Config.LoRaDevices[Channel].Altitude );

	/* See habitat-autotracker/autotracker/earthmaths.py. */
	double c = M_PI / 180;
	double lat1, lon1, lat2, lon2, alt1, alt2;
	lat1 = Config.myLat * c;
	lon1 = Config.myLon * c;
	alt1 = Config.myAlt;
	lat2 = Config.LoRaDevices[Channel].Latitude * c;
	lon2 = Config.LoRaDevices[Channel].Longitude * c;
	alt2 = Config.LoRaDevices[Channel].Altitude;

	double radius, d_lon, sa, sb, aa, ab, angle_at_centre, // bearing,
		   ta, tb, ea, eb, elevation, distance;

	radius = 6371000.0;

	d_lon = lon2 - lon1;
	sa = cos( lat2 ) * sin( d_lon );
	sb = ( cos( lat1 ) * sin( lat2 ) ) - ( sin( lat1 ) * cos( lat2 ) * cos( d_lon ) );
	// bearing = atan2(sa, sb) * (180/M_PI);
	aa = sqrt( ( sa * sa ) + ( sb * sb ) );
	ab = ( sin( lat1 ) * sin( lat2 ) ) + ( cos( lat1 ) * cos( lat2 ) * cos( d_lon ) );
	angle_at_centre = atan2( aa, ab );

	ta = radius + alt1;
	tb = radius + alt2;
	ea = ( cos( angle_at_centre ) * tb ) - ta;
	eb = sin( angle_at_centre ) * tb;

	elevation = atan2( ea, eb ) * ( 180 / M_PI );
	Config.LoRaDevices[Channel].Elevation = elevation;
	distance = sqrt( ( ta * ta ) + ( tb * tb ) -
					 2 * tb * ta * cos( angle_at_centre ) );
	Config.LoRaDevices[Channel].Distance = distance / 1000;

	ChannelPrintf( Channel, 1, 1, "%3.1lfkm, elevation %1.1lf  ", distance / 1000, elevation );
}

int NewBoard( void ) {
	FILE *cpuFd ;
	char line [120] ;
	static int boardRev = -1 ;

	if ( boardRev < 0 ) {
		if ( ( cpuFd = fopen( "/proc/cpuinfo", "r" ) ) != NULL ) {
			while ( fgets( line, 120, cpuFd ) != NULL )
				if ( strncmp( line, "Revision", 8 ) == 0 ) {
					break ;
				}

			fclose( cpuFd ) ;

			if ( strncmp( line, "Revision", 8 ) == 0 ) {
				// printf ("RPi %s", line);
				boardRev = ( ( strstr( line, "0010" ) != NULL ) || ( strstr( line, "0012" ) != NULL ) );  // B+ or A+
			}
		}
	}

	return boardRev;
}

uint16_t CRC16( char *ptr, size_t len ) {
	uint16_t CRC, xPolynomial;
	int j;

	CRC = 0xffff;           // Seed
	xPolynomial = 0x1021;

	for (; len > 0; len-- )
	{   // For speed, repeat calculation instead of looping for each bit
		CRC ^= ( ( (unsigned int)*ptr++ ) << 8 );
		for ( j = 0; j < 8; j++ )
		{
			if ( CRC & 0x8000 ) {
				CRC = ( CRC << 1 ) ^ xPolynomial;
			} else {
				CRC <<= 1;
			}
		}
	}

	return CRC;
}


char packet[2][300];
void getPacket( int Channel ) {
	if ( digitalRead( Config.LoRaDevices[Channel].DIO0 ) ) {
		packet[Channel][0] = receiveMessage( Channel, &packet[Channel][1] );
	}
}

void gpioInterrupt0( void ) {
	getPacket( 0 );
}

void gpioInterrupt1( void ) {
	getPacket( 1 );
}

char *DoTelemetry(char *message) {
	char messlog[260];
	char *nextmess;

	if ( (message[0] != '$')&&(message[0] != '%'))
		return NULL;	
	nextmess  = strchr( message, '\n' );
	if (nextmess == NULL)
		return NULL;

	nextmess[0] = 0;
	LogMessage( "%s\n", message );
	sprintf( messlog, "%s\n", message );
	UpdatePayloadLOG( messlog );

	if (message[0] == '%' )
		message[0] = '$';
	UploadTelemetryPacket( message );

	return nextmess + 1;
}

int main( int argc, char **argv ) {
	char *Message;
	uint8_t  Channel, Bytes;
	uint32_t CallsignCode, LoopCount;
	WINDOW * mainwin;

	if ( wiringPiSetup() < 0 ) {
		fprintf( stderr, "Failed to open wiringPi\n" );
		exit( 1 );
	}

	curlInit();
	mainwin = InitDisplay();
	LogMessage( "**** LoRa Gateway by daveake ****\n" );

	LoopCount = 0;
	Config.LoRaDevices[0].InUse = 0;
	Config.LoRaDevices[1].InUse = 0;
	packet[0][0] = 0;
	packet[1][0] = 0;

	if ( NewBoard() ) {
		// For dual card.  These are for the second prototype (earlier one will need overrides)

		Config.LoRaDevices[0].DIO0 = 6;
		Config.LoRaDevices[0].DIO5 = 5;

		Config.LoRaDevices[1].DIO0 = 31;
		Config.LoRaDevices[1].DIO5 = 26;

		LogMessage( "Pi A+/B+ board\n" );
	} else
	{
		Config.LoRaDevices[0].DIO0 = 6;
		Config.LoRaDevices[0].DIO5 = 5;

		Config.LoRaDevices[1].DIO0 = 3;
		Config.LoRaDevices[1].DIO5 = 4;

		LogMessage( "Pi A/B board\n" );
	}

	LoadConfigFile();
	LoadPayloadFiles();

	for ( Channel = 0; Channel <= 1; Channel++ ) {
		setupRFM98( Channel );
		Config.LoRaDevices[Channel].LastPacketAt = time( NULL );
		if (( Config.LoRaDevices[Channel].InUse )) // && ( Config.LoRaDevices[Channel].SpeedMode != MODE_FSK ))
			wiringPiISR( Config.LoRaDevices[Channel].DIO0, INT_EDGE_RISING, Channel ? &gpioInterrupt1 : &gpioInterrupt0 );
	}

	while ( !curl_terminate )
	{
		for ( Channel = 0; Channel <= 1; Channel++ )
		{
			if ( Config.LoRaDevices[Channel].InUse ) {
				{
					Message = packet[Channel];
					Bytes = Message[0];
					Message[0] = 0;
					//if (hab2fsk) dostuff; else
					if ( Bytes > 0 ) {
						if ( Message[1] == 0xe6 ) // repeated SSDV
							Message[1] = 0x66;
						if ( Message[1] == '!' ) {
							LogMessage( "Ch %d: Uploaded message %s\n", Channel, Message + 1 );
						} else if ( Message[1] == '^' )     {
							ChannelPrintf( Channel, 3, 1, "Calling message: %d bytes    ", strlen( Message + 1 ) );
							ProcessCallingMessage( Channel, Message + 3 );
							LogMessage( "%s\n", Message + 1 );
						} else if (( Message[1] == '$' )||( Message[1] == '%' ))     {
							Config.LoRaDevices[Channel].TelemetryCount++;
							ChannelPrintf( Channel, 3, 1, "Telemetry: %d bytes          ", Bytes );

							char *nextmess = &Message[1];
							// LogMessage( "%s\n", Message + 1 );
							if ( ProcessLine( Channel, nextmess ) )	{
								DoPositionCalcs( Channel );
								char stats[100];
								sprintf( stats, "Stats:%1.1lf,%1.1lf,%d,%d,%d,%d\n",
									Config.LoRaDevices[Channel].Distance,
									Config.LoRaDevices[Channel].Elevation,
									Config.LoRaDevices[Channel].base_rssi,
									Config.LoRaDevices[Channel].packet_rssi,
									Config.LoRaDevices[Channel].packet_snr,
									Config.LoRaDevices[Channel].freq_offset );
								UpdatePayloadLOG( stats );
							}
							while (nextmess) {
								nextmess = DoTelemetry(nextmess);
							}
						} else if ( (char)0x80 == Message[1] )     {
							// Binary telemetry packet
							struct TBinaryPacket BinaryPacket;
							char Data[100], Sentence[100];

							ChannelPrintf( Channel, 3, 1, "Binary Telemetry              " );

							strcpy( Config.LoRaDevices[Channel].Payload,
									Payloads[0xf & BinaryPacket.PayloadID].Payload );
							memcpy( &BinaryPacket, &Message[1], sizeof( BinaryPacket ) );
							Config.LoRaDevices[Channel].Seconds = (unsigned long) BinaryPacket.BiSeconds * 2L;
							Config.LoRaDevices[Channel].Counter = BinaryPacket.Counter;
#if 1
							Config.LoRaDevices[Channel].Latitude = ( 1e-7 ) * BinaryPacket.Latitude.i;
							Config.LoRaDevices[Channel].Longitude = ( 1e-7 ) * BinaryPacket.Longitude.i;
#else
							Config.LoRaDevices[Channel].Latitude = (double)BinaryPacket.Latitude.f;
							Config.LoRaDevices[Channel].Longitude = (double)BinaryPacket.Longitude.f;
#endif
							Config.LoRaDevices[Channel].Altitude = BinaryPacket.Altitude;

							if ( BinaryPacket.Checksum == CRC16( &Message[1], sizeof( BinaryPacket ) - 2 ) ) {
								sprintf( Data, "%s,%u,%02d:%02d:%02d,%1.5f,%1.5f,%u,%u,%d",
										 Payloads[0xf & BinaryPacket.PayloadID].Payload,
										 BinaryPacket.Counter,
										 (int)( Config.LoRaDevices[Channel].Seconds / 3600 ),
										 (int)( ( Config.LoRaDevices[Channel].Seconds / 60 ) % 60 ),
										 (int)( Config.LoRaDevices[Channel].Seconds % 60 ),
										 Config.LoRaDevices[Channel].Latitude,
										 Config.LoRaDevices[Channel].Longitude,
										 Config.LoRaDevices[Channel].Altitude,
										 BinaryPacket.Fix,
										 BinaryPacket.Temperature );
								sprintf( Sentence, "$$%s*%04X", Data, CRC16( Data, strlen( Data ) ) );

								UploadTelemetryPacket( Sentence );
								DoPositionCalcs( Channel );
								Config.LoRaDevices[Channel].TelemetryCount++;
								sprintf( Sentence + strlen( Sentence ),
												 "\nStats:%d,%1.1lf,%1.1lf,%d,%d,%d,%d\n",
												 Channel,
												 Config.LoRaDevices[Channel].Distance,
												 Config.LoRaDevices[Channel].Elevation,
												 Config.LoRaDevices[Channel].base_rssi,
												 Config.LoRaDevices[Channel].packet_rssi,
												 Config.LoRaDevices[Channel].packet_snr,
												 Config.LoRaDevices[Channel].freq_offset );
								UpdatePayloadLOG( Sentence );
								LogMessage( "Ch %d:%s",Channel, Sentence );

							} else {
								Config.LoRaDevices[Channel].BadCRCCount++;
							}
						} else if ( (0x66 <= Message[1]) && (0x68 >= Message[1]) && (Bytes > 220) ) {
							// SSDV packet
							char Callsign[8];

							CallsignCode = Message[2]; CallsignCode <<= 8;
							CallsignCode |= Message[3]; CallsignCode <<= 8;
							CallsignCode |= Message[4]; CallsignCode <<= 8;
							CallsignCode |= Message[5];

							decode_callsign( Callsign, CallsignCode );
							Callsign[7]= 0; 

							// ImageNumber = Message[6];
							// PacketNumber = Message[8];

							LogMessage( "SSDV Packet, Callsign %s, Image %d, Packet %d\n",
										Callsign, Message[6], Message[7] * 256 + Message[8] );

							if ( Config.EnableSSDV ) {
								Message[0] = 0x55; //  add SSDV sync byte at start of  packet
								UploadImagePacket( &Message[0] );
								Message[0] = 0x00; //  also used to flag length of next packet
							}

							Config.LoRaDevices[Channel].SSDVCount++;
						} else
						{
							LogMessage( " Unknown type: %x\n", Message[1] );
							Config.LoRaDevices[Channel].UnknownCount++;
						}

						Config.LoRaDevices[Channel].LastPacketAt = time( NULL );
					}
				}

				// redraw screen every second
				if ( 0 == LoopCount ) {
					uint32_t interval;
					int	packrssi, rssimode;
					char *timescale = "s";

					interval = time( NULL ) - Config.LoRaDevices[Channel].LastPacketAt; 
					if ( interval > 99 * 60 ) {
						interval /= 60 * 60;
						timescale = "h";
					} else if ( interval > 99 ) {
						interval /= 60;
						timescale = "m";
					}
					if ( Config.LoRaDevices[Channel].SpeedMode == MODE_FSK ) {
						rssimode = (uint8_t)readRegister( Channel, REG_FSK_RSSI);
						packrssi = (  rssimode - 2 * Config.LoRaDevices[Channel].packet_rssi - 12) >> 1;
						if (packrssi < 0)
							packrssi = 0;
						writeRegister( Channel, REG_RSSI_FLOOR, (uint8_t)packrssi);
						rssimode = -(rssimode / 2);
						Config.LoRaDevices[Channel].packet_rssi = -(packrssi / 2);

						char intflags = readRegister( Channel, REG_FSK_IRQ2);
						if (intflags & (1<<1)) // FSK Packet CRC OK.
							packet[Channel][0] = receiveMessage( Channel, &packet[Channel][1] );
						writeRegister( Channel, REG_FSK_IRQ2, 0xFF );
					} else {
						rssimode = (int)(uint8_t)readRegister( Channel, REG_CURRENT_RSSI) - RSSI_OFFSET;
						getPacket( Channel ); //  Check for missed interrupt
					}
					ChannelPrintf( Channel,  5, 1, "%u%s since last packet   ", interval, timescale );
					ChannelPrintf( Channel,  6, 1, "Telem Packets: %d   ", Config.LoRaDevices[Channel].TelemetryCount );
					ChannelPrintf( Channel,  7, 1, "Image Packets: %d   ", Config.LoRaDevices[Channel].SSDVCount );
					ChannelPrintf( Channel,  8, 1, "Bad CRC: %d Bad Type: %d", Config.LoRaDevices[Channel].BadCRCCount,
												Config.LoRaDevices[Channel].UnknownCount );
					ChannelPrintf( Channel,  9, 1, "Packet   SNR: %4d   ", Config.LoRaDevices[Channel].packet_snr );
					ChannelPrintf( Channel, 10, 1, "Packet  RSSI: %4d   ", Config.LoRaDevices[Channel].packet_rssi );
					ChannelPrintf( Channel, 11, 1, "Current RSSI: %4d   ", rssimode );
					ChannelPrintf( Channel, 12, 1, "Freq. offset: %4d kHz  ", Config.LoRaDevices[Channel].freq_offset >> 10 );
				}
			}
		}
		delay( 20 );
		if ( ++LoopCount > 50 ) {
			LoopCount = 0;
			curlPush();
			UploadMultiImages();
			ChannelPrintf( 0, 4, 1, "Uploads: %4d", curlUploads() );
			ChannelPrintf( 1, 4, 1, "Retries: %4d,%d ", curlRetries(), curlConflicts() );
		}
	}

	CloseDisplay( mainwin );
	curlClean();
	return 0;
}
