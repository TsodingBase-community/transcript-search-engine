import os
import re
import csv
import glob
import datetime
import itertools
import collections
from dataclasses import dataclass

@dataclass
class Header:
    creation_ts: int = 0
    file_size: int = 0
    video_count: int = 0
    video_table_offset: int = 0
    vocab_count: int = 0
    vocab_table_offset: int = 0
    term_count: int = 0
    term_table_offset: int = 0

    def write(self, out):
        out.seek(0)
        out.write(b'TSOD0001')
        write_u64(out, self.creation_ts)
        write_u32(out, 8 + 8 + 8 * 4) # header size
        write_u32(out, self.file_size)
        write_u32(out, self.video_count)
        write_u32(out, self.video_table_offset)
        write_u32(out, self.vocab_count)
        write_u32(out, self.vocab_table_offset)
        write_u32(out, self.term_count)
        write_u32(out, self.term_table_offset)

@dataclass(slots=True)
class Term:
    id: int
    video_id: int
    pos: int
    ts_min: int
    ts_max: int
    byte_pos: int

class TermDiffEnc:
    def __init__(self):
        self.prev_term = Term(0, 0, 0, 0, 0, 0)

    def write(self, out, term):
        if term.video_id != self.prev_term.video_id:
            self.prev_term = Term(0, self.prev_term.video_id, 0, 0, 0, 0)
        write_diff(out, term.video_id - self.prev_term.video_id)
        write_diff(out, term.pos - self.prev_term.pos)
        write_diff(out, term.ts_min - self.prev_term.ts_min)
        write_diff(out, term.ts_max - self.prev_term.ts_max)
        write_diff(out, term.byte_pos - self.prev_term.byte_pos)
        self.prev_term = term

@dataclass
class VOD:
    id: int
    name: str
    transcript_offset: int = 0
    _transcript_offset_pos: int | None = None

    def write(self, out):
        self._transcript_offset_pos = out.tell()
        write_u32(out, 0)
        write_str8(out, self.name)

def tokenize(text):
    for w in re.findall(r"['a-z0-9]+", text.lower()):
        yield w

def iter_terms(video_id, vocab, input_file_name):
    with open(input_file_name) as f:
        r = csv.DictReader(f)
        pos = 0
        for record in r:
            start = int(record['start'])
            end = int(record['end'])
            text = record['text']
            for word in tokenize(text):
                yield Term(vocab[word], video_id, pos, start, end, 0)
                pos += 1

def main():
    input_dir = '/home/denis/private/prg/tsodingbase/VOD-Transcripts/transcripts'
    out = open('tsoding.dat', 'wb')
    vods = []
    header = Header()
    header.creation_ts = int(datetime.datetime.now().timestamp())
    header.write(out)

    terms = []

    vocab = collections.defaultdict(itertools.count().__next__)

    # extract terms, vocab, write a vod table
    print('Extracting terms')
    header.video_table_offset = out.tell()
    for i, input_file_name in enumerate(glob.glob(os.path.join(input_dir, '*.csv'))):
        name = os.path.splitext(os.path.basename(input_file_name))[0]
        vods.append(VOD(id=i, name=name))
        vods[-1].write(out)
        terms.extend(iter_terms(i, vocab, input_file_name))
        print(i, name, len(terms), len(vocab))
    header.video_count = len(vods)

    print('Sorting terms')
    # write terms, remember term offsets and count
    terms.sort(key=lambda t: (t.id, t.video_id, t.pos))
    term_id_to_offset = {}
    term_id_to_count = {}
    last_term_id = None
    cnt = 0
    print('Writing terms')
    for term in terms:
        if last_term_id != term.id:
            term_id_to_offset[term.id] = out.tell()
            term_diff_enc = TermDiffEnc()
            cnt = 0
        term_diff_enc.write(out, term)
        last_term_id = term.id
        cnt += 1
        term_id_to_count[term.id] = cnt

    print('Writing term offsets')
    # write term_id to term_offset table
    align32(out)
    header.term_count = len(term_id_to_offset)
    header.term_table_offset = out.tell()
    for i in range(len(term_id_to_offset)):
        write_u32(out, term_id_to_offset[i])
        write_u32(out, term_id_to_count[i])

    print('Writing vocab')
    # write vocab
    header.vocab_count = len(vocab)
    header.vocab_table_offset = out.tell()
    for k, v in vocab.items():
        write_u32(out, v)
        write_str8(out, k + '\0')
    header.file_size = out.tell()
    header.write(out)

    out.close()
    print(header)

def write_u8(f, n):
    f.write(n.to_bytes(1, 'little'))

def write_u32(f, n):
    f.write(n.to_bytes(4, 'little'))

def write_u64(f, n):
    f.write(n.to_bytes(8, 'little'))

def write_str8(f, s):
    s = s.encode('utf-8')
    if len(s) > 255:
        raise ValueError(f'String {s!r} too long for str8')
    write_u8(f, len(s))
    f.write(s)

def write_diff(f, n):
    if n < 0:
        raise ValueError(f"Can't encode negative numbers")
    while True:
        if n < 128:
            write_u8(f, n)
            break
        else:
            write_u8(f, (n & 0b0111_1111) | 0b1000_0000)
            n >>= 7

def align32(f):
    pos = f.tell()
    f.write(b'\0' * ((4 - pos % 4) % 4))

if __name__ == '__main__':
    main()
