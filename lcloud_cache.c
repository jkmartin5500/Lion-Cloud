////////////////////////////////////////////////////////////////////////////////
//
//  File           : lcloud_cache.c
//  Description    : This is the cache implementation for the LionCloud
//                   assignment for CMPSC311.
//
//   Author        : Jonathan Martin
//   Last Modified : 17 Apr 2020 7:03 PM EDT
//

// Includes 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <cmpsc311_log.h>
#include <lcloud_cache.h>

//
// Cache structure
typedef struct{
    char            buffer[256];                        // A buffer for the stored block's data
    int             entry_time;                         // The time the cache was entered into the block
    LcDeviceId      dev_id;                             // Device id of the stored block
    uint16_t        sec;                                // Sector id of the stored block
    uint16_t        blk;                                // Block id of the stored block
}lcloud_cache;

//
// Global Variables
lcloud_cache*       LRU_cache;                          // A pointer to the cache array
int                 hits, misses, cache_time;           // Talleys of hits, misses, and the cache_time
int                 cache_lines;                        // Number of lines in the cache


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
    int i;

    cache_time++;                                   // Increment cache time 

                                                    
    for(i = 0; i < cache_lines; i++) {              // Loop through the cache linearly
                                                    // If the block identifiers are equal, the block is in cache
        if (LRU_cache[i].dev_id == did && LRU_cache[i].sec == sec && LRU_cache[i].blk == blk) {
            hits++;                                 // Increment hits
            LRU_cache[i].entry_time = cache_time;   // Update the cache's time
            return( LRU_cache[i].buffer );
        }
    }
    misses++;                                       // Block wasn't retrieved, increment misses return null

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
    int i, least_time = cache_time, least_recent;
    lcloud_cache cache;

    cache_time++;                                       // Increment the running time

    for(i = 0; i < cache_lines; i++) {                  // Loop through cache linearly
        cache = LRU_cache[i];                           // Assign the current cache
                                                        // If the block identifier matches, we update that block
        if (cache.dev_id == did && cache.sec == sec && cache.blk == blk) {
            least_recent = i;                           // We update this block as its already in the cache
            break;                                      // Break out of the for loop to update the block
        } else if (cache.entry_time < least_time) {
            least_time = cache.entry_time;
            least_recent = i;
        }
    }
    cache.entry_time = cache_time;                      // The cache entry gets current cache time
    cache.dev_id = did;                                 // Cache entry gets the parameter device id
    cache.sec = sec;                                    // Cache entry gets the parameter device id
    cache.blk = blk;                                    // Cache entry gets the parameter device id

    strncpy(cache.buffer, block, 256);                  // Copy the input block's 256 bytes to the cache buffer

    LRU_cache[least_recent] = cache;                    // Input the cache entry to the cache array

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
    int i;
    cache_lines = maxblocks;                // Set the global cache_lines value

                                            // Dynamically allocate the cache array
    LRU_cache = (lcloud_cache *)malloc(sizeof(lcloud_cache) * cache_lines);
    for(i = 0; i < cache_lines; i++) {      // Loop through the allocated array
        LRU_cache[i].entry_time = -1;       // Set cache values to default values
        LRU_cache[i].dev_id = -1;
        LRU_cache[i].sec = -1;
        LRU_cache[i].blk = -1;
       memset(LRU_cache[i].buffer, 0, 256);
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

    free(LRU_cache);                // Free the cache array from memory, called during shutdown

    logMessage(LOG_OUTPUT_LEVEL, "Successfully de-allocated cache");
    logMessage(LOG_OUTPUT_LEVEL, "Hits: [%d] Misses[%d] Ratio: [%.2f]", hits, misses, ((float)hits / (hits + misses)));


    /* Return successfully */
    return( 0 );
}