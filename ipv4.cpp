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

static uint8_t TempBuffer[ 4096 ];

int UDP_BuildOutgoingPacket( uint32_t SourceIP, uint32_t TargetIP, uint16_t Port, const uint8_t* Data, int DataLength ) {
    uint8_t DestinationMACAddress[ MACAddressLen ];
    uint8_t* BufferPtr = TempBuffer;
    int BytesToWrite = 0;

    if ( Route( TargetIP, DestinationMACAddress ) ) {
        BytesToWrite+= PrepareEthernetHeader( ( struct EtherFrame* ) TempBuffer, OurMACAddress, DestinationMACAddress, EtherType_IPv4 );
        BytesToWrite+= PrepareTCPHeader( ( struct ip_packet* ) ( TempBuffer + BytesToWrite ), SourceIP, TargetIP, DataLength, 0 );
        BytesToWrite+= PrepareUDPHeader( ( struct udp_packet* ) ( TempBuffer + BytesToWrite ), Port, DataLength );

        memcpy( ( TempBuffer + BytesToWrite ), Data, DataLength );
        BytesToWrite+= DataLength;

        EtherWrite( TempBuffer, BytesToWrite );
        return 1;
    } else {
        DebugPrintf( "UDP_BuildOutgoingPacket: No route to host.\n" );
    }

    return 0;
}

int PrepareTCPHeader( struct ip_packet* IPHeader, const uint32_t SourceIP, const uint32_t DestIP, int DataLength, int DontFragment ) {
  IPHeader->HeaderLengthInWords = 5;
  IPHeader->Version = 4;
  IPHeader->TypeOfService = 0;
  IPHeader->Length = htons( DataLength + sizeof( struct ip_packet ) + sizeof( struct udp_packet ) );
  IPHeader->Identification = millis( ) & 0xFFFF;
  IPHeader->Fragment = htons( DontFragment ? 2 : 0 );
  IPHeader->TimeToLive = 127;
  IPHeader->Protocol = IP_PROTO_UDP;
  IPHeader->HeaderChecksum = 0;
  IPHeader->SourceIP = SourceIP;
  IPHeader->DestIP = DestIP;
  IPHeader->HeaderChecksum = inet_chksum( ( void* ) IPHeader, sizeof( struct ip_packet ) );

  return sizeof( struct ip_packet );
}

int PrepareUDPHeader( struct udp_packet* UDPHeader, uint16_t Port, int DataLength ) {
  UDPHeader->SourcePort = htons( Port - 11 );
  UDPHeader->DestPort = htons( Port );
  UDPHeader->Length = htons( sizeof( struct udp_packet ) + DataLength );
  UDPHeader->Checksum = 0;

  return sizeof( struct udp_packet );
}

int TCP_EtherEncapsulate( const uint8_t* Packet, int Length ) {
  const struct ip_packet* IPHeader = ( const struct ip_packet* ) Packet;
  static uint8_t Buffer[ 2048 ];
  struct EtherFrame* FrameHeader = ( struct EtherFrame* ) Buffer;
  int Local = 0;

  Local = AreWeOnTheSameSubnet( IPHeader->DestIP );

  if ( ARP_RequestMACFromIP_Blocking( Local ? IPHeader->DestIP : ( uint32_t ) OurGateway, FrameHeader->DestMAC ) ) {
    if ( ( Length + sizeof( struct EtherFrame ) ) > sizeof( Buffer ) ) {
      DebugPrintf( "FATAL: EtherWrite packet overrun!\n" );
      Length = 0;
    }

    memcpy( &Buffer[ sizeof( struct EtherFrame ) ], Packet, Length );
    memcpy( FrameHeader->SourceMAC, OurMACAddress, MACAddressLen );

    FrameHeader->LengthOrType = htons( EtherType_IPv4 );
    Length+= sizeof( struct EtherFrame );

    EtherWrite( Buffer, Length );
    return 1;
  } else {
    DebugPrintf( "Timeout or didn't get target MAC\n" );
  }

  return 0;
}

/*
 * Returns 1 if the given IP address is a broadcast address. 
 * NOTE: 
 * This expects the IP and Mask to be in HOST BYTE ORDER. 
 */
int IsBroadcastIP( uint32_t IP, uint32_t Mask ) {
    return ( IP & ~Mask ) == 255 ? 1 : 0;
}

int Route( uint32_t IPAddr, uint8_t* MACAddress ) {
  int IsLocalAddress = 0;
  int Result = 0;

  if ( IsBroadcastIP( ntohl( IPAddr ), ntohl( OurNetmask ) ) ) {
    memset( MACAddress, 0xFF, MACAddressLen );
    Result = 1;
  }
  else {
    IsLocalAddress = AreWeOnTheSameSubnet( IPAddr );
    Result = ARP_RequestMACFromIP_Blocking( IsLocalAddress ? IPAddr : ( uint32_t ) OurGateway, MACAddress );
  }

  return Result;
}

#define DHCP_PORT 67
#define DHCP_Opcode_Request 1
#define DHCP_Opcode_Reply 2

