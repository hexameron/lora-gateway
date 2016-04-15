#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
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
uint8_t currentMode = 0x81;

#define REG_FIFO                    0x00
#define REG_FREQ                    0x06
#define REG_FIFO_ADDR_PTR           0x0D
#define REG_FIFO_TX_BASE_AD         0x0E
#define REG_FIFO_RX_BASE_AD         0x0F
#define REG_RX_NB_BYTES             0x13
#define REG_OPMODE                  0x01
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

// MODES
#define RF96_MODE_RX_CONTINUOUS     0x85
#define RF96_MODE_SLEEP             0x80
#define RF96_MODE_STANDBY           0x81

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

//#define RSSI_OFFSET 50
#define RSSI_OFFSET 164
//#define RSSI_OFFSET 157

const char *Modes[5] = {"slow", "SSDV", "repeater", "turbo", "TurboX"};

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
	if ( newMode == currentMode ) {
		return;
	}

	switch ( newMode )
	{
	case RF96_MODE_RX_CONTINUOUS:
		writeRegister( Channel, REG_PA_CONFIG, PA_OFF_BOOST ); // TURN PA OFF FOR RECIEVE??
		writeRegister( Channel, REG_LNA, LNA_MAX_GAIN ); // LNA_MAX_GAIN);  // MAX GAIN FOR RECIEVE
		writeRegister( Channel, REG_OPMODE, newMode );
		currentMode = newMode;
		// LogMessage("Changing to Receive Continuous Mode\n");
		break;
	case RF96_MODE_SLEEP:
		writeRegister( Channel, REG_OPMODE, newMode );
		currentMode = newMode;
		// LogMessage("Changing to Sleep Mode\n");
		break;
	case RF96_MODE_STANDBY:
		writeRegister( Channel, REG_OPMODE, newMode );
		currentMode = newMode;
		// LogMessage("Changing to Standby Mode\n");
		break;
	default: return;
	}

	if ( newMode != RF96_MODE_SLEEP ) {
		delay( 1 );
	}

	// LogMessage("Mode Change Done\n");
	return;
}


void setLoRaMode( int Channel ) {
	double Frequency;
	unsigned long FrequencyValue;

	// LogMessage("Setting LoRa Mode\n");
	setMode( Channel, RF96_MODE_SLEEP );
	writeRegister( Channel, REG_OPMODE,0x80 );

	setMode( Channel, RF96_MODE_SLEEP );

	if ( sscanf( Config.LoRaDevices[Channel].Frequency, "%lf", &Frequency ) ) {
		FrequencyValue = (unsigned long)( Frequency * 7110656 / 434 );
		// LogMessage("FrequencyValue = %06Xh\n", FrequencyValue);
		writeRegister( Channel, REG_FREQ,   ( FrequencyValue >> 16 ) & 0xFF );
		writeRegister( Channel, REG_FREQ + 1, ( FrequencyValue >> 8 ) & 0xFF );
		writeRegister( Channel, REG_FREQ + 2, FrequencyValue & 0xFF );
	}

	// LogMessage("Mode = %d\n", readRegister(Channel, REG_OPMODE));
}

