#include "zip_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static FILE* g_debug_log = NULL;

static void debug_log_step(const char* format, ...) {
    va_list args;

    if (!g_debug_log || !format) {
        return;
    }

    va_start(args, format);
    vfprintf(g_debug_log, format, args);
    va_end(args);
    fputc('\n', g_debug_log);
    fflush(g_debug_log);
}

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
        debug_log_step("Datei konnte nicht geoeffnet werden: %s", path);
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
                debug_log_step("Fehler beim byteweisen Lesen der Datei: %s", path);
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
                debug_log_step("Speichererweiterung fehlgeschlagen beim Lesen der Datei");
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
    debug_log_step("Datei byteweise eingelesen: %zu Bytes", length);
    return ZIP_OK;
}

static ZipStatus write_output_file(const char* name, const uint8_t* data, size_t size) {
    FILE* out;

    out = fopen(name, "wb");
    if (!out) {
        debug_log_step("Ausgabedatei konnte nicht erstellt werden: %s", name);
        return ZIP_ERR_IO;
    }

    if (size > 0 && fwrite(data, 1, size, out) != size) {
        fclose(out);
        debug_log_step("Schreibfehler in Ausgabedatei: %s", name);
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
        debug_log_step("Local File Record ungueltig an Offset %u: %s", cdr->local_header_offset, zip_status_to_string(st));
        return st;
    }

    debug_log_step(
        "Local File Record gelesen: method=%u compressed=%u uncompressed=%u flags=0x%04x",
        lfh.compression_method,
        lfh.compressed_size,
        lfh.uncompressed_size,
        lfh.general_purpose_flag);

    if (has_data_descriptor(lfh.general_purpose_flag) || has_data_descriptor(cdr->general_purpose_flag)) {
        debug_log_step("Nicht unterstuetzt: Data Descriptor (bit 3) gesetzt");
        return ZIP_ERR_UNSUPPORTED;
    }

    if (lfh.compression_method != 0) {
        debug_log_step("Nicht unterstuetzte Kompressionsmethode: %u", lfh.compression_method);
        return ZIP_ERR_UNSUPPORTED;
    }

    file_name = (char*)malloc((size_t)cdr->file_name_length + 1);
    if (!file_name) {
        debug_log_step("Speicherfehler fuer Dateinamenpuffer");
        return ZIP_ERR_MEMORY;
    }

    memcpy(file_name, cdr->file_name, cdr->file_name_length);
    file_name[cdr->file_name_length] = '\0';

    if (is_directory_entry(file_name)) {
        debug_log_step("Verzeichniseintrag uebersprungen: %s", file_name);
        free(file_name);
        return ZIP_OK;
    }

    sanitize_name(file_name);

    out = (uint8_t*)malloc(cdr->uncompressed_size > 0 ? cdr->uncompressed_size : 1);
    if (!out) {
        free(file_name);
        debug_log_step("Speicherfehler fuer Ausgabepuffer");
        return ZIP_ERR_MEMORY;
    }

    if (lfh.compressed_size != cdr->uncompressed_size) {
        free(out);
        free(file_name);
        debug_log_step("Formatfehler: compressed_size (%u) != uncompressed_size (%u) bei STORE", lfh.compressed_size, cdr->uncompressed_size);
        return ZIP_ERR_BAD_FORMAT;
    }
    memcpy(out, lfh.compressed_data, cdr->uncompressed_size);
    debug_log_step("STORE-Daten kopiert: %u Bytes", cdr->uncompressed_size);

    st = write_output_file(file_name, out, cdr->uncompressed_size);
    debug_log_step("Ausgabe geschrieben: %s (Status: %s)", file_name, zip_status_to_string(st));
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
    const char* zip_path = NULL;
    int debug_enabled = 0;
    int exit_code = ZIP_OK;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-debug") == 0) {
            debug_enabled = 1;
            continue;
        }

        if (!zip_path) {
            zip_path = argv[i];
            continue;
        }

        fprintf(stderr, "Usage: %s [-debug] <zip-file>\n", argv[0]);
        return ZIP_ERR_INVALID_ARG;
    }

    if (!zip_path) {
        fprintf(stderr, "Usage: %s [-debug] <zip-file>\n", argv[0]);
        return ZIP_ERR_INVALID_ARG;
    }

    if (debug_enabled) {
        g_debug_log = fopen("debug.log", "w");
        if (!g_debug_log) {
            fprintf(stderr, "debug.log konnte nicht erstellt werden.\n");
            return ZIP_ERR_IO;
        }
        debug_log_step("Debug-Modus aktiviert");
        debug_log_step("ZIP-Datei: %s", zip_path);
    }

    debug_log_step("Starte byteweises Einlesen der Datei");

    st = read_file_bytes(zip_path, &data, &size);
    if (st != ZIP_OK) {
        fprintf(stderr, "Datei konnte nicht gelesen werden: %s\n", zip_status_to_string(st));
        debug_log_step("Abbruch: %s", zip_status_to_string(st));
        exit_code = st;
        goto cleanup;
    }

    debug_log_step("Suche End Of Central Directory");
    st = find_end_of_central_directory(data, size, &eocd_offset);
    if (st != ZIP_OK) {
        fprintf(stderr, "EOCD nicht gefunden: %s\n", zip_status_to_string(st));
        debug_log_step("Abbruch beim Suchen von EOCD: %s", zip_status_to_string(st));
        exit_code = st;
        goto cleanup;
    }
    debug_log_step("EOCD gefunden bei Offset: %zu", eocd_offset);

    st = read_end_of_central_directory_record(data, size, eocd_offset, &eocd, NULL);
    if (st != ZIP_OK) {
        fprintf(stderr, "EOCD ungültig: %s\n", zip_status_to_string(st));
        debug_log_step("Abbruch beim Lesen von EOCD: %s", zip_status_to_string(st));
        exit_code = st;
        goto cleanup;
    }

    debug_log_step(
        "EOCD gelesen: entries=%u central_offset=%u central_size=%u",
        eocd.total_central_directory_records,
        eocd.central_directory_offset,
        eocd.central_directory_size);

    central_offset = eocd.central_directory_offset;
    for (uint16_t i = 0; i < eocd.total_central_directory_records; ++i) {
        CentralDirectoryRecord cdr;
        size_t next = 0;

        debug_log_step("Lese Central Directory Record %u bei Offset %zu", i, central_offset);

        st = read_central_directory_record(data, size, central_offset, &cdr, &next);
        if (st != ZIP_OK) {
            fprintf(stderr, "Central Directory Record %u ungültig: %s\n", i, zip_status_to_string(st));
            debug_log_step("Abbruch beim CDR %u: %s", i, zip_status_to_string(st));
            exit_code = st;
            goto cleanup;
        }

        debug_log_step(
            "CDR %u gelesen: method=%u compressed=%u uncompressed=%u local_offset=%u",
            i,
            cdr.compression_method,
            cdr.compressed_size,
            cdr.uncompressed_size,
            cdr.local_header_offset);

        st = extract_one(data, size, &cdr);
        if (st != ZIP_OK) {
            fprintf(stderr, "Entpacken von Eintrag %u fehlgeschlagen: %s\n", i, zip_status_to_string(st));
            debug_log_step("Abbruch beim Entpacken von Eintrag %u: %s", i, zip_status_to_string(st));
            exit_code = st;
            goto cleanup;
        }

        central_offset = next;
    }

    printf("Entpacken abgeschlossen.\n");
    debug_log_step("Entpacken abgeschlossen");
    exit_code = ZIP_OK;

cleanup:
    free(data);
    if (g_debug_log) {
        debug_log_step("Programmende mit Rueckgabewert: %d", exit_code);
        fclose(g_debug_log);
        g_debug_log = NULL;
    }
    return exit_code;
}
