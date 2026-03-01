/**
 * main.c — Console entry point for the decompress tool
 *
 * Usage:
 *   decompress <archive.zip> [<output-dir>]
 *
 *   archive.zip   Path to the ZIP file (or XLSX / any ZIP-based format)
 *   output-dir    Directory to extract files into (default: current directory)
 *
 * The program:
 *   1. Opens the file.
 *   2. Reads the End of Central Directory record (EOCD).
 *   3. Iterates over every Central Directory entry.
 *   4. For each entry, reads the Local File Header and decompresses
 *      (STORE or DEFLATE) the data byte by byte.
 *   5. Prints a summary and exits with 0 on success or 1 on error.
 */

#include "zip_records.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <direct.h>
#  define MKDIR(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  define MKDIR(p) mkdir((p), 0755)
#endif

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <archive.zip> [<output-dir>]\n"
            "\n"
            "  archive.zip   ZIP file to decompress (XLSX, DOCX, etc. are supported)\n"
            "  output-dir    Extraction directory (default: current directory)\n",
            prog);
}

/** Ensure output directory exists; create it if necessary. */
static int ensure_dir(const char *path)
{
    int r = MKDIR(path);
    return (r == 0 || errno == EEXIST) ? 0 : -1;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *archive_path = argv[1];
    const char *out_dir      = (argc >= 3) ? argv[2] : ".";

    /* ------------------------------------------------------------------
     * Step 1: Open the file
     * ---------------------------------------------------------------- */
    FILE *fp;
#if defined(_WIN32)
    if (fopen_s(&fp, archive_path, "rb") != 0) fp = NULL;
#else
    fp = fopen(archive_path, "rb");
#endif
    if (!fp) {
        fprintf(stderr, "Error: cannot open '%s': %s\n",
                archive_path, strerror(errno));
        return 1;
    }

    printf("Archive : %s\n", archive_path);
    printf("Output  : %s\n\n", out_dir);

    /* ------------------------------------------------------------------
     * Step 2: Read the End of Central Directory record
     * ---------------------------------------------------------------- */
    zip_eocd_t eocd;
    int rc = zip_read_eocd(fp, &eocd);
    if (rc != ZIP_OK) {
        fprintf(stderr, "Error reading EOCD: %s\n", zip_strerror(rc));
        fclose(fp);
        return 1;
    }

    printf("Entries : %u\n", eocd.cd_entries_total);
    printf("CD size : %u bytes at offset %u\n\n", eocd.cd_size, eocd.cd_offset);

    /* ------------------------------------------------------------------
     * Step 3: Prepare output directory
     * ---------------------------------------------------------------- */
    if (ensure_dir(out_dir) != 0) {
        fprintf(stderr, "Error: cannot create output directory '%s': %s\n",
                out_dir, strerror(errno));
        fclose(fp);
        return 1;
    }

    /* ------------------------------------------------------------------
     * Step 4 + 5: Extract all entries
     * ---------------------------------------------------------------- */
    rc = zip_extract_all(fp, &eocd, out_dir);

    fclose(fp);

    if (rc != ZIP_OK) {
        fprintf(stderr, "\nFailed: %s\n", zip_strerror(rc));
        return 1;
    }

    printf("\nDone.\n");
    return 0;
}
