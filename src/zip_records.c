/**
 * zip_records.c
 *
 * Byte-oriented readers for the three core ZIP record types, plus a
 * high-level extraction routine.
 *
 * ZIP format reference: APPNOTE.TXT (PKWARE)
 *   https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
 */

#include "zip_records.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <direct.h>
#else
#  include <sys/stat.h>
#endif

#include <zlib.h>

/* -------------------------------------------------------------------------
 * Internal constants
 * ---------------------------------------------------------------------- */
#define SIG_LOCAL_FILE_HEADER     0x04034B50U
#define SIG_CENTRAL_DIR_HEADER    0x02014B50U
#define SIG_EOCD                  0x06054B50U

#define METHOD_STORE   0
#define METHOD_DEFLATE 8

/* Maximum ZIP archive comment length (per spec) */
#define MAX_COMMENT_LEN 65535

/* Decompression chunk size */
#define INFLATE_CHUNK 16384U

/* -------------------------------------------------------------------------
 * Error descriptions
 * ---------------------------------------------------------------------- */
const char *zip_strerror(int code)
{
    switch (code) {
    case ZIP_OK:              return "success";
    case ZIP_ERR_IO:          return "I/O error or unexpected end of file";
    case ZIP_ERR_SIGNATURE:   return "invalid record signature";
    case ZIP_ERR_UNSUPPORTED: return "unsupported compression method";
    case ZIP_ERR_CRC:         return "CRC-32 mismatch";
    case ZIP_ERR_MEMORY:      return "memory allocation failed";
    case ZIP_ERR_FORMAT:      return "invalid ZIP format";
    default:                  return "unknown error";
    }
}

/* -------------------------------------------------------------------------
 * Portable byte-by-byte readers for little-endian integers.
 * Each function reads exactly the stated number of bytes from fp and
 * assembles the value without relying on host endianness or struct padding.
 * ---------------------------------------------------------------------- */

/** Read one byte; return 0 on success, ZIP_ERR_IO on failure. */
static int read_u8(FILE *fp, uint8_t *out)
{
    int c = fgetc(fp);
    if (c == EOF) return ZIP_ERR_IO;
    *out = (uint8_t)c;
    return ZIP_OK;
}

/** Read a 2-byte little-endian uint16_t. */
static int read_u16(FILE *fp, uint16_t *out)
{
    uint8_t b[2];
    int rc;
    if ((rc = read_u8(fp, &b[0])) != ZIP_OK) return rc;
    if ((rc = read_u8(fp, &b[1])) != ZIP_OK) return rc;
    *out = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
    return ZIP_OK;
}

/** Read a 4-byte little-endian uint32_t. */
static int read_u32(FILE *fp, uint32_t *out)
{
    uint8_t b[4];
    int rc;
    for (int i = 0; i < 4; ++i)
        if ((rc = read_u8(fp, &b[i])) != ZIP_OK) return rc;
    *out = (uint32_t)(b[0]
                    | ((uint32_t)b[1] << 8)
                    | ((uint32_t)b[2] << 16)
                    | ((uint32_t)b[3] << 24));
    return ZIP_OK;
}

/**
 * Read and verify a 4-byte signature.
 * Returns ZIP_ERR_SIGNATURE if the bytes do not match @p expected.
 */
static int read_signature(FILE *fp, uint32_t expected)
{
    uint32_t sig;
    int rc = read_u32(fp, &sig);
    if (rc != ZIP_OK) return rc;
    return (sig == expected) ? ZIP_OK : ZIP_ERR_SIGNATURE;
}

/**
 * Read @p length bytes and store them in a freshly malloc'd, NUL-terminated
 * buffer.  On success *out points to the buffer; caller must free() it.
 */
static int read_string(FILE *fp, uint16_t length, char **out)
{
    *out = (char *)malloc((size_t)length + 1);
    if (!*out) return ZIP_ERR_MEMORY;

    for (uint16_t i = 0; i < length; ++i) {
        uint8_t b;
        int rc = read_u8(fp, &b);
        if (rc != ZIP_OK) { free(*out); *out = NULL; return rc; }
        (*out)[i] = (char)b;
    }
    (*out)[length] = '\0';
    return ZIP_OK;
}

/** Skip @p n bytes by reading (and discarding) them one at a time. */
static int skip_bytes(FILE *fp, uint32_t n)
{
    uint8_t b;
    for (uint32_t i = 0; i < n; ++i) {
        int rc = read_u8(fp, &b);
        if (rc != ZIP_OK) return rc;
    }
    return ZIP_OK;
}

