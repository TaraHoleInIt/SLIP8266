#ifndef _UTIL_H_
#define _UTIL_H_

/*
 * Formats a 6 byte MAC address into a string.
 */
int MACsprintf( const uint8_t* MAC, char* Buffer, size_t BufferLength );

/*
 * Formats a 4 byte IP address into a string.
 */
int IPsprintf( uint32_t IP, char* Buffer, int BufferLength );

/* Returns 1 if the given MAC address is a broadcast address (FF:FF:FF:FF:FF:FF) */
int IsMACBroadcast( uint8_t* MAC );

/* Returns 1 if the given MAC address is a zero address (00:00:00:00:00:00) */
int IsMACZero( uint8_t* MAC );

#endif

