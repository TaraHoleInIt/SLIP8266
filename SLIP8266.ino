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
#include <lwip/inet_chksum.h>
#include <user_interface.h>
}

const char* SSID = "";
const char* Password = "";

int IsConnectedToWiFi = 0;

netif_linkoutput_fn OriginalLinkoutputFn = NULL;
netif_output_fn OriginalOutputFn = NULL;
netif_input_fn OriginalInputFn = NULL;
struct netif* ESPif = NULL;

uint8_t OurMACAddress[ MACAddressLen ];

IPAddress OurIPAddress;
IPAddress OurNetmask;
IPAddress OurGateway;

static uint8_t RXPacketBuffer[ 4096 ];
static volatile int RXPacketLength = 0;
static volatile int RXPacketBusy = 0;

volatile int RXBytesRead = 0;
volatile int RXBytesDropped = 0;

volatile int TXBytesSent = 0;
volatile int TXBytesDropped = 0;

struct BufferEntry {
  uint8_t Buffer[ 2048 ];
  volatile int Length;
};

#define PacketBufferCount 8

static struct BufferEntry PacketBuffers[ PacketBufferCount ];
volatile static struct BufferEntry* BufferHead = &PacketBuffers[ PacketBufferCount - 1 ];
volatile static struct BufferEntry* BufferTail = &PacketBuffers[ 0 ];

int AddBufferToRing( uint8_t* Buffer, int Length ) {
  int Result = 0;

  if ( ! BufferHead->Length ) 
    Result = 1;

  memcpy( ( void* ) BufferHead->Buffer, Buffer, Length );

  BufferHead->Length = Length;
  BufferHead++;

  if ( BufferHead >= &PacketBuffers[ PacketBufferCount ] )
    BufferHead = &PacketBuffers[ 0 ];

  return Result;
}

int PlaybackBuffer( void ) {
  int Result = 0;

  if ( BufferTail->Length ) {
    OnDataReceived( ( const uint8_t* ) BufferTail->Buffer, BufferTail->Length );
    BufferTail->Length = 0;

    Result = 1;
  }

  BufferTail++;

  if ( BufferTail >= &PacketBuffers[ PacketBufferCount ] )
    BufferTail = &PacketBuffers[ 0 ];

  return 0;
}

/*
 * Called by the hardware WiFi stack whenever a packet is received. 
 */
err_t MyInputFn( struct pbuf* p, struct netif* inp ) {
  struct pbuf* Ptr = NULL;
  struct pbuf* Temp = NULL;
  int Count = 0;

  noInterrupts( );

  for ( Ptr = p; Ptr; Count++ ) {
    if ( Ptr->len > sizeof( BufferHead->Buffer ) ) {
      RXBytesDropped+= Ptr->len;
      DebugPrintf( "len > buffer size!\n" );
    } else {
      if ( AddBufferToRing( ( uint8_t* ) Ptr->payload, Ptr->len ) == 0 ) {
        DebugPrintf( "Overwrote packet in ring buffer!\n" );
        RXBytesDropped+= Ptr->len;
      } else {
        RXBytesRead+= Ptr->len;
      }
    }

    Temp = Ptr->next;
    pbuf_free( Ptr );
    Ptr = Temp;
  }

  //DebugPrintf( "Processed %d pbufs\n", Count );

  interrupts( );
  return 0;
}

err_t MyOutputFn( struct netif* inp, struct pbuf* p, ip_addr_t* ipaddr ) {
  noInterrupts( );
  pbuf_free( p );
  interrupts( );

  return 0;
}

err_t MyLinkoutputFn( struct netif* inp, struct pbuf* p ) {
  noInterrupts( );
  pbuf_free( p );
  interrupts( );

  return 0;
}

int ConnectToWiFi( int Timeout ) {
  uint32_t WhenToGiveUp = millis( ) + Timeout;

  Serial1.print( "Connecting to " );
  Serial1.print( SSID );
  Serial1.print( "..." );
  
  WiFi.begin( SSID, Password );
  WiFi.config( OurIPAddress, OurGateway, OurNetmask );

  while ( millis( ) < WhenToGiveUp ) {
    switch ( WiFi.status( ) ) {
      case WL_CONNECTED: {
        return 1;
      }
      default: {
        Serial1.print( "." );
        delay( 500 );
        
        continue;
      }
    };

    yield( );
  }
  
  return 0;
}

void setup( void ) {
  int i = 0;

  memset( PacketBuffers, 0, sizeof( PacketBuffers ) );

  OurIPAddress = IPAddress( 192, 168, 2, 177 );
  OurNetmask = IPAddress( 255, 255, 255, 0 );
  OurGateway = IPAddress( 192, 168, 2, 1 );

  WiFi.macAddress( OurMACAddress );

  if ( ( ESPif = eagle_lwip_getif( 0 ) ) != NULL ) {
    OriginalLinkoutputFn = ESPif->linkoutput;
    OriginalOutputFn = ESPif->output;
    OriginalInputFn = ESPif->input;

    ESPif->linkoutput = MyLinkoutputFn;
    ESPif->output = MyOutputFn;
    ESPif->input = MyInputFn;
  }  

  Serial.begin( 115200 );
  Serial1.begin( 115200 );

  while ( ! Serial || ! Serial1 )
    yield( );

  Serial1.println( "\nReady..." );
  Serial.setTimeout( 0 );

  do {
    IsConnectedToWiFi = ConnectToWiFi( 10000 );
  } while ( ! IsConnectedToWiFi );

  Serial1.println( " Connected!" );
  Serial1.print( "Local IP: " );
  Serial1.println( WiFi.localIP( ) );

  WiFi.macAddress( OurMACAddress );
  OurIPAddress = WiFi.localIP( );

  ARP_Init( );
}

void HeartBeat_Tick( void ) {
  static uint32_t NextTick = 0;
  uint32_t Now = millis( );

  if ( Now >= NextTick ) {
   DebugPrintf( "%s: RX Bytes Read/Dropped [%d,%d] / TX Bytes Written/Dropped [%d,%d]\n", __FUNCTION__, RXBytesRead, RXBytesDropped, TXBytesSent, TXBytesDropped );
   NextTick = Now + SecondsToMS( 10 );
  }

}

void loop( void ) {
  while ( 1 ) {
    if ( IsConnectedToWiFi ) {
      ARP_Tick( );

      while ( PlaybackBuffer( ) ) {
        SLIP_Tick( );
        yield( );
      }

      HeartBeat_Tick( );
      SLIP_Tick( );
    }

    yield( );
  }
}


