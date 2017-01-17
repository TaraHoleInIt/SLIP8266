#ifndef _SLIP_H_
#define _SLIP_H_

// From RFC 1055
#define SLIPMaxPacketLen 1006

#define SLIP_END 0xC0
#define SLIP_ESC 0xDB
#define SLIP_REPLACE 0xDC

typedef void ( SLIPCompleteCB ) ( uint8_t* Packet, int Length );
typedef void ( WriteByteFn ) ( uint8_t Data );
typedef uint8_t ( ReadByteFn ) ( void );

int SLIP_ReadPacket_Blocking( ReadByteFn ReadByte, SLIPCompleteCB OnSLIPComplete );
void SLIP_WritePacket_Blocking( WriteByteFn WriteByte, uint8_t* Packet, int Length );

/*
 * Called every "frame" or run through the main loop. 
 */
void SLIP_Tick( void );

int SLIP_WritePacket( const uint8_t* Buffer, int Length );
int SLIP_QueuePacketForWrite( const uint8_t* Buffer, int Length );

#endif
