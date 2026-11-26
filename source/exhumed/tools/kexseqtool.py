#!/usr/bin/env python3
"""SEQ reader/writer + round-trip test against demo ANUBIS.SEQ."""
import struct, sys

GRP = "/Users/alonamir/games/powerslave_demo/STUFF.DAT"

def grp_extract(grp_path, want):
    with open(grp_path, "rb") as f:
        hdr = f.read(16)
        assert hdr[:12] == b"KenSilverman", hdr
        numfiles = struct.unpack("<I", hdr[12:16])[0]
        entries = []
        for _ in range(numfiles):
            e = f.read(16)
            name = e[:12].rstrip(b"\x00").decode("ascii")
            size = struct.unpack("<I", e[12:16])[0]
            entries.append((name, size))
        out = {}
        for name, size in entries:
            data = f.read(size)
            if name.upper() in want:
                out[name.upper()] = data
    return out

def read_seq(data):
    """Parse a .SEQ file into a plain dict (file-local values, no relocation)."""
    off = 0
    def rd(fmt):
        nonlocal off
        sz = struct.calcsize(fmt)
        v = struct.unpack_from("<" + fmt, data, off)
        off += sz
        return v if len(v) > 1 else v[0]
    tag = data[0:2]
    off = 2
    assert tag in (b"IH", b"DS"), tag  # 'HI' or 'SD' as little-endian short
    centerx = rd("h"); centery = rd("h")
    nseqs = rd("h")
    seq_base = list(struct.unpack_from("<%dh" % nseqs, data, off)); off += 2*nseqs
    seq_size = list(struct.unpack_from("<%dh" % nseqs, data, off)); off += 2*nseqs
    seq_flag = list(struct.unpack_from("<%dh" % nseqs, data, off)); off += 2*nseqs
    nframes = rd("h")
    fr_base = list(struct.unpack_from("<%dh" % nframes, data, off)); off += 2*nframes
    fr_size = list(struct.unpack_from("<%dh" % nframes, data, off)); off += 2*nframes
    fr_flag = list(struct.unpack_from("<%dh" % nframes, data, off)); off += 2*nframes
    nchunks = rd("h")
    ck_x = list(struct.unpack_from("<%dh" % nchunks, data, off)); off += 2*nchunks
    ck_y = list(struct.unpack_from("<%dh" % nchunks, data, off)); off += 2*nchunks
    ck_pict = list(struct.unpack_from("<%dh" % nchunks, data, off)); off += 2*nchunks
    ck_flag = list(struct.unpack_from("<%dh" % nchunks, data, off)); off += 2*nchunks
    snd_names = []
    snd_events = []
    if tag == b"DS":
        n_names = rd("h")
        for _ in range(n_names):
            snd_names.append(data[off:off+8]); off += 8
        n_ev = rd("h")
        for _ in range(n_ev):
            frame = rd("h"); code = rd("h")
            snd_events.append((frame, code))
    trailing = data[off:]
    return dict(tag=tag, centerx=centerx, centery=centery,
                seq_base=seq_base, seq_size=seq_size, seq_flag=seq_flag,
                fr_base=fr_base, fr_size=fr_size, fr_flag=fr_flag,
                ck_x=ck_x, ck_y=ck_y, ck_pict=ck_pict, ck_flag=ck_flag,
                snd_names=snd_names, snd_events=snd_events, trailing=trailing)

def write_seq_bytes(s):
    out = bytearray()
    has_sound = bool(s["snd_names"]) or bool(s["snd_events"]) or s["tag"] == b"DS"
    out += b"DS" if has_sound else b"IH"
    out += struct.pack("<hh", s["centerx"], s["centery"])
    out += struct.pack("<h", len(s["seq_base"]))
    for arr in (s["seq_base"], s["seq_size"], s["seq_flag"]):
        out += struct.pack("<%dh" % len(arr), *arr)
    out += struct.pack("<h", len(s["fr_base"]))
    for arr in (s["fr_base"], s["fr_size"], s["fr_flag"]):
        out += struct.pack("<%dh" % len(arr), *arr)
    out += struct.pack("<h", len(s["ck_x"]))
    for arr in (s["ck_x"], s["ck_y"], s["ck_pict"], s["ck_flag"]):
        out += struct.pack("<%dh" % len(arr), *arr)
    if has_sound:
        out += struct.pack("<h", len(s["snd_names"]))
        for nm in s["snd_names"]:
            assert len(nm) == 8
            out += nm
        out += struct.pack("<h", len(s["snd_events"]))
        for frame, code in s["snd_events"]:
            out += struct.pack("<hh", frame, code)
    return bytes(out)

if __name__ == "__main__":
    files = grp_extract(GRP, {"ANUBIS.SEQ", "MUMMY.SEQ", "WASP.SEQ"})
    for name, data in sorted(files.items()):
        s = read_seq(data)
        rewritten = write_seq_bytes(s)
        ident = rewritten == data
        print("%s: %d bytes, tag=%s, center=(%d,%d) seqs=%d frames=%d chunks=%d "
              "sndnames=%d sndevents=%d trailing=%d roundtrip=%s"
              % (name, len(data), s["tag"], s["centerx"], s["centery"],
                 len(s["seq_base"]), len(s["fr_base"]), len(s["ck_x"]),
                 len(s["snd_names"]), len(s["snd_events"]), len(s["trailing"]), ident))
        if not ident:
            # find first diff
            m = min(len(rewritten), len(data))
            for i in range(m):
                if rewritten[i] != data[i]:
                    print("  first diff at %d: file=%02x re=%02x" % (i, data[i], rewritten[i]))
                    break
            print("  len file=%d re=%d" % (len(data), len(rewritten)))
    # dump anubis details
    s = read_seq(files["ANUBIS.SEQ"])
    print("ANUBIS seq_base:", s["seq_base"][:16])
    print("ANUBIS seq_size:", s["seq_size"][:16])
    print("ANUBIS seq_flag:", s["seq_flag"][:16])
    print("ANUBIS fr_base[:12]:", s["fr_base"][:12])
    print("ANUBIS fr_size[:12]:", s["fr_size"][:12])
    print("ANUBIS fr_flag[:24]:", s["fr_flag"][:24])
    print("ANUBIS ck first 8:", list(zip(s["ck_x"][:8], s["ck_y"][:8], s["ck_pict"][:8], s["ck_flag"][:8])))
    print("ANUBIS snd_names:", s["snd_names"])
    print("ANUBIS snd_events:", s["snd_events"])
