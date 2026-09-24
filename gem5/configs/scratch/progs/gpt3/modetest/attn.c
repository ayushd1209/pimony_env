/* Isolates ONE thing: the KV-cache walk in attention, nothing else.
 * Same source builds bare-metal FS and hosted SE, so any difference in
 * L2 miss latency is the mode's physical placement, not the code. */
#include <stdint.h>
#define HIDDEN   2560
#define HEADS    32
#define HEAD_DIM (HIDDEN / HEADS)
#define CTX      256
#define REPS     8

int sgemv_n(long,long,long,float,float*,long,float*,long,float*,long,float*);
int sgemv_t(long,long,long,float,float*,long,float*,long,float*,long,float*);

static float Kc[CTX+1][HIDDEN], Vc[CTX+1][HIDDEN];
static float Q[HIDDEN], att[HEADS][CTX+1], ctx[HIDDEN], buf[4096];
volatile float sink;

static inline void m5_reset_stats(void){ register uint64_t a0 asm("a0")=0, a1 asm("a1")=0;
    __asm__ volatile(".word 0x4000007B" ::"r"(a0),"r"(a1):"memory"); }
static inline void m5_dump_stats(void){ register uint64_t a0 asm("a0")=0, a1 asm("a1")=0;
    __asm__ volatile(".word 0x4100007B" ::"r"(a0),"r"(a1):"memory"); }

__attribute__((used, optimize("no-tree-loop-distribute-patterns")))
void *memset(void *d, int c, unsigned long n){ unsigned char*p=d; while(n--)*p++=(unsigned char)c; return d; }

int main(void){
    for (int j=0;j<=CTX;j++) for (int d=0;d<HIDDEN;d++){ Kc[j][d]=(float)((j*7+d)%13); Vc[j][d]=(float)((j*3+d)%11); }
    for (int d=0;d<HIDDEN;d++) Q[d]=1.0f;

    m5_reset_stats();
    for (int r=0;r<REPS;r++)
      for (int h=0;h<HEADS;h++){
        int off=h*HEAD_DIM;
        for (int j=0;j<=CTX;j++) att[h][j]=0.0f;
        sgemv_t(HEAD_DIM, CTX+1, 0, 0.125f, &Kc[0][off], HIDDEN, &Q[off], 1, &att[h][0], 1, buf);
        for (int d=0;d<HEAD_DIM;d++) ctx[off+d]=0.0f;
        sgemv_n(HEAD_DIM, CTX+1, 0, 1.0f, &Vc[0][off], HIDDEN, &att[h][0], 1, &ctx[off], 1, buf);
      }
    m5_dump_stats();
    sink = ctx[0]+att[0][0];
    return 0;
}
