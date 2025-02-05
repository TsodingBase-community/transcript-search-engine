#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/mman.h>
#include "stb_ds.h"

typedef struct {
    char magic[8];
    uint64_t creation_ts;
    uint32_t header_size;
    uint32_t file_size;
    uint32_t video_count;
    uint32_t video_table_offset;
    uint32_t vocab_count;
    uint32_t vocab_table_offset;
    uint32_t term_count;
    uint32_t term_table_offset;
} Header;
Header header = {0};

char *mapped_file;

typedef struct {
    char *key;
    uint32_t value;
} Vocab;

Vocab *vocab = NULL;
const uint32_t NOT_FOUND = 0xFFFFFFFF;

typedef struct {
    uint32_t offset;
    uint32_t count;
} TermOffset;

TermOffset *term_offsets;

void load_index_data(void) {
    shdefault(vocab, NOT_FOUND);
    uint32_t offs = 0;
    for (int i = 0; i < header.vocab_count; i++) {
        uint32_t term_id = *(uint32_t*) (mapped_file + header.vocab_table_offset + offs);
        uint8_t word_length = *(uint8_t*) (mapped_file + header.vocab_table_offset + 4 + offs);
        char *word = (char*) (mapped_file + header.vocab_table_offset + 5 + offs);
        shput(vocab, word, term_id);
        offs += word_length + 5;
    }
    term_offsets = (TermOffset*) (mapped_file + header.term_table_offset);
}

int readln(char *buf, size_t len) {
    size_t i;
    buf[0] = '\0';
    for (i = 0; i < len - 1; i++) {
        int c = getchar();
        if (c < 0) return -1;
        if (c == '\n') break;
        buf[i] = c;
    }
    buf[i] = '\0';
    return i;
}

int main(int argc, char **argv) {
    FILE *index_file = fopen("tsoding.dat", "r");
    if (!index_file) {
        fprintf(stderr, "Can't open tsoding.dat\n");
        exit(1);
    }
    int c = fread(&header, sizeof(header), 1, index_file);
    if (c != 1) {
        fprintf(stderr, "File is smaller than the header, something quite wrong\n");
        exit(1);
    }
    if (memcmp(header.magic, "TSOD0001", 8) != 0) {
        fprintf(stderr, "File has unexpected magic number\n");
        exit(1);
    }
    if (header.header_size != sizeof(header)) {
        fprintf(stderr, "File specifies unexpected header size\n");
        exit(1);
    }
    fseek(index_file, 0, SEEK_END);
    long file_size = ftell(index_file);
    if (file_size != header.file_size) {
        fprintf(stderr, "File seems truncated\n");
        exit(1);
    }
    mapped_file = mmap(NULL, file_size, PROT_READ, MAP_SHARED, fileno(index_file), 0);
    fclose(index_file);
    if (mapped_file == MAP_FAILED) {
        fprintf(stderr, "mmap failed\n");
        exit(1);
    }
    load_index_data();
    while (true) {
        char buf[100];
        printf("> ");
        if (readln(buf, 100) < 0) break;
        uint32_t id = shget(vocab, buf);
        if (id == NOT_FOUND) {
            printf("vocab['%s'] not found\n", buf);
            continue;
        }
        printf("word='%s', term_id=%d, offset=%d, count=%d\n",
                buf, id, term_offsets[id].offset, term_offsets[id].count);
    }
    printf("\n");
    return 0;
}