enum {
    DHCP_Field_Pad = 0,
    DHCP_Field_Hostname = 12,
    DHCP_Field_Request = 53,
    DHCP_Field_ClientIdentifier = 61,
    DHCP_Field_End = 255
};

enum {
    DHCPDISCOVER = 1,
    DHCPOFFER,
    DHCPREQUEST,
    DHCPDECLINE,
    DHCPACK,
    DHCPNACK,
    DHCPRELEASE,
    DHCPINFORM
};

struct dhcp_option {
    uint8_t Opcode;
    uint8_t Length;
    uint8_t Data[ 1 ];
};

struct dhcp_packet {
    uint8_t Opcode;
    uint8_t HardwareType;
    uint8_t HardwareAddressLen;
    uint8_t Hops;
    uint32_t TransactionID;
    uint16_t Seconds;
    uint16_t Flags;

    uint32_t ClientIPAddress;
    uint32_t YourIPAddress;
    uint32_t ServerIPAddress;
    uint32_t GatewayIPAddress;

    uint8_t ClientHWAddress[ 16 ];
    signed char ServerName[ 64 ];
    signed char BootFileName[ 128 ];

    uint8_t Options[ 64 ];
} __attribute__( ( packed ) );

void DHCPRequest( void ) {
    static uint8_t Buffer[ 512 ];
    struct dhcp_packet* Request = ( struct dhcp_packet* ) Buffer;
    struct dhcp_option* Option = NULL;
    int BytesToWrite = 0;

    memset( Buffer, 0, sizeof( Buffer ) );

    Request->Opcode = DHCP_Opcode_Request;
    Request->HardwareType = 1;
    Request->HardwareAddressLen = MACAddressLen;
    Request->Hops = 0;
    Request->TransactionID = millis( );
    Request->Seconds = 0;
    Request->Flags = htons( 0 );
    Request->ClientIPAddress = 0;
    Request->YourIPAddress = 0;
    Request->ServerIPAddress = 0;
    Request->GatewayIPAddress = 0;

    memcpy( Request->ClientHWAddress, OurMACAddress, MACAddressLen );
    memset( Request->ServerName, 0, sizeof( Request->ServerName ) );
    memset( Request->BootFileName, 0, sizeof( Request->BootFileName ) );

    BytesToWrite = sizeof( struct dhcp_packet ) - 64;

    Buffer[ BytesToWrite++ ] = 99;
    Buffer[ BytesToWrite++ ] = 130;
    Buffer[ BytesToWrite++ ] = 83;
    Buffer[ BytesToWrite++ ] = 99;

    Option = ( struct dhcp_option* ) &Buffer[ BytesToWrite ];
    Option->Opcode = DHCP_Field_Request;
    Option->Length = 1;
    Option->Data[ 0 ] = DHCPDISCOVER;

    BytesToWrite+= sizeof( struct dhcp_option ) + Option->Length - sizeof( Option->Data );

    Option = ( struct dhcp_option* ) &Buffer[ BytesToWrite ];
    Option->Opcode = DHCP_Field_Hostname;
    Option->Length = 9;

    memcpy( Option->Data, "Buttslol\0", 9 );
    BytesToWrite+= sizeof( struct dhcp_option ) + Option->Length - sizeof( Option->Data );

    Buffer[ BytesToWrite++ ] = DHCP_Field_End;

    //DebugPrintf( "Size of dhcp_packet: %d bytes.\n", ( int ) sizeof( struct dhcp_packet ) );
    UDP_BuildOutgoingPacket( IPAddress( 0, 0, 0, 0 ), IPAddress( 255, 255, 255, 255 ), DHCP_PORT, Buffer, BytesToWrite );
}

void OnIPv4Packet( const uint8_t* Data, int Length, const struct EtherFrame* FrameHeader ) {
    struct ip_packet* IPHeader = ( struct ip_packet* ) Data;
    struct udp_packet* UDPHeader = NULL;
    char SourceMACStr[ 32 ];
    char DestMACStr[ 32 ];
    char SourceIPStr[ 32 ];
    char DestIPStr[ 32 ];

    MACsprintf( FrameHeader->SourceMAC, SourceMACStr, sizeof( SourceMACStr ) );
    MACsprintf( FrameHeader->DestMAC, DestMACStr, sizeof( DestMACStr ) );

    IPsprintf( IPHeader->SourceIP, SourceIPStr, sizeof( SourceIPStr ) );
    IPsprintf( IPHeader->DestIP, DestIPStr, sizeof( DestIPStr ) );

    if ( IPHeader->Protocol == IP_PROTO_UDP ) {
       UDPHeader = ( struct udp_packet* ) &Data[ IPHeader->HeaderLengthInWords * 4 ];

       DebugPrintf( "UDP: %d/%d\n", ( int ) ntohs( UDPHeader->SourcePort ), ( int ) ntohs( UDPHeader->DestPort ) );
    }
    else {
        DebugPrintf( "%s: [Src: %s] [Dst: %s] [Len: %d] [Proto:%02X]\n", __FUNCTION__, SourceMACStr, DestMACStr, Length, IPHeader->Protocol );
    }
}