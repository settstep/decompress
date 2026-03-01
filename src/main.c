#include "zip_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int has_data_descriptor(uint16_t flags) {
    return (flags & 0x0008u) != 0;
}

static int is_directory_entry(const char* name) {
    size_t len = strlen(name);
    return len > 0 && name[len - 1] == '/';
}

static void sanitize_name(char* name) {
    for (size_t i = 0; name[i] != '\0'; ++i) {
        if (name[i] == '/' || name[i] == '\\' || name[i] == ':') {
            name[i] = '_';
        }
    }
}

static ZipStatus read_file_bytes(const char* path, uint8_t** out_data, size_t* out_size) {
    FILE* fp;
    uint8_t* buffer = NULL;
    size_t capacity = 4096;
    size_t length = 0;

    if (!path || !out_data || !out_size) {
        return ZIP_ERR_INVALID_ARG;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return ZIP_ERR_IO;
    }

    buffer = (uint8_t*)malloc(capacity);
    if (!buffer) {
        fclose(fp);
        return ZIP_ERR_MEMORY;
    }

    for (;;) {
        int byte = fgetc(fp);
        if (byte == EOF) {
            if (ferror(fp)) {
                free(buffer);
                fclose(fp);
                return ZIP_ERR_IO;
            }
            break;
        }

        if (length == capacity) {
            size_t new_capacity = capacity * 2;
            uint8_t* tmp = (uint8_t*)realloc(buffer, new_capacity);
            if (!tmp) {
                free(buffer);
                fclose(fp);
                return ZIP_ERR_MEMORY;
            }
            buffer = tmp;
            capacity = new_capacity;
        }

        buffer[length++] = (uint8_t)byte;
    }

    fclose(fp);

    *out_data = buffer;
    *out_size = length;
    return ZIP_OK;
}

static ZipStatus write_output_file(const char* name, const uint8_t* data, size_t size) {
    FILE* out;

    out = fopen(name, "wb");
    if (!out) {
        return ZIP_ERR_IO;
    }

    if (size > 0 && fwrite(data, 1, size, out) != size) {
        fclose(out);
        return ZIP_ERR_IO;
    }

    fclose(out);
    return ZIP_OK;
}

static ZipStatus extract_one(
    const uint8_t* data,
    size_t size,
    const CentralDirectoryRecord* cdr) {
    LocalFileRecord lfh;
    size_t next_offset = 0;
    ZipStatus st;
    char* file_name = NULL;
    uint8_t* out = NULL;

    if (!data || !cdr) {
        return ZIP_ERR_INVALID_ARG;
    }

    st = read_local_file_record(data, size, cdr->local_header_offset, &lfh, &next_offset);
    if (st != ZIP_OK) {
        return st;
    }

    if (has_data_descriptor(lfh.general_purpose_flag) || has_data_descriptor(cdr->general_purpose_flag)) {
        return ZIP_ERR_UNSUPPORTED;
    }

    if (lfh.compression_method != 0) {
        return ZIP_ERR_UNSUPPORTED;
    }

    file_name = (char*)malloc((size_t)cdr->file_name_length + 1);
    if (!file_name) {
        return ZIP_ERR_MEMORY;
    }

    memcpy(file_name, cdr->file_name, cdr->file_name_length);
    file_name[cdr->file_name_length] = '\0';

    if (is_directory_entry(file_name)) {
        free(file_name);
        return ZIP_OK;
    }

    sanitize_name(file_name);

    out = (uint8_t*)malloc(cdr->uncompressed_size > 0 ? cdr->uncompressed_size : 1);
    if (!out) {
        free(file_name);
        return ZIP_ERR_MEMORY;
    }

    if (lfh.compressed_size != cdr->uncompressed_size) {
        free(out);
        free(file_name);
        return ZIP_ERR_BAD_FORMAT;
    }
    memcpy(out, lfh.compressed_data, cdr->uncompressed_size);

    st = write_output_file(file_name, out, cdr->uncompressed_size);
    free(out);
    free(file_name);

    return st;
}

int main(int argc, char** argv) {
    uint8_t* data = NULL;
    size_t size = 0;
    size_t eocd_offset = 0;
    size_t central_offset = 0;
    EndOfCentralDirectoryRecord eocd;
    ZipStatus st;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <zip-file>\n", argv[0]);
        return ZIP_ERR_INVALID_ARG;
    }

    st = read_file_bytes(argv[1], &data, &size);
    if (st != ZIP_OK) {
        fprintf(stderr, "Datei konnte nicht gelesen werden: %s\n", zip_status_to_string(st));
        return st;
    }

    st = find_end_of_central_directory(data, size, &eocd_offset);
    if (st != ZIP_OK) {
        fprintf(stderr, "EOCD nicht gefunden: %s\n", zip_status_to_string(st));
        free(data);
        return st;
    }

    st = read_end_of_central_directory_record(data, size, eocd_offset, &eocd, NULL);
    if (st != ZIP_OK) {
        fprintf(stderr, "EOCD ungültig: %s\n", zip_status_to_string(st));
        free(data);
        return st;
    }

    central_offset = eocd.central_directory_offset;
    for (uint16_t i = 0; i < eocd.total_central_directory_records; ++i) {
        CentralDirectoryRecord cdr;
        size_t next = 0;

        st = read_central_directory_record(data, size, central_offset, &cdr, &next);
        if (st != ZIP_OK) {
            fprintf(stderr, "Central Directory Record %u ungültig: %s\n", i, zip_status_to_string(st));
            free(data);
            return st;
        }

        st = extract_one(data, size, &cdr);
        if (st != ZIP_OK) {
            fprintf(stderr, "Entpacken von Eintrag %u fehlgeschlagen: %s\n", i, zip_status_to_string(st));
            free(data);
            return st;
        }

        central_offset = next;
    }

    free(data);
    printf("Entpacken abgeschlossen.\n");
    return ZIP_OK;
}
