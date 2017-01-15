#include <ESP8266WiFi.h>
#include <lwip/netif.h>
#include <lwip/err.h>
#include "util.h"
#include "ether.h"

extern "C" {
#include <netif/wlan_lwip_if.h>
#include <user_interface.h>
}

/*
 * Formats a 6 byte MAC address into a string.
 */
int MACsprintf( const uint8_t* MAC, char* Buffer, size_t BufferLength ) {
    return snprintf( Buffer, BufferLength, "%02X:%02X:%02X:%02X:%02X:%02X", MAC[ 0 ], MAC[ 1 ], MAC[ 2 ], MAC[ 3 ], MAC[ 4 ], MAC[ 5 ] );
}

/*
 * Formats a 4 byte IP address into a string.
 */
int IPsprintf( uint32_t IP, char* Buffer, int BufferLength ) {
    uint8_t* p = ( uint8_t* ) &IP;
    return snprintf( Buffer, BufferLength, "%d.%d.%d.%d", p[ 0 ], p[ 1 ], p[ 2 ], p[ 3 ] );
}

/* Returns 1 if the given MAC address is a broadcast address (FF:FF:FF:FF:FF:FF) */
int IsMACBroadcast( uint8_t* MAC ) {
    int i = 0;

    for ( i = 0; i < MACAddressLen; i++ ) {
        if ( MAC[ i ] != 0xFF )
            return 0;
    }

    return 1;
}

/* Returns 1 if the given MAC address is a zero address (00:00:00:00:00:00) */
int IsMACZero( uint8_t* MAC ) {
    int i = 0;

    for ( i = 0; i < MACAddressLen; i++ ) {
        if ( MAC[ i ] != 0 )
            return 0;
    }

    return 1;
}

