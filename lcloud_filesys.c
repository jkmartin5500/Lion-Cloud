//////////////////////////////////////////////////////////////////////////////// 
//
//  File           : lcloud_filesys.c
//  Description    : This is the implementation of the Lion Cloud device 
//                   filesystem interfaces.
//
//   Author        : Jonathan Martin
//   Last Modified : Thu 30 Apr 2020 1:56 AM EDT
//

// Include files
#include <stdlib.h>
#include <string.h>
#include <cmpsc311_log.h>

// Project include files
#include <lcloud_filesys.h>
#include <lcloud_controller.h>
#include <lcloud_cache.h>
#include <lcloud_network.h>

//
// File system interface implementation

//
// Block structure
typedef struct {
    int         next_sector;    // The next block's sector number, for linking puposes
    int         next_block;     // The next block number, for linking purposes
    int         next_dev_id;    // The next block's device id, for linking purposes
    int         used;           // An integer representing whether the block is occupied, can be 0 or 1
} lcloud_block;

//
// Device structure
typedef struct {
    lcloud_block**  sector_block;   // 2D array for sector, block memory structure
    int             sectors;        // Store number of sectors available for device
    int             blocks;         // Store number of blocks available for device
    int             dev_id;         // An represents device id, -1 if never initialized
} lcloud_device;

//
// File structure
typedef struct {
    LcFHandle   fh;             // A unique file handle to the file
    char        name[260];      // A character array to hold path, windows 10 uses 260 characters, why can't we
    int         pos;            // The position of the read/write head for the file
    int         size;           // The size of the file
    int         block;          // Block number of the file's starting block
    int         sector;         // Sector number of the file's starting block
    int         dev_id;         // The device id
    int         opened;         // Tracker for whether the file was last opened or closed
}lcloud_file;

//
// Global variables 

LcFHandle       file_handle = 0;                                                    // Global tracker for file handles, initialized to -1
lcloud_file     files[0xfff];                                                       // Array to hold files, initialized for 4096 files
lcloud_device   devices[16];                                                        // Array to hold device structures
int64_t         b0, b1, c0, c1, c2, d0, d1;                                         // Holders for 7 operation registers

//
// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : create_lcloud_registers
// Description  : Takes registers as parameters and packs into a 64-bit word
//                to create a register structure.
//
// Inputs       : Integers - 7 registers to be packed
// Outputs      : A packed register if successful test, -1 if failure

LCloudRegisterFrame create_lcloud_registers(int64_t B0_4bit, int64_t B1_4bit, int64_t C0_8bit, int64_t C1_8bit, 
                                            int64_t C2_8bit, int64_t D0_16bit, int64_t D1_16bit) {

    LCloudRegisterFrame packed_register;                                    // The register to be returned
    int64_t t_b0, t_b1, t_c0, t_c1, t_c2, t_d0, t_d1;                       // Temporary registers for packing         

    // Get the bits we need
    t_b0 = (B0_4bit & 0xf) << 60;                                           // Get last 4 bits, shift to first register position
    t_b1 = (B1_4bit & 0xf) << 56;                                           // Get last 4 bits, shift to second register position
    t_c0 = (C0_8bit & 0xff) << 48;                                          // Get last 8 bits, shift to third register position
    t_c1 = (C1_8bit & 0xff) << 40;                                          // Get last 8 bits, shift to fourth register position
    t_c2 = (C2_8bit & 0xff) << 32;                                          // Get last 8 bits, shift to fifth register position
    t_d0 = (D0_16bit & 0xffff) << 16;                                       // Get last 8 bits, shift to sixth register position
    t_d1 = (D1_16bit & 0xffff);                                             // Get last 8 bits, leave as seventh register position

    // Pack the register
    packed_register = t_b0 | t_b1 | t_c0 | t_c1 | t_c2 | t_d0 | t_d1;

    // Return packed register
    return(packed_register);
} 

////////////////////////////////////////////////////////////////////////////////
//
// Function     : extract_lcloud_registers
// Description  : Takes a packed register and unpacks it to get the register values
//                then places the register values into given pointers
//
// Inputs       : resp - A 64 bit register structure
//                pointers - pointers to registers to place the extraction
// Outputs      : register values in pointers and 0 if successful test, -1 if failure

