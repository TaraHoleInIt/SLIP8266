#include <ESP8266WiFi.h>
#include <lwip/netif.h>
#include <lwip/err.h>
#include "ether.h"
#include "ipv4.h"
#include "util.h"
#include "slip.h"
#include "mydebug.h"

#define SerialBufferSize 64

#define DetailDebug( Message ) DebugPrintf( "%s::%s::%d: %s", __FILE__, __FUNCTION__, __LINE__, Message );

/*
 * HACKHACKHACK
 * I should hope that any de-escaped SLIP packet does not exceed
 * double the size of the maximum SLIP packet length. 
 */
static uint8_t PacketBuffer[ SLIPMaxPacketLen * 2 ];
static uint8_t SLIPBuffer[ SLIPMaxPacketLen * 2 ];

static uint8_t TXPacketBuffer[ SLIPMaxPacketLen * 2 ];
static volatile int IsTXPacketQueued = 0;
static volatile int TXPacketLength = 0;

uint32_t PacketStartTime = 0;
uint32_t PacketEndTime = 0;

extern volatile int TXBytesSent;
extern volatile int TXBytesDropped;

void SLIP_PacketComplete( const uint8_t* Packet, int Length ) {
    if ( Length > 0 )
        TCP_EtherEncapsulate( Packet, Length );
}

#if 0
int CopyByteToPacketBuffer( uint8_t Data ) {
    static int IsInESC = 0;
    int BytesWritten = 0;

    if ( ( SLIPPacketLength + 1 ) == sizeof( PacketBuffer ) ) {
        DebugPrintf( "FATAL: Packet buffer overrun.\n" );
        return 0;
    }

    if ( Data == 219 ) {
        IsInESC = 1;
    } else {
        if ( IsInESC ) {
            if ( Data == 220 ) 
                PacketBuffer[ SLIPPacketLength++ ] = 192;

            IsInESC = 0;
        } else {
            PacketBuffer[ SLIPPacketLength++ ] = Data;
        }
    }

    return 1;
}
#endif

int UnSLIP( const uint8_t* Src, uint8_t* Dest, int Size ) {
    int OutSize = 0;
    uint8_t T1 = 0;
    uint8_t T2 = 0;    
    int i = 0;

    for ( i = 0; i < Size; ) {
        T1 = Src[ i ];
        T2 = Src[ i + 1 ];

        if ( T1 == 219 && T2 == 220 ) {
            Dest[ OutSize++ ] = 192;
            i+= 2;
        } else {
            Dest[ OutSize++ ] = T1;
            i++;
        }
    }

    return OutSize;
}

static uint8_t LeftoverBuffer[ 512 ];
static int LeftoverBytes = 0;

int AvailableWrapper( void ) { 
    return LeftoverBytes ? LeftoverBytes : Serial.available( );
}

int ReadBytesWrapper( uint8_t* Buffer, int Length ) {
    int BytesRead = 0;
    int Count = 0;    

    if ( LeftoverBytes > 0 ) {
        /*
         * If there are even more bytes left over after this move 
         * them to the front of the Leftover buffer and adjust
         * the remaining byte count accordingly. 
         */
        if ( LeftoverBytes > Length ) {
            Count = LeftoverBytes - Length;

            memcpy( Buffer, LeftoverBuffer, Length );
            memmove( LeftoverBuffer, &LeftoverBuffer[ Length ], Count );

            LeftoverBytes-= Count;
            BytesRead = Length;
        } else {
            /* 
             * There are less than or equal to the number of bytes requested in 
             * the Leftover buffer, copy in what we have. 
             */
             memcpy( Buffer, LeftoverBuffer, LeftoverBytes );
             
             BytesRead = LeftoverBytes;
             LeftoverBytes = 0;
        }
    } else {
        BytesRead = Serial.readBytes( Buffer, Length );
    }

    return BytesRead;
}