/////////////////////////////////////
//    Method:   Setup to receive continuously
//////////////////////////////////////
void startReceiving( int Channel ) {
	writeRegister( Channel, REG_MODEM_CONFIG, Config.LoRaDevices[Channel].ImplicitOrExplicit | Config.LoRaDevices[Channel].ErrorCoding | Config.LoRaDevices[Channel].Bandwidth );
	writeRegister( Channel, REG_MODEM_CONFIG2, Config.LoRaDevices[Channel].SpreadingFactor | CRC_ON );
	writeRegister( Channel, REG_MODEM_CONFIG3, 0x04 | Config.LoRaDevices[Channel].LowDataRateOptimize );                                  // 0x04: AGC sets LNA gain
	writeRegister( Channel, REG_DETECT_OPT, ( readRegister( Channel, REG_DETECT_OPT ) & 0xF8 ) | ( ( Config.LoRaDevices[Channel].SpreadingFactor == SPREADING_6 ) ? 0x05 : 0x03 ) );  // 0x05 For SF6; 0x03 otherwise
	writeRegister( Channel, REG_DETECTION_THRESHOLD, ( Config.LoRaDevices[Channel].SpreadingFactor == SPREADING_6 ) ? 0x0C : 0x0A );        // 0x0C for SF6, 0x0A otherwise

	LogMessage( "Channel %d %s mode\n", Channel, Modes[Config.LoRaDevices[Channel].SpeedMode] );

	writeRegister( Channel, REG_PAYLOAD_LENGTH, Config.LoRaDevices[Channel].PayloadLength );
	writeRegister( Channel, REG_RX_NB_BYTES, Config.LoRaDevices[Channel].PayloadLength );

	// writeRegister(Channel, REG_HOP_PERIOD,0xFF);

	// writeRegister(Channel, REG_FIFO_ADDR_PTR, readRegister(Channel, REG_FIFO_RX_BASE_AD));
	writeRegister( Channel, REG_FIFO_RX_BASE_AD, 0 );
	writeRegister( Channel, REG_FIFO_ADDR_PTR, 0 );

	// Setup Receive Continous Mode
	setMode( Channel, RF96_MODE_RX_CONTINUOUS );
}

void setupRFM98( int Channel ) {
	if ( Config.LoRaDevices[Channel].InUse ) {
		// initialize the pins
		pinMode( Config.LoRaDevices[Channel].DIO0, INPUT );
		pinMode( Config.LoRaDevices[Channel].DIO5, INPUT );

		if ( wiringPiSPISetup( Channel, 1000000 ) < 0 ) {
			fprintf( stderr, "Failed to open SPI port.  Try loading spi library with 'gpio load spi'" );
			exit( 1 );
		}

		// LoRa mode
		setLoRaMode( Channel );

		startReceiving( Channel );
	}
}

void ShowPacketCounts( int Channel ) {
	if ( Config.LoRaDevices[Channel].InUse ) {
		ChannelPrintf( Channel, 6, 1, "Telem Packets = %d", Config.LoRaDevices[Channel].TelemetryCount );
		ChannelPrintf( Channel, 7, 1, "Image Packets = %d", Config.LoRaDevices[Channel].SSDVCount );
		ChannelPrintf( Channel, 8, 1, "Bad CRC = %d Bad Type = %d", Config.LoRaDevices[Channel].BadCRCCount, Config.LoRaDevices[Channel].UnknownCount );
	}
}

