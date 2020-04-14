////////////////////////////////////////////////////////////////////////////////
//
//  File           : lcloud_cache.c
//  Description    : This is the cache implementation for the LionCloud
//                   assignment for CMPSC311.
//
//   Author        : Patrick McDaniel
//   Last Modified : Thu 19 Mar 2020 09:27:55 AM EDT
//

// Includes 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <cmpsc311_log.h>
#include <lcloud_support.h>
#include <lcloud_cache.h>

typedef struct {
    char *data;
    LcDeviceId dev;
    uint16_t sec;
    uint16_t blk;
    uint16_t t;
} LcCacheBlk;

LcCacheBlk *cache_array;
int hitc, missc;
int max_blocks;
int cache_size;
uint16_t access_time = 0;

//
// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_getcache
// Description  : Search the cache for a block 
//
// Inputs       : did - device number of block to find
//                sec - sector number of block to find
//                blk - block number of block to find
// Outputs      : cache block if found (pointer), NULL if not or failure

char * lcloud_getcache( LcDeviceId did, uint16_t sec, uint16_t blk ) {
    for(int i = 0; i < cache_size; i++) {
        if(cache_array[i].dev == did && cache_array[i].sec == sec && cache_array[i].blk == blk) {
            hitc += 1;
            char *data_ptr = cache_array[i].data;
            logMessage(LcDriverLLevel, "Block [%d/%d/%d] (t = %d) retrieved from cache", cache_array[i].dev, cache_array[i].sec, cache_array[i].blk, cache_array[i].t);
            return(data_ptr);
        }
    }
    logMessage(LcDriverLLevel, "Block [%d/%d/%d] not found in cache", did, sec, blk);
    missc += 1;
    /* Return not found */
    return( NULL );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_putcache
// Description  : Put a value in the cache 
//
// Inputs       : did - device number of block to insert
//                sec - sector number of block to insert
//                blk - block number of block to insert
// Outputs      : 0 if succesfully inserted, -1 if failure

int lcloud_putcache( LcDeviceId did, uint16_t sec, uint16_t blk, char *block ) {
    int i;
    // Check if block is already in cache and update data and access time
    for(int j = 0; j < cache_size; j++) {
        if(cache_array[j].dev == did && cache_array[j].sec == sec && cache_array[j].blk == blk) {
            if((cache_array[j].data = realloc(cache_array[j].data, LC_DEVICE_BLOCK_SIZE * sizeof(LC_DEVICE_BLOCK_SIZE))) == NULL) return(-1);
            memcpy(cache_array[j].data, block, LC_DEVICE_BLOCK_SIZE);
            cache_array[j].t = access_time;
            access_time += 1;
            logMessage(LcDriverLLevel, "Block [%d/%d/%d] (t = %d) updated in cache", cache_array[j].dev, cache_array[j].sec, cache_array[j].blk, cache_array[j].t);
            return(0);
        }
    }
    // If size of cache is not maximum, add new block to cache
    if(cache_size < max_blocks) {
        cache_size += 1;
        i = cache_size - 1;
        if((cache_array[i].data = malloc(LC_DEVICE_BLOCK_SIZE * sizeof(char))) == NULL) return(-1);
    // Else, choose oldest block in cache to evict
    } else {
        int oldest = 0;
        for(int j = 1; j < cache_size; j++) {
            if(cache_array[j].t < cache_array[oldest].t) {
                oldest = j;
            }
        }
        i = oldest;
        logMessage(LcDriverLLevel, "Block [%d/%d/%d] (t = %d) evicted from cache", cache_array[i].dev, cache_array[i].sec, cache_array[i].blk, cache_array[i].t);
        if((cache_array[i].data = realloc(cache_array[i].data, LC_DEVICE_BLOCK_SIZE * sizeof(char))) == NULL) return(-1);
    }
    // Update data, device, sector, and block info
    memcpy(cache_array[i].data, block, LC_DEVICE_BLOCK_SIZE);
    cache_array[i].dev = did;
    cache_array[i].sec = sec;
    cache_array[i].blk = blk;
    cache_array[i].t = access_time;
    logMessage(LcDriverLLevel, "Block [%d/%d/%d] (t = %d) written to cache", did, sec, blk, access_time);
    access_time += 1;
    /* Return successfully */
    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_initcache
// Description  : Initialze the cache by setting up metadata a cache elements.
//
// Inputs       : maxblocks - the max number number of blocks 
// Outputs      : 0 if successful, -1 if failure

int lcloud_initcache( int maxblocks ) {
    if((cache_array = malloc(maxblocks * sizeof(LcCacheBlk))) == NULL) {
        return(-1);
    }
    max_blocks = maxblocks;
    for(int i = 0; i < max_blocks; i++) {
        cache_array[i].dev = -1;
        cache_array[i].sec = -1;
        cache_array[i].blk = -1;
        cache_array[i].t = -1;
    }
    /* Return successfully */
    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_closecache
// Description  : Clean up the cache when program is closing
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int lcloud_closecache( void ) {
    for(int i = 0; i < cache_size; i++) {
        free(cache_array[i].data);
        cache_array[i].data = NULL;
    }
    free(cache_array);
    cache_array = NULL;

    logMessage(LcDriverLLevel, "Total cache hits: %d", hitc);
    logMessage(LcDriverLLevel, "Total cache misses: %d",missc);
    logMessage(LcDriverLLevel, "Hit ratio: %f", (float) hitc / (hitc + missc));

    /* Return successfully */
    return( 0 );
}