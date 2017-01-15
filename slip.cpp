#include <ESP8266WiFi.h>
#include <lwip/netif.h>
#include <lwip/err.h>
#include "ether.h"
#include "ipv4.h"
#include "util.h"
#include "slip.h"
#include "mydebug.h"

#define SerialBufferSize 64

/*
 * HACKHACKHACK
 * I should hope that any de-escaped SLIP packet does not exceed
 * double the size of the maximum SLIP packet length. 
 */
static uint8_t PacketBuffer[ SLIPMaxPacketLen * 2 ];

static int SLIPPacketLength = 0;
static int IsInSLIPStream = 0;

void SLIP_PacketComplete( void ) {
    DebugPrintf( "%s: [Length: %d]\n", __FUNCTION__, SLIPPacketLength );
    TCP_EtherEncapsulate( PacketBuffer, SLIPPacketLength );
    SLIPPacketLength = 0;
}

int IsInESC = 0;

int CopyByteToPacketBuffer( uint8_t Data ) {
//    static int IsInESC = 0;
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

/*
 * Called every "frame" or run through the main loop. 
 */
void SLIP_Tick( void ) {
    static uint32_t Next = 0;
    uint8_t Buffer[ SerialBufferSize ];
    int BytesAvailable = 0;
    int BytesRead = 0;
    int i = 0;

    if ( millis( ) >= Next ) {
        DebugPrintf( "SLIP State: [IsInSLIP: %d], [Length: %d] [InEsc: %d]\n", IsInSLIPStream, SLIPPacketLength, IsInESC );
        Next = millis( ) + 5000;
    }

    if ( ( BytesAvailable = Serial.available( ) ) > 0 ) {
        BytesRead = Serial.readBytes( Buffer, BytesAvailable );

        for ( i = 0; i < BytesRead; i++ ) {
            if ( Buffer[ i ] == SLIP_END ) {
                if ( IsInSLIPStream == 0 ) {
                    /* Start of new SLIP stream. */
                    SLIPPacketLength = 0;
                    IsInSLIPStream = 1;
                } else {
                    /* End of SLIP stream. */
                    SLIP_PacketComplete( );

                    SLIPPacketLength = 0;
                    IsInSLIPStream = 0;
                }
            } else {
                if ( IsInSLIPStream )
                    CopyByteToPacketBuffer( Buffer[ i ] );
            }
        }
    }
}

/*
int SLIP_WritePacket( const uint8_t* Buffer, int Length ) {
    uint8_t OutputBuffer[ SerialBufferSize ];
    int BytesInOutputBuffer = 0;
    int BytesWritten = 0;
    uint32_t Start = 0;

    Start = millis( );
    OutputBuffer[ BytesInOutputBuffer++ ] = SLIP_END;

    while ( Length > 0 ) {
        if ( BytesInOutputBuffer == SerialBufferSize ) {
            Serial.write( OutputBuffer, SerialBufferSize );
            Serial.flush( );

            BytesWritten+= SerialBufferSize;
            Length-= SerialBufferSize;
            BytesInOutputBuffer = 0;

            continue;
        }

        if ( *Buffer == SLIP_END ) {
            Serial.write( OutputBuffer, BytesInOutputBuffer );
            Serial.flush( );

            BytesWritten+= BytesInOutputBuffer;
            Length-= BytesInOutputBuffer;
            BytesInOutputBuffer = 0;

            OutputBuffer[ BytesInOutputBuffer++ ] = 219;
            OutputBuffer[ BytesInOutputBuffer++ ] = 220;

            BytesWritten+= 2;
        }
        else {
            OutputBuffer[ BytesInOutputBuffer++ ] = *Buffer;
            BytesWritten++;
        }

        Length--;
        Buffer++;
    }

    if ( BytesInOutputBuffer == SerialBufferSize ) {
        Serial.write( OutputBuffer, SerialBufferSize );
        Serial.flush( );
    }
    else {
        OutputBuffer[ BytesInOutputBuffer++ ] = SLIP_END;

        Serial.write( OutputBuffer, BytesInOutputBuffer );
        Serial.flush( );
    }

    DebugPrintf( "%s: Took %dms\n", __FUNCTION__, ( int ) ( millis( ) - Start ) );

    return BytesWritten;
}
*/

int SLIP_WritePacket( const uint8_t* Buffer, int Length ) {
    int BytesWritten = 0;
    uint32_t Start = 0;
    uint32_t End = 0;

    Start = millis( );

    Serial.write( SLIP_END );
    BytesWritten++;

    DebugPrintf( "%s: Writing %d bytes to UART... ", __FUNCTION__, Length );

    while ( Length > 0 ) {
        if ( *Buffer == SLIP_END ) {
            Serial.write( 219 );
            Serial.write( 220 );

            BytesWritten+= 2;
        } else {
            Serial.write( *Buffer );
            BytesWritten++;
        }

        Buffer++;
        Length--;
    }

    Serial.write( SLIP_END );
    BytesWritten++;

    End = millis( );

    DebugPrintf( "done.\n" );
    //DebugPrintf( "%s: Took %dms\n", __FUNCTION__, ( int ) ( End - Start ) );

    return BytesWritten;
}

