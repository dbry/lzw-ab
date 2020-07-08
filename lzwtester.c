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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "lzwlib.h"

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
" Usage:     lzwtester [options] file [...]\n\n"
" Options:   -1 ... -8 = test using only specified max symbol size (9 - 16)\n"
"            -0        = cycle through all maximum symbol sizes (default)\n"
"            -f        = fuzz test (randomly corrupt compressed data)\n\n"
" Web:       Visit www.github.com/dbry/lzw-ab for latest version and info\n\n";

typedef struct {
    int size, index, wrapped, byte_errors, first_error, fuzz_testing;
    unsigned char *buffer;
} streamer;

static int read_buff (void *ctx)
{
    streamer *stream = ctx;

    if (stream->index == stream->size)
        return EOF;

    return stream->buffer [stream->index++];
}

static void write_buff (int value, void *ctx)
{
    streamer *stream = ctx;

    // for fuzz testing, randomly corrupt 1 byte in every 65536 (on average)

    if (stream->fuzz_testing) {
        static unsigned long long kernel = 0x3141592653589793;
        kernel = ((kernel << 4) - kernel) ^ 1;
        kernel = ((kernel << 4) - kernel) ^ 1;
        kernel = ((kernel << 4) - kernel) ^ 1;

        if (!(kernel >> 48))
            value ^= (int)(kernel >> 40);
    }

    if (stream->index == stream->size) {
        stream->index = 0;
        stream->wrapped++;
    }

    stream->buffer [stream->index++] = value;
}

static void check_buff (int value, void *ctx)
{
    streamer *stream = ctx;

    if (stream->index == stream->size) {
        stream->wrapped++;
        return;
    }

    if (stream->buffer [stream->index] != value)
        if (!stream->byte_errors++)
            stream->first_error = stream->index;

    stream->index++;
}

#ifdef _WIN32

long long DoGetFileSize (FILE *hFile)
{
    LARGE_INTEGER Size;
    HANDLE        fHandle;

    if (hFile == NULL)
        return 0;

    fHandle = (HANDLE)_get_osfhandle(_fileno(hFile));
    if (fHandle == INVALID_HANDLE_VALUE)
        return 0;

    Size.u.LowPart = GetFileSize(fHandle, &Size.u.HighPart);

    if (Size.u.LowPart == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
        return 0;

    return (long long)Size.QuadPart;
}

#else

long long DoGetFileSize (FILE *hFile)
{
    struct stat statbuf;

    if (!hFile || fstat (fileno (hFile), &statbuf) || !S_ISREG(statbuf.st_mode))
        return 0;

    return (long long) statbuf.st_size;
}

#endif

int main (int argc, char **argv)
{
    int index, checked = 0, skipped = 0, errors = 0, set_maxbits = 0;
    streamer reader, writer, checker;

    memset (&reader, 0, sizeof (reader));
    memset (&writer, 0, sizeof (writer));
    memset (&checker, 0, sizeof (checker));

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
            writer.fuzz_testing = 1;
            continue;
        }

        if (strlen (filename) == 2 && filename [0] == '-' && filename [1] >= '0' && filename [1] <= '8') {
            if (filename [1] > '0')
                set_maxbits = filename [1] - '0' + 8;
            else
                set_maxbits = 0;

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

        reader.buffer = malloc (reader.size = (int) file_size);
        writer.buffer = malloc (writer.size = (int) (file_size + (file_size >> 2) + 10));

        if (!reader.buffer || !writer.buffer) {
            printf ("\nfile %s is too big!\n", filename);
            if (writer.buffer) free (writer.buffer);
            if (reader.buffer) free (reader.buffer);
            skipped++;
            continue;
        }

        bytes_read = fread (reader.buffer, 1, (int) file_size, infile);
        fclose (infile);

        if (bytes_read != (int) file_size) {
            printf ("\nfile %s could not be read!\n", filename);
            free (writer.buffer);
            free (reader.buffer);
            skipped++;
            continue;
        }

        printf ("\n");
        checked++;

        for (maxbits = set_maxbits ? set_maxbits : 9; maxbits <= (set_maxbits ? set_maxbits : 16); ++maxbits) {
            int res;

            reader.index = writer.index = writer.wrapped = 0;

            if (lzw_compress (write_buff, &writer, read_buff, &reader, maxbits)) {
                printf ("lzw_compress() returned error on file %s, maxbits = %d\n", filename, maxbits);
                errors++;
                continue;
            }

            if (writer.wrapped) {
                printf ("over 25%% inflation on file %s, maxbits = %d!\n", filename, maxbits);
                errors++;
                continue;
            }

            printf ("file %s, maxbits = %2d: %d bytes --> %d bytes, %.2f%%\n", filename, maxbits,
                reader.index, writer.index, writer.index * 100.0 / reader.index);

            checker.buffer = reader.buffer;
            checker.size = reader.size;
            checker.wrapped = checker.byte_errors = checker.index = 0;

            reader.buffer = writer.buffer;
            reader.size = writer.index;
            reader.index = 0;

            res = lzw_decompress (check_buff, &checker, read_buff, &reader);

            reader.buffer = checker.buffer;
            reader.size = checker.size;

            if (res) {
                printf ("lzw_decompress() returned error on file %s, maxbits = %d\n", filename, maxbits);
                errors++;
                continue;
            }

            if (checker.index != checker.size || checker.wrapped) {
                printf ("byte count error on file %s, maxbits = %d\n", filename, maxbits);
                errors++;
            }
            else if (checker.byte_errors) {
                printf ("%d byte data errors on file %s starting at index %d, maxbits = %d\n",
                    checker.byte_errors, filename, checker.first_error, maxbits);
                errors++;
            }
        }

        free (writer.buffer);
        free (reader.buffer);
    }

    printf ("\n%d errors detected in %d files (%d skipped)\n\n", errors, checked, skipped);

    return errors;
}
