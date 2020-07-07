////////////////////////////////////////////////////////////////////////////
//                            **** LZW-AB ****                            //
//               Adjusted Binary LZW Compressor/Decompressor              //
//                  Copyright (c) 2016-2020 David Bryant                  //
//                           All Rights Reserved                          //
//      Distributed under the BSD Software License (see license.txt)      //
////////////////////////////////////////////////////////////////////////////

#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "lzw-lib.h"

/* This module provides a command-line test harness for the lzw library.
 * Given a list of files, it will read each one and byte-for-byte verify
 * the data after a round-trip through a compression / decompression cycle
 * at each of the 8 available maximum symbol size settings.
 *
 * It can also optionally perform fuzz testing by randomly corrupting the
 * compressed bitstream. Obviously this will introduce integrity failures,
 * but it should not cause a crash.
 */

static const char *usage =
" Usage:     lzwtester [option] file [...]\n\n"
" Option:    -f = fuzz test (randomly corrupt compressed data)\n\n"
" Web:       Visit www.github.com/dbry/lzw-ab for latest version and info\n\n";

static unsigned char *read_buffer, *write_buffer, *check_buffer;
static int read_buffer_size, read_buffer_index;
static int write_buffer_size, write_buffer_index, write_buffer_wrapped;
static int check_buffer_size, check_buffer_index, check_buffer_wrapped, check_buffer_byte_errors, check_buffer_first_error;
static int fuzz_testing;

static int read_buff (void)
{
    if (read_buffer_index == read_buffer_size)
        return EOF;

    return read_buffer [read_buffer_index++];
}

static void write_buff (int value)
{
    // for fuzz testing, randomly corrupt 1 byte in every 65536 (on average)

    if (fuzz_testing) {
        static unsigned long long kernel = 0x3141592653589793;
        kernel = ((kernel << 4) - kernel) ^ 1;
        kernel = ((kernel << 4) - kernel) ^ 1;
        kernel = ((kernel << 4) - kernel) ^ 1;

        if (!(kernel >> 48))
            value ^= (int)(kernel >> 40);
    }

    if (write_buffer_index == write_buffer_size) {
        write_buffer_index = 0;
        write_buffer_wrapped++;
    }

    write_buffer [write_buffer_index++] = value;
}

static void check_buff (int value)
{
    if (check_buffer_index == check_buffer_size) {
        check_buffer_wrapped++;
        return;
    }

    if (check_buffer [check_buffer_index] != value)
        if (!check_buffer_byte_errors++)
            check_buffer_first_error = check_buffer_index;

    check_buffer_index++;
}

long long DoGetFileSize (FILE *hFile)
{
    struct stat statbuf;

    if (!hFile || fstat (fileno (hFile), &statbuf) || !S_ISREG(statbuf.st_mode))
        return 0;

    return (long long) statbuf.st_size;
}

int main (int argc, char **argv)
{
    int index, checked = 0, skipped = 0, errors = 0;

    if (argc < 2) {
        printf ("%s", usage);
        return 0;
    }

    for (index = 1; index < argc; ++index) {
        const char *filename = argv [index];
        int bytes_read, maxbits;
        long long file_size;
        FILE *infile;

        if (!strcmp (filename, "-f")) {
            fuzz_testing = 1;
            continue;
        }

        infile = fopen (filename, "rb");

        if (!infile) {
            printf ("\ncan't open file %s!\n", filename);
            skipped++;
            continue;
        }

        file_size = DoGetFileSize (infile);

        if (!file_size) {
            printf ("\ncan't get file size of %s (may be zero)!\n", filename);
            skipped++;
            continue;
        }

        if (file_size > 1024LL * 1024LL * 1024LL) {
            printf ("\nfile %s is too big!\n", filename);
            skipped++;
            continue;
        }

        read_buffer = malloc (read_buffer_size = (int) file_size);
        write_buffer = malloc (write_buffer_size = (int) (file_size + (file_size >> 2) + 10));

        if (!read_buffer || !write_buffer) {
            printf ("\nfile %s is too big!\n", filename);
            if (write_buffer) free (write_buffer);
            if (read_buffer) free (read_buffer);
            skipped++;
            continue;
        }

        bytes_read = fread (read_buffer, 1, (int) file_size, infile);
        fclose (infile);

        if (bytes_read != (int) file_size) {
            printf ("\nfile %s could not be read!\n", filename);
            free (write_buffer);
            free (read_buffer);
            skipped++;
            continue;
        }

        printf ("\n");
        checked++;

        for (maxbits = 9; maxbits <= 16; ++maxbits) {

            read_buffer_index = write_buffer_index = write_buffer_wrapped = 0;

            if (lzw_compress (write_buff, read_buff, maxbits)) {
                printf ("lzw_compress() returned error on file %s, maxbits = %d\n", filename, maxbits);
                errors++;
                continue;
            }

            if (write_buffer_wrapped) {
                printf ("over 25%% inflation on file %s, maxbits = %d!\n", filename, maxbits);
                errors++;
                continue;
            }

            printf ("file %s, maxbits = %2d: %d bytes --> %d bytes, %.2f%%\n", filename, maxbits,
                read_buffer_index, write_buffer_index, write_buffer_index * 100.0 / read_buffer_index);

            check_buffer = read_buffer;
            check_buffer_size = read_buffer_size;
            check_buffer_wrapped = check_buffer_byte_errors = check_buffer_index = 0;

            read_buffer = write_buffer;
            read_buffer_size = write_buffer_index;
            read_buffer_index = 0;

            int res = lzw_decompress (check_buff, read_buff);

            read_buffer = check_buffer;
            read_buffer_size = check_buffer_size;

            if (res) {
                printf ("lzw_decompress() returned error on file %s, maxbits = %d\n", filename, maxbits);
                errors++;
                continue;
            }

            if (check_buffer_index != check_buffer_size || check_buffer_wrapped) {
                printf ("byte count error on file %s, maxbits = %d\n", filename, maxbits);
                errors++;
            }
            else if (check_buffer_byte_errors) {
                printf ("%d byte data errors on file %s starting at index %d, maxbits = %d\n",
                    check_buffer_byte_errors, filename, check_buffer_first_error, maxbits);
                errors++;
            }
        }

        free (write_buffer);
        free (read_buffer);
    }

    printf ("\n%d errors detected in %d files (%d skipped)\n\n", errors, checked, skipped);

    return errors;
}
