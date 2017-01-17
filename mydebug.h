#ifndef _MYDEBUG_H_
#define _MYDEBUG_H_

#define DEBUG_UART
// #define DEBUG_ETHERFRAME
// #define DEBUG_UDP

#if defined ( DEBUG_UART )
#define DebugPrintf DebugPrintf_UART
#elif defined ( DEBUG_ETHERFRAME )
#define DebugPrintf DebugPrintf_EtherFrame
#elif defined ( DEBUG_UDP )
#define DebugPrintf DebugPrintf_UDP
#else
#define DebugPrintf( a, ... )
#endif

/*
 * Sends a printf formatted string and arguments to the serial port. 
 */
int DebugPrintf_UART( const char* Message, ... );

/*
 * Sends a printf formatted string and arugments over WiFi with an ethertype of 0xBEEF. 
 */
int DebugPrintf_EtherFrame( const char* Message, ... );

/*
 * Sends a printf formatted string and arguments as a UDP broadcast to port 7810. 
 */
int DebugPrintf_UDP( const char* Message, ... );

#endif
