/* k_expf (gpt3.c's) vs libm expf, same binary, two stats phases.
 * N matches one layer's softmax: HEADS 32 x (CTX+1) 257 = 8224 calls. */
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#define N 8224
#define REPS 20
static float in[N], out[N];
volatile float sink;

static inline void m5_dump_reset(void){ register uint64_t a0 asm("a0")=0,a1 asm("a1")=0;
    __asm__ volatile(".word 0x8400007B" ::"r"(a0),"r"(a1):"memory"); }

static float k_expf(float v){
    if (v >  87.0f) return INFINITY;
    if (v < -87.0f) return 0.0f;
    const float LOG2E=1.4426950408889634f, LN2=0.6931471805599453f;
    float fn=v*LOG2E; int n=(int)(fn+(fn>=0.0f?0.5f:-0.5f));
    float r=v-(float)n*LN2;
    float p=1.0f+r*(1.0f+r*(0.5f+r*(0.16666667f+r*(0.041666668f+r*(0.008333334f+r*0.0013888889f)))));
    union{float f;uint32_t u;}s; s.u=(uint32_t)((127+n)<<23);
    return p*s.f;
}
int main(void){
    for(int i=0;i<N;i++) in[i] = -8.0f + 16.0f*((float)(i%1000)/1000.0f);
    float acc=0;
    m5_dump_reset();
    for(int r=0;r<REPS;r++){ for(int i=0;i<N;i++) out[i]=k_expf(in[i]); acc+=out[N-1]; }
    m5_dump_reset();                      /* phase 1 = k_expf */
    for(int r=0;r<REPS;r++){ for(int i=0;i<N;i++) out[i]=expf(in[i]);  acc+=out[N-1]; }
    m5_dump_reset();                      /* phase 2 = libm expf */
    sink=acc; printf("done %f\n", acc);
    return 0;
}