int SLIP_ReadUntilEND( uint8_t* Buffer, int BufferMaxLen ) {
    uint8_t RXBuffer[ 256 ];
    uint8_t Data = 0;
    int IsInSLIPStream = 0;
    int BytesAvailable = 0;
    int SLIPLength = 0;
    int BytesRead = 0;
    int i = 0;

    IsInSLIPStream = 1;

    while ( IsInSLIPStream == 1 ) {
        BytesRead = Serial.readBytes( &Data, 1 );

        if ( BytesRead == 1 ) {
            if ( Data == SLIP_END ) {
                PacketEndTime = millis( );
                IsInSLIPStream = 0;

                if ( SLIPLength > 0 )
                    DebugPrintf( "SLIP: UART Read %d bytes in %dms.\n", SLIPLength, ( int ) ( PacketEndTime - PacketStartTime ) );
            } else {
                Buffer[ SLIPLength++ ] = Data;
            }
        }
    }

/*
    while ( IsInSLIPStream ) {
        BytesAvailable = AvailableWrapper( );

        if ( BytesAvailable > 0 ) {
            BytesRead = ReadBytesWrapper( RXBuffer, BytesAvailable > sizeof( RXBuffer ) ? sizeof( RXBuffer ) : BytesAvailable );

            for ( i = 0; i < BytesRead; i++ ) {
                if ( RXBuffer[ i ] == SLIP_END ) {
                    PacketEndTime = millis( );
                    IsInSLIPStream = 0;

                    DebugPrintf( "SLIP: UART Read %d bytes in %dms.\n", SLIPLength, ( int ) ( PacketEndTime - PacketStartTime ) );
                    break;
                }

                if ( ( SLIPLength + 1 ) < BufferMaxLen )
                    Buffer[ SLIPLength++ ] = RXBuffer[ i ];
            }

            if ( i < BytesRead ) {
                memcpy( LeftoverBuffer, &RXBuffer[ i ], ( BytesRead - i ) );
                LeftoverBytes = ( BytesRead - i );
            }
        }
    }
*/

    return SLIPLength;
}

/*
 * Called every "frame" or run through the main loop. 
 */
void SLIP_Tick( void ) {
    int SLIPLength = 0;
    int PacketLength = 0;
    uint8_t Data = 0;

    if ( Serial.readBytes( &Data, 1 ) == 1 && Data == SLIP_END ) {
        PacketStartTime = millis( );
        SLIPLength = SLIP_ReadUntilEND( SLIPBuffer, sizeof( SLIPBuffer ) );

        if ( SLIPLength > 0 ) {
            PacketLength = UnSLIP( SLIPBuffer, PacketBuffer, SLIPLength );

            if ( PacketLength > 0 )
                SLIP_PacketComplete( PacketBuffer, SLIPLength );

            SLIPLength = 0;
        }
    }

/*
    if ( IsTXPacketQueued ) {
        SLIP_WritePacket( ( const uint8_t* ) TXPacketBuffer, TXPacketLength );

        TXPacketLength = 0;
        IsTXPacketQueued = 0;
    }
*/
}

int SLIP_QueuePacketForWrite( const uint8_t* Buffer, int Length ) {
    const uint8_t* Ptr = Buffer;
    int Len = Length;

    if ( IsTXPacketQueued ) {
        DebugPrintf( "%s: TX Buffer busy, dropping packet.\n", __FUNCTION__ );
        TXBytesDropped+= Len;

        return 0;
    }

    //DebugPrintf( "%s: %d bytes queued for write.\n", __FUNCTION__, Length );
    memcpy( TXPacketBuffer, Ptr, Len );
    
    TXPacketLength = Len;
    IsTXPacketQueued = 1;

    return 1;
}

int SLIP( const uint8_t* Src, int SrcLen, uint8_t* Dest, int MaxDestLen ) {
    int OutLength = 0;
    int i = 0;

    Dest[ OutLength++ ] = SLIP_END;

    for ( i = 0; i < SrcLen && i < MaxDestLen; i++ ) {
        if ( Src[ i ] == SLIP_END ) {
            Dest[ OutLength++ ] = 219;
            Dest[ OutLength++ ] = 220;
        } else {
            Dest[ OutLength++ ] = Src[ i ];
        }
    }

    Dest[ OutLength++ ] = SLIP_END;
    return OutLength;
}

int SLIP_WritePacket( const uint8_t* Buffer, int Length ) {
    static uint8_t OutBuffer[ 2048 ];
    uint8_t* BufPtr = OutBuffer;
    int BytesSLIPPED = 0;
    int BytesWritten = 0;
    int Temp = 0;
    uint32_t Start = 0;
    uint32_t End = 0;

    Start = millis( );

    //Serial.flush( );

    if ( ( BytesSLIPPED = SLIP( Buffer, Length, OutBuffer, sizeof( OutBuffer ) ) ) > 0 ) {
        while ( BytesSLIPPED > 0 ) {
            /*
            Temp = Serial.write( BufPtr, BytesSLIPPED >= 64 ? 64 : BytesSLIPPED );

            BytesWritten+= Temp;
            BytesSLIPPED-= Temp;
            BufPtr+= Temp;
            */
            Serial.write( *BufPtr++ );
            
            BytesSLIPPED--;
            BytesWritten++;
        }
    }

    //Serial.flush( );

    End = millis( );
    //DebugPrintf( "%s: Wrote %d bytes in %dms\n", __FUNCTION__, BytesWritten, ( int ) ( End - Start ) );

    TXBytesSent+= BytesWritten;
    return BytesWritten;
}

