////////////////////////////////////////////////////////////////////////////////
//
//  File          : lcloud_client.c
//  Description   : This is the client side of the Lion Clound network
//                  communication protocol.
//
//  Author        : Jonathan Martin
//  Last Modified : Thu 30 Apr 2020 1:57 AM EDT
//

// Include Files
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>

// Project Include Files
#include <lcloud_network.h>
#include <cmpsc311_log.h>
#include <lcloud_filesys.h>
#include <cmpsc311_util.h>

//
// Global Variables
LcFHandle       socket_handle = -1;         // Socket handle to connect to, initialized to -1 for setup
int64_t         b0, b1, c0, c1, c2, d0, d1;                                         // Holders for 7 operation registers

//
// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_client_extract_registers
// Description  : Takes a packed register and unpacks it to get the register values
//                then places the register values into given pointers
//
// Inputs       : resp - A 64 bit register structure
//                pointers - pointers to registers to place the extraction
// Outputs      : register values in pointers and 0 if successful test, -1 if failure

int lcloud_client_extract_registers(LCloudRegisterFrame resp, int64_t *B0_4bit, int64_t *B1_4bit, int64_t *C0_8bit, 
                             int64_t *C1_8bit, int64_t *C2_8bit, int64_t *D0_16bit, int64_t *D1_16bit) {

    *D1_16bit = resp & 0xffff;                                              // Place last 16 bits from resp into D1_16bit 
    resp = resp >> 16;                                                      // Shift over 16 bits

    *D0_16bit = resp & 0xffff;                                              // Place last 16 bits from resp into D0_16bit
    resp = resp >> 16;                                                      // Shift over 16 bits

    *C2_8bit = resp & 0xff;                                                 // Place last 8 bits from resp into C2_8bit
    resp = resp >> 8;                                                       // Shift over 8 bits

    *C1_8bit = resp & 0xff;                                                 // Place last 8 bits from resp into C1_8bit
    resp = resp >> 8;                                                       // Shift over 8 bits

    *C0_8bit = resp & 0xff;                                                 // Place last 8 bits from resp into C0_8bit
    resp = resp >> 8;                                                       // Shift over 8 bits

    *B1_4bit = resp & 0xf;                                                  // Place last 4 bits from resp into B1_4bit
    resp = resp >> 4;                                                       // Shift over 4 bits

    *B0_4bit = resp & 0xf;                                                  // Place last 4 bits from resp into B0_4bit

    return( 0 );
} 

////////////////////////////////////////////////////////////////////////////////
//
// Function     : client_lcloud_bus_request
// Description  : This the client regstateeration that sends a request to the 
//                lion client server.   It will:
//
//                1) if INIT make a connection to the server
//                2) send any request to the server, returning results
//                3) if CLOSE, will close the connection
//
// Inputs       : reg - the request reqisters for the command
//                buf - the block to be read/written from (READ/WRITE)
// Outputs      : the response structure encoded as needed

