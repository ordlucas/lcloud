#ifndef LCLOUD_FILESYS_INCLUDED
#define LCLOUD_FILESYS_INCLUDED

////////////////////////////////////////////////////////////////////////////////
//
//  File           : lcloud_filesys.h
//  Description    : This is the declaration of interface of the Lion
//                   Cloud device filesystem interface.
//
//   Author        : Patrick McDaniel
//   Last Modified : Sat Jan 25 09:30:06 PST 2020
//

// Includes
#include <stddef.h>
#include <stdint.h>

// Defines 

// Type definitions
typedef int32_t LcFHandle;
typedef uint64_t LCloudRegisterFrame;

// File system interface definitions
LcFHandle lcopen( const char *path );
    // Open the file for for reading and writing

int lcread( LcFHandle fh, char *buf, size_t len );
    // Read data from the file hande

int lcwrite( LcFHandle fh, char *buf, size_t len );
    // Write data to the file

int lcseek( LcFHandle fh, size_t off );
    // Seek to a specific place in the file

int lcclose( LcFHandle fh );
    // Close the file

int lcshutdown( void );
    // Shut down the filesystem

int extract_lcloud_registers(LCloudRegisterFrame resp, int *b0, int *b1, int *c0, int *c1, 
    int *c2, int *d0, int *d1);
    // Helper function to extract information from register frame

#endif
