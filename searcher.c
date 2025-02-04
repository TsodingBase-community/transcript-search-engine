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

void load_vocab(void) {
    shdefault(vocab, NOT_FOUND);
    uint32_t offs = 0;
    for (int i = 0; i < header.vocab_count; i++) {
        uint32_t term_id = *(uint32_t*) (mapped_file + header.vocab_table_offset + offs);
        uint8_t word_length = *(uint8_t*) (mapped_file + header.vocab_table_offset + 4 + offs);
        char *word = (char*) (mapped_file + header.vocab_table_offset + 5 + offs);
        printf("word: %s\n", word);
        shput(vocab, word, term_id);
        offs += word_length + 5;
    }
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
    load_vocab();
    while (true) {
        char buf[100];
        printf("> ");
        scanf("%s", buf);
        uint32_t id = shget(vocab, buf);
        if (id != NOT_FOUND) {
            printf("vocab['%s'] = %d\n", buf, id);
        } else {
            printf("vocab['%s'] not found\n", buf);
        }
    }
    return 0;
}
