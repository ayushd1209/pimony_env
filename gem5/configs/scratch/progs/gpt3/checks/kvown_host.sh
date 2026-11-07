#!/bin/bash
# Host check for gpt3_kvown.c: kv_init() must leave each head's history with the same bits gpt3.c's init gave it
# (fill Kc, fill Vc, pim_att_init), and nowhere else. Functions are cut out of both sources; pimK/pimV/Kc/Vc are host
# arrays starting at 0, as unwritten DRAM does. Per N and kv_dup: CPU-head columns of Kc/Vc, PIM-head columns of Kc/Vc
# (0 when kv_dup=0, gpt3.c's FP32 copy when 1), all of pimK/pimV, the fence.cl line sequence, the PRNG state after.
# Usage (from progs/gpt3): bash checks/kvown_host.sh      seconds; exit 0 = all PASS
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
python3 -I - "$T" <<'PY'
import sys, re
T = sys.argv[1]
old, new = open('gpt3.c').read(), open('gpt3_kvown.c').read()
def func(src, sig):
    i = src.index(sig); j = src.index('{', i); d = 0
    for k in range(j, len(src)):
        if src[k] == '{': d += 1
        elif src[k] == '}':
            d -= 1
            if d == 0: return src[i:k + 1]
    raise SystemExit('unbalanced ' + sig)
def define(src, name):
    m = re.search(r'^#define ' + re.escape(name) + r'\b.*$', src, re.M)
    if not m: raise SystemExit('no #define ' + name)
    return m.group(0)
shared = ['static uint32_t rng_u32(void)', 'static float rng_uniform(void)',
          'static void fill(float *p, uint64_t n, float scale)', 'static uint64_t pim_idx(int j, int i)']
macros = ['SEED', 'PIM_UNITS', 'PIM_STEPS_ROW', 'PIM_S_CH', 'PIM_S_BG', 'PIM_S_STEP', 'PIM_S_BANK', 'PIM_S_RANK',
          'PIM_S_ROW', 'PIM_WAVES', 'PIM_K_ELEMS', 'PIM_V_ELEMS']
for s in shared: assert func(old, s) == func(new, s), 'not the same in both files: ' + s
for m in macros: assert define(old, m) == define(new, m), 'not the same in both files: ' + m
for pat in [r'#define HIDDEN\s+2560\b', r'#define HEADS\s+32\b', r'#define FFN\s+10240\b', r'#define CTX\s+256\b',
            r'#define HEAD_DIM \(HIDDEN / HEADS\)', r'#define WT_PER_STEP 16\b',
            r'const float sh = 1\.0f / k_sqrtf\(\(float\)HIDDEN\);']:
    assert re.search(pat, old) and re.search(pat, new), 'expected in both files: ' + pat