/* -------------------------------------------------------------------------
 * Local File Header
 * ---------------------------------------------------------------------- */

int zip_read_local_file_header(FILE *fp, zip_local_file_header_t *out)
{
    if (!fp || !out) return ZIP_ERR_FORMAT;
    memset(out, 0, sizeof(*out));

    int rc;

    /* Signature: 0x04034B50 */
    if ((rc = read_signature(fp, SIG_LOCAL_FILE_HEADER)) != ZIP_OK)
        return rc;

    if ((rc = read_u16(fp, &out->version_needed))         != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->general_purpose_flags))  != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->compression_method))     != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->last_mod_time))          != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->last_mod_date))          != ZIP_OK) return rc;
    if ((rc = read_u32(fp, &out->crc32))                  != ZIP_OK) return rc;
    if ((rc = read_u32(fp, &out->compressed_size))        != ZIP_OK) return rc;
    if ((rc = read_u32(fp, &out->uncompressed_size))      != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->filename_length))        != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->extra_field_length))     != ZIP_OK) return rc;

    if ((rc = read_string(fp, out->filename_length, &out->filename)) != ZIP_OK)
        return rc;

    /* Skip extra field */
    if ((rc = skip_bytes(fp, out->extra_field_length)) != ZIP_OK) {
        free(out->filename);
        out->filename = NULL;
        return rc;
    }

    return ZIP_OK;
}

void zip_local_file_header_free(zip_local_file_header_t *h)
{
    if (h) { free(h->filename); h->filename = NULL; }
}

/* -------------------------------------------------------------------------
 * Central Directory Header
 * ---------------------------------------------------------------------- */

int zip_read_central_dir_header(FILE *fp, zip_central_dir_header_t *out)
{
    if (!fp || !out) return ZIP_ERR_FORMAT;
    memset(out, 0, sizeof(*out));

    int rc;

    /* Signature: 0x02014B50 */
    if ((rc = read_signature(fp, SIG_CENTRAL_DIR_HEADER)) != ZIP_OK)
        return rc;

    if ((rc = read_u16(fp, &out->version_made_by))           != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->version_needed))             != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->general_purpose_flags))      != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->compression_method))         != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->last_mod_time))              != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->last_mod_date))              != ZIP_OK) return rc;
    if ((rc = read_u32(fp, &out->crc32))                      != ZIP_OK) return rc;
    if ((rc = read_u32(fp, &out->compressed_size))            != ZIP_OK) return rc;
    if ((rc = read_u32(fp, &out->uncompressed_size))          != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->filename_length))            != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->extra_field_length))         != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->file_comment_length))        != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->disk_number_start))          != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->internal_file_attributes))   != ZIP_OK) return rc;
    if ((rc = read_u32(fp, &out->external_file_attributes))   != ZIP_OK) return rc;
    if ((rc = read_u32(fp, &out->local_header_offset))        != ZIP_OK) return rc;

    if ((rc = read_string(fp, out->filename_length, &out->filename)) != ZIP_OK)
        return rc;

    /* Skip extra field and file comment */
    rc = skip_bytes(fp, (uint32_t)out->extra_field_length +
                        (uint32_t)out->file_comment_length);
    if (rc != ZIP_OK) {
        free(out->filename);
        out->filename = NULL;
        return rc;
    }

    return ZIP_OK;
}

void zip_central_dir_header_free(zip_central_dir_header_t *h)
{
    if (h) { free(h->filename); h->filename = NULL; }
}

/* -------------------------------------------------------------------------
 * End of Central Directory Record
 * ---------------------------------------------------------------------- */

