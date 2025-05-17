from __future__ import annotations
import os, sys, math, mmap, time, itertools, tempfile, heapq, contextlib, warnings, hmac
from enum import Enum
from typing import Iterable, Generator, List
import numpy as np, blake3

VERSION = "36.5"
if sys.version_info < (3, 9):
    raise RuntimeError("ΞSort requires Python ≥ 3.9")

# ────────────────────── IEEE helpers ──────────────────────
MASK = np.uint64(0x8000_0000_0000_0000)
SENT = np.uint64(0xFFFF_FFFF_FFFF_FFF8)
K_NEGINF, K_POSINF, K_NEG0, K_POS0, K_NAN = [SENT + i for i in range(5)]

def ieee_key(a: np.ndarray) -> np.ndarray:
    bits = a.view(np.uint64)
    if not bits.dtype.isnative:
        bits = bits.byteswap().newbyteorder()
    key  = np.empty_like(bits)
    neg  = (bits & MASK) != 0
    key[neg]  = ~bits[neg]
    key[~neg] = bits[~neg] | MASK
    key[np.isneginf(a)] = K_NEGINF
    key[np.isposinf(a)] = K_POSINF
    z = a == 0.0
    key[z & np.signbit(a)]  = K_NEG0
    key[z & ~np.signbit(a)] = K_POS0
    key[np.isnan(a)] = K_NAN
    return key

