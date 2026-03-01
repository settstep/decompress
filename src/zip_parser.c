#include "zip_parser.h"

#include <string.h>

#define LFH_SIGNATURE 0x04034b50u
#define CDR_SIGNATURE 0x02014b50u
#define EOCD_SIGNATURE 0x06054b50u
#define EOCD_MIN_SIZE 22u
#define EOCD_MAX_COMMENT 65535u

static ZipStatus ensure_available(size_t size, size_t offset, size_t needed) {
    if (offset > size || needed > size - offset) {
        return ZIP_ERR_EOF;
    }
    return ZIP_OK;
}

static ZipStatus read_u16_le(const uint8_t* data, size_t size, size_t offset, uint16_t* value) {
    ZipStatus st = ensure_available(size, offset, 2);
    if (st != ZIP_OK) {
        return st;
    }
    *value = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
    return ZIP_OK;
}

static ZipStatus read_u32_le(const uint8_t* data, size_t size, size_t offset, uint32_t* value) {
    ZipStatus st = ensure_available(size, offset, 4);
    if (st != ZIP_OK) {
        return st;
    }
    *value = (uint32_t)data[offset]
        | ((uint32_t)data[offset + 1] << 8)
        | ((uint32_t)data[offset + 2] << 16)
        | ((uint32_t)data[offset + 3] << 24);
    return ZIP_OK;
}

ZipStatus read_local_file_record(
    const uint8_t* data,
    size_t size,
    size_t offset,
    LocalFileRecord* out,
    size_t* next_offset) {
    uint32_t signature = 0;
    ZipStatus st;
    size_t cursor = offset;

    if (!data || !out) {
        return ZIP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    st = read_u32_le(data, size, cursor, &signature);
    if (st != ZIP_OK) {
        return st;
    }
    if (signature != LFH_SIGNATURE) {
        return ZIP_ERR_BAD_SIGNATURE;
    }
    cursor += 4;

    if ((st = read_u16_le(data, size, cursor, &out->version_needed)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->general_purpose_flag)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->compression_method)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->last_mod_time)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->last_mod_date)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u32_le(data, size, cursor, &out->crc32)) != ZIP_OK) return st; cursor += 4;
    if ((st = read_u32_le(data, size, cursor, &out->compressed_size)) != ZIP_OK) return st; cursor += 4;
    if ((st = read_u32_le(data, size, cursor, &out->uncompressed_size)) != ZIP_OK) return st; cursor += 4;
    if ((st = read_u16_le(data, size, cursor, &out->file_name_length)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->extra_field_length)) != ZIP_OK) return st; cursor += 2;

    st = ensure_available(size, cursor, (size_t)out->file_name_length + (size_t)out->extra_field_length);
    if (st != ZIP_OK) {
        return st;
    }

    out->file_name = &data[cursor];
    cursor += out->file_name_length;

    out->extra_field = &data[cursor];
    cursor += out->extra_field_length;

    st = ensure_available(size, cursor, out->compressed_size);
    if (st != ZIP_OK) {
        return st;
    }

    out->compressed_data = &data[cursor];
    cursor += out->compressed_size;

    if (next_offset) {
        *next_offset = cursor;
    }
    return ZIP_OK;
}

ZipStatus read_central_directory_record(
    const uint8_t* data,
    size_t size,
    size_t offset,
    CentralDirectoryRecord* out,
    size_t* next_offset) {
    uint32_t signature = 0;
    ZipStatus st;
    size_t cursor = offset;

    if (!data || !out) {
        return ZIP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    st = read_u32_le(data, size, cursor, &signature);
    if (st != ZIP_OK) {
        return st;
    }
    if (signature != CDR_SIGNATURE) {
        return ZIP_ERR_BAD_SIGNATURE;
    }
    cursor += 4;

    if ((st = read_u16_le(data, size, cursor, &out->version_made_by)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->version_needed)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->general_purpose_flag)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->compression_method)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->last_mod_time)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->last_mod_date)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u32_le(data, size, cursor, &out->crc32)) != ZIP_OK) return st; cursor += 4;
    if ((st = read_u32_le(data, size, cursor, &out->compressed_size)) != ZIP_OK) return st; cursor += 4;
    if ((st = read_u32_le(data, size, cursor, &out->uncompressed_size)) != ZIP_OK) return st; cursor += 4;
    if ((st = read_u16_le(data, size, cursor, &out->file_name_length)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->extra_field_length)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->file_comment_length)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->disk_number_start)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->internal_file_attributes)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u32_le(data, size, cursor, &out->external_file_attributes)) != ZIP_OK) return st; cursor += 4;
    if ((st = read_u32_le(data, size, cursor, &out->local_header_offset)) != ZIP_OK) return st; cursor += 4;

    st = ensure_available(size, cursor, (size_t)out->file_name_length + (size_t)out->extra_field_length + (size_t)out->file_comment_length);
    if (st != ZIP_OK) {
        return st;
    }

    out->file_name = &data[cursor];
    cursor += out->file_name_length;

    out->extra_field = &data[cursor];
    cursor += out->extra_field_length;

    out->file_comment = &data[cursor];
    cursor += out->file_comment_length;

    if (next_offset) {
        *next_offset = cursor;
    }

    return ZIP_OK;
}