int extract_lcloud_registers(LCloudRegisterFrame resp, int64_t *B0_4bit, int64_t *B1_4bit, int64_t *C0_8bit, 
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
// Function     : device_power_on
// Description  : Helper function to power on the devices and probe them
//
// Inputs       : None
// Outputs      : 0 on successful test, -1 otherwise

int device_power_on() {
    LCloudRegisterFrame frm, rfrm;
                                                                                            // Power on the devices
    frm = create_lcloud_registers(0, 0, LC_POWER_ON, 0, 0, 0, 0);
    if ( (frm == -1) || ((rfrm = client_lcloud_bus_request(frm, NULL)) == -1) ||
        (extract_lcloud_registers(rfrm, &b0, &b1, &c0, &c1, &c2, &d0, &d1)) ||
        (b0 != 1) || (b1 != 1) || (c0 != LC_POWER_ON) ) {
            logMessage( LOG_ERROR_LEVEL, "LC failure powering on");
            return( -1 );
    }

                                                                                            // Probe the devices
    frm = create_lcloud_registers(0, 0, LC_DEVPROBE, 0, 0, 0, 0);
    if ( (frm == -1) || ((rfrm = client_lcloud_bus_request(frm, NULL)) == -1) ||
        (extract_lcloud_registers(rfrm, &b0, &b1, &c0, &c1, &c2, &d0, &d1)) ||
        (b0 != 1) || (b1 != 1) || (c0 != LC_DEVPROBE)) {
            logMessage( LOG_ERROR_LEVEL, "LC failure probing device");
            return( -1 );
    }

    int id, i, j, probe = d0;
    lcloud_device dev;

    for(id = 0; id < 16; id++) {                                                            // Check the first 16 bits for devices
        if(probe & 1) {                                                                     // If the LSB is 1, then there is a device
                                                                                            // Initialize device
            frm = create_lcloud_registers(0, 0, LC_DEVINIT, id, 0, 0, 0);
            if ( (frm == -1) || ((rfrm = client_lcloud_bus_request(frm, NULL)) == -1) ||
                (extract_lcloud_registers(rfrm, &b0, &b1, &c0, &c1, &c2, &d0, &d1)) ||
                (b0 != 1) || (b1 != 1) || (c0 != LC_DEVINIT)) {
                    logMessage( LOG_ERROR_LEVEL, "LC failure initializing device");
                    return( -1 );
            }

            dev.dev_id = id;
            dev.sectors = d0;
            dev.blocks = d1;
            dev.sector_block = (lcloud_block **)malloc(d0 * sizeof(lcloud_block*));         // Allocate memory for the d0 sectors
                                                                                            // Create block structure for device
            for(i = 0; i < d0; i++) {
                dev.sector_block[i] = (lcloud_block *)malloc(d1 * sizeof(lcloud_block));    // Allocate memory for d1 blocks in each sector
                for(j = 0; j < d1; j++) {
                    dev.sector_block[i][j].next_sector = -1;                                // Let -1 represent no next sector
                    dev.sector_block[i][j].next_block = -1;                                 // Let -1 represent no next block
                    dev.sector_block[i][j].next_dev_id = -1;                                // Let -1 represent no next device
                    dev.sector_block[i][j].used = 0;                                        // Set all blocks to unused
                }
            }
            devices[id] = dev;
            logMessage(LOG_OUTPUT_LEVEL, "Successfully initialized device [%d] with [sectors:blocks] [%d:%d]", dev.dev_id, dev.sectors, dev.blocks);
        } else {
            devices[id].dev_id = -1;                                                        // device id of -1 means device is off
        }
        probe = probe >> 1;                                                                 // Shift probe to probe next device
    }
    lcloud_initcache(LC_CACHE_MAXBLOCKS);
    
    return( 0 );                                                                            // Successful test
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : validate_fh
// Description  : Determines if fh is valid and the associated file is open
//
// Inputs       : fh - A unique file handle
//                file - A pointer to the file to be assigned
// Outputs      : 0 for successful test, -1 otherwise

int validate_fh(LcFHandle fh, lcloud_file *file) {
    if(fh > file_handle) {
        logMessage(LOG_ERROR_LEVEL, "LC failure invalid file handle [%d]", fh);
        return( -1 );                                                       // Invalid file handle
    } else if(files[fh].opened == 0) {
        logMessage(LOG_ERROR_LEVEL, "LC failure file not opened [%d]", fh);
        return( -1 );                                                       // File at handle is not opened, also invalid
    }
    *file = files[fh];                                                      // If valid file handle, assign the file
    return( 0 );                                                            // Successful test
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : allocate_device
// Description  : Linearly assigns a device, block, and id for use
//
// Inputs       : *sec - the address of the file's sector
//                *blk - the address of the file's block
// Outputs      : 0 for successful test, -1 otherwise

int allocate_block(int *sec,int *blk) {
    int id, i, j;
    lcloud_device dev;
    for(id = 0; id < 16; id++) {
        dev = devices[id];
        if (dev.dev_id != -1) {                                     // If the device was initialized
            for(i = 0; i < dev.sectors; i++) {                      // Loop through the 2D array
                for(j = 0; j < dev.blocks; j++) {
                    if(dev.sector_block[i][j].used == 0) { 
                        *sec = i;
                        *blk = j;
                        dev.sector_block[i][j].used = 1;
                        return( id );                               // Return id of allocated block
                    }
                }
            }
        }
    }
    logMessage( LOG_ERROR_LEVEL, "LC failure allocating block, memory structure full.");
    return( -1 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : get_block
// Description  : Traverses through a file's block list to find the last block used to write to
//
// Inputs       : file - A file passed by value
//                sec - A pointer to the sector to assign the sector id
//                blk - A pointer to the block to assign the block id
// Outputs      : 0 for successful test, -1 otherwise

int get_block(lcloud_file file, int *sec, int *blk) {
    int next_sec = file.sector, next_blk = file.block, i = 0, curr_sec, curr_blk;
    lcloud_device next_dev = devices[file.dev_id], curr_dev;

    do {
        if((next_sec == -1) || (next_blk == -1)) {                                  // The file should have allocated blocks up to file position
                                                                                    // Thus, should not occur before loop parameter is triggered
            logMessage( LOG_ERROR_LEVEL, "LC failure fetching block, invalid file position.");
            return( -1 );
        }
                                                                                    // Standard loop through linked list
        curr_sec = next_sec;                                                        // Next ids become current
        curr_blk = next_blk;
        curr_dev = next_dev;                                          
        next_dev = devices[curr_dev.sector_block[curr_sec][curr_blk].next_dev_id];  // Get the next device to get the next block from
        next_sec = curr_dev.sector_block[curr_sec][curr_blk].next_sector;           // Next ids from new current become the new next ids
        next_blk = curr_dev.sector_block[curr_sec][curr_blk].next_block;

        i += 256;                                                                   // Increment i by block length because we are comparing it to file position
    } while(i <= file.pos);                                                         // If i > file.pos, we curr_blk holds the file position

    *sec = curr_sec;                                                                // Assign sec and blk to the retrieved ids
    *blk = curr_blk;

    return( curr_dev.dev_id );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : add_block
// Description  : Gets the last block of the file, chooses a new block, and adds the block to the linked list
//
// Inputs       : file - A file structure passed by value
// Outputs      : 0 for successful test, -1 otherwise

int add_block(lcloud_file file) {
    int sec, blk, next_sec, next_blk, dev_id, next_dev_id;
    file.pos--;                                                             // Decrement the file pos so get_block doesn't try to get an unallocated block
                                                                            // Note that add_block only gets called at file.pos % 256 = 0, so always decrement
    if ( ((dev_id = get_block(file, &sec, &blk)) == -1) || 
         ((next_dev_id = allocate_block(&next_sec, &next_blk)) == -1) ) {   // If get_block returns -1, test fails
        return( -1 );
    }

    devices[dev_id].sector_block[sec][blk].next_sector = next_sec;          // Assign to the retrieved block the id of the next sector
    devices[dev_id].sector_block[sec][blk].next_block = next_blk;           // Assign to the retrieved block the id of the next block
    devices[dev_id].sector_block[sec][blk].next_dev_id = next_dev_id;       // Assign to the retrieved block the device id of the next block

    logMessage(LOG_OUTPUT_LEVEL, "Allocated block for data [%d/%d/%d]", file.dev_id, next_sec, next_blk);
    return( 0 );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcopen
// Description  : Open the file for for reading and writing
//
// Inputs       : path - the path/filename of the file to be read
// Outputs      : file handle if successful test, -1 if failure

LcFHandle lcopen( const char *path ) {
    if(file_handle == 0) {                                                  // First open operation, power on devices
        if(device_power_on() == -1) {                                       // Start by powering on device
            return(-1);                                                     // Throw error if device_power_on fails
        }
    }

    // Check if the file already exists
    LcFHandle fh;
    for(fh = 0; fh < file_handle; fh++) {                                   // When no name matches the path, fh = file_handle and is unique
        if(files[fh].name == path) {                                        // If a file with this path exists, check if it is already opened
            if(files[fh].opened == 1) {
                return( -1 );                                               // If the file is already opened, the function fails
                logMessage( LOG_ERROR_LEVEL, "LC failure opening file, file already opened.");
            } else {                                                        // Otherwise, open the file
                files[fh].pos = 0;                                          // Set the read/write head to 0
                files[fh].opened = 1;                                       // The file is opened
                return( fh );                                               // Return the file handle       
            }
        }
    }

    lcloud_file file;                                                       // The file does not exist, create it

    file_handle += 1;                                                       // Increment the file_handle tracker to maintain uniqueness
    file.fh = file_handle;                                                  // Assign the file's handle to the unique file_handle tracker
    
    strncpy(file.name, path, 259);                                          // Assign 259 characters of path to file.name
    file.name[259] = '\0';                                                  // Place null terminator at end of path in case path length > 260
    
    file.pos = 0;                                                           // Set the file's read/write head to 0
    file.size = 0;                                                          // Initialize the file's size to 0
    
                                                                            // File device id, block, and sector go unassigned until a write occurs

    file.opened = 1;                                                        // Set the file to opened

    files[fh] = file;                                                       // Add the current file to the files array
    return(fh);                                                             // Returns the uniquely generated file header
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcread
// Description  : Read data from the file 
//
// Inputs       : fh - file handle for the file to read from
//                buf - place to put the data
//                len - the length of the read
// Outputs      : number of bytes read, -1 if failure

int lcread( LcFHandle fh, char *buf, size_t len ) {
    char temp[256], *cache_block;                                           // Temporary buffer to perform reads in 256 byte chunks

    lcloud_file file;
    if(validate_fh(fh, &file) == -1) {                                      // Validate the file handle and assign the file from handle
        return( - 1 );
    }

    int i = 0, pos_in_block, sec, blk, dev_id;

    if (file.size == 0) {                                                   // No data to read
        return ( 0 );
    }
    if (file.pos + len > file.size) {                                       // If the length of the read goes over the file sieze
        len = file.size - file.pos;                                         // Set the length of the read to rest of file
    }

    while(i < len) {                                                        // Loop to read in blocks, i is incremented depending on case

        pos_in_block = file.pos % 256;                                      // Get the position of the read head in the block
        if ( (dev_id = get_block(file, &sec, &blk)) == -1 ) {               // Set sec and blk for the read
            return( -1 );
        }

        if( (cache_block = lcloud_getcache(dev_id, sec, blk)) == NULL ) {   // The block is not in cache
            memset(temp, 0, 256);

            LCloudRegisterFrame frm, rfrm;                                  // Perform a read operation to put data at sec, block into temp
            frm = create_lcloud_registers(0, 0, LC_BLOCK_XFER, dev_id, LC_XFER_READ, sec, blk);
            if ( (frm == -1) || ((rfrm = client_lcloud_bus_request(frm, temp)) == -1) ||
                (extract_lcloud_registers(rfrm, &b0, &b1, &c0, &c1, &c2, &d0, &d1)) ||
                (b0 != 1) || (b1 != 1) || (c0 != LC_BLOCK_XFER) ) {
                    logMessage( LOG_ERROR_LEVEL, "LC failure reading blkc [%d,%d,%d]", dev_id, sec, blk);
                    return( -1 );                                           // Failed read operation
            }
            logMessage( LOG_OUTPUT_LEVEL, "LC success reading blkc [%d/%d/%d]", dev_id, sec,blk);  

        } else {
            memcpy(temp, cache_block, 256);
            logMessage( LOG_OUTPUT_LEVEL, "LC success retrieving blkc from cache [%d/%d/%d]", dev_id, sec,blk);  
        }
        
        if(pos_in_block == 0) {                                             // Case: read from beginning of block
            if((len - i) < 256) {                                           // Case: read to middle of block
                memcpy(&buf[i], temp, len - i);                             // Copy first part of temp into buf at i
                file.pos += len - i;                                        // Increment pos by bytes read
                i = len;                                                    // Last read command, i now equal to length
            }
            else {                                                          // Case: read to end of block
                memcpy(&buf[i], temp, 256);                                 // Copy next 256 bytes of temp into buf at i
                file.pos += 256;                                            // Increment pos by bytes written
                i += 256;                                                   // Increment i by the 256 written bytes
            }
        }
        else {                                                              // Case: read at middle of block
            if ((len + pos_in_block - i) < 256) {                           // Case: read to middle of block
                memcpy(&buf[i], &temp[pos_in_block], len - i);              // Copy temp from pos_in_block to the rest of len into buf at i
                file.pos += len - i;                                        // Increment pos by bytes written
                i = len;                                                    // Case only happens as single write (first and last), loop won't continue
            }
            else {                                                          // Case: read to end of block
                memcpy(&buf[i], &temp[pos_in_block], 256 - pos_in_block);   // Copy temp from pos_in_block to the end into buf at i
                file.pos += 256 - pos_in_block;                             // Increment pos by bytes written
                i += 256 - pos_in_block;                                    // Increment i by how many bytes of buf were written
            }
        }
    }
    logMessage(LOG_OUTPUT_LEVEL, "Driver read %d bytes from file %s (at %d)", len, file.name, files[fh].pos);

    files[fh] = file;                                                       // Update the file in the file list
    return( len );                                                          // returns number of bytes read on sucessful test, if operation passed, file.size this value was changed
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcwrite
// Description  : write data to the file
//
// Inputs       : fh - file handle for the file to write to
//                buf - pointer to data to write
//                len - the length of the write
// Outputs      : number of bytes written if successful test, -1 if failure

int lcwrite( LcFHandle fh, char *buf, size_t len ) {
    char temp[256];                                                             // Temporary buffer to perform write in 256 byte chunks

    lcloud_file file;
    if (validate_fh(fh, &file) == -1) {                                         // Validate the file handle and assign the file from handle
        return( - 1 );                                                          // Invalid file handle
    }

    int i = 0, pos_in_block, sec, blk, dev_id;

    if (file.size == 0) {                                                       // File has not been written yet, a block must be allocated
        if ((file.dev_id = allocate_block(&file.sector, &file.block)) == -1) {  // Allocate block
            return( -1 );
        }                     
    }

    while (i < len) {                                                           // Loop to write in blocks, i is incremented depending on case
        pos_in_block = file.pos % 256;                                          // Get the position of the write head in the block
        if ( (dev_id = get_block(file, &sec, &blk)) == -1 ) {                   // Set sec and blk for the write operation
            return( -1 );
        } 

        // Read the block into temp
        memset(temp, 0, 256);
        lcseek(fh, file.pos - pos_in_block);                                    // Seek to beginning of block
        lcread(fh, temp, 256);                                                  // read the current block into temp     

        if(pos_in_block == 0) {                                                 // Case: write at beginning of block
            if((len - i) < 256) {                                               // Case: write to middle of block
                memcpy(temp, &buf[i], len - i);                                 // Copy the end of buf to the beginning of temp, leave the rest zero'd
                file.pos += len - i;                                            // Increment pos by bytes written
                i = len;                                                        // Last write command, i now equal to length
            }
            else {                                                              // Case: write to end of block
                memcpy(temp, &buf[i], 256);                                     // Copy next 256 bytes of buf into temp
                file.pos += 256;                                                // Increment pos by bytes written
                i += 256;                                                       // Increment i by the 256 written bytes                                          
            }
        }
        else {                                                                  // Case: write at middle of block
            if ((len + pos_in_block - i) < 256) {                               // Case: write to middle of block
                memcpy(&temp[pos_in_block], &buf[i], len - i);                  // Copy the end of buf into the second part of temp, leave the rest zero'd
                file.pos += len - i;                                            // Increment pos by bytes written
                i = len;                                                        // Case only happens as single write (first and last), loop won't continue
            }
            else {                                                              // Case: write to end of block
                memcpy(&temp[pos_in_block], &buf[i], 256 - pos_in_block);       // Copy buf to second part of temp
                file.pos += 256 - pos_in_block;                                 // Increment pos by bytes written
                i += 256 - pos_in_block;                                        // Increment i by how many bytes of buf were written 
            }
        }

        LCloudRegisterFrame frm, rfrm;                                          // Perform a write operation to put temp into the file at block, sector
        frm = create_lcloud_registers(0, 0, LC_BLOCK_XFER, dev_id, LC_XFER_WRITE, sec, blk);
        if ( (frm == -1) || ((rfrm = client_lcloud_bus_request(frm, temp)) == -1) ||
             (extract_lcloud_registers(rfrm, &b0, &b1, &c0, &c1, &c2, &d0, &d1)) ||
             (b0 != 1) || (b1 != 1) || (c0 != LC_BLOCK_XFER) ) {
                logMessage( LOG_ERROR_LEVEL, "LC failure writing blkc [%d/%d/%d]", dev_id, sec, blk);
                return( -1 );                                                   // Failed write operation
        }

        if ( lcloud_putcache(dev_id, sec, blk, temp) == -1) {
            return( -1 );
        }

        if (file.pos >= file.size) {                                            // When writing to the end of the file
            file.size = file.pos;                                               // Update the file size to the write head
            if (file.pos % 256 == 0) {                                          // If the write was at the end of the file and the end of a block
                if(add_block(file) == -1) {                                     // We need to allocate a block for the file
                    return( -1 );                                           
                } 
            }
        }
        logMessage(LOG_OUTPUT_LEVEL, "LC success writing blkc [%d/%d/%d]", dev_id, sec, blk);
        files[fh] = file;                                                       // Update the file in the file list within the loop for read and seek calls
    }

    logMessage(LOG_OUTPUT_LEVEL, "Driver wrote %d bytes to file %s (now %d bytes)", len, file.name, file.size);
    return( len );                                                              // returns number of bytes written on sucessful test
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcseek
// Description  : Seek to a specific place in the file
//
// Inputs       : fh - the file handle of the file to seek in
//                off - offset within the file to seek to
// Outputs      : 0 if successful test, -1 if failure

int lcseek( LcFHandle fh, size_t off ) {

    lcloud_file file;
    if(validate_fh(fh, &file) == -1) {                                      // Validate the file handle and assign the file from handle
        return( - 1 );                                                      // Invalid file handle
    }

    if ((off < 0) || (off > file.size)) {                                   // Validity check: 0 < off < sile.size so that new position is within file
        logMessage(LOG_ERROR_LEVEL, "LC failure seek bounds out of range [%d,%d]", file.size, off);
        return( -1 );                                                       // Failed seek
    }

    file.pos = off;                                                         // Set the file position to the seek offset
    files[fh] = file;                                                       // Update the file in the file list
    logMessage(LOG_OUTPUT_LEVEL, "LC successfully seeked file %s to [%d]", file.name, off);
    return( file.pos );                                                     // Successful seek
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcclose
// Description  : Close the file
//
// Inputs       : fh - the file handle of the file to close
// Outputs      : 0 if successful test, -1 if failure

int lcclose( LcFHandle fh ) {
    lcloud_file file;
    if(validate_fh(fh, &file) == -1) {                                      // Validate the file handle and assign the file from handle
        return( - 1 );                                                      // Invalid file handle
    }
    if(file.opened == 0) {                                                  // If the file is not opened, it can't be closed
        logMessage( LOG_ERROR_LEVEL, "LC failure closing file [%d] file not openend", fh);
        return( -1 );                                                       // Failed close
    }
    file.opened = 0;                                                        // File no longer opened, set opened to 0
    files[fh] = file;                                                       // Update the file in the file list
    logMessage(LOG_OUTPUT_LEVEL, "Driver successfully closed file %s", file.name);
    return( 0 );                                                            // Succesful close      
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcshutdown
// Description  : Shut down the filesystem
//
// Inputs       : none
// Outputs      : 0 if successful test, -1 if failure

int lcshutdown( void ) {
    int i;
    for(i = 0; i < file_handle; i++) {                                      // Loop through all files
        if(files[i].opened == 1) {                                          // If the file is opened
            if(lcclose(i) == -1) {
                logMessage( LOG_ERROR_LEVEL, "LC failure shutting down system, cannot close file [%d]", i);
                return( - 1);                                               // Failed shutdown
            }
        }
    }

    for(i = 0; i < 16; i++) {                                               // Loop through all devices
        if(devices[i].dev_id != -1) {                                       // If the device was initialized
            free(devices[i].sector_block);                                  // Free the memory allocated to memory sturcture
            devices[i].sector_block = NULL;
        }
    }

    LCloudRegisterFrame frm, rfrm;                                          // Run shutdown operation
    frm = create_lcloud_registers(0, 0, LC_POWER_OFF, 0, 0, 0, 0);
    if ( (frm == -1) || ((rfrm = client_lcloud_bus_request(frm, NULL)) == -1) ||
            (extract_lcloud_registers(rfrm, &b0, &b1, &c0, &c1, &c2, &d0, &d1)) ||
            (b0 != 1) || (b1 != 1) || (c0 != LC_POWER_OFF) ) {
            logMessage( LOG_ERROR_LEVEL, "LC failure shutting down system");
            return( -1 );                                                   // Failed shutdown operation
    }

    lcloud_closecache();                                                    // Print out cache statistics at the end

    return( 0 );                                                            // Successful shutdown operation
}
