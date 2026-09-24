/* Bare-metal link test: can the OpenBLAS GEMV kernels live in a freestanding
 * binary with no libc? Exits via m5_exit if the maths is right, spins if not. */
#include <stdint.h>
int sgemv_n(long,long,long,float,float*,long,float*,long,float*,long,float*);
int sgemv_t(long,long,long,float,float*,long,float*,long,float*,long,float*);

static inline void m5_exit(void){ register uint64_t a0 asm("a0")=0;
    __asm__ volatile(".word 0x4200007B" ::"r"(a0):"memory"); }

#define L 5
#define HD 4
#define LD 7
#define OFF 2
static float Kc[L*LD], q[LD], y[L], buf[4096];
volatile float g_y[L];

int main(void){
    for (int j=0;j<L;j++) for (int d=0;d<LD;d++) Kc[j*LD+d]=(float)(j*10+d);
    for (int d=0;d<LD;d++) q[d]=(float)(d+1);
    for (int j=0;j<L;j++) y[j]=0.0f;
    sgemv_t(HD, L, 0, 1.0f, &Kc[OFF], LD, &q[OFF], 1, y, 1, buf);
    for (int j=0;j<L;j++) g_y[j]=y[j];
    if (g_y[0]!=68.0f || g_y[4]!=788.0f) for(;;);     /* wrong -> hang */
    m5_exit();                                        /* right -> clean exit */
    for(;;);
}

__attribute__((naked, section(".text._start")))
void _start(void){
    __asm__ volatile(
        ".option push\n.option norelax\n"
        "la gp, __global_pointer$\n"
        ".option pop\n"
        "la sp, _stack_top\n"
        "call main\n"
        "1: j 1b\n");
}