# ────────────────────── minimal xoshiro256** ──────────────
class _R:
    """Small, deterministic PRNG (xoshiro256**) with vector helpers."""
    __slots__ = ("s",)

    @staticmethod
    def _rotl(x: int, k: int) -> int:
        return ((x << k) & 0xFFFFFFFFFFFFFFFF) | (x >> (64 - k))

    def __init__(self, seed: int):
        def sm64(x: int) -> int:
            x = (x + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9 & 0xFFFFFFFFFFFFFFFF
            x = (x ^ (x >> 27)) * 0x94D049BB133111EB & 0xFFFFFFFFFFFFFFFF
            return x ^ (x >> 31)
        self.s = [sm64(seed + i) for i in range(4)]

    def _next(self) -> int:
        s0, s1, s2, s3 = self.s
        res = (self._rotl(s1 * 5, 7) * 9) & 0xFFFFFFFFFFFFFFFF
        t   = (s1 << 17) & 0xFFFFFFFFFFFFFFFF
        s2 ^= s0; s3 ^= s1; s1 ^= s2; s0 ^= s3
        s2 ^= t;  s3 = self._rotl(s3, 45)
        self.s = [s0, s1, s2, s3]
        return res

    def rand(self, n: int) -> np.ndarray:
        raw = np.empty(n, dtype=np.uint64)
        for i in range(0, n, 4):
            raw[i] = self._next()
            if i+1 < n: raw[i+1] = self._next()
            if i+2 < n: raw[i+2] = self._next()
            if i+3 < n: raw[i+3] = self._next()
        raw >>= 11
        out = raw.astype(np.float64)
        out *= (2**-53)
        return out

    def randint(self, high: int, size: int) -> np.ndarray:
        lim  = np.uint64(high)
        out  = np.empty(size, dtype=np.uint64)
        for i in range(size):
            limit = ((1 << 64) // lim) * lim
            while True:
                r = self._next()
                if r < limit:
                    out[i] = r % lim
                    break
        return out

    def shuffle(self, a: np.ndarray):
        for i in range(len(a) - 1, 0, -1):
            lim   = i + 1
            limit = ((1 << 64) // lim) * lim
            while True:
                r = self._next()
                if r < limit:
                    j = r % lim
                    a[i], a[j] = a[j], a[i]
                    break

# ────────────────────── dtypes ────────────────────────────
F64   = np.dtype("float64")
REC_F = np.dtype([("val", F64), ("key", "<u8"), ("tie", F64), ("seq", "<u8")])
REC_I = np.dtype([("val", F64), ("key", "<u8"), ("tie", "<u8"), ("seq", "<u8")])
assert REC_F.itemsize == REC_I.itemsize == 32, "ABI drift detected"

# ────────────────────── helpers ───────────────────────────
WIN0        = 256 * 1024 * 1024
_auto_seed  = lambda: int.from_bytes(os.urandom(8), "little") ^ (os.getpid() << 16) ^ time.time_ns()
_det_name   = lambda wd, i: os.path.join(wd, f"c_{i:012d}")

def _dir_sync(p: str):
    with contextlib.suppress(Exception):
        d = os.path.dirname(p)
        if os.name == "posix" and hasattr(os, "O_DIRECTORY"):
            fd = os.open(d, os.O_RDONLY | os.O_DIRECTORY)
            os.fsync(fd); os.close(fd)

# ────────────────────── public sorter ─────────────────────
class Mode(Enum):      STRICT = "strict"; CURVED = "curved"
class TieBreak(Enum):  VALUE  = "value";  INDEX  = "index"; RANDOM = "random"; SHUFFLE = "shuffle"

class XiSort:
    __slots__ = ("mode","tie","rng","det","nan","max","disk","wd","idx","gseq",
                 "integrity","soft","_maxseq","eps","dtype","buf")

    def __init__(self, *, mode=Mode.STRICT, epsilon=0.01,
                 tie_break=TieBreak.VALUE, seed: int | None = None,
                 require_deterministic=False, nan_shuffle=False,
                 max_gb=1.0, tmpdir: str | None = None,
                 integrity=True, soft_verify=False):

        if epsilon * math.pi >= 1:
            raise ValueError("ε too large (π·ε must be < 1)")
        if require_deterministic and seed is None:
            raise ValueError("deterministic=True requires explicit seed")
        if seed is None:
            seed = _auto_seed()

        self.mode, self.tie, self.rng = mode, tie_break, _R(seed)
        self.det, self.nan           = bool(require_deterministic), bool(nan_shuffle)
        self.max, self.disk          = int(max_gb * 1024**3), 0
        self.wd                      = tempfile.mkdtemp(prefix="xisort_36_5_", dir=tmpdir)
        self.idx = self.gseq         = 0
        self.integrity, self.soft    = bool(integrity), bool(soft_verify)
        self._maxseq                 = (1 << 64) - 1
        self.eps                     = float(epsilon)
        self.dtype                   = REC_I if tie_break is TieBreak.VALUE else REC_F
        self.buf                     = np.empty(2 ** 18, dtype=np.float64)

    def _budget(self, delta: int):
        self.disk = max(self.disk + delta, 0)
        if self.disk > self.max:
            raise MemoryError("ΞSort scratch quota exceeded")

    def _metric(self, a: np.ndarray) -> np.ndarray:
        if self.mode is Mode.STRICT:
            return a
        lo, hi  = a.min(), a.max()
        span    = hi - lo
        norm    = np.zeros_like(a) if span <= np.finfo(a.dtype).tiny else (a - lo) / span
        return norm + self.eps * np.cos(np.pi * norm)

    def stream_sort(self, itr: Iterable[float], *, chunk_size: int = 2**18) -> Generator[float,None,None]:
        if chunk_size > self.buf.size:
            self.buf = np.empty(chunk_size, dtype=np.float64)

        chunks: List[str] = []
        tail_tmp = tail_fin = None
        h_tail   = blake3.blake3()
        it       = iter(itr)

        try:
            while True:
                i = 0
                try:
                    while i < chunk_size:
                        self.buf[i] = next(it)
                        i += 1
                except StopIteration:
                    pass
                if i == 0:
                    break

                arr   = self.buf[:i]
                mask  = np.isnan(arr) | np.isinf(arr)
                finite = arr[~mask]

                if mask.any():
                    tail_tmp = tail_tmp or os.path.join(self.wd, "tail.tmp")
                    tail     = arr[mask].copy()
                    if self.nan:
                        self.rng.shuffle(tail)
                    raw = tail.tobytes()
                    h_tail.update(raw)
                    with open(tail_tmp, "ab", 0) as fh:
                        fh.write(raw)
                    self._budget(len(raw))

                if not finite.size:
                    continue
                if self.gseq + len(finite) > self._maxseq:
                    raise OverflowError("sequence counter exhausted")

                key = ieee_key(self._metric(finite))
                if   self.tie is TieBreak.VALUE:
                    tie = key
                elif self.tie is TieBreak.INDEX:
                    tie = np.arange(len(finite), dtype=np.uint64) + self.gseq
                else: # TieBreak.RANDOM or TieBreak.SHUFFLE (SHUFFLE handled by nan_shuffle for NaNs, random for finites)
                    tie = self.rng.rand(len(finite)) # For finites, tie-breaking is random if not VALUE or INDEX

                seq = np.arange(len(finite), dtype=np.uint64) + self.gseq
                self.gseq += len(finite)

                rec = np.empty(len(finite), dtype=self.dtype)
                rec["val"], rec["key"], rec["tie"], rec["seq"] = finite, key, tie, seq
                rec.sort(order=("key", "tie", "seq"))

                fname = _det_name(self.wd, self.idx)
                with open(fname, "wb", 0) as fh:
                    buf_data = rec.tobytes()
                    fh.write(buf_data)
                    fh.write(blake3.blake3(buf_data).digest(length=16))
                _dir_sync(fname)
                self._budget(os.path.getsize(fname))
                chunks.append(fname)
                self.idx += 1

            if tail_tmp:
                tag = h_tail.digest(length=16)
                with open(tail_tmp, "ab", 0) as fh:
                    fh.write(tag)
                tail_fin = os.path.join(self.wd, "tail.fin")
                os.rename(tail_tmp, tail_fin)
                _dir_sync(tail_fin)
                self._budget(16) # For the tag

            merged_iter = heapq.merge(
                *(_reader(p, self.integrity, self.soft, dtype=self.dtype) for p in chunks),
                key=lambda r: (r["key"], r["tie"], r["seq"])
            )
            for r in merged_iter:
                yield r["val"]

            if tail_fin:
                yield from _tail_emit(tail_fin, self.rng)

        finally:
            paths_to_remove = []
            paths_to_remove.extend(chunks)
            if tail_fin and os.path.exists(tail_fin):
                paths_to_remove.append(tail_fin)
            if tail_tmp and os.path.exists(tail_tmp):
                 if not tail_fin or (tail_fin and tail_tmp != tail_fin):
                    paths_to_remove.append(tail_tmp)
            
            for p in set(paths_to_remove):
                try:
                    if os.path.exists(p):
                        size = os.path.getsize(p)
                        os.remove(p)
                        self._budget(-size)
                except OSError as e:
                    warnings.warn(f"Could not remove temporary file {os.path.basename(p)} during cleanup: {e}")
            
            with contextlib.suppress(OSError):
                if self.wd and os.path.isdir(self.wd):
                    if not os.listdir(self.wd):
                        os.rmdir(self.wd)
                    else:
                        warnings.warn(f"Temporary directory {self.wd} was not empty during cleanup.")


# ────────────────────── tail emit ─────────────────────────
def _tail_emit(path: str, rng: _R):
    if not os.path.exists(path) or os.path.getsize(path) <= 16:
        return

    payload_size = os.path.getsize(path) - 16
    if payload_size <= 0:
        return

    if payload_size <= 512 * 1024 * 1024: # ≤ 512 MiB
        mm = None
        try:
            with open(path, "rb") as fh:
                mm = mmap.mmap(fh.fileno(), payload_size, access=mmap.ACCESS_READ, offset=0)
                
                if mm.size() > 0:
                    current_payload_size = mm.size()
                    arr_to_yield = None

                    if current_payload_size % F64.itemsize != 0:
                        warnings.warn(f"Tail payload size in {os.path.basename(path)} ({current_payload_size} bytes) is not a multiple of itemsize {F64.itemsize}. Truncating.")
                        num_elements = current_payload_size // F64.itemsize
                        if num_elements > 0:
                            arr_to_yield = np.frombuffer(mm, dtype=np.float64, count=num_elements).copy()
                    else:
                        num_elements = current_payload_size // F64.itemsize
                        if num_elements > 0:
                             arr_to_yield = np.frombuffer(mm, dtype=np.float64, count=num_elements).copy()
                    
                    if arr_to_yield is not None and arr_to_yield.size > 0:
                        rng.shuffle(arr_to_yield)
                        yield from arr_to_yield
        except FileNotFoundError:
            warnings.warn(f"Tail file {os.path.basename(path)} not found during emit.")
            return
        except Exception as e:
            warnings.warn(f"Error processing tail file {os.path.basename(path)}: {e}")
            return
        finally:
            if mm:
                mm.close()

    else: # vectorised Vitter-R reservoir for > 512 MiB
        cap_bytes = 64 * 1024 * 1024
        cap_elements = cap_bytes // F64.itemsize
        
        buf = np.empty(cap_elements, dtype=np.float64)
        fill = 0 
        items_processed_by_reservoir_logic = 0

        fd = -1
        data_map = None
        try:
            fd = os.open(path, os.O_RDONLY)
            data_map = mmap.mmap(fd, payload_size, access=mmap.ACCESS_READ, offset=0)

            if data_map.size() == 0:
                return

            view_elements = payload_size // F64.itemsize
            if payload_size % F64.itemsize != 0:
                warnings.warn(f"Tail payload size in {os.path.basename(path)} ({payload_size} bytes) is not a multiple of itemsize {F64.itemsize}. Truncating.")
                if view_elements == 0: return
            
            view = np.frombuffer(data_map, dtype=np.float64, count=view_elements)

            chunk_size_elements = min(view.size, 1_048_576) 
            if chunk_size_elements == 0: return

            num_chunks = math.ceil(view.size / chunk_size_elements)

            for i in range(num_chunks):
                current_mmap_chunk = view[i*chunk_size_elements : (i+1)*chunk_size_elements]
                elements_to_process_from_chunk = current_mmap_chunk
                
                if fill < cap_elements:
                    take = min(cap_elements - fill, elements_to_process_from_chunk.size)
                    buf[fill : fill + take] = elements_to_process_from_chunk[:take]
                    fill += take
                    elements_to_process_from_chunk = elements_to_process_from_chunk[take:]

                for item_idx_in_remaining_chunk in range(elements_to_process_from_chunk.size):
                    idx_for_randint = cap_elements + items_processed_by_reservoir_logic
                    m_arr = rng.randint(idx_for_randint + 1, size=1)
                    m = m_arr[0]
                    if m < cap_elements:
                        buf[m] = elements_to_process_from_chunk[item_idx_in_remaining_chunk]
                    items_processed_by_reservoir_logic += 1
        
        except FileNotFoundError:
            warnings.warn(f"Tail file {os.path.basename(path)} not found during emit (large file path).")
            return
        except Exception as e:
            warnings.warn(f"Error processing tail file {os.path.basename(path)} (large file path): {e}")
            return
        finally:
            if data_map:
                data_map.close()
            if fd != -1:
                os.close(fd)
        
        if fill > 0:
            rng.shuffle(buf[:fill])
            yield from buf[:fill]


# ────────────────────── reader ────────────────────────────
def _reader(path: str, integrity: bool, soft: bool, *, dtype) -> Generator:
    if not os.path.exists(path):
        return

    size = os.path.getsize(path)
    
    def _handle_integrity_error_closure(filename_for_error):
        def _handle():
            msg = f"Integrity check failed (tag mismatch) in {filename_for_error}"
            if soft:
                warnings.warn(msg)
            else:
                raise IOError(msg)
        return _handle
    _handle_integrity_error = _handle_integrity_error_closure(os.path.basename(path))

    if size <= 16: 
        if integrity:
            expected_payload_size = 0 # For this branch, payload is effectively 0 or file is too small
            tag_size_to_read = 16
            if size < 16 : 
                 msg = f"File {os.path.basename(path)} is smaller ({size} bytes) than an integrity tag (16 bytes)."
                 if soft: warnings.warn(msg)
                 else: raise IOError(msg)
                 return 
            
            with open(path, "rb") as fh:
                # If size == 16, it's just a tag for an empty payload. Read all 16 bytes.
                # If size > 16 (but this branch is for size <= 16), this case isn't hit.
                # So, fh.read(16) is correct if size == 16.
                tag_from_file = fh.read(tag_size_to_read) 
            
            computed_tag = blake3.blake3(b'').digest(length=16)
            if not hmac.compare_digest(computed_tag, tag_from_file):
                _handle_integrity_error()
        return 

    payload_size = size - 16
    rec_itemsize = dtype.itemsize
    
    if payload_size % rec_itemsize != 0:
        msg = f"Payload size in {os.path.basename(path)} ({payload_size} bytes) is not a multiple of record size ({rec_itemsize} bytes)."
        if soft:
            warnings.warn(msg + " Truncating to fit.")
            payload_size = (payload_size // rec_itemsize) * rec_itemsize
            # If payload_size becomes 0 after truncation, it's handled by the next block
        else:
            raise IOError(msg)
    
    if payload_size == 0: 
        if integrity:
            # This assumes the tag for an "effectively empty" payload (due to truncation or actual)
            # is calculated over b'' and is located right after the original payload data.
            # The file structure is [payload_data][tag]. Tag is always at offset `original_file_size - 16`.
            # If effective payload_size is 0, we still compare against hash of b''.
            with open(path, "rb") as fh_tag_check:
                fh_tag_check.seek(size - 16) # Go to where the tag is physically stored
                tag_from_file = fh_tag_check.read(16)
            computed_tag = blake3.blake3(b'').digest(length=16) # Expect hash of empty if effective payload is 0
            if not hmac.compare_digest(computed_tag, tag_from_file):
                _handle_integrity_error()
        return

    # At this point, payload_size > 0 and is a multiple of rec_itemsize.
    win_size = math.lcm(WIN0, rec_itemsize)

    with open(path, "rb") as fh:
        if payload_size < win_size or sys.maxsize < (1 << 33): # Small file or 32-bit Python
            computed_tag_for_payload = None
            if integrity:
                with mmap.mmap(fh.fileno(), payload_size, access=mmap.ACCESS_READ, offset=0) as temp_mm_for_hash:
                    computed_tag_for_payload = blake3.blake3(temp_mm_for_hash).digest(length=16)
            
            with mmap.mmap(fh.fileno(), payload_size, access=mmap.ACCESS_READ, offset=0) as mm:
                for record in np.frombuffer(mm, dtype=dtype).copy(): # FIX APPLIED
                    yield record
            
            if integrity:
                fh.seek(payload_size, os.SEEK_SET) 
                tag_from_file = fh.read(16)
                if computed_tag_for_payload is None: 
                     warnings.warn(f"Internal issue: computed_tag_for_payload is None for {os.path.basename(path)} with payload_size {payload_size}")
                     computed_tag_for_payload = blake3.blake3(b'').digest(length=16) 

                if not hmac.compare_digest(computed_tag_for_payload, tag_from_file):
                    _handle_integrity_error()
            return

        else: # Large file on 64-bit Python: mmap in windows
            h = blake3.blake3() if integrity else None
            off = 0
            while off < payload_size:
                current_read_size = min(win_size, payload_size - off)
                if current_read_size == 0: break

                with mmap.mmap(fh.fileno(), current_read_size, access=mmap.ACCESS_READ, offset=off) as mm_window:
                    if integrity and h is not None:
                        h.update(mm_window)
                    for record in np.frombuffer(mm_window, dtype=dtype).copy(): # FIX APPLIED
                        yield record
                off += current_read_size
            
            if integrity and h is not None:
                fh.seek(payload_size, os.SEEK_SET) 
                tag_from_file = fh.read(16)
                if not hmac.compare_digest(h.digest(length=16), tag_from_file):
                    _handle_integrity_error()
