#include <curses.h>

struct TLoRaDevice
{
	int InUse;
	int DIO0;
	int DIO5;
	char Frequency[16];
	int SpeedMode;
	int PayloadLength;
	int ImplicitOrExplicit;
	int ErrorCoding;
	int Bandwidth;
	double Reference;
	int SpreadingFactor;
	int LowDataRateOptimize;

	WINDOW *Window;

	unsigned int TelemetryCount, SSDVCount, BadCRCCount, UnknownCount, SSDVMissing;

	char Payload[16], Time[12];
	unsigned int Counter;
	unsigned long Seconds;
	double Longitude, Latitude;
	double Distance, Elevation;
	unsigned int Altitude, PreviousAltitude;
	unsigned int Satellites;
	unsigned long LastPositionAt;
	time_t LastPacketAt;
	float AscentRate;

	int base_rssi, packet_rssi, packet_snr, freq_offset;
};

struct TConfig
{
	char Tracker[16];
	int EnableHabitat;
	int EnableSSDV;
	char ftpServer[100];
	char ftpUser[32];
	char ftpPassword[32];
	char ftpFolder[64];
	struct TLoRaDevice LoRaDevices[2];
	int LogLevel;
	double myLat, myLon, myAlt;
};

extern struct TConfig Config;

#pragma pack(1)
struct TBinaryPacket // lora style binary packet
{
	uint8_t  PayloadType;
	uint8_t  PayloadID;
	uint16_t Counter;
	uint16_t BiSeconds;
	union { float f; int32_t i; } Latitude;
	union { float f; int32_t i; } Longitude;
	uint16_t Altitude;
	uint8_t  Fix;         // optional
	int8_t   Temperature; // optional
	uint16_t Checksum;    // Always last, not always present
};

struct SBinaryPacket // 16 byte Horus-like binary Payload
{
uint8_t   PayloadID;	// Legacy list
uint8_t   Counter;	// 8 bit counter
uint16_t  Biseconds;	// Time of day / 2
uint8_t   Latitude[3];	// (int)(float * 1.0e7) / (1<<8)
uint8_t   Longitude[3];	// ( better than 10m precision )
uint16_t  Altitude;	// 0 - 65 km
uint8_t   Voltage;	// scaled 5.0v in 255 range
uint8_t   User;		// Temp / Sats
	// Temperature	6 bits MSB => (+30 to -32)
	// Satellites	2 bits LSB => 0,4,8,12 is good enough
uint16_t  Checksum;	// CRC16-CCITT Checksum.
}; // 16 data bytes, for (128,384) LDPC FEC
	// (32 Parity bytes may be ignored for now)

#pragma pack(8)
struct TPayload
{
        int InUse;
        char Payload[32];
};
