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

typedef struct {
    uint32_t transcript_offset;
    char *name;
} Video;

Video *videos;

typedef struct {
    uint32_t term_id;
    uint32_t video_id;
    uint32_t pos;
    uint32_t ts_min;
    uint32_t ts_max;
    uint32_t byte_pos;
    uint32_t count;
    uint8_t *ptr;
} TermIterator;

typedef struct {
    uint32_t video_id;
    uint32_t ts_min;
    uint32_t ts_max;
    uint32_t byte_pos_min;
    uint32_t byte_pos_max;
    uint32_t rank;
} SearchResult;

const size_t MAX_TOKEN_LEN = 100;
const size_t MAX_QUERY_LEN = 100;

uint32_t get_diff(uint8_t **ptr) {
    uint32_t result = 0;
    int shift = 0;
    while (true) {
        uint8_t n = **ptr;
        result |= (n & 0x7f) << shift;
        (*ptr)++;
        if ((n & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

bool next_term(TermIterator *term) {
    if (term->count == 0) {
        return false;
    }
    uint32_t video_id_diff = get_diff(&term->ptr);
    uint32_t pos_diff = get_diff(&term->ptr);
    uint32_t ts_min_diff = get_diff(&term->ptr);
    uint32_t ts_max_diff = get_diff(&term->ptr);
    uint32_t byte_pos_diff = get_diff(&term->ptr);
    if (video_id_diff > 0) {
        term->pos = 0;
        term->ts_min = 0;
        term->ts_max = 0;
        term->byte_pos = 0;
    }
    term->video_id += video_id_diff;
    term->pos += pos_diff;
    term->ts_min += ts_min_diff;
    term->ts_max += ts_max_diff;
    term->byte_pos += byte_pos_diff;
    term->count -= 1;
    return true;
}

TermIterator create_term_iterator(uint32_t term_id) {
    TermIterator term;
    TermOffset term_offset = term_offsets[term_id];
    term.term_id = term_id;
    term.ptr = (uint8_t*) (mapped_file + term_offset.offset);
    term.count = term_offset.count;
    next_term(&term);
    return term;
}

void post_process_token(char *buf) {
    // lowercase ascii for now. TODO: stemming
    size_t i = 0;
    while (buf[i] != '\0') {
        if (buf[i] >= 'A' && buf[i] <= 'Z') {
            buf[i] -= 'A' - 'a';
        }
        i++;
    }
}

TermIterator *tokenize(char *search_query, size_t *word_count) {
    TermIterator *terms = NULL;
    char buf[MAX_TOKEN_LEN];
    size_t qlen = strlen(search_query);
    size_t tlen = 0;
    for (size_t i = 0; i < qlen; i++) {
        char c = search_query[i];
        if (c != ' ') {
            if (tlen < MAX_TOKEN_LEN - 1) {
                buf[tlen] = c;
                tlen++;
            }
        } else {
            if (tlen > 0) {
                (*word_count)++;
                buf[tlen] = '\0';
                post_process_token(buf);
                uint32_t term_id = shget(vocab, buf);
                if (term_id != NOT_FOUND) {
                    arrput(terms, create_term_iterator(term_id));
                }
                tlen = 0;
            }
        }
    }
    if (tlen > 0) {
        (*word_count)++;
        buf[tlen] = '\0';
        post_process_token(buf);
        uint32_t term_id = shget(vocab, buf);
        if (term_id != NOT_FOUND) {
            arrput(terms, create_term_iterator(term_id));
        }
    }
    return terms;
}

SearchResult *search(char *search_query) {
    SearchResult *results = NULL;
    size_t word_count = 0;
    TermIterator *terms = tokenize(search_query, &word_count);
    printf("Word count: %ld, token count: %ld, tokens: [", word_count, arrlen(terms));
    for (size_t i = 0; i < arrlen(terms); i++) {
        printf("%d", terms[i].term_id);
        if (i < arrlen(terms) - 1) {
            printf(", ");
        }
    }
    printf("]\n");
    arrfree(terms);
    return results;
}

void load_index_data(void) {
    // vocab
    shdefault(vocab, NOT_FOUND);
    uint32_t offs = header.vocab_table_offset;
    for (int i = 0; i < header.vocab_count; i++) {
        uint32_t term_id = *(uint32_t*) (mapped_file + offs);
        uint8_t word_length = *(uint8_t*) (mapped_file + 4 + offs);
        char *word = (char*) (mapped_file + 5 + offs);
        shput(vocab, word, term_id);
        offs += word_length + 5;
    }
    // terms
    term_offsets = (TermOffset*) (mapped_file + header.term_table_offset);
    // videos
    offs = header.video_table_offset;
    videos = calloc(header.video_count, sizeof(videos[0]));
    for (int i = 0; i < header.video_count; i++) {
        videos[i].transcript_offset = *(uint32_t*) (mapped_file + offs);
        uint8_t word_length = *(uint8_t*) (mapped_file + 4 + offs);
        videos[i].name = (char*) (mapped_file + 5 + offs);
        offs += word_length + 5;
    }
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
        char buf[MAX_QUERY_LEN];
        printf("> ");
        if (readln(buf, MAX_QUERY_LEN) < 0) break;
        SearchResult *results = search(buf);
        arrfree(results);
    }
    printf("\n");
    return 0;
}
