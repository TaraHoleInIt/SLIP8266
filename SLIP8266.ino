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

err_t MyInputFn( struct pbuf* p, struct netif* inp ) {
  OnDataReceived( ( const uint8_t* ) p->payload, p->len );
  pbuf_free( p );
  return 0;
}

err_t MyOutputFn( struct netif* inp, struct pbuf* p, ip_addr_t* ipaddr ) {
  pbuf_free( p );
  return 0;
}

err_t MyLinkoutputFn( struct netif* inp, struct pbuf* p ) {
  pbuf_free( p );
  return 0;
}

int ConnectToWiFi( int Timeout ) {
  uint32_t WhenToGiveUp = millis( ) + Timeout;

  Serial.print( "Connecting to " );
  Serial.print( SSID );
  Serial.print( "..." );
  
  WiFi.begin( SSID, Password );
  WiFi.config( OurIPAddress, OurGateway, OurNetmask );

  while ( millis( ) < WhenToGiveUp ) {
    switch ( WiFi.status( ) ) {
      case WL_CONNECTED: {
        return 1;
      }
      default: {
        Serial.print( "." );
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

  while ( ! Serial.availableForWrite( ) )
    yield( );

  for ( i = 0; i < 32; i++ ) {
    Serial.write( 0 );
    delay( 1 );
  }
  
  Serial.println( "\nReady..." );

  do {
    IsConnectedToWiFi = ConnectToWiFi( 10000 );
  } while ( ! IsConnectedToWiFi );

  Serial.println( " Connected!" );
  Serial.print( "Local IP: " );
  Serial.println( WiFi.localIP( ) );

  WiFi.macAddress( OurMACAddress );
  OurIPAddress = WiFi.localIP( );

  ARP_Init( );
}

void HeartBeat_Tick( void ) {
  static uint32_t NextTick = 0;
  uint32_t Now = millis( );

  if ( Now >= NextTick ) {
   DebugPrintf( "%s: %d\n", __FUNCTION__, NextTick );
   NextTick = Now + SecondsToMS( 5 );
  }
}

void loop( void ) {
  if ( IsConnectedToWiFi ) {
    ARP_Tick( );

    HeartBeat_Tick( );
    SLIP_Tick( );
  }

  yield( );
}


