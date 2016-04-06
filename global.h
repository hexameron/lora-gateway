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
struct TBinaryPacket
{
	uint8_t PayloadType;
	uint8_t PayloadID;
	uint16_t Counter;
	uint16_t BiSeconds;
	union { float f; int32_t i; } Latitude;
	union { float f; int32_t i; } Longitude;
	uint16_t Altitude;
	uint16_t Checksum;
};

#pragma pack(8)
struct TPayload
{
        int InUse;
        char Payload[32];
};