int zip_read_eocd(FILE *fp, zip_eocd_t *out)
{
    if (!fp || !out) return ZIP_ERR_FORMAT;
    memset(out, 0, sizeof(*out));

    /* Determine file size */
    if (fseek(fp, 0, SEEK_END) != 0) return ZIP_ERR_IO;
    long file_size = ftell(fp);
    if (file_size < 22) return ZIP_ERR_FORMAT; /* Too small to be a ZIP */

    /* Scan backward for the EOCD signature.
     * The EOCD record is at least 22 bytes; the optional comment can be
     * up to 65535 bytes long.  We read one byte at a time from the back
     * so the search is explicit and easy to understand. */
    long search_start = file_size - 22;
    long search_end   = file_size - 22 - MAX_COMMENT_LEN;
    if (search_end < 0) search_end = 0;

    long eocd_pos = -1;
    for (long pos = search_start; pos >= search_end; --pos) {
        if (fseek(fp, pos, SEEK_SET) != 0) return ZIP_ERR_IO;

        uint32_t sig;
        int rc = read_u32(fp, &sig);
        if (rc != ZIP_OK) return rc;

        if (sig == SIG_EOCD) {
            eocd_pos = pos;
            break;
        }
    }

    if (eocd_pos < 0) return ZIP_ERR_SIGNATURE; /* No EOCD found */

    /* Re-read from the start of the EOCD record (after the signature,
     * which read_u32 already consumed). */
    if (fseek(fp, eocd_pos + 4, SEEK_SET) != 0) return ZIP_ERR_IO;

    int rc;
    if ((rc = read_u16(fp, &out->disk_number))        != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->disk_with_cd_start)) != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->cd_entries_on_disk)) != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->cd_entries_total))   != ZIP_OK) return rc;
    if ((rc = read_u32(fp, &out->cd_size))            != ZIP_OK) return rc;
    if ((rc = read_u32(fp, &out->cd_offset))          != ZIP_OK) return rc;
    if ((rc = read_u16(fp, &out->comment_length))     != ZIP_OK) return rc;

    return ZIP_OK;
}

/* -------------------------------------------------------------------------
 * Decompression helpers
 * ---------------------------------------------------------------------- */

/** Read compressed_size bytes from fp; write uncompressed data to dest_fp.
 *  Uses zlib inflate (raw DEFLATE, no zlib/gzip header). */
static int inflate_entry(FILE *fp, FILE *dest_fp,
                         uint32_t compressed_size,
                         uint32_t uncompressed_size,
                         uint32_t expected_crc)
{
    uint8_t in_buf[INFLATE_CHUNK];
    uint8_t out_buf[INFLATE_CHUNK];
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        return ZIP_ERR_IO;

    uLong running_crc = crc32(0L, NULL, 0);
    uint32_t remaining = compressed_size;
    uint32_t written   = 0;
    int zret = Z_OK;
    int rc   = ZIP_OK;

    while (remaining > 0 && zret != Z_STREAM_END) {
        uint32_t chunk = (remaining < INFLATE_CHUNK) ? remaining : INFLATE_CHUNK;

        /* Read chunk bytes one at a time via our byte reader, or in bulk
         * when the underlying I/O is already byte-oriented through FILE*. */
        if (fread(in_buf, 1, chunk, fp) != chunk) {
            rc = ZIP_ERR_IO; break;
        }
        remaining -= chunk;

        strm.next_in  = in_buf;
        strm.avail_in = chunk;

        while (strm.avail_in > 0) {
            strm.next_out  = out_buf;
            strm.avail_out = INFLATE_CHUNK;

            zret = inflate(&strm, Z_NO_FLUSH);
            if (zret == Z_NEED_DICT || zret == Z_DATA_ERROR ||
                zret == Z_MEM_ERROR) {
                rc = ZIP_ERR_FORMAT; goto done;
            }

            uint32_t produced = INFLATE_CHUNK - strm.avail_out;
            running_crc = crc32(running_crc, out_buf, produced);
            written += produced;

            if (fwrite(out_buf, 1, produced, dest_fp) != produced) {
                rc = ZIP_ERR_IO; goto done;
            }
        }
    }

done:
    inflateEnd(&strm);
    if (rc != ZIP_OK) return rc;

    if (written != uncompressed_size) return ZIP_ERR_FORMAT;
    if ((uint32_t)running_crc != expected_crc) return ZIP_ERR_CRC;
    return ZIP_OK;
}

/** Read stored (uncompressed) entry data from fp to dest_fp. */
static int store_entry(FILE *fp, FILE *dest_fp,
                       uint32_t size, uint32_t expected_crc)
{
    uLong running_crc = crc32(0L, NULL, 0);
    uint8_t buf[INFLATE_CHUNK];
    uint32_t remaining = size;

    while (remaining > 0) {
        uint32_t chunk = (remaining < INFLATE_CHUNK) ? remaining : INFLATE_CHUNK;
        if (fread(buf, 1, chunk, fp) != chunk) return ZIP_ERR_IO;
        running_crc = crc32(running_crc, buf, chunk);
        if (fwrite(buf, 1, chunk, dest_fp) != chunk) return ZIP_ERR_IO;
        remaining -= chunk;
    }

    if ((uint32_t)running_crc != expected_crc) return ZIP_ERR_CRC;
    return ZIP_OK;
}

/* -------------------------------------------------------------------------
 * High-level extraction
 * ---------------------------------------------------------------------- */