LCloudRegisterFrame client_lcloud_bus_request(LCloudRegisterFrame reg, void *buf) {
    LCloudRegisterFrame nbo, hbo;
    // If there isn't an open connection already created
    // Use a global variable 'socket_handle', set initially equal to '-1'.

    // IF 'socket_handle' == -1, there is no open connection.
    // ELSE, there is an open connection.

    // IF there isn't an open connection already created, three things need 
    // to be done.
    //    (a) Setup the address
    //    (b) Create the socket
    //    (c) Create the connection

    if ( socket_handle == -1 ) {                                // IF 'socket_handle' == -1, there is no open connection.
                                                                // Step - Setup the address
        struct sockaddr_in client_addr;                         // Create an address structure
        client_addr.sin_family = AF_INET;                       // Set the adress family
        client_addr.sin_port = htons(LCLOUD_DEFAULT_PORT);      // Set the port to the default port
        if ( inet_aton(LCLOUD_DEFAULT_IP, &client_addr.sin_addr) == 0 ) {   // Using the default ip, create a useful structure
            logMessage(LOG_ERROR_LEVEL, "Failure converting address [%s]", LCLOUD_DEFAULT_IP);
            return( -1 );
        }
                                                                // Step - Create the socket
        socket_handle = socket(PF_INET, SOCK_STREAM, 0);        // Create the socket
        if (socket_handle == -1) {                              // If there was an error creating the socket, function fails
            logMessage(LOG_ERROR_LEVEL, "Error on socket creation");  
            return( -1 );
        }   
                                                                // Step - Create the connection
        if ( connect(socket_handle, (const struct sockaddr *)&client_addr, sizeof(client_addr)) == -1 ) {   // Connect to socket, catch errors
            logMessage(LOG_ERROR_LEVEL, "Error on socket connect [%d]", socket_handle);
            return( -1 );
        }
    }
    
    lcloud_client_extract_registers(reg, &b0, &b1, &c0, &c1, &c2, &d0, &d1);    // Extract the input register to get opcode registers
    nbo = htonll64(reg);                                                        // Convert the register to netweork byte order

    // CASE 1: read operation (look at the c0 and c2 fields)
    // SEND: (reg) <- Network format : send the register reg to the network
    // after converting the register to 'network format'.
    //
    // RECEIVE: (reg) -> Host format
    //          256-byte block (Data read from that frame)
    // Read registers: 0, 0, LC_BLOCK_XFER, dev_id, LC_XFER_READ, sec, blk
    if ( c0 == LC_BLOCK_XFER && c2 == LC_XFER_READ) {
        if ( write(socket_handle, &nbo, sizeof(nbo)) != sizeof(nbo) ) {
            logMessage(LOG_ERROR_LEVEL, "Client IO Bus [Read] failure writing register to socket [%d]", socket_handle);
            return ( -1 );
        }

        if ( read(socket_handle, &nbo, sizeof(nbo)) != sizeof(nbo) ) {
                logMessage(LOG_ERROR_LEVEL, "Client IO Bus [Read] failure reading register from socket [%d]", socket_handle);
                return( -1 );
        }

        if ( read(socket_handle, buf, LC_DEVICE_BLOCK_SIZE) != LC_DEVICE_BLOCK_SIZE ) {
            logMessage(LOG_ERROR_LEVEL, "Client IO Bus read error");
            return( -1 );
        }

        hbo = ntohll64(nbo);    // Convert the return register to host byte order for return
        return(hbo);            // Return the register in host byte order
    }

    // CASE 2: write operation (look at the c0 and c2 fields)
    // SEND: (reg) <- Network format : send the register reg to the network 
    // after converting the register to 'network format'.
    //       buf 256-byte block (Data to write to that frame)
    //
    // RECEIVE: (reg) -> Host format
    // Write registers: 0, 0, LC_BLOCK_XFER, dev_id, LC_XFER_WRITE, sec, blk
    else if ( c0 == LC_BLOCK_XFER && c2 == LC_XFER_WRITE ) {
        if ( write(socket_handle, &nbo, sizeof(nbo)) != sizeof(nbo) ) {
            logMessage(LOG_ERROR_LEVEL, "Client IO Bus [Write] failure writing register to socket [%d]", socket_handle);
            return ( -1 );
        }

        if ( write(socket_handle, buf, LC_DEVICE_BLOCK_SIZE) != LC_DEVICE_BLOCK_SIZE ) {
            logMessage(LOG_ERROR_LEVEL, "Client IO Bus write error");
            return( -1 );
        }

        if ( read(socket_handle, &nbo, sizeof(nbo)) != sizeof(nbo) ) {
                logMessage(LOG_ERROR_LEVEL, "Client IO Bus [Write] failure reading register from socket [%d]", socket_handle);
                return( -1 );
        }

        hbo = ntohll64(nbo);    // Convert the return register to host byte order for return
        return(hbo);            // Return the register in host byte order
    }

    // CASE 3: power off operation
    // SEND: (reg) <- Network format : send the register reg to the network 
    // after converting the register to 'network format'.
    //
    // RECEIVE: (reg) -> Host format
    //
    // Close the socket when finished : reset socket_handle to initial value of -1.
    // close(socket_handle)
    // Power off registers: 0, 0, LC_POWER_OFF, 0, 0, 0, 0
    else if ( c0 == LC_POWER_OFF ) {
        if ( write(socket_handle, &nbo, sizeof(nbo)) != sizeof(nbo) ) {
            logMessage(LOG_ERROR_LEVEL, "Client IO Bus [Power Off] failure writing register to socket [%d]", socket_handle);
            return ( -1 );
        }
        
        if ( read(socket_handle, &nbo, sizeof(nbo)) != sizeof(nbo) ) {
                logMessage(LOG_ERROR_LEVEL, "Client IO Bus [Power Off] failure reading register from socket [%d]", socket_handle);
                return( -1 );
        }

        close(socket_handle);   // Close the socket
        socket_handle = -1;     // Set to -1 to avoid calling operations on closed socket

        hbo = ntohll64(nbo);
        return(hbo);
    }

    // CASE 4: Other operations (probes, ...)
    // SEND: (reg) <- Network format : send the register reg to the network 
    // after converting the register to 'network format'.
    //
    // RECEIVE: (reg) -> Host format
    else {
        if ( write(socket_handle, &nbo, sizeof(nbo)) != sizeof(nbo) ) {
            logMessage(LOG_ERROR_LEVEL, "Client IO Bus [Other] failure writing register to socket [%d]", socket_handle);
            return ( -1 );
        }
        if ( read(socket_handle, &nbo, sizeof(nbo)) != sizeof(nbo) ) {
                logMessage(LOG_ERROR_LEVEL, "Client IO Bus [Other] failure reading register from socket [%d]", socket_handle);
                return( -1 );
        }

        hbo = ntohll64(nbo);    // Convert the return register to host byte order for return
        return(hbo);            // Return the register in host byte order
    }

    return (0); // Sucessful test
}

