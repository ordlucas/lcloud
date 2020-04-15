////////////////////////////////////////////////////////////////////////////////
//
//  File           : lcloud_filesys.c
//  Description    : This is the implementation of the Lion Cloud device 
//                   filesystem interfaces.
//
//   Author        : Lucas Benning
//   Last Modified : 4/10/20
//

// Include files
#include <stdlib.h>
#include <string.h>
#include <cmpsc311_log.h>

// Project include files
#include <lcloud_filesys.h>
#include <lcloud_controller.h>
#include <lcloud_cache.h>
#include <lcloud_support.h>
#include <lcloud_network.h>

//
// File system interface implementation
// Define LcFile struct to store file metadata
typedef struct {
    uint16_t sec;
    uint16_t blk;
    LcDeviceId dev;
} LcBlock;

typedef struct {
    char *path;
    LcFHandle handle;
    size_t pos;
    size_t size;
    LcBlock *blocks;
    char open;
} LcFile;

typedef struct {
    LcDeviceId id;
    uint16_t num_sec;
    uint16_t num_blk;
    uint16_t next_sec;
    uint16_t next_blk;
    char full;
} LcDevice;

LcFile *files = NULL; // Array of files
LcDevice *devices = NULL; // Array of present devices
int filec; // Number of files
int devc; // Number of devices
char pwr = 0; // 1 if powered on, 0 if off

