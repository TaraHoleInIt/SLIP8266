#ifndef _IPV4_H_
#define _IPV4_H_

#define IP_PROTO_UDP 0x11

// Copied from netinet/ip.h
struct ip_packet {
    uint8_t HeaderLengthInWords : 4,
            Version : 4;

    uint8_t TypeOfService;
    uint16_t Length;                    
    uint16_t Identification;            
    uint16_t Fragment;                  

    int8_t TimeToLive;                  
    uint8_t Protocol;                   
    uint16_t HeaderChecksum;            

    uint32_t SourceIP;                  
    uint32_t DestIP;                    
} __attribute__( ( packed ) );

struct udp_packet {
    uint16_t SourcePort;
    uint16_t DestPort;
    uint16_t Length;
    uint16_t Checksum;
};

int PrepareTCPHeader( struct ip_packet* IPHeader, const uint32_t SourceIP, const uint32_t DestIP, int DataLength, int DontFragment, int Protocol );
int PrepareUDPHeader( struct udp_packet* UDPHeader, uint16_t Port, int DataLength );
int TCP_EtherEncapsulate( const uint8_t* Packet, int Length );
int Route( uint32_t IPAddr, uint8_t* MACAddress );
int UDP_BuildOutgoingPacket( uint32_t SourceIP, uint32_t TargetIP, uint16_t Port, const uint8_t* Data, int DataLength );
void OnIPv4Packet( const uint8_t* Data, int Length, const struct EtherFrame* FrameHeader );

#endif