head = r'''#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define HIDDEN 2560
#define HEADS 32
#define FFN 10240
#define SEQ 1
#define CTX 256
#define HEAD_DIM (HIDDEN / HEADS)
#define WT_PER_STEP 16
typedef _Float16 wt_t;
static uint32_t rng_state;
int pim_att_heads, kv_dup;
static float Kc_o[CTX + 1][HIDDEN], Vc_o[CTX + 1][HIDDEN], Kc_n[CTX + 1][HIDDEN], Vc_n[CTX + 1][HIDDEN];
'''
defs = '\n'.join(define(old, m) for m in macros) + '\n'
arrays = r'''
static wt_t pimK_o[HEADS][PIM_K_ELEMS], pimV_o[HEADS][PIM_V_ELEMS], pimK_n[HEADS][PIM_K_ELEMS], pimV_n[HEADS][PIM_V_ELEMS];
#define NREC (HEADS * (PIM_K_ELEMS + PIM_V_ELEMS) / 32)
static uint64_t rec_o[NREC], rec_n[NREC], *rec; static long nrec; static const char *bK, *bV;
static void pim_fence_cl(const void *p) {          /* records the line: K offset, or V offset with bit 63 */
  const char *q = (const char *)p;
  rec[nrec++] = (q >= bK && q < bK + sizeof pimK_o) ? (uint64_t)(q - bK) : (1ULL << 63) | (uint64_t)(q - bV); }
'''
body = '\n'.join(func(old, s) for s in shared) + '\n'
body += '#define Kc Kc_o\n#define Vc Vc_o\n#define pimK pimK_o\n#define pimV pimV_o\n'
body += func(old, 'static void pim_att_init(void)') + '\n'
body += '#undef Kc\n#undef Vc\n#undef pimK\n#undef pimV\n#define Kc Kc_n\n#define Vc Vc_n\n#define pimK pimK_n\n#define pimV pimV_n\n'
body += func(new, 'static __attribute__((noinline, section(".kvinit"))) void kv_init(float sh)') + '\n'
body += '#undef Kc\n#undef Vc\n#undef pimK\n#undef pimV\n'
main = r'''
static int zero32(const float *p, int n) { const uint32_t *u = (const uint32_t *)p; for (int i = 0; i < n; i++) if (u[i]) return 0; return 1; }
int main(void) {
  const float sh = 1.0f / sqrtf((float)HIDDEN);
  uint32_t shb; memcpy(&shb, &sh, 4);
  if (shb != 0x3ca1e89bu) { printf("sh bits %08x, the RISC-V binaries use 3ca1e89b\n", shb); return 1; }
  /* PRNG state at the KV fill in init_params: after Wq bq .. W2 b2, g1 beta1 g2 beta2, x */
  uint64_t prior = 4ULL * HIDDEN * HIDDEN + 4ULL * HIDDEN + 2ULL * FFN * HIDDEN + FFN + HIDDEN + 4ULL * HIDDEN + (uint64_t)SEQ * HIDDEN;
  rng_state = SEED; for (uint64_t i = 0; i < prior; i++) rng_u32();
  const uint32_t s0 = rng_state;
  int fails = 0, ns[] = {0, 1, 4, 27, 28, 31, 32};
  for (unsigned k = 0; k < sizeof ns / sizeof ns[0]; k++) for (int dup = 0; dup < 2; dup++) {
    int n = ns[k], np = n * HEAD_DIM; pim_att_heads = n; kv_dup = dup;
    memset(Kc_o, 0, sizeof Kc_o); memset(Vc_o, 0, sizeof Vc_o); memset(Kc_n, 0, sizeof Kc_n); memset(Vc_n, 0, sizeof Vc_n);
    memset(pimK_o, 0, sizeof pimK_o); memset(pimV_o, 0, sizeof pimV_o); memset(pimK_n, 0, sizeof pimK_n); memset(pimV_n, 0, sizeof pimV_n);
    rng_state = s0;                                           /* gpt3.c */
    fill(&Kc_o[0][0], (uint64_t)CTX * HIDDEN, sh);
    fill(&Vc_o[0][0], (uint64_t)CTX * HIDDEN, sh);
    rec = rec_o; nrec = 0; bK = (const char *)pimK_o; bV = (const char *)pimV_o;
    pim_att_init(); long nfo = nrec; uint32_t so = rng_state;
    rng_state = s0;                                           /* gpt3_kvown.c */
    rec = rec_n; nrec = 0; bK = (const char *)pimK_n; bV = (const char *)pimV_n;
    kv_init(sh); long nfn = nrec; uint32_t sn = rng_state;
    int cpu = 1, pimcol = 1, last = 1;
    for (int t = 0; t < CTX; t++) {
      cpu &= !memcmp(&Kc_o[t][np], &Kc_n[t][np], 4 * (HIDDEN - np)) && !memcmp(&Vc_o[t][np], &Vc_n[t][np], 4 * (HIDDEN - np));
      if (dup) pimcol &= !memcmp(&Kc_o[t][0], &Kc_n[t][0], 4 * np) && !memcmp(&Vc_o[t][0], &Vc_n[t][0], 4 * np);
      else     pimcol &= zero32(&Kc_n[t][0], np) && zero32(&Vc_n[t][0], np);
    }
    last = zero32(Kc_n[CTX], HIDDEN) && zero32(Vc_n[CTX], HIDDEN);   /* slot CTX is the decode step's */
    int pim = !memcmp(pimK_o, pimK_n, sizeof pimK_o) && !memcmp(pimV_o, pimV_n, sizeof pimV_o);
    int fence = nfo == nfn && nfo == (long)n * (PIM_K_ELEMS + PIM_V_ELEMS) / 32 && !memcmp(rec_o, rec_n, 8 * nfo);
    int ok = cpu && pimcol && last && pim && fence && so == sn;
    fails += !ok;
    printf("  N=%2d kv_dup=%d: CPU heads Kc/Vc %s | PIM heads Kc/Vc %s | slot CTX empty %s | pimK/pimV %s | fence.cl %6ld lines %s | PRNG after %s  %s\n",
           n, dup, cpu ? "same" : "DIFF", pimcol ? (dup ? "same as gpt3.c" : "never written") : "WRONG",
           last ? "yes" : "NO", pim ? "same" : "DIFF", nfn, fence ? "same order" : "DIFF", so == sn ? "same" : "DIFF", ok ? "PASS" : "FAIL");
  }
  printf("%s\n", fails ? "FAIL" : "all PASS");
  return fails;
}
'''
open(T + '/kvown_host.c', 'w').write(head + defs + arrays + body + main)
PY
gcc -O2 -ffp-contract=off -o "$T/kvown_host" "$T/kvown_host.c" -lm
"$T/kvown_host"