int zip_extract_all(FILE *fp, const zip_eocd_t *eocd, const char *out_dir)
{
    if (!fp || !eocd || !out_dir) return ZIP_ERR_FORMAT;

    /* Seek to the start of the Central Directory */
    if (fseek(fp, (long)eocd->cd_offset, SEEK_SET) != 0)
        return ZIP_ERR_IO;

    for (uint16_t i = 0; i < eocd->cd_entries_total; ++i) {
        zip_central_dir_header_t cd;
        int rc = zip_read_central_dir_header(fp, &cd);
        if (rc != ZIP_OK) return rc;

        /* Skip directory entries */
        uint16_t name_len = cd.filename_length;
        int is_dir = (name_len > 0 &&
                      cd.filename[name_len - 1] == '/');

        if (is_dir) {
            zip_central_dir_header_free(&cd);
            continue;
        }

        /* Remember where the Central Directory scan is, then jump to
         * the Local File Header for this entry. */
        long cd_pos = ftell(fp);
        if (cd_pos < 0) { zip_central_dir_header_free(&cd); return ZIP_ERR_IO; }

        if (fseek(fp, (long)cd.local_header_offset, SEEK_SET) != 0) {
            zip_central_dir_header_free(&cd);
            return ZIP_ERR_IO;
        }

        zip_local_file_header_t lh;
        rc = zip_read_local_file_header(fp, &lh);
        if (rc != ZIP_OK) {
            zip_central_dir_header_free(&cd);
            return rc;
        }

        /* Use sizes/CRC from the Central Directory (authoritative) */
        uint32_t comp_size   = cd.compressed_size;
        uint32_t uncomp_size = cd.uncompressed_size;
        uint32_t entry_crc   = cd.crc32;

        if (cd.compression_method != METHOD_STORE &&
            cd.compression_method != METHOD_DEFLATE) {
            fprintf(stderr, "  skip  %s  (unsupported method %u)\n",
                    cd.filename, cd.compression_method);
            zip_local_file_header_free(&lh);
            zip_central_dir_header_free(&cd);
            if (fseek(fp, cd_pos, SEEK_SET) != 0) return ZIP_ERR_IO;
            continue;
        }

        /* Build destination path */
        size_t dir_len      = strlen(out_dir);
        size_t filename_len = strlen(cd.filename);
        char *dest = (char *)malloc(dir_len + 1 + filename_len + 1);
        if (!dest) {
            zip_local_file_header_free(&lh);
            zip_central_dir_header_free(&cd);
            return ZIP_ERR_MEMORY;
        }
        memcpy(dest, out_dir, dir_len);
        dest[dir_len] = '/';
        memcpy(dest + dir_len + 1, cd.filename, filename_len + 1);

#ifdef _WIN32
        /* Normalize path separators on Windows */
        for (size_t k = dir_len + 1; k < dir_len + 1 + filename_len; ++k)
            if (dest[k] == '/') dest[k] = '\\';
#endif

        /* Create any missing parent directories */
        for (size_t k = dir_len + 2; k <= dir_len + 1 + filename_len; ++k) {
            if (dest[k] == '/' || dest[k] == '\\') {
                size_t filename_offset = k - dir_len - 1;
                dest[k] = '\0';
#ifdef _WIN32
                _mkdir(dest);
#else
                mkdir(dest, 0755);
#endif
                dest[k] = cd.filename[filename_offset];
            }
        }

        FILE *dest_fp;
#ifdef _WIN32
        if (fopen_s(&dest_fp, dest, "wb") != 0) dest_fp = NULL;
#else
        dest_fp = fopen(dest, "wb");
#endif
        if (!dest_fp) {
            fprintf(stderr, "  error  cannot create %s\n", dest);
            free(dest);
            zip_local_file_header_free(&lh);
            zip_central_dir_header_free(&cd);
            if (fseek(fp, cd_pos, SEEK_SET) != 0) return ZIP_ERR_IO;
            continue;
        }

        if (cd.compression_method == METHOD_DEFLATE)
            rc = inflate_entry(fp, dest_fp, comp_size, uncomp_size, entry_crc);
        else
            rc = store_entry(fp, dest_fp, comp_size, entry_crc);

        fclose(dest_fp);

        if (rc == ZIP_OK)
            printf("  ok    %s  (%u bytes)\n", dest, uncomp_size);
        else
            fprintf(stderr, "  error  %s: %s\n", dest, zip_strerror(rc));

        free(dest);
        zip_local_file_header_free(&lh);
        zip_central_dir_header_free(&cd);

        /* Return to Central Directory position for next entry */
        if (fseek(fp, cd_pos, SEEK_SET) != 0) return ZIP_ERR_IO;

        if (rc != ZIP_OK) return rc;
    }

    return ZIP_OK;
}
