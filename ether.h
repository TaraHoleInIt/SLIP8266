#ifndef _ETHER_H_
#define _ETHER_H_

#define EtherTypeMinLength 1501
#define MACAddressLen 6

#define EtherType_IPv4 0x0800
#define EtherType_ARP 0x0806

#define SecondsToMS( x ) ( x * 1000 )
#define TimeToFlushARP SecondsToMS( 300 )

struct EtherFrame {
    uint8_t DestMAC[ MACAddressLen ];
    uint8_t SourceMAC[ MACAddressLen ];
    uint16_t LengthOrType;
} __attribute__( ( packed ) );

struct ARPHeader {
    uint16_t HardwareType;
    uint16_t ProtocolType;

    uint8_t HWAddressLen;
    uint8_t ProtoAddressLen;

    uint16_t Operation;

    uint8_t SenderMAC[ MACAddressLen ];
    uint32_t SenderIP;

    uint8_t TargetMAC[ MACAddressLen ];
    uint32_t TargetIP;
} __attribute__( ( packed ) );

struct ARPEntry {
    uint8_t MACAddress[ MACAddressLen ];
    uint32_t IPAddress;
    uint32_t TimeAdded;
    int Set;
};

/*
 * Returns 1 if the given IP is on the same subnet as us. 
 */
int AreWeOnTheSameSubnet( uint32_t IPAddress );

/*
 * Sets up an ethernet frame header, pretty straightforward. 
 */
int PrepareEthernetHeader( struct EtherFrame* FrameHeader, const uint8_t* SourceMAC, const uint8_t* DestMAC, uint16_t LengthOrType ) ;

/*
 * Called when the network interface receives an ethernet frame. 
 */
void OnDataReceived( const uint8_t* Data, int Length );

/*
 * Writes the given ethernet frame to the network interface. 
 */
err_t EtherWrite( void* Data, int Length );

/*
 * Finds the oldest entry in the ARP table so it can be reused. 
 */
struct ARPEntry* ARP_FindOldestEntry( void );

/*
 * Clears out all entries in the ARP table. 
 */
void ARP_ClearTable( void );

/* Looks for an unused "slot" in the ARP table, returns a pointer
 * to it if found, otherwise NULL. 
 */
struct ARPEntry* ARP_FindFreeEntry( void );

/* Look in the ARP table for an entry by IP address and returns
 * a pointer to it, otherwise NULL. 
 */
struct ARPEntry* ARP_FindEntryByIP( uint32_t IP );
 
/* Look in the ARP table for an entry by MAC address and returns
 * a pointer to it, otherwise NULL. 
 */
struct ARPEntry* ARP_FindEntryByMAC( const uint8_t* MAC );

/* Adds an entry into the ARP table given the supplies MAC address and IP. 
 * Returns a pointer to a new table entry or an existing one if one was already previously. 
 */
struct ARPEntry* ARP_AddToTable( const uint8_t* MAC, uint32_t IP );

/*
 * Writes the ARP table contents to the console. 
 */
void ARP_DumpTableToConsole( void );

/*
 * Given an IP address, send out an ARP request over the wire. 
 */
void ARP_RequestMACFromIP( uint32_t IP );

/*
 * Given an IP address, send out an ARP request over the wire.
 * Blocks for (ARPResponseTimeoutMS) before giving up.
 */
int ARP_RequestMACFromIP_Blocking( uint32_t IP, uint8_t* MAC );

/*
 * Zeroes the ARP table and adds a static entry for ourself. 
 */
void ARP_Init( void );

/*
 * Called once every "frame" or run throught the main loop. 
 */
void ARP_Tick( void );

/*
 * Adds a static entry to the ARP table. 
 */
struct ARPEntry* ARP_AddStaticRoute( uint32_t IP, uint8_t* MACAddress );

extern uint8_t BroadcastMACAddress[ MACAddressLen ];
extern uint8_t OurMACAddress[ MACAddressLen ];

extern IPAddress OurIPAddress;
extern IPAddress OurNetmask;
extern IPAddress OurGateway;

#endif

