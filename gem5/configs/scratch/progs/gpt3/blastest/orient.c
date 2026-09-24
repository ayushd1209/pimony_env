/* Does sgemv_t/sgemv_n compute what I claim for a row-major head block?
 * Hosted build, printf available -- this only checks the maths. */
#include <stdio.h>
#include <string.h>
int sgemv_n(long,long,long,float,float*,long,float*,long,float*,long,float*);
int sgemv_t(long,long,long,float,float*,long,float*,long,float*,long,float*);

#define L   5           /* cache entries            */
#define HD  4           /* head_dim                 */
#define LD  7           /* row stride (== HIDDEN)   */
#define OFF 2           /* head column offset       */

static float Kc[L*LD], q[LD], y[L], ref[L], buf[4096];

int main(void){
    for (int j=0;j<L;j++) for (int d=0;d<LD;d++) Kc[j*LD+d] = (float)(j*10+d);
    for (int d=0;d<LD;d++) q[d] = (float)(d+1);

    for (int j=0;j<L;j++){ float s=0; for(int d=0;d<HD;d++) s+=q[OFF+d]*Kc[j*LD+OFF+d]; ref[j]=s; }

    memset(y,0,sizeof y);
    sgemv_t(HD, L, 0, 1.0f, &Kc[OFF], LD, &q[OFF], 1, y, 1, buf);
    int ok=1;
    for (int j=0;j<L;j++){ printf("scores j=%d blas %8.2f ref %8.2f\n", j, y[j], ref[j]);
                           if (y[j]!=ref[j]) ok=0; }

    /* context: ctx[d] = sum_j att[j] * Vc[j][OFF+d] */
    float att[L], ctx[HD], cref[HD];
    for (int j=0;j<L;j++) att[j]=(float)(j+1);
    for (int d=0;d<HD;d++){ float s=0; for(int j=0;j<L;j++) s+=att[j]*Kc[j*LD+OFF+d]; cref[d]=s; }
    memset(ctx,0,sizeof ctx);
    sgemv_n(HD, L, 0, 1.0f, &Kc[OFF], LD, att, 1, ctx, 1, buf);
    for (int d=0;d<HD;d++){ printf("ctx    d=%d blas %8.2f ref %8.2f\n", d, ctx[d], cref[d]);
                            if (ctx[d]!=cref[d]) ok=0; }
    printf("%s\n", ok?"ORIENTATION OK":"ORIENTATION WRONG");
    return 0;
}