double FrequencyError( int Channel ) {
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
	if ( ( Offset > +900.0 ) || ( Offset < -900.0 ) ) {
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

		Frequency -= FrequencyError( Channel );
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
	int i, Bytes, currentAddr, x;
	unsigned char data[257];

	Bytes = 0;

	x = readRegister( Channel, REG_IRQ_FLAGS );
	// clear the rxDone flag
	writeRegister( Channel, REG_IRQ_FLAGS, 0x40 );

	Config.LoRaDevices[Channel].packet_rssi = readRegister( Channel, REG_PACKET_RSSI ) - RSSI_OFFSET;

	// check for payload crc issues (0x20 is the bit we are looking for
	if ( ( x & 0x20 ) == 0x20 ) {
		writeRegister( Channel, REG_IRQ_FLAGS, 0x20 );
		Config.LoRaDevices[Channel].BadCRCCount++;
	} else
	{
		currentAddr = readRegister( Channel, REG_FIFO_RX_CURRENT_ADDR );
		Bytes = readRegister( Channel, REG_RX_NB_BYTES );
		writeRegister( Channel, REG_FIFO_ADDR_PTR, currentAddr );

		data[0] = REG_FIFO;
		wiringPiSPIDataRW( Channel, data, Bytes + 1 );
		for ( i = 0; i <= Bytes; i++ )
		{
			message[i] = data[i + 1];
		}
		message[Bytes] = '\0';
		Config.LoRaDevices[Channel].packet_snr = ( (int8_t)readRegister( Channel, REG_PACKET_SNR ) ) / 4;
		Config.LoRaDevices[Channel].base_rssi = readRegister( Channel, REG_CURRENT_RSSI ) - RSSI_OFFSET;
		if ( Config.LoRaDevices[Channel].Bandwidth < BANDWIDTH_41K7 ) {
			Config.LoRaDevices[Channel].freq_offset = (int)FrequencyError( Channel );
		} else {
			Config.LoRaDevices[Channel].freq_offset = 0;
		}
	}

	// Clear all flags
	writeRegister( Channel, REG_IRQ_FLAGS, 0xFF );
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

void UploadImagePacket( char *EncodedCallsign, char *EncodedEncoding, char *EncodedData ) {
	CURL *curl;
	char PostImage[1000];

	/* get a curl handle */
	curl = curl_easy_init();
	if ( curl ) {
		// So that the response to the curl POST doesn;'t mess up my finely crafted display!
		curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, write_data );

		/* Set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */
		curl_easy_setopt( curl, CURLOPT_URL, "http://www.sanslogic.co.uk/ssdv/data.php" );

		/* Now specify the POST data */
		sprintf( PostImage, "callsign=%s&encoding=%s&packet=%s", Config.Tracker, EncodedEncoding, EncodedData );
		curl_easy_setopt( curl, CURLOPT_COPYPOSTFIELDS, PostImage );

		curlQueue( curl );
		/* cleanup handle later*/
	}
}

void ReadString( FILE *fp, char *keyword, char *Result, int Length, int NeedValue ) {
	char line[100], *token, *value;

	fseek( fp, 0, SEEK_SET );
	*Result = '\0';

	while ( fgets( line, sizeof( line ), fp ) != NULL )
	{
		token = strtok( line, "=" );
		if ( strcasecmp( keyword, token ) == 0 ) {
			value = strtok( NULL, "\n" );
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

			LogMessage( "Channel %d frequency set to %s\n", Channel, Config.LoRaDevices[Channel].Frequency );
			Config.LoRaDevices[Channel].InUse = 1;

			// DIO0 / DIO5 overrides
			sprintf( Keyword, "DIO0_%d", Channel );
			Config.LoRaDevices[Channel].DIO0 = ReadInteger( fp, Keyword, 0, Config.LoRaDevices[Channel].DIO0 );

			sprintf( Keyword, "DIO5_%d", Channel );
			Config.LoRaDevices[Channel].DIO5 = ReadInteger( fp, Keyword, 0, Config.LoRaDevices[Channel].DIO5 );

			LogMessage( "LoRa Channel %d DIO0=%d DIO5=%d\n", Channel, Config.LoRaDevices[Channel].DIO0, Config.LoRaDevices[Channel].DIO5 );

			Config.LoRaDevices[Channel].SpeedMode = 0;
			sprintf( Keyword, "mode_%d", Channel );
			Config.LoRaDevices[Channel].SpeedMode = ReadInteger( fp, Keyword, 0, 0 );
			Config.LoRaDevices[Channel].PayloadLength = 255;
			ChannelPrintf( Channel, 0, 1, "Channel %d %sMHz %s mode", Channel, Config.LoRaDevices[Channel].Frequency, Modes[Config.LoRaDevices[Channel].SpeedMode] );

			if ( Config.LoRaDevices[Channel].SpeedMode == 4 ) {
				// Testing
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

	FieldCount = sscanf( Line + 2, "%15[^,],%u,%8[^,],%lf,%lf,%u",
						 ( Config.LoRaDevices[Channel].Payload ),
						 &( Config.LoRaDevices[Channel].Counter ),
						 ( Config.LoRaDevices[Channel].Time ),
						 &( Config.LoRaDevices[Channel].Latitude ),
						 &( Config.LoRaDevices[Channel].Longitude ),
						 &( Config.LoRaDevices[Channel].Altitude ) );

	// HAB->HAB_status = FieldCount == 6;
	return ( FieldCount == 6 );
}

void DoPositionCalcs( Channel ) {
#if 0
	unsigned long Now;
	struct tm tm;
	float Climb, Period;

	strptime( Config.LoRaDevices[Channel].Time, "%H:%M:%S", &tm );
	Now = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;

	if ( ( Config.LoRaDevices[Channel].LastPositionAt > 0 ) && ( Now > Config.LoRaDevices[Channel].LastPositionAt ) ) {
		Climb = (float)Config.LoRaDevices[Channel].Altitude - (float)Config.LoRaDevices[Channel].PreviousAltitude;
		Period = (float)Now - (float)Config.LoRaDevices[Channel].LastPositionAt;
		Config.LoRaDevices[Channel].AscentRate = Climb / Period;
	} else
	{
		Config.LoRaDevices[Channel].AscentRate = 0;
	}

	Config.LoRaDevices[Channel].PreviousAltitude = Config.LoRaDevices[Channel].Altitude;
	Config.LoRaDevices[Channel].LastPositionAt = Now;
#endif
	ChannelPrintf( Channel, 4, 1, "%8.5lf, %8.5lf, %05u   ",
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
void getPacket( Channel ) {
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

int main( int argc, char **argv ) {
	char *Message;
	int Channel, Bytes;
	uint32_t CallsignCode, LoopCount[2];
	WINDOW * mainwin;

	if ( wiringPiSetup() < 0 ) {
		fprintf( stderr, "Failed to open wiringPi\n" );
		exit( 1 );
	}

	curlInit();
	mainwin = InitDisplay();
	LogMessage( "**** LoRa Gateway by daveake ****\n" );

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
		LoopCount[Channel] = 0;
		if ( Config.LoRaDevices[Channel].InUse ) {
			ShowPacketCounts( Channel );

			if ( digitalRead( Config.LoRaDevices[Channel].DIO0 ) ) {
				packet[Channel][0] = receiveMessage( Channel, &packet[Channel][1] );
			}
			wiringPiISR( Config.LoRaDevices[Channel].DIO0, INT_EDGE_RISING, Channel ? &gpioInterrupt1 : &gpioInterrupt0 );
		}
	}

	while ( 1 )
	{
		for ( Channel = 0; Channel <= 1; Channel++ )
		{
			if ( Config.LoRaDevices[Channel].InUse ) {
				{
					Message = packet[Channel];
					Bytes = Message[0];
					Message[0] = 0;
					if ( Bytes > 0 ) {
						if ( Message[1] == '!' ) {
							LogMessage( "Ch %d: Uploaded message %s\n", Channel, Message + 1 );
						} else if ( Message[1] == '^' )     {
							ChannelPrintf( Channel, 2, 1, "Calling message %d bytes      ", strlen( Message + 1 ) );
							ProcessCallingMessage( Channel, Message + 3 );
							LogMessage( "%s\n", Message + 1 );
						} else if ( Message[1] == '$' )     {
							ChannelPrintf( Channel, 3, 1, "Telemetry %d bytes            ", strlen( Message + 1 ) - 1 );
							UploadTelemetryPacket( Message + 1 );
							if ( ProcessLine( Channel, Message + 1 ) ) {
								DoPositionCalcs( Channel );
							}
							Config.LoRaDevices[Channel].TelemetryCount++;

							if ( LOG_TELEM & Config.LogLevel ) {
								if ( LOG_RADIO & Config.LogLevel ) {
									sprintf( Message + 1 + strlen( Message + 1 ),
											 "Stats:%d,%1.1lf,%1.1lf,%d,%d,%d,%d\n",
											 Channel,
											 Config.LoRaDevices[Channel].Distance,
											 Config.LoRaDevices[Channel].Elevation,
											 Config.LoRaDevices[Channel].base_rssi,
											 Config.LoRaDevices[Channel].packet_rssi,
											 Config.LoRaDevices[Channel].packet_snr,
											 Config.LoRaDevices[Channel].freq_offset );
								}
								UpdatePayloadLOG( Message + 1 );
							}

							Message[strlen( Message + 1 )] = '\0';
							LogMessage( "%s\n", Message + 1 );
						} else if ( 0x80 == Message[1] )     {
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
								sprintf( Data, "%s,%u,%02d:%02d:%02d,%8.5f,%8.5f,%u,%u,%d",
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
								sprintf( Sentence, "$$%s*%04X\n", Data, CRC16( Data, strlen( Data ) ) );

								UploadTelemetryPacket( Sentence );
								DoPositionCalcs( Channel );
								Config.LoRaDevices[Channel].TelemetryCount++;
								if ( LOG_TELEM & Config.LogLevel ) {
									if ( LOG_RADIO & Config.LogLevel ) {
										sprintf( Sentence + strlen( Sentence ),
												 "Stats:%d,%1.1lf,%1.1lf,%d,%d,%d,%d\n",
												 Channel,
												 Config.LoRaDevices[Channel].Distance,
												 Config.LoRaDevices[Channel].Elevation,
												 Config.LoRaDevices[Channel].base_rssi,
												 Config.LoRaDevices[Channel].packet_rssi,
												 Config.LoRaDevices[Channel].packet_snr,
												 Config.LoRaDevices[Channel].freq_offset );
									}
									UpdatePayloadLOG( Sentence );
								}
								LogMessage( "Ch %d:%s",Channel, Sentence );

							} else {
								Config.LoRaDevices[Channel].BadCRCCount++;
							}
						} else if ( Message[1] == 0x66 )     {
							// SSDV packet
							char Callsign[7], *EncodedCallsign, *EncodedEncoding, *EncodedData, HexString[513];
							Message[0] = 0x55;

							CallsignCode = Message[2]; CallsignCode <<= 8;
							CallsignCode |= Message[3]; CallsignCode <<= 8;
							CallsignCode |= Message[4]; CallsignCode <<= 8;
							CallsignCode |= Message[5];

							decode_callsign( Callsign, CallsignCode );

							// ImageNumber = Message[6];
							// PacketNumber = Message[8];

							LogMessage( "SSDV Packet, Callsign %s, Image %d, Packet %d\n",
										Callsign, Message[6], Message[7] * 256 + Message[8] );
							ChannelPrintf( Channel, 3, 1, "SSDV Packet %d bytes          ", Bytes );

							// Upload to server
							if ( Config.EnableSSDV ) {
								EncodedCallsign = url_encode( Callsign );
								EncodedEncoding = url_encode( "hex" );

								ConvertStringToHex( HexString, Message, 256 );
								EncodedData = url_encode( HexString );

								UploadImagePacket( EncodedCallsign, EncodedEncoding, EncodedData );

								free( EncodedCallsign );
								free( EncodedEncoding );
								free( EncodedData );
							}

							Config.LoRaDevices[Channel].SSDVCount++;
						} else
						{
							Config.LoRaDevices[Channel].UnknownCount++;
						}

						Config.LoRaDevices[Channel].LastPacketAt = time( NULL );
					}
				}

				// redraw screen every second
				if ( ++LoopCount[Channel] > 50 ) {
					// Check for missed interrupt
					getPacket( Channel );

					LoopCount[Channel] = 0;
					ChannelPrintf( Channel,  2, 1, "Freq. Offset = %4d   ", Config.LoRaDevices[Channel].freq_offset );
					ChannelPrintf( Channel,  5, 1, "%us since last packet   ", (unsigned int)( time( NULL ) 
												- Config.LoRaDevices[Channel].LastPacketAt ) );
					ShowPacketCounts( Channel ); // lines 6,7,8
					ChannelPrintf( Channel,  9, 1, "Packet   SNR = %4d   ", Config.LoRaDevices[Channel].packet_snr );
					ChannelPrintf( Channel, 10, 1, "Packet  RSSI = %4d   ", Config.LoRaDevices[Channel].packet_rssi );
					ChannelPrintf( Channel, 11, 1, "Current RSSI = %4d   ", readRegister( Channel, REG_CURRENT_RSSI )
															- RSSI_OFFSET );
					curlPush();
				}
			}
		}
		delay( 20 );
	}

	CloseDisplay( mainwin );
	curlClean();
	return 0;
}
