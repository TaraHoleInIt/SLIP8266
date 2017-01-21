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
static uint8_t PacketBuffer[ SLIPMaxPacketLen * 4 ];
static int SLIPPacketLength = 0;

static uint8_t TXPacketBuffer[ SLIPMaxPacketLen * 4 ];
static volatile int IsTXPacketQueued = 0;
static volatile int TXPacketLength = 0;

uint32_t PacketStartTime = 0;
uint32_t PacketEndTime = 0;

extern volatile int TXBytesSent;
extern volatile int TXBytesDropped;

void SLIP_PacketComplete( void ) {
    if ( SLIPPacketLength > 0 ) {
        TCP_EtherEncapsulate( PacketBuffer, SLIPPacketLength );
        DebugPrintf( "%s: %d byte SLIP Packet read in %dms.\n", __FUNCTION__, SLIPPacketLength, ( int ) ( PacketEndTime - PacketStartTime ) );
    }

    SLIPPacketLength = 0;
}

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

static int IsInSLIPStream = 0;

static uint8_t SLIPPacket[ SLIPMaxPacketLen * 2 ];
static int SizeOfSLIPPacket = 0;

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

/*
 * Called every "frame" or run through the main loop. 
 */
void SLIP_Tick( void ) {
    static uint8_t Buffer[ 1024 ];
    int BytesAvailable = 0;
    int BytesRead = 0;
    int i = 0;

    if ( ( BytesAvailable = AvailableWrapper( ) ) > 0 ) {
        //DebugPrintf( "%s: Reading %d bytes from UART...", __FUNCTION__, BytesAvailable );
        BytesRead = ReadBytesWrapper( Buffer, BytesAvailable );

        for ( i = 0; i < BytesRead; i++ ) {
            if ( Buffer[ i ] == SLIP_END ) {
                if ( IsInSLIPStream == 0 ) {
                    /* Start of new SLIP stream. */
                    SLIPPacketLength = 0;
                    IsInSLIPStream = 1;
                    PacketStartTime = millis( );
                } else {
                    PacketEndTime = millis( );

                    /* End of SLIP stream. */
                    SLIP_PacketComplete( );

                    SLIPPacketLength = 0;
                    IsInSLIPStream = 0;
                    PacketEndTime = 0;
                }
            } else {
                if ( IsInSLIPStream )
                    CopyByteToPacketBuffer( Buffer[ i ] );
            }
        }

        //DebugPrintf( " Done.\n" );
    }

    if ( IsTXPacketQueued ) {
        SLIP_WritePacket( ( const uint8_t* ) TXPacketBuffer, TXPacketLength );

        TXPacketLength = 0;
        IsTXPacketQueued = 0;
    }
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
    
    TXBytesSent+= Len;

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

    Serial.flush( );

    if ( ( BytesSLIPPED = SLIP( Buffer, Length, OutBuffer, sizeof( OutBuffer ) ) ) > 0 ) {
        while ( BytesSLIPPED > 0 ) {
            Temp = Serial.write( BufPtr, BytesSLIPPED >= 64 ? 64 : BytesSLIPPED );

            BytesWritten+= Temp;
            BytesSLIPPED-= Temp;
            BufPtr+= Temp;
        }
    }

    Serial.flush( );

    End = millis( );
    //DebugPrintf( "%s: Wrote %d bytes in %dms\n", __FUNCTION__, BytesWritten, ( int ) ( End - Start ) );

    return BytesWritten;
}

