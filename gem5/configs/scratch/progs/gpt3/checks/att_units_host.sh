#!/bin/bash
# Host check for step 3: CPU heads run as units (cpu_att_unit) must give the same att/ctx as
# attention_scores() + attention_context(), run (32-N)*28 units, and never touch the PIM heads' rows.
# The functions are cut out of the current gpt3.c; sgemv_t/_n are reference kernels with OpenBLAS
# semantics (y += alpha*A^T x, y += alpha*A x, column-major). ctx error is measured against the size
# of the summed terms (sum of w*|V|), since a context value can be near 0 by cancellation.
# Usage (from progs/gpt3): bash checks/att_units_host.sh      seconds; exit 0 = all PASS
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
python3 -I - "$T" <<'PY'
import sys
T = sys.argv[1]
src = open('gpt3.c').read()
def func(sig):
    i = src.index(sig); j = src.index('{', i); d = 0
    for k in range(j, len(src)):
        if src[k] == '{': d += 1
        elif src[k] == '}':
            d -= 1
            if d == 0: return src[i:k + 1]
    raise SystemExit('unbalanced ' + sig)
i = src.index('#ifdef PIM_ATT\n/* As below, branch-free'); e = src.index('#else\n/* e^v via 2^n', i)
assert '#define ATT_NB 9' in src and '#define ATT_UNITS (3 * ATT_NB + 1)' in src
parts = [src[i + len('#ifdef PIM_ATT\n'):e],
         '#define ATT_NB 9\n#define ATT_UNITS (3 * ATT_NB + 1)\nstatic int cw_h, cw_u;\nstatic float cw_m, cw_sum;\n',
         func('static void cw_reset(void)'), func('static int cpu_att_unit(void)'),
         func('static void attention_scores(int h0, int h1)'), func('static void attention_context(void)')]
head = r'''#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define PIM_ATT 1
#define HIDDEN 2560
#define HEADS 32
#define HEAD_DIM 80
#define CTX 256
#define SEQ 1
#define k_sqrtf(v) sqrtf(v)
int pim_att_heads;
#define PIM_ATT_NH pim_att_heads
static float Kc[CTX + 1][HIDDEN], Vc[CTX + 1][HIDDEN], Q[SEQ][HIDDEN], att[HEADS][CTX + 1], ctx[SEQ][HIDDEN], blas_buf[4096];
static float att_ref[HEADS][CTX + 1], ctx_ref[SEQ][HIDDEN];
static int sgemv_t(long m, long n, long d, float alpha, float *a, long lda, float *x, long incx, float *y, long incy, float *buf) {
  (void)d; (void)buf; for (long j = 0; j < n; j++) { float s = 0; for (long i = 0; i < m; i++) s += a[j*lda+i]*x[i*incx]; y[j*incy] += alpha*s; } return 0; }
static int sgemv_n(long m, long n, long d, float alpha, float *a, long lda, float *x, long incx, float *y, long incy, float *buf) {
  (void)d; (void)buf; for (long j = 0; j < n; j++) { float t = alpha*x[j*incx]; for (long i = 0; i < m; i++) y[i*incy] += t*a[j*lda+i]; } return 0; }
'''
main = r'''
static uint32_t rs = 12345; static float rnd(void) { rs = rs*1664525u + 1013904223u; return ((rs>>8)/16777216.0f - 0.5f) * 4.0f; }
static void sentinel(void) { for (int h=0;h<HEADS;h++) for (int j=0;j<=CTX;j++) att[h][j]=7777.0f; for (int d=0;d<HIDDEN;d++) ctx[0][d]=7777.0f; }
int main(void) {
  for (int t=0;t<=CTX;t++) for (int d=0;d<HIDDEN;d++) { Kc[t][d]=rnd(); Vc[t][d]=rnd(); }
  for (int d=0;d<HIDDEN;d++) Q[0][d]=rnd();
  int fails = 0, ns[] = {0, 1, 4, 27, 28, 31, 32};
  for (unsigned k=0;k<sizeof ns/sizeof ns[0];k++) {
    int n = ns[k]; pim_att_heads = n;
    sentinel(); attention_scores(n, HEADS); attention_context();
    memcpy(att_ref, att, sizeof att); memcpy(ctx_ref, ctx, sizeof ctx);
    for (int rep=0; rep<2; rep++) {                         /* twice: cw_reset must restart cleanly */
      sentinel(); cw_reset(); long units = 0; while (cpu_att_unit()) units++;
      int after = cpu_att_unit();                            /* stays done */
      double ma = 0, mc = 0; int untouched = 1;
      for (int h=0; h<HEADS; h++) for (int j=0;j<=CTX;j++) {
        if (h < n) { if (att[h][j] != 7777.0f) untouched = 0; continue; }
        double r = fabs(att[h][j]-att_ref[h][j]) / (fabs(att_ref[h][j]) + 1e-30); if (r > ma) ma = r; }
      for (int d=0; d<HIDDEN; d++) {
        if (d < n*HEAD_DIM) { if (ctx[0][d] != 7777.0f) untouched = 0; continue; }
        int h = d / HEAD_DIM; double scale = 0;
        for (int t=0; t<=CTX; t++) scale += fabs(att_ref[h][t] * Vc[t][d]);
        double r = fabs(ctx[0][d]-ctx_ref[0][d]) / scale; if (r > mc) mc = r; }
      int ok = units == (long)(HEADS-n)*ATT_UNITS && after == 0 && untouched && ma < 1e-5 && mc < 1e-5;
      fails += !ok;
      printf("  N=%2d run %d: units %4ld (expect %4d)  PIM heads untouched %s  max rel diff att %.2e ctx %.2e  %s\n",
             n, rep+1, units, (HEADS-n)*ATT_UNITS, untouched ? "yes" : "NO", ma, mc, ok ? "PASS" : "FAIL");
    }
  }
  return fails;
}
'''
open(T + '/unitchk.c', 'w').write(head + '\n'.join(parts) + main)
PY
cd "$T"
echo "== plain -O2";                gcc -O2 -Wall -Wno-unused-function -o u1 unitchk.c -lm && ./u1
echo "== build's float flags + FMA"; gcc -O3 -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math -mfma -o u2 unitchk.c -lm && ./u2
rm -rf "$T"
