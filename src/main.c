#include "zip_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#define MAKE_DIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#define MAKE_DIR(path) mkdir(path, 0755)
#endif

static FILE* g_debug_log = NULL;
static void sanitize_name(char* name);
static int normalize_entry_path(char* name);

static int dir_exists(const char* path) {
    struct stat st;
    if (!path) {
        return 0;
    }
    if (stat(path, &st) != 0) {
        return 0;
    }
    return (st.st_mode & S_IFDIR) != 0;
}

static const char* path_basename_ptr(const char* path) {
    const char* base = path;
    if (!path) {
        return NULL;
    }
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return base;
}

static char* build_output_directory_name(const char* input_path) {
    const char* base = path_basename_ptr(input_path);
    size_t len;
    char* out;
    char* dot;

    if (!base || *base == '\0') {
        return NULL;
    }

    len = strlen(base);
    out = (char*)malloc(len + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, base, len + 1);
    dot = strrchr(out, '.');
    if (dot) {
        *dot = '\0';
    }

    if (out[0] == '\0') {
        free(out);
        return NULL;
    }

    sanitize_name(out);
    return out;
}

static char* build_debug_log_path(const char* input_path) {
    const char* last_sep = NULL;
    const char* log_name = "debug.log";
    size_t log_name_len = strlen(log_name);

    if (!input_path || *input_path == '\0') {
        char* out = (char*)malloc(log_name_len + 1);
        if (!out) {
            return NULL;
        }
        memcpy(out, log_name, log_name_len + 1);
        return out;
    }

    for (const char* p = input_path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            last_sep = p;
        }
    }

    if (!last_sep) {
        char* out = (char*)malloc(log_name_len + 1);
        if (!out) {
            return NULL;
        }
        memcpy(out, log_name, log_name_len + 1);
        return out;
    }

    {
        size_t dir_len = (size_t)(last_sep - input_path + 1);
        char* out = (char*)malloc(dir_len + log_name_len + 1);
        if (!out) {
            return NULL;
        }
        memcpy(out, input_path, dir_len);
        memcpy(out + dir_len, log_name, log_name_len + 1);
        return out;
    }
}

static ZipStatus ensure_output_directory(const char* output_dir) {
    if (!output_dir || *output_dir == '\0') {
        return ZIP_ERR_INVALID_ARG;
    }

    if (dir_exists(output_dir)) {
        return ZIP_OK;
    }

    if (MAKE_DIR(output_dir) != 0) {
        return ZIP_ERR_IO;
    }

    return ZIP_OK;
}

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
    return len > 0 && (name[len - 1] == '/' || name[len - 1] == '\\');
}

static void sanitize_name(char* name) {
    for (size_t i = 0; name[i] != '\0'; ++i) {
        if (name[i] == '/' || name[i] == '\\' || name[i] == ':') {
            name[i] = '_';
        }
    }
}

