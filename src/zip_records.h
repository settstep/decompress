/**
 * zip_records.h
 *
 * Structures and reader functions for the three core ZIP record types:
 *   - Local File Header
 *   - Central Directory Header
 *   - End of Central Directory (EOCD) Record
 *
 * All multi-byte fields are stored little-endian in the ZIP format.
 * The reader functions here parse each record byte by byte so that the
 * layout is explicit and portable across all platforms.
 *
 * Return codes
 * ------------
 *   ZIP_OK              success
 *   ZIP_ERR_IO          read error or unexpected end of file
 *   ZIP_ERR_SIGNATURE   record signature did not match
 *   ZIP_ERR_UNSUPPORTED compression method not supported
 *   ZIP_ERR_CRC         CRC-32 mismatch after decompression
 *   ZIP_ERR_MEMORY      malloc/realloc failed
 *   ZIP_ERR_FORMAT      archive is structurally invalid
 */

#ifndef ZIP_RECORDS_H
#define ZIP_RECORDS_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Return codes
 * ---------------------------------------------------------------------- */
#define ZIP_OK              0
#define ZIP_ERR_IO         -1
#define ZIP_ERR_SIGNATURE  -2
#define ZIP_ERR_UNSUPPORTED -3
#define ZIP_ERR_CRC        -4
#define ZIP_ERR_MEMORY     -5
#define ZIP_ERR_FORMAT     -6

/** Convert a return code to a human-readable string. */
const char *zip_strerror(int code);

/* -------------------------------------------------------------------------
 * Local File Header  (signature 0x04034B50)
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t version_needed;
    uint16_t general_purpose_flags;
    uint16_t compression_method;    /**< 0 = STORE, 8 = DEFLATE */
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_length;
    uint16_t extra_field_length;

    char    *filename;              /**< Heap-allocated, NUL-terminated */
} zip_local_file_header_t;

/**
 * Read a Local File Header from the current file position.
 *
 * @param fp    Open file handle positioned at the start of the record
 *              (i.e. at the 0x04034B50 signature bytes).
 * @param out   Output structure; caller must call zip_local_file_header_free()
 *              when done.
 * @return      ZIP_OK on success, negative ZIP_ERR_* code on failure.
 */
int zip_read_local_file_header(FILE *fp, zip_local_file_header_t *out);

/** Free heap memory owned by a zip_local_file_header_t. */
void zip_local_file_header_free(zip_local_file_header_t *h);

/* -------------------------------------------------------------------------
 * Central Directory Header  (signature 0x02014B50)
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t version_made_by;
    uint16_t version_needed;
    uint16_t general_purpose_flags;
    uint16_t compression_method;
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_length;
    uint16_t extra_field_length;
    uint16_t file_comment_length;
    uint16_t disk_number_start;
    uint16_t internal_file_attributes;
    uint32_t external_file_attributes;
    uint32_t local_header_offset;   /**< Offset of corresponding local header */

    char    *filename;              /**< Heap-allocated, NUL-terminated */
} zip_central_dir_header_t;

/**
 * Read a Central Directory Header from the current file position.
 *
 * @param fp    Open file handle positioned at the start of the record
 *              (i.e. at the 0x02014B50 signature bytes).
 * @param out   Output structure; caller must call zip_central_dir_header_free()
 *              when done.
 * @return      ZIP_OK on success, negative ZIP_ERR_* code on failure.
 */
int zip_read_central_dir_header(FILE *fp, zip_central_dir_header_t *out);

/** Free heap memory owned by a zip_central_dir_header_t. */
void zip_central_dir_header_free(zip_central_dir_header_t *h);

/* -------------------------------------------------------------------------
 * End of Central Directory Record  (signature 0x06054B50)
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t disk_number;
    uint16_t disk_with_cd_start;
    uint16_t cd_entries_on_disk;
    uint16_t cd_entries_total;
    uint32_t cd_size;               /**< Size of the central directory */
    uint32_t cd_offset;             /**< Offset of start of central directory */
    uint16_t comment_length;
} zip_eocd_t;

/**
 * Locate and read the End of Central Directory record.
 *
 * Scans backward from the end of the file for the EOCD signature.
 *
 * @param fp    Open file handle (seekable).
 * @param out   Output structure.
 * @return      ZIP_OK on success, negative ZIP_ERR_* code on failure.
 */
int zip_read_eocd(FILE *fp, zip_eocd_t *out);

/* -------------------------------------------------------------------------
 * High-level: extract all entries from an open ZIP file
 * ---------------------------------------------------------------------- */

/**
 * Extract all entries from the ZIP archive to the given output directory.
 *
 * For each entry the function:
 *   1. Reads the Central Directory Header.
 *   2. Seeks to and reads the corresponding Local File Header.
 *   3. Decompresses (STORE or DEFLATE) the entry data.
 *   4. Verifies the CRC-32.
 *   5. Writes the data to <out_dir>/<entry_name>.
 *
 * @param fp       Open, seekable file handle for the ZIP archive.
 * @param eocd     Pre-parsed EOCD record.
 * @param out_dir  Destination directory path (must already exist).
 * @return         ZIP_OK on success, negative ZIP_ERR_* code on first error.
 */
int zip_extract_all(FILE *fp, const zip_eocd_t *eocd, const char *out_dir);

#ifdef __cplusplus
}
#endif

#endif /* ZIP_RECORDS_H */
