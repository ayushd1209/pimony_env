#include <stdint.h>

/* Test 0 + Test 1 — prove pim.fence.{cl,inv,flush} produce the SAME cache<->DRAM
   traffic as the standard cbo they wrap. A/B in one run:
     0a cbo.clean(X)  vs 1a pim.fence.cl(P)     -> both WRITE to DRAM
     0b cbo.inval(Y)  vs 1b pim.fence.inv(Q)    -> both: 2nd read MISSES to DRAM
     0c cbo.flush(F)  vs 1c pim.fence.flush(G)  -> both: WRITE (clean) + 2nd read MISS (drop)
   PIM does no arithmetic, so we verify via the architectural effect, not a value. */

#define X 0x81000000ULL   /* 0a cbo.clean       */
#define Y 0x81000040ULL   /* 0b cbo.inval       */
#define P 0x81000100ULL   /* 1a pim.fence.cl    */
#define Q 0x81000140ULL   /* 1b pim.fence.inv   */
#define F 0x81000200ULL   /* 0c cbo.flush       */
#define G 0x81000240ULL   /* 1c pim.fence.flush */

/* standard Zicbom (baseline) */
static inline void cbo_clean(uint64_t a){ register uint64_t x asm("a0")=a;
    __asm__ volatile(".word 0x0015200F"::"r"(x):"memory"); }
static inline void cbo_inval(uint64_t a){ register uint64_t x asm("a0")=a;
    __asm__ volatile(".word 0x0005200F"::"r"(x):"memory"); }
static inline void cbo_flush(uint64_t a){ register uint64_t x asm("a0")=a;
    __asm__ volatile(".word 0x0025200F"::"r"(x):"memory"); }

/* our custom pim.fence (opcode 0x0B / custom-0), FUNCT3 = 1/2/4, rs1=a0 */
static inline void pim_fence_cl   (uint64_t a){ register uint64_t x asm("a0")=a;
    __asm__ volatile(".word 0x0005100B"::"r"(x):"memory"); }
static inline void pim_fence_inv  (uint64_t a){ register uint64_t x asm("a0")=a;
    __asm__ volatile(".word 0x0005200B"::"r"(x):"memory"); }
static inline void pim_fence_flush(uint64_t a){ register uint64_t x asm("a0")=a;
    __asm__ volatile(".word 0x0005400B"::"r"(x):"memory"); }

static inline void m5_exit(void){ register uint64_t a0 asm("a0")=0;
    __asm__ volatile(".word 0x4200007B"::"r"(a0):"memory"); }

int main(void)
{
    volatile uint64_t *x=(volatile uint64_t*)X,*y=(volatile uint64_t*)Y;
    volatile uint64_t *p=(volatile uint64_t*)P,*q=(volatile uint64_t*)Q;
    volatile uint64_t *f=(volatile uint64_t*)F,*g=(volatile uint64_t*)G;

    /* ---- clean: WRITE ---- */
    *x=0xA1; __asm__ volatile("fence rw,rw":::"memory"); cbo_clean(X);
    *p=0xB1; __asm__ volatile("fence rw,rw":::"memory"); pim_fence_cl(P);

    /* ---- invalidate: 2nd read MISSES ---- */
    volatile uint64_t a=*y;(void)a; cbo_inval(Y);       a=*y;(void)a;
    volatile uint64_t b=*q;(void)b; pim_fence_inv(Q);   b=*q;(void)b;

    /* ---- flush: WRITE (clean) then 2nd read MISSES (drop) ---- */
    *f=0xC1; __asm__ volatile("fence rw,rw":::"memory"); cbo_flush(F);
    volatile uint64_t c=*f;(void)c;                      /* miss -> RD */
    *g=0xD1; __asm__ volatile("fence rw,rw":::"memory"); pim_fence_flush(G);
    volatile uint64_t d=*g;(void)d;                      /* miss -> RD (must match F) */

    m5_exit();
    for(;;){}
    return 0;
}