////////////////////////////////////////////////////////////////////////////////
//
// Function     : create_lcloud_register
// Description  : Packs registers b0 through d1 into an LCloudRegisterFrame (unsigned 64 bit integer)
//
// Inputs       : b0 ... d1: the values of each LCloud register
// Outputs      : packed LCloudRegisterframe
LCloudRegisterFrame create_lcloud_register(int b0, int b1, int c0, int c1, int c2, int d0, int d1) {
    int b, c, d;
    LCloudRegisterFrame out;

    b = b0 << 4 | b1; 
    c = c0 << 16 | c1 << 8 | c2;
    d = d0 << 16 | d1;

    out = b;
    out = out << 24 | c;
    out = out << 32 | d;
    return(out);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : extract_lcloud_registers
// Description  : Extracts packed LCloudRegisterFrame into its constituent registers
//
// Inputs       : resp: packed register frame
//                b0 ... d1: pointers for the values of each LCloud register
// Outputs      : 0 if success
int extract_lcloud_registers(LCloudRegisterFrame resp, int *b0, int *b1, int *c0, int *c1,
 int *c2, int *d0, int *d1) {
    // Query bits and assign to appropriate register
    *b0 = ((resp & 0xF000000000000000) >> 60);
    *b1 = ((resp & 0x0F00000000000000) >> 56);
    *c0 = ((resp & 0x00FF000000000000) >> 48);
    *c1 = ((resp & 0x0000FF0000000000) >> 40);
    *c2 = ((resp & 0x000000FF00000000) >> 32);
    *d0 = ((resp & 0x00000000FFFF0000) >> 16);
    *d1 = (resp & 0x000000000000FFFF);

    // Check if any error occurred from response
    if (*b0 == 1 && *b1 != 1) {
        return(-1);
    }

    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : devprobe_bus
// Description  : Sends a devprobe signal to the lcloud devices and returns the
//                id of the device present
// Inputs       : pointer to array of device IDs
// Outputs      : 0 if success, -1 if failure
int devprobe_bus(void) {
    int b0, b1, c0, c1, c2, d0, d1;
    LCloudRegisterFrame resp, devprobe;
    int count = 0;
    if((devprobe = create_lcloud_register(0, 0, LC_DEVPROBE, 0, 0, 0, 0)) == -1 ||
        (resp = client_lcloud_bus_request(devprobe, NULL)) == -1 ||
        extract_lcloud_registers(resp, &b0, &b1, &c0, &c1, &c2, &d0, &d1) == -1 ||
        b0 != 1 || b1 != 1 || c0 != LC_DEVPROBE) {
        return(-1);
    }
    
    for(int i = 16; i >= 0; i--) {
        if(((d0 & (0x1 << i)) >> i) == 1) {
            if(count == 0) {
                devices = (LcDevice*) malloc(sizeof(LcDevice));
            } else {
                devices = (LcDevice*) realloc(devices, (count + 1)*sizeof(LcDevice));
            }
            devices[count].id = i;
            devc++;
            count++;
        }        
    }
    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : pwr_on_bus
// Description  : Sends a power on signal to the lcloud devices on the cluster
//
// Inputs       : void
// Outputs      : 0 if success, -1 if failure
int pwr_on_bus(void) {
    int b0, b1, c0, c1, c2, d0, d1;
    LCloudRegisterFrame pwr_on, resp;
    pwr = 1;
    if((pwr_on = create_lcloud_register(0, 0, LC_POWER_ON, 0, 0, 0, 0)) == -1 ||
        (resp = client_lcloud_bus_request(pwr_on, NULL)) == -1 ||
        extract_lcloud_registers(resp, &b0, &b1, &c0, &c1, &c2, &d0, &d1) == -1 ||
        b0 != 1 || b1 != 1 || c0 != LC_POWER_ON) {
        return(-1);
    }
    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : pwr_off_bus
// Description  : Sends a poer off signal to the lcloud devices on the cluster
//
// Inputs       : void
// Outputs      : 0 if success, -1 if failure
int pwr_off_bus(void) {
    int b0, b1, c0, c1, c2, d0, d1;
    LCloudRegisterFrame resp, pwr_off;
    pwr = 0;
    if((pwr_off = create_lcloud_register(0, 0, LC_POWER_OFF, 0, 0, 0, 0)) == -1 ||
        (resp = client_lcloud_bus_request(pwr_off, NULL)) == -1 ||
        extract_lcloud_registers(resp, &b0, &b1, &c0, &c1, &c2, &d0, &d1) == -1 ||
        b0 != 1 || b1 != 1 || c0 != LC_POWER_OFF) {
        return(-1);
    }
    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : read_bus
// Description  : Sends a read signal to the specified lcloud device, reads the content
//                from specified sector and block and copies to buf
//
// Inputs       : buf: pointer to buffer
//                dev_id: device id to write to
//                sec: sector to write to
//                blk: block to write to
// Outputs      : 0 if success, -1 if failure
int read_bus(char *buf, int dev_id, int sec, int blk) {
    int b0, b1, c0, c1, c2, d0, d1;
    LCloudRegisterFrame resp, read;
    if((read = create_lcloud_register(0, 0, LC_BLOCK_XFER, dev_id, LC_XFER_READ, sec, blk)) == -1 ||
        (resp = client_lcloud_bus_request(read, buf)) == -1 ||
        extract_lcloud_registers(resp, &b0, &b1, &c0, &c1, &c2, &d0, &d1) == -1 || 
        b0 != 1 || b1 != 1 || c0 != LC_BLOCK_XFER) {
        return(-1);
    }
    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : write_bus
// Description  : Sends a write signal to the specified lcloud device, writes the contents
//                of buf to the specified sector and block
//
// Inputs       : buf: pointer to buffer
//                dev_id: device id to write to
//                sec: sector to write to
//                blk: block to write to
// Outputs      : 0 if success, -1 if failure
int write_bus(char *buf, int dev_id, int sec, int blk) {
    int b0, b1, c0, c1, c2, d0, d1;
    LCloudRegisterFrame resp, write;
    if((write = create_lcloud_register(0, 0, LC_BLOCK_XFER, dev_id, LC_XFER_WRITE, sec, blk)) == -1 ||
        (resp = client_lcloud_bus_request(write, buf)) == -1 ||
        extract_lcloud_registers(resp, &b0, &b1, &c0, &c1, &c2, &d0, &d1) == -1 ||
        b0 != 1 || b1 != 1 || c0 != LC_BLOCK_XFER) {
        return(-1);
    }
    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : devinit_bus
// Description  : Sends a devinit signal to the specified lcloud device. Sets number of sectors and blocks
//                to the device data structure
// Inputs       : dev: LcDevice struct of device to initialize
// Outputs      : 0 if success, -1 if failure
int devinit_bus(LcDevice *dev) {
    int b0, b1, c0, c1, c2, d0, d1;
    LCloudRegisterFrame resp, devinit;
    if((devinit = create_lcloud_register(0, 0, LC_DEVINIT, dev->id, 0, 0, 0)) == -1 ||
        (resp = client_lcloud_bus_request(devinit, NULL)) == -1 ||
        extract_lcloud_registers(resp, &b0, &b1, &c0, &c1, &c2, &d0, &d1) == -1 ||
        b0 != 1 || b1 != 1 || c0 != LC_DEVINIT || c2 != dev->id) {
            return(-1);
        }
    dev->num_sec = d0;
    dev->num_blk = d1;
    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_assign_helper
// Description  : Assigns blocks start through end in given file to next available blocks
// Inputs       : file: LcFile pointer
//                start, end: block indices to start and end assignment  
// Outputs      : 0 if success
int block_assign_helper(LcFile *file, int start, int end) {
    for(int b = start; b < end; b++) {
        for(int i = 0; i < devc; i++) {
            LcDevice *dev = &devices[i];
            if(dev->full == 0) {
                file->blocks[b].dev = dev->id;
                file->blocks[b].sec = dev->next_sec;
                file->blocks[b].blk = dev->next_blk;

                dev->next_blk += 1;
                if(dev->next_blk == dev->num_blk) {
                    dev->next_sec += 1;
                    dev->next_blk = 0;
                }

                if(dev->next_sec == dev->num_sec) {
                    dev->full = 1;
                }
                break;
            }
        }
    }
    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcopen
// Description  : Open the file for for reading and writing
//
// Inputs       : path - the path/filename of the file to be read
// Outputs      : file handle if successful test, -1 if failure
LcFHandle lcopen( const char *path ) {
    // Check if file is already open
    for(int i = 0; i < filec; i++) {
        if(strcmp(files[i].path, path) == 0 && files[i].open == 1) {
            logMessage(LOG_ERROR_LEVEL, "File already open");
            return(-1);
        }
    }

    // TODO
    // Check if the device cluster is powered on - if not, sends pwr_on signal
    if(pwr == 0) {
        if(pwr_on_bus() == -1) return(-1);

        // Probe for available devices, store in device array
        if(devprobe_bus() == -1) return(-1);

        // Initialize devices
        for(int i = 0; i < devc; i++) {
            // Retrieve sector and block info from devices
            if(devinit_bus(&devices[i]) == -1) {
                return(-1);
            } 

            devices[i].next_sec = 0;
            devices[i].next_blk = 0;
            devices[i].full = 0;
        }

        // Initialize cache
        if(lcloud_initcache(LC_CACHE_MAXBLOCKS) == -1) return(-1);
    }

    // Check if file has been created already
    for(int i = 0; i < filec; i++) {
        if(strcmp(files[i].path, path) == 0) {
            files[i].open = 1;
            return(files[i].handle);
        }
    }
    
    // Create a new file
    // Allocate memory for file table
    if(filec == 0) {
        if((files = (LcFile*) malloc(sizeof(LcFile))) == NULL) {
            logMessage(LOG_ERROR_LEVEL, "Memory allocation error");
            return(-1);
        }
    } else {
        if((files = (LcFile*) realloc(files, (filec + 1) * sizeof(LcFile))) == NULL) {
            logMessage(LOG_ERROR_LEVEL, "Memory allocation error");
            return(-1);
        }
    }


    // Copy path string to file struct
    if((files[filec].path = malloc(strlen(path) + 1)) == NULL) return(-1);
    strcpy(files[filec].path, path);

    // Initialize open file to default fields
    files[filec].handle = filec;
    files[filec].pos = 0;
    files[filec].size = 0;
    files[filec].blocks = NULL;
    files[filec].open = 1;
    filec++;

    return(files[filec - 1].handle);
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
    // File handle is incorrent, file is not open
    if(fh >= filec || files[fh].open == 0) {
        logMessage(LOG_ERROR_LEVEL, "File not open");
        return(-1);
    }
    // Devices not powered on (i.e. no files are opened)
    if(pwr == 0) {
        logMessage(LOG_ERROR_LEVEL, "Device(s) not powered on");
        return(-1);
    }

    //////////////////////
    /* INITIALIZE READ */
    ////////////////////
    // Reset buffer
    LcFile *open_file = &files[fh];
    memset(buf, 0, len);
    char *tmp = calloc(LC_DEVICE_BLOCK_SIZE, sizeof(char));

    // Number of reads that are performed
    // (position in block) + (write length) / (block size) [rounded up]
    int blocks_to_read = (open_file->pos % LC_DEVICE_BLOCK_SIZE + len + LC_DEVICE_BLOCK_SIZE) / LC_DEVICE_BLOCK_SIZE;

    ////////////
    /* READS */
    //////////
    // Length remaining in read
    size_t current_len = len;

    // Truncate read length if it goes beyond EOF
    if(open_file->pos + current_len > open_file->size) {
        // Change length of read to be until end of file
        current_len = open_file->size - open_file->pos;
    }

    for(int i = 0; i < blocks_to_read; i++) {
        // Calculate current block
        int current_index = ((open_file->pos + LC_DEVICE_BLOCK_SIZE) / LC_DEVICE_BLOCK_SIZE) - 1;
        
        LcDeviceId dev = open_file->blocks[current_index].dev;
        uint16_t sec = open_file->blocks[current_index].sec;
        uint16_t blk = open_file->blocks[current_index].blk;
        
        // Calculate position within block
        uint16_t block_pos = open_file->pos % LC_DEVICE_BLOCK_SIZE;

        // Check if block is in cache
        char *cache_blk;
        if((cache_blk = lcloud_getcache(dev, sec, blk)) == NULL) {
            // Read block from device
            if((read_bus(tmp, dev, sec, blk)) == -1) {
                logMessage(LOG_ERROR_LEVEL, "Read error on block [%d/%d/%d]", dev, sec, blk);
                return(-1);
            }
            // Push block to cache
            if(lcloud_putcache(dev, sec, blk, tmp) == -1) return(-1);
        } 
        // Copy retrieved cache block to tmp
        else {
            memcpy(tmp, cache_blk, LC_DEVICE_BLOCK_SIZE);
            cache_blk = NULL;
        }

        // Remaining read length is larger than block size or continues into another block
        if(current_len >= LC_DEVICE_BLOCK_SIZE || (open_file->pos + current_len + LC_DEVICE_BLOCK_SIZE) / LC_DEVICE_BLOCK_SIZE > (open_file->pos + LC_DEVICE_BLOCK_SIZE) / LC_DEVICE_BLOCK_SIZE) {
            memcpy(buf + (len - current_len), tmp + block_pos,  LC_DEVICE_BLOCK_SIZE - block_pos);
            current_len -= LC_DEVICE_BLOCK_SIZE - block_pos;
            open_file->pos += LC_DEVICE_BLOCK_SIZE - block_pos;
        }
        // Read is entirely within block
        else {
            memcpy(buf + (len - current_len), tmp + block_pos, current_len);
            open_file->pos += current_len;
        }

        logMessage(LcDriverLLevel, "Success reading from block [%d/%d/%d]", dev, sec, blk);

    }

    ///////////////
    /* CLEAN UP */
    /////////////
    // Free tmp buffer
    free(tmp);
    tmp = NULL;

    // Log read
    logMessage(LcDriverLLevel, "Read %d bytes from %s at position %d", len, open_file->path, open_file->pos - len);
    
    open_file = NULL;
    
    return(len);
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
    // File handle is incorrent, file is not open
    if(fh >= filec || files[fh].open == 0) {
        return(-1);
    }
    // Devices not powered on (i.e. no files are opened)
    if(pwr == 0) {
        logMessage(LOG_ERROR_LEVEL, "Device(s) not powered on");
        return(-1);
    }

    ///////////////////////
    /* INITIALIZE WRITE */
    /////////////////////
    LcFile *open_file = &files[fh];
    char *tmp;
    if((tmp = malloc(LC_DEVICE_BLOCK_SIZE)) == NULL) return(-1);

    // Number of writes that are performed
    // (position in block) + (write length) / (block size) [rounded up]
    int blocks_to_write = (open_file->pos % LC_DEVICE_BLOCK_SIZE + len + LC_DEVICE_BLOCK_SIZE) / LC_DEVICE_BLOCK_SIZE;

    //////////////////////////////////////////
    /* ALLOCATE MEMORY AND ASSIGN BLOCKS */
    //////////////////////////////////////////
    if(open_file->size == 0) {
        // Allocate memory
        if((open_file->blocks = (LcBlock*) malloc(blocks_to_write * sizeof(LcBlock))) == NULL) {
            return(-1);
        }

        // Search for available blocks to assign to newly created blocks
        block_assign_helper(open_file, 0, blocks_to_write);
        
    } else if(open_file->pos + len > open_file->size) {
        // Calculate number of new blocks to create and current blocks in file
        int new_blocks = (open_file->size + len - open_file->pos + LC_DEVICE_BLOCK_SIZE) / LC_DEVICE_BLOCK_SIZE;
        int total_blocks = (open_file->size + LC_DEVICE_BLOCK_SIZE) / LC_DEVICE_BLOCK_SIZE;

        // Allocate new memory
        if((open_file->blocks = (LcBlock*) realloc(open_file->blocks, (total_blocks + new_blocks) * sizeof(LcBlock))) == NULL) {
            return(-1);
        }

        // Search for available blocks to assign to newly created blocks
        block_assign_helper(open_file, total_blocks, total_blocks + new_blocks);
    }

    ////////////
    /* WRITES */
    ////////////
    // Length remaining in write
    size_t current_len = len;

    for(int i = 0; i < blocks_to_write; i++) {
        // Calculate current block
        int current_index = ((open_file->pos + LC_DEVICE_BLOCK_SIZE) / LC_DEVICE_BLOCK_SIZE) - 1;
        
        LcDeviceId dev = open_file->blocks[current_index].dev;
        uint16_t sec = open_file->blocks[current_index].sec;
        uint16_t blk = open_file->blocks[current_index].blk;

        // Calculate position within block
        int block_pos = open_file->pos % LC_DEVICE_BLOCK_SIZE;

        // Read contents of current block
        char *cache_blk;
        // Check if block is in cache
        if((cache_blk = lcloud_getcache(dev, sec, blk)) == NULL) {
            // Read block from device
            if((read_bus(tmp, dev, sec, blk)) == -1) {
                logMessage(LOG_ERROR_LEVEL, "Read error on block [%d/%d/%d]", dev, sec, blk);
                return(-1);
            }
        } 
        // Copy retrieved cache block to tmp
        else {
            memcpy(tmp, cache_blk, LC_DEVICE_BLOCK_SIZE);
            cache_blk = NULL;
        }

        // Remaining write length is larger than the block size or continues into another block
        if(current_len >= LC_DEVICE_BLOCK_SIZE || (open_file->pos + current_len + LC_DEVICE_BLOCK_SIZE) / LC_DEVICE_BLOCK_SIZE > (open_file->pos + LC_DEVICE_BLOCK_SIZE) / LC_DEVICE_BLOCK_SIZE) {
            memcpy(tmp + block_pos, buf + (len - current_len), LC_DEVICE_BLOCK_SIZE - block_pos);
            current_len -= LC_DEVICE_BLOCK_SIZE - block_pos;
            open_file->pos += LC_DEVICE_BLOCK_SIZE - block_pos;
        }
        // Write is entirely within the block 
        else {
            memcpy(tmp + block_pos, buf + (len - current_len), current_len);
            open_file->pos += current_len;
        }
        
        // Write contents of tmp to device
        if((write_bus(tmp, dev, sec, blk)) == -1) {
            logMessage(LOG_ERROR_LEVEL, "Write error in block [%d/%d/%d]", dev, sec, blk);
            return(-1);
        }
        // Push new block to cache
        if(lcloud_putcache(dev, sec, blk, tmp) == -1) {
            logMessage(LOG_ERROR_LEVEL, "Error writing block [%d/%d/%d] to cache", dev, sec, blk);
            return(-1);
        }

        logMessage(LcDriverLLevel, "Success writing to block [%d/%d/%d]", dev, sec, blk);
    }

    /////////////
    /* CLEAN UP*/
    /////////////
    // Free tmp buffer
    free(tmp);
    tmp = NULL;

    // Update file size
    if(open_file->pos > open_file->size) {
        open_file->size = open_file->pos;
    }
    
    // Log write
    logMessage(LcDriverLLevel, "Wrote %d bytes to %s (size %d bytes)", len, open_file->path, open_file->size);

    open_file = NULL;

    return(len);
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
    // File handle is incorrent, file is not open
    if(fh >= filec || files[fh].open == 0) {
        return(-1);
    }

    // Updates file position if off is within file size
    if(off <= files[fh].size) {
        files[fh].pos = off;
        return(files[fh].pos);
    }

    return(-1);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcclose
// Description  : Close the file
//
// Inputs       : fh - the file handle of the file to close
// Outputs      : 0 if successful test, -1 if failure
int lcclose( LcFHandle fh ) {
    // File handle is incorrent, file is not open
    if(fh >= filec || files[fh].open == 0) return(-1);

    files[fh].open = 0;

    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcshutdown
// Description  : Shut down the filesystem
//
// Inputs       : none
// Outputs      : 0 if successful test, -1 if failure
int lcshutdown( void ) {
    // Don't need to shutdown filesystem if it's not on
    if(pwr == 1) {
        // Free device data
        free(devices);
        devices = NULL;

        // Free file data
        for(int i = 0; i < filec; i++) {
            free(files[i].path);
            free(files[i].blocks);
            
            files[i].path = NULL;
            files[i].blocks = NULL;
        }
        free(files);
        files = NULL;

        // Close cache
        lcloud_closecache();

        // Send power off signal
        return(pwr_off_bus());
    }
    return(-1);
} 
