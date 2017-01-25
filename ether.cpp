#include <ESP8266WiFi.h>
#include <lwip/netif.h>
#include <lwip/err.h>
#include "ether.h"
#include "ipv4.h"
#include "util.h"
#include "slip.h"
#include "mydebug.h"

extern "C" {
#include <netif/wlan_lwip_if.h>
#include <user_interface.h>
}

#define ARPResponseTimeoutMS 250
#define ARPTableEntries 10

static struct ARPEntry ARPTable[ ARPTableEntries ];

extern netif_linkoutput_fn OriginalLinkoutputFn;
extern netif_output_fn OriginalOutputFn;
extern struct netif* ESPif;

static void OnARPPacket( struct ARPHeader* ARP );
static void ARP_RespondToRequest( struct ARPHeader* ARP );

uint8_t BroadcastMACAddress[ MACAddressLen ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/*
 * Returns 1 if the given IP is on the same subnet as us. 
 */
int AreWeOnTheSameSubnet( uint32_t IPAddress ) {
  uint32_t A = OurIPAddress & OurNetmask;
  uint32_t B = IPAddress & OurNetmask;

  return A == B ? 1 : 0;
}

/*
 * Sets up an ethernet frame header, pretty straightforward. 
 */
int PrepareEthernetHeader( struct EtherFrame* FrameHeader, const uint8_t* SourceMAC, const uint8_t* DestMAC, uint16_t LengthOrType ) {
  memcpy( FrameHeader->SourceMAC, SourceMAC, MACAddressLen );
  memcpy( FrameHeader->DestMAC, DestMAC, MACAddressLen );
  FrameHeader->LengthOrType = htons( LengthOrType );

  return sizeof( struct EtherFrame );
}

/*
 * Called when the network interface receives an ethernet frame. 
 */
void OnDataReceived( const uint8_t* Data, int Length ) {
  struct EtherFrame* EHeader = ( struct EtherFrame* ) Data;
  struct ip_packet* IPHeader = ( struct ip_packet* ) &Data[ sizeof( struct EtherFrame ) ];

  switch ( htons( EHeader->LengthOrType ) ) {
    case EtherType_IPv4: {
      if ( IPHeader->DestIP == OurIPAddress )
        SLIP_WritePacket( &Data[ sizeof( struct EtherFrame ) ], Length - sizeof( struct EtherFrame ) );

      //SLIP_QueuePacketForWrite( &Data[ sizeof( struct EtherFrame ) ], Length - sizeof( struct EtherFrame ) );
      //OnIPv4Packet( &Data[ sizeof( struct EtherFrame ) ], Length, ( const struct EtherFrame* ) Data );
      break;
    }
    case EtherType_ARP: {
      if ( Length >= ( int ) sizeof( struct ARPHeader ) )
        OnARPPacket( ( struct ARPHeader* ) ( ( ( uint8_t* ) Data ) + sizeof( struct EtherFrame ) ) );
        
      break;
    }
    default: break;
  };
}

/*
 * Writes the given ethernet frame to the network interface. 
 */
err_t EtherWrite( void* Data, int Length ) {
  struct pbuf* OutPBuf = NULL;
  err_t Result = ERR_OK;

  OutPBuf = pbuf_alloc( PBUF_LINK, Length, PBUF_RAM );

  if ( OutPBuf ) {
    memcpy( OutPBuf->payload, Data, Length );

    Result = OriginalLinkoutputFn( ESPif, OutPBuf );
    pbuf_free( OutPBuf );
  }

  return Result;
}

/*
 * Simple enough, call this to respond to an ARP request. 
 */
static void ARP_RespondToRequest( struct ARPHeader* ARP ) {
    uint8_t Buffer[ 512 ];
    struct ARPHeader* Response = ( struct ARPHeader* ) &Buffer[ sizeof( struct EtherFrame ) ];
    struct EtherFrame* Frame = ( struct EtherFrame* ) Buffer;

    memcpy( Frame->SourceMAC, OurMACAddress, MACAddressLen );
    memset( Frame->DestMAC, 0xFF, MACAddressLen );
    Frame->LengthOrType = htons( EtherType_ARP );

    Response->HardwareType = htons( 1 );
    Response->ProtocolType = htons( EtherType_IPv4 );
    Response->HWAddressLen = MACAddressLen;
    Response->ProtoAddressLen = 4;
    Response->Operation = htons( 2 );

    memcpy( Response->TargetMAC, ARP->SenderMAC, MACAddressLen );
    memcpy( Response->SenderMAC, OurMACAddress, MACAddressLen );

    Response->SenderIP = OurIPAddress;
    Response->TargetIP = ARP->SenderIP;

    EtherWrite( Buffer, sizeof( struct EtherFrame ) + sizeof( struct ARPHeader ) );
}

/*
 * Called when we receive an ARP packet (response or request)
 */
static void OnARPPacket( struct ARPHeader* ARP ) {
  int IsRequest = htons( ARP->Operation ) == 1 ? 1 : 0;

  /*
   * Is someone requesting our MAC address?
   * If so, send a timely response. 
   */
  if ( IsRequest ) {
    if ( ARP->TargetIP == OurIPAddress ) {
      ARP_RespondToRequest( ARP );
    }
  }

  /* Add sender to ARP table if the sender MAC and IP addresses are not zero or broadcast addresses. */
  if ( ! IsMACBroadcast( ARP->SenderMAC ) && ! IsMACZero( ARP->SenderMAC ) && ARP->SenderIP && ARP->SenderIP != 0xFFFFFFFF )
    ARP_AddToTable( ARP->SenderMAC, ARP->SenderIP );

  /* Ditto except for target */
    if ( ! IsMACBroadcast( ARP->TargetMAC ) && ! IsMACZero( ARP->TargetMAC ) && ARP->TargetIP && ARP->TargetIP != 0xFFFFFFFF )
      ARP_AddToTable( ARP->TargetMAC, ARP->TargetIP );    
}

static void ARP_AddDefaultRoutes( void ) {
  ARP_AddStaticRoute( OurIPAddress, OurMACAddress );
}

/*
 * Zeroes the ARP table and adds a static entry for ourself. 
 */
void ARP_Init( void ) {
    ARP_ClearTable( );
    ARP_AddDefaultRoutes( );
}

/*
 * Adds a static entry to the ARP table. 
 */
struct ARPEntry* ARP_AddStaticRoute( uint32_t IP, uint8_t* MACAddress ) {
  struct ARPEntry* Entry = NULL;

  if ( ( Entry = ARP_FindFreeEntry( ) ) != NULL ) {
    Entry->TimeAdded = 0xFFFFFFFF;
    Entry->IPAddress = IP;
    Entry->Set = 1;

    memcpy( Entry->MACAddress, MACAddress, MACAddressLen );
  }

  return Entry;
}

/*
 * Called once every "frame" or run throught the main loop. 
 */
void ARP_Tick( void ) {
    static uint32_t NextFlush = 0;
    uint32_t Now = millis( );

    if ( Now >= NextFlush ) {
      NextFlush = Now + TimeToFlushARP;

      DebugPrintf( "Flushing ARP cache.\n" );
      ARP_Init( );
    }
}

/*
 * Clears out all entries in the ARP table. 
 */
void ARP_ClearTable( void ) {
  memset( ARPTable, 0, sizeof( ARPTable ) );
}

/*
 * Finds the oldest entry in the ARP table so it can be reused. 
 */
struct ARPEntry* ARP_FindOldestEntry( void ) {
  struct ARPEntry* OldestEntry = &ARPTable[ 0 ];
  uint32_t Oldest = 0;
  int i = 0;

  for ( i = 0; i < ARPTableEntries; i++ ) {
    if ( Oldest == 0 || ( ARPTable[ i ].TimeAdded <= Oldest && ARPTable[ i ].Set ) ) {
      Oldest = ARPTable[ i ].TimeAdded;
      OldestEntry = &ARPTable[ i ];
    }
  }

  return OldestEntry;
}

/* Looks for an unused "slot" in the ARP table, returns a pointer
 * to it if found, otherwise NULL. 
 */
struct ARPEntry* ARP_FindFreeEntry( void ) {
    int i = 0;

    for ( i = 0; i < ARPTableEntries; i++ ) {
        if ( ARPTable[ i ].Set == 0 )
            return &ARPTable[ i ];
    }

    return ARP_FindOldestEntry( );
}

/* Look in the ARP table for an entry by IP address and returns
 * a pointer to it, otherwise NULL. 
 */
struct ARPEntry* ARP_FindEntryByIP( uint32_t IP ) {
    int i = 0;

    for ( i = 0; i < ARPTableEntries; i++ ) {
        if ( ARPTable[ i ].Set && ARPTable[ i ].IPAddress == IP ) {
            return &ARPTable[ i ];
        }
    }

    return NULL;
}

/* Look in the ARP table for an entry by MAC address and returns
 * a pointer to it, otherwise NULL. 
 */
struct ARPEntry* ARP_FindEntryByMAC( const uint8_t* MAC ) {
    int i = 0;

    for ( i = 0; i < ARPTableEntries; i++ ) {
        if ( ARPTable[ i ].Set && memcmp( ARPTable[ i ].MACAddress, MAC, MACAddressLen ) == 0 )
            return &ARPTable[ i ];
    }

    return NULL;
}

/* Adds an entry into the ARP table given the supplies MAC address and IP. 
 * Returns a pointer to a new table entry or an existing one if one was already previously. 
 */
struct ARPEntry* ARP_AddToTable( const uint8_t* MAC, uint32_t IP ) {
    struct ARPEntry* Ptr = NULL;

    /* Try to find an existing entry by IP address */
    if ( ( Ptr = ARP_FindEntryByIP( IP ) ) == NULL ) {
        /* Try to find an existing entry by MAC address */
        if ( ( Ptr = ARP_FindEntryByMAC( MAC ) ) == NULL ) {
            /* If none found, add a new one */
            if ( ( Ptr = ARP_FindFreeEntry( ) ) != NULL ) {
                memcpy( Ptr->MACAddress, MAC, MACAddressLen );

                Ptr->TimeAdded = millis( );
                Ptr->IPAddress = IP;
                Ptr->Set = 1;
            }
        }
    }
    return Ptr;
}

/*
 * Writes the ARP table contents to the console. 
 */
void ARP_DumpTableToConsole( void ) {
#if 0
    static char Text[ 256 ];
    char MACString[ 32 ];
    char IPString[ 32 ];
    int Entries = 0;
    int i = 0;

    Serial.println( "ARP Table contents:" );

    for ( i = 0; i < ARPTableEntries; i++ ) {
        if ( ARPTable[ i ].Set ) {
            Entries++;

            MACsprintf( ARPTable[ i ].MACAddress, MACString, sizeof( MACString ) );
            IPsprintf( ARPTable[ i ].IPAddress, IPString, sizeof( IPString ) );

            snprintf( Text, sizeof( Text ), "MAC: %s\tIP: %s", MACString, IPString );
            Serial.println( Text );
        }
    }

    snprintf( Text, sizeof( Text ), "End of ARP table contents. %d entries.", Entries );
    Serial.println( Text );
#endif
}

/*
 * Given an IP address, send out an ARP request over the wire. 
 */
void ARP_RequestMACFromIP( uint32_t IP ) {
    struct ARPEntry* Entry = NULL;
    static uint8_t Buffer[ sizeof( struct EtherFrame ) + sizeof( struct ARPHeader ) ];
    struct ARPHeader* ARPPacket = ( struct ARPHeader* ) &Buffer[ sizeof( struct EtherFrame ) ];
    struct EtherFrame* Frame = ( struct EtherFrame* ) Buffer;

    memset( Buffer, 0, sizeof( Buffer ) );

    /* If the entry already exists in the ARP table, do nothing */
    if ( ( Entry = ARP_FindEntryByIP( IP ) ) != NULL )
      return;

    /* Setup the ethernet frame which is just the source MAC, destination MAC, and frame type */
    WiFi.macAddress( Frame->SourceMAC );
    memset( Frame->DestMAC, 0xFF, sizeof( Frame->DestMAC ) );
    Frame->LengthOrType = htons( EtherType_ARP );      /* ARP */

    ARPPacket->HardwareType = htons( 1 );       /* Ethernet */
    ARPPacket->ProtocolType = htons( EtherType_IPv4 );  /* TCP/IP */
    ARPPacket->HWAddressLen = MACAddressLen;     /* 6 Byte hardware address (MAC) */
    ARPPacket->ProtoAddressLen = 4;              /* 4 Bytes for IPV4 address */
    ARPPacket->Operation = htons( 1 );          /* Request */

    WiFi.macAddress( ARPPacket->SenderMAC );     /* Our hardware address */
    memset( ARPPacket->TargetMAC, 0xFF, MACAddressLen); /* Broadcast address */

    ARPPacket->TargetIP = IP;                   /* Who we're lookin' for */
    ARPPacket->SenderIP = WiFi.localIP( );      /* Who we are */

    EtherWrite( Buffer, sizeof( Buffer ) );
}

/*
 * Given an IP address, send out an ARP request over the wire.
 * Blocks for (ARPResponseTimeoutMS) before giving up.
 */
int ARP_RequestMACFromIP_Blocking( uint32_t IP, uint8_t* MAC ) {
  struct ARPEntry* ARP = NULL;
  uint32_t Now = millis( );
  uint32_t Timeout = Now + ARPResponseTimeoutMS;
  
  /*
   * Check to see if it already exists in the ARP table. 
   * If it does, this will return WAY sooner. 
   */
  ARP = ARP_FindEntryByIP( IP );
  
  if ( ARP == NULL )
    ARP_RequestMACFromIP( IP );

  /*
   * Don't block forever, in fact this is awful and should go away.
   */
  while ( Timeout >= Now && ARP == NULL ) {
    ARP = ARP_FindEntryByIP( IP );
    Now = millis( );

    yield( );
  }

  if ( ARP && MAC )
    memcpy( MAC, ARP->MACAddress, MACAddressLen );

  return ARP == NULL ? 0 : 1;
}
