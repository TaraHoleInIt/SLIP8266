#include <ESP8266WiFi.h>
#include <lwip/netif.h>
#include <lwip/err.h>
#include <stdarg.h>
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

static char DebugTextBuffer[ 1024 ];

/*
 * Sends a printf formatted string and arguments to the serial port. 
 */
int DebugPrintf_UART( const char* Message, ... ) {
    int Length = 0;
    va_list Argp;

    va_start( Argp, Message );
    Length = vsnprintf( DebugTextBuffer, sizeof( DebugTextBuffer ), Message, Argp );
    va_end( Argp );

    Serial1.write( DebugTextBuffer );
    return Length;
}

/*
 * Sends a printf formatted string and arugments over WiFi with an ethertype of 0xBEEF. 
 */
int DebugPrintf_EtherFrame( const char* Message, ... ) {
    struct EtherFrame* FrameHeader = ( struct EtherFrame* ) DebugTextBuffer;
    int Length = 0;
    int Offset = 0;
    va_list Argp;

    Offset = PrepareEthernetHeader( FrameHeader, OurMACAddress, BroadcastMACAddress, 0xBEEF );

    va_start( Argp, Message );
    Length = vsnprintf( &DebugTextBuffer[ Offset ], sizeof( DebugTextBuffer ) - Offset, Message, Argp );
    va_end( Argp );

    EtherWrite( ( uint8_t* ) DebugTextBuffer, Offset + Length + 1 );
    return Length;
}

/*
 * Sends a printf formatted string and arguments as a UDP broadcast to port 7810. 
 */
int DebugPrintf_UDP( const char* Message, ... ) {
    int Length = 0;
    va_list Argp;

    va_start( Argp, Message );
    Length = vsnprintf( DebugTextBuffer, sizeof( DebugTextBuffer ), Message, Argp );
    va_end( Argp );

    UDP_BuildOutgoingPacket( OurIPAddress, IPAddress( 192, 168, 2, 255 ), 7810, ( const uint8_t* ) DebugTextBuffer, Length + 1 );
    return Length;
}