ZipStatus read_end_of_central_directory_record(
    const uint8_t* data,
    size_t size,
    size_t offset,
    EndOfCentralDirectoryRecord* out,
    size_t* next_offset) {
    uint32_t signature = 0;
    ZipStatus st;
    size_t cursor = offset;

    if (!data || !out) {
        return ZIP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    st = read_u32_le(data, size, cursor, &signature);
    if (st != ZIP_OK) {
        return st;
    }
    if (signature != EOCD_SIGNATURE) {
        return ZIP_ERR_BAD_SIGNATURE;
    }
    cursor += 4;

    if ((st = read_u16_le(data, size, cursor, &out->disk_number)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->central_directory_start_disk)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->central_directory_records_on_disk)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u16_le(data, size, cursor, &out->total_central_directory_records)) != ZIP_OK) return st; cursor += 2;
    if ((st = read_u32_le(data, size, cursor, &out->central_directory_size)) != ZIP_OK) return st; cursor += 4;
    if ((st = read_u32_le(data, size, cursor, &out->central_directory_offset)) != ZIP_OK) return st; cursor += 4;
    if ((st = read_u16_le(data, size, cursor, &out->comment_length)) != ZIP_OK) return st; cursor += 2;

    st = ensure_available(size, cursor, out->comment_length);
    if (st != ZIP_OK) {
        return st;
    }

    out->comment = &data[cursor];
    cursor += out->comment_length;

    if (next_offset) {
        *next_offset = cursor;
    }

    return ZIP_OK;
}

ZipStatus find_end_of_central_directory(
    const uint8_t* data,
    size_t size,
    size_t* eocd_offset) {
    size_t min_offset;

    if (!data || !eocd_offset) {
        return ZIP_ERR_INVALID_ARG;
    }
    if (size < EOCD_MIN_SIZE) {
        return ZIP_ERR_BAD_FORMAT;
    }

    min_offset = (size > (EOCD_MIN_SIZE + EOCD_MAX_COMMENT))
        ? size - (EOCD_MIN_SIZE + EOCD_MAX_COMMENT)
        : 0;

    for (size_t i = size - EOCD_MIN_SIZE + 1; i-- > min_offset;) {
        uint32_t sig = 0;
        if (read_u32_le(data, size, i, &sig) != ZIP_OK) {
            continue;
        }
        if (sig == EOCD_SIGNATURE) {
            *eocd_offset = i;
            return ZIP_OK;
        }
    }

    return ZIP_ERR_BAD_SIGNATURE;
}

const char* zip_status_to_string(ZipStatus status) {
    switch (status) {
        case ZIP_OK: return "ZIP_OK";
        case ZIP_ERR_INVALID_ARG: return "ZIP_ERR_INVALID_ARG";
        case ZIP_ERR_IO: return "ZIP_ERR_IO";
        case ZIP_ERR_EOF: return "ZIP_ERR_EOF";
        case ZIP_ERR_BAD_SIGNATURE: return "ZIP_ERR_BAD_SIGNATURE";
        case ZIP_ERR_BAD_FORMAT: return "ZIP_ERR_BAD_FORMAT";
        case ZIP_ERR_UNSUPPORTED: return "ZIP_ERR_UNSUPPORTED";
        case ZIP_ERR_MEMORY: return "ZIP_ERR_MEMORY";
        case ZIP_ERR_DECOMPRESS: return "ZIP_ERR_DECOMPRESS";
        default: return "ZIP_ERR_UNKNOWN";
    }
}
