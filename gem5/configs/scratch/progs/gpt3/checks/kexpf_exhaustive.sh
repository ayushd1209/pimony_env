#!/bin/bash
# Host check for step 2: the branch-free k_expf (-DPIM_ATT) must return the same bits as the original
# for every non-NaN float, in 3 FP modes (plain IEEE; the build's float flags; + FMA contraction).
# Both functions are cut out of the current gpt3.c, so this always tests the real source.
# Usage (from progs/gpt3): bash checks/kexpf_exhaustive.sh      ~3 min on 14 cores; exit 0 = all identical
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
python3 -I - "$T" <<'PY'
import sys
T = sys.argv[1]
src = open('gpt3.c').read()
i = src.index('#ifdef PIM_ATT\n/* As below, branch-free')
e = src.index('#else\n/* e^v via 2^n', i)
f = src.index('#endif', e)
new = src[i + len('#ifdef PIM_ATT\n'):e].replace('static float k_expf(', 'static float k_expf_new(')
old = src[e + len('#else\n'):f].replace('static float k_expf(', 'static float k_expf_old(')
main = r'''
static inline uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
int main(void) {
  unsigned long long nall = 0, dall = 0, nsm = 0, dsm = 0; double maxrel = 0; float worst = 0;
  #pragma omp parallel for reduction(+:nall,dall,nsm,dsm) schedule(static)
  for (long long k = 0; k < (1LL << 32); k++) {
    uint32_t x = (uint32_t)k; float v; memcpy(&v, &x, 4);
    if (v != v) continue;                                   /* NaN */
    float a = k_expf_old(v), b = k_expf_new(v);
    int d = bits(a) != bits(b);
    nall++; dall += d; if (v <= 0.0f) { nsm++; dsm += d; }
    if (d) {
      #pragma omp critical
      { if (dall < 5) printf("  MISMATCH v=%a old=%a new=%a\n", v, a, b); }
    }
  }
  for (float v = -87.0f; v <= 0.0f; v = nextafterf(v, 1.0f)) {   /* accuracy of the shared result */
    double t = exp((double)v), r = fabs((double)k_expf_new(v) - t) / t;
    if (r > maxrel) { maxrel = r; worst = v; }
  }
  printf("  softmax range (v<=0): %llu inputs, %llu not bit-identical\n", nsm, dsm);
  printf("  all non-NaN floats:   %llu inputs, %llu not bit-identical\n", nall, dall);
  printf("  max rel. error vs exp() on [-87,0]: %.3g (%.2f ulp of 2^-24) at v=%a\n", maxrel, maxrel / 5.96e-8, worst);
  return dall != 0;
}
'''
hdr = '#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n#include <math.h>\n#define K_INF __builtin_inff()\n'
open(T + '/expchk.c', 'w').write(hdr + old + new + main)
PY
cd "$T"
F="-O3 -fopenmp -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math"
echo "== mode 1: -O2 -ffp-contract=off (plain IEEE)";   gcc -O2 -fopenmp -ffp-contract=off -o m1 expchk.c -lm && ./m1
echo "== mode 2: build's float flags, no FMA";          gcc $F -ffp-contract=off -o m2 expchk.c -lm && ./m2
echo "== mode 3: build's float flags + FMA contraction"; gcc $F -mfma -ffp-contract=fast -o m3 expchk.c -lm && ./m3
rm -rf "$T"
