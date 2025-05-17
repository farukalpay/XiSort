import os, sys, argparse, time
import numpy as np
from core import VERSION, Mode, TieBreak, XiSort, _R

# ────────────────────── CLI self-test ─────────────────────
def _selftest():
    z = np.array([0.0] * 4096 + [-0.0] * 4096)
    srt = XiSort(tie_break=TieBreak.VALUE, seed=1, require_deterministic=True)
    out = np.fromiter(srt.stream_sort(z), dtype=np.float64)
    ok1 = np.all(out[:4096] == -0.0) and np.all(out[4096:] == 0.0)
    print("signed-zero", "OK" if ok1 else "FAIL")

    rng  = _R(1)
    uni  = rng.rand(1_000_000)
    bins = np.histogram(uni, 10)[0]
    expected_count = uni.size / 10.0
    chi2 = ((bins - expected_count)**2 / expected_count).sum()
    print("χ² =", round(chi2, 1), "OK" if chi2 < 20 else "FAIL")

# ────────────────────── main / args ───────────────────────
if __name__ == "__main__":
    # ── CLI ────────────────────────────────────────────────────────────────
    ap = argparse.ArgumentParser(description="ΞSort float external sorter")
    ap.add_argument("--selftest", action="store_true",
                    help="run built-in sanity checks and exit")

    # core algo settings
    ap.add_argument("--mode",      type=Mode, choices=list(Mode),
                    default=Mode.STRICT, help="Sorting mode")
    ap.add_argument("--epsilon",   type=float, default=0.01,
                    help="Epsilon for CURVED mode")
    ap.add_argument("--tie-break", type=TieBreak, choices=list(TieBreak),
                    default=TieBreak.VALUE, help="Tie-breaking strategy")
    ap.add_argument("--seed",      type=int, default=None,
                    help="Seed for PRNG (None for auto)")
    ap.add_argument("--require-deterministic", action="store_true",
                    help="Fail if --seed is not given")
    ap.add_argument("--nan-shuffle", action="store_true",
                    help="Shuffle NaNs and Infs inside the tail segment")
    ap.add_argument("--max-gb",    type=float, default=1.0,
                    help="Max GB allowed for scratch files")
    ap.add_argument("--tmpdir",    type=str, default=None,
                    help="Directory to place scratch files")
    ap.add_argument("--no-integrity", action="store_false", dest="integrity",
                    help="Disable chunk Blake3 tags")
    ap.add_argument("--soft-verify", action="store_true",
                    help="Warn instead of error on tag mismatch")

    # quality-of-life
    ap.add_argument("--count",    type=int, default=1_000_000,
                    help="How many random doubles to sort (default 1 000 000)")
    ap.add_argument("--verify-sorted", action="store_true",
                    help="Abort on first out-of-order value (single pass)")
    ap.add_argument("--progress", action="store_true",
                    help="Print progress every 5 % (≥5 s interval)")

    args = ap.parse_args()

    # ── self-test ──────────────────────────────────────────────────────────
    if args.selftest:
        _selftest()
        sys.exit(0)

    # ── construct sorter ───────────────────────────────────────────────────
    sorter_options = dict(
        mode                  = args.mode,
        epsilon               = args.epsilon,
        tie_break             = args.tie_break,
        seed                  = args.seed,
        require_deterministic = args.require_deterministic,
        nan_shuffle           = args.nan_shuffle,
        max_gb                = args.max_gb,
        tmpdir                = args.tmpdir,
        integrity             = args.integrity,
        soft_verify           = args.soft_verify,
    )
    sorter = XiSort(**sorter_options)

    # ── create synthetic input stream ──────────────────────────────────────
    rng_np = np.random.default_rng(0)
    src    = (x for x in rng_np.standard_normal(args.count))

    print(f"ΞSort {VERSION} running with options: {sorter_options}")
    print(f"Sorting {args.count:,} standard-normal values…")

    # ── main streaming loop with optional verification & progress ──────────
    output_count   = 0
    next_milestone = max(args.count // 20, 1)       # 5 %
    last_ping      = time.monotonic()
    first_ten      = []

    last_val = -float("inf")                        # for --verify-sorted

    for val in sorter.stream_sort(src):
        if output_count < 10:
            first_ten.append(val)

        if args.verify_sorted and val < last_val:
            raise RuntimeError(f"Out-of-order value at index {output_count}: "
                               f"{val} < {last_val}")
        last_val = val
        output_count += 1

        if args.progress and output_count >= next_milestone:
            now = time.monotonic()
            if now - last_ping >= 5:
                pct = 100.0 * output_count / args.count
                print(f"{pct:5.1f}%  ({output_count:,}/{args.count:,})")
                last_ping = now
                next_milestone += args.count // 20

    # ── summary ────────────────────────────────────────────────────────────
    print(f"Sorting complete. Total numbers sorted: {output_count:,}")

    if first_ten:
        formatted = [f"{x:.3f}..." if np.isfinite(x) else str(x) for x in first_ten]
        print("First 10 sorted values:", formatted)

    if args.verify_sorted:
        print("Order verified – sequence is strictly non-decreasing.")