static int normalize_entry_path(char* name) {
    size_t segment_start = 0;

    if (!name || name[0] == '\0') {
        return 0;
    }

    if (name[0] == '/' || name[0] == '\\') {
        return 0;
    }

    if (((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z')) && name[1] == ':') {
        return 0;
    }

    for (size_t i = 0; name[i] != '\0'; ++i) {
        if (name[i] == '\\') {
            name[i] = '/';
        } else if (name[i] == ':') {
            name[i] = '_';
        }
    }

    for (size_t i = 0;; ++i) {
        if (name[i] == '/' || name[i] == '\0') {
            size_t segment_len = i - segment_start;
            if (segment_len == 2 && name[segment_start] == '.' && name[segment_start + 1] == '.') {
                return 0;
            }
            if (name[i] == '\0') {
                break;
            }
            segment_start = i + 1;
        }
    }

    return 1;
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

static ZipStatus write_output_file(const char* output_dir, const char* name, const uint8_t* data, size_t size) {
    FILE* out;
    size_t path_len;
    char* out_path;

    if (!output_dir || !name) {
        return ZIP_ERR_INVALID_ARG;
    }

    path_len = strlen(output_dir) + 1 + strlen(name) + 1;
    out_path = (char*)malloc(path_len);
    if (!out_path) {
        return ZIP_ERR_MEMORY;
    }

    snprintf(out_path, path_len, "%s/%s", output_dir, name);

    {
        char* dir_path = (char*)malloc(path_len);
        if (!dir_path) {
            free(out_path);
            return ZIP_ERR_MEMORY;
        }

        memcpy(dir_path, out_path, path_len);
        for (size_t i = 0; dir_path[i] != '\0'; ++i) {
            if (dir_path[i] == '/' || dir_path[i] == '\\') {
                char saved = dir_path[i];
                dir_path[i] = '\0';
                if (dir_path[0] != '\0' && !dir_exists(dir_path)) {
                    if (MAKE_DIR(dir_path) != 0 && !dir_exists(dir_path)) {
                        debug_log_step("Zwischenordner konnte nicht erstellt werden: %s", dir_path);
                        free(dir_path);
                        free(out_path);
                        return ZIP_ERR_IO;
                    }
                }
                dir_path[i] = saved;
            }
        }
        free(dir_path);
    }

    out = fopen(out_path, "wb");
    if (!out) {
        debug_log_step("Ausgabedatei konnte nicht erstellt werden: %s", out_path);
        free(out_path);
        return ZIP_ERR_IO;
    }

    if (size > 0 && fwrite(data, 1, size, out) != size) {
        fclose(out);
        debug_log_step("Schreibfehler in Ausgabedatei: %s", out_path);
        free(out_path);
        return ZIP_ERR_IO;
    }

    fclose(out);
    free(out_path);
    return ZIP_OK;
}

static ZipStatus decompress_deflate(const uint8_t* in, size_t in_size, uint8_t* out, size_t out_size) {
#ifdef HAVE_ZLIB
    z_stream stream;
    int zret;

    memset(&stream, 0, sizeof(stream));
    stream.next_in = (Bytef*)in;
    stream.avail_in = (uInt)in_size;
    stream.next_out = (Bytef*)out;
    stream.avail_out = (uInt)out_size;

    zret = inflateInit2(&stream, -MAX_WBITS);
    if (zret != Z_OK) {
        debug_log_step("inflateInit2 fehlgeschlagen: %d", zret);
        return ZIP_ERR_DECOMPRESS;
    }

    zret = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    if (zret != Z_STREAM_END || stream.total_out != out_size) {
        debug_log_step(
            "DEFLATE fehlgeschlagen: zret=%d total_out=%lu erwartet=%zu",
            zret,
            (unsigned long)stream.total_out,
            out_size);
        return ZIP_ERR_DECOMPRESS;
    }

    return ZIP_OK;
#else
    (void)in;
    (void)in_size;
    (void)out;
    (void)out_size;
    debug_log_step("DEFLATE nicht verfuegbar: ohne ZLIB gebaut");
    return ZIP_ERR_UNSUPPORTED;
#endif
}

static ZipStatus extract_one(
    const uint8_t* data,
    size_t size,
    const CentralDirectoryRecord* cdr,
    const char* output_dir) {
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

    if (lfh.compression_method != 0 && lfh.compression_method != 8) {
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

    if (!normalize_entry_path(file_name)) {
        debug_log_step("Ungueltiger Dateipfad im ZIP-Eintrag: %s", file_name);
        free(file_name);
        return ZIP_ERR_BAD_FORMAT;
    }

    out = (uint8_t*)malloc(cdr->uncompressed_size > 0 ? cdr->uncompressed_size : 1);
    if (!out) {
        free(file_name);
        debug_log_step("Speicherfehler fuer Ausgabepuffer");
        return ZIP_ERR_MEMORY;
    }

    if (lfh.compression_method == 0) {
        if (lfh.compressed_size != cdr->uncompressed_size) {
            free(out);
            free(file_name);
            debug_log_step("Formatfehler: compressed_size (%u) != uncompressed_size (%u) bei STORE", lfh.compressed_size, cdr->uncompressed_size);
            return ZIP_ERR_BAD_FORMAT;
        }
        memcpy(out, lfh.compressed_data, cdr->uncompressed_size);
        debug_log_step("STORE-Daten kopiert: %u Bytes", cdr->uncompressed_size);
    } else {
        st = decompress_deflate(lfh.compressed_data, lfh.compressed_size, out, cdr->uncompressed_size);
        if (st != ZIP_OK) {
            free(out);
            free(file_name);
            return st;
        }
        debug_log_step("DEFLATE-Daten dekomprimiert: %u Bytes", cdr->uncompressed_size);
    }

    st = write_output_file(output_dir, file_name, out, cdr->uncompressed_size);
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
    char* output_dir = NULL;
    char* debug_path = NULL;
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

    output_dir = build_output_directory_name(zip_path);
    if (!output_dir) {
        fprintf(stderr, "Ausgabeordner konnte nicht bestimmt werden.\n");
        return ZIP_ERR_INVALID_ARG;
    }

    st = ensure_output_directory(output_dir);
    if (st != ZIP_OK) {
        fprintf(stderr, "Ausgabeordner konnte nicht erstellt werden: %s\n", output_dir);
        free(output_dir);
        return st;
    }

    if (debug_enabled) {
        debug_path = build_debug_log_path(zip_path);
        if (!debug_path) {
            free(output_dir);
            return ZIP_ERR_MEMORY;
        }

        g_debug_log = fopen(debug_path, "w");
        if (!g_debug_log) {
            fprintf(stderr, "debug.log konnte nicht erstellt werden.\n");
            free(debug_path);
            free(output_dir);
            return ZIP_ERR_IO;
        }
        debug_log_step("Debug-Modus aktiviert");
        debug_log_step("ZIP-Datei: %s", zip_path);
        debug_log_step("Ausgabeordner: %s", output_dir);
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

        st = extract_one(data, size, &cdr, output_dir);
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
    free(debug_path);
    free(output_dir);
    if (g_debug_log) {
        debug_log_step("Programmende mit Rueckgabewert: %d", exit_code);
        fclose(g_debug_log);
        g_debug_log = NULL;
    }
    return exit_code;
}
