////////////////////////////////////////////////////////////////////////////////
//
//  File          : lcloud_client.c
//  Description   : This is the client side of the Lion Clound network
//                  communication protocol.
//
//  Author        : Patrick McDaniel
//  Last Modified : Sat 28 Mar 2020 09:43:05 AM EDT
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
#include <cmpsc311_util.h>

int32_t socket_handle = -1;
struct sockaddr_in addr;

//
// Functions

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

LCloudRegisterFrame client_lcloud_bus_request( LCloudRegisterFrame reg, void *buf ) {
    int b0, b1, c0, c1, c2, d0, d1;
    char reg_buf[sizeof(LCloudRegisterFrame)];
    LCloudRegisterFrame inet_reg = htonll64(reg);
    LCloudRegisterFrame inet_resp, resp;

    if(socket_handle == -1) {
        addr.sin_family = AF_INET;
        addr.sin_port = htons(LCLOUD_DEFAULT_PORT);

        if(inet_aton(LCLOUD_DEFAULT_IP, &(addr.sin_addr)) == 0) return(-1);

        if((socket_handle = socket(AF_INET, SOCK_STREAM, 0)) == -1) return(-1);

        if(connect(socket_handle, (const struct sockaddr*) &(addr), sizeof(addr)) == -1) return(-1);
    }

    if(extract_lcloud_registers(reg, &b0, &b1, &c0, &c1, &c2, &d0, &d1) == -1) return(-1);
    
    memcpy(reg_buf, (char*) &inet_reg, sizeof(LCloudRegisterFrame));
    if(write(socket_handle, reg_buf, sizeof(LCloudRegisterFrame)) == -1) return(-1);
    memset(reg_buf, 0, sizeof(LCloudRegisterFrame));

    // Read
    if(c0 == LC_BLOCK_XFER && c2 == LC_XFER_READ) {

        if(read(socket_handle, reg_buf, sizeof(LCloudRegisterFrame)) == -1) return(-1);      
        if(read(socket_handle, buf, LC_DEVICE_BLOCK_SIZE) == -1) return(-1);
    }
    // Write
    else if(c0 == LC_BLOCK_XFER && c2 == LC_XFER_WRITE) {
        if(write(socket_handle, buf, LC_DEVICE_BLOCK_SIZE) == -1) return(-1);
        
        if(read(socket_handle, reg_buf, sizeof(LCloudRegisterFrame)) == -1) return(-1);
    }
    // Power off
    else if(c0 == LC_POWER_OFF) {
        if(read(socket_handle, reg_buf, sizeof(LCloudRegisterFrame)) == -1) return(-1);

        if(close(socket_handle) == -1) return(-1);
        socket_handle = -1;
    }
    // Other
    else {
        if(read(socket_handle, reg_buf, sizeof(LCloudRegisterFrame)) == -1) return(-1);
    }

    memcpy((char*) &inet_resp, reg_buf, sizeof(LCloudRegisterFrame));
    resp = htonll64(inet_resp);
    return(resp);
}

