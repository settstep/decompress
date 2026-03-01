#ifndef ZIP_PARSER_H
#define ZIP_PARSER_H

#include <stdint.h>
#include <stddef.h>

typedef enum ZipStatus {
    ZIP_OK = 0,
    ZIP_ERR_INVALID_ARG,
    ZIP_ERR_IO,
    ZIP_ERR_EOF,
    ZIP_ERR_BAD_SIGNATURE,
    ZIP_ERR_BAD_FORMAT,
    ZIP_ERR_UNSUPPORTED,
    ZIP_ERR_MEMORY,
    ZIP_ERR_DECOMPRESS
} ZipStatus;

typedef struct LocalFileRecord {
    uint16_t version_needed;
    uint16_t general_purpose_flag;
    uint16_t compression_method;
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t file_name_length;
    uint16_t extra_field_length;
    const uint8_t* file_name;
    const uint8_t* extra_field;
    const uint8_t* compressed_data;
} LocalFileRecord;

typedef struct CentralDirectoryRecord {
    uint16_t version_made_by;
    uint16_t version_needed;
    uint16_t general_purpose_flag;
    uint16_t compression_method;
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t file_name_length;
    uint16_t extra_field_length;
    uint16_t file_comment_length;
    uint16_t disk_number_start;
    uint16_t internal_file_attributes;
    uint32_t external_file_attributes;
    uint32_t local_header_offset;
    const uint8_t* file_name;
    const uint8_t* extra_field;
    const uint8_t* file_comment;
} CentralDirectoryRecord;

typedef struct EndOfCentralDirectoryRecord {
    uint16_t disk_number;
    uint16_t central_directory_start_disk;
    uint16_t central_directory_records_on_disk;
    uint16_t total_central_directory_records;
    uint32_t central_directory_size;
    uint32_t central_directory_offset;
    uint16_t comment_length;
    const uint8_t* comment;
} EndOfCentralDirectoryRecord;

ZipStatus read_local_file_record(
    const uint8_t* data,
    size_t size,
    size_t offset,
    LocalFileRecord* out,
    size_t* next_offset);

ZipStatus read_central_directory_record(
    const uint8_t* data,
    size_t size,
    size_t offset,
    CentralDirectoryRecord* out,
    size_t* next_offset);

ZipStatus read_end_of_central_directory_record(
    const uint8_t* data,
    size_t size,
    size_t offset,
    EndOfCentralDirectoryRecord* out,
    size_t* next_offset);

ZipStatus find_end_of_central_directory(
    const uint8_t* data,
    size_t size,
    size_t* eocd_offset);

const char* zip_status_to_string(ZipStatus status);

#endif
