// Simple square matrix multiply: C = A * B
// Sized so the working set (3 x N*N ints) is far larger than a 1KiB L1 but
// interacts with a 32KiB L1 -- makes the cache-size comparison in cores.py
// actually show a difference. N=64 -> 16KiB per matrix.
#define N 64

static int A[N][N];
static int B[N][N];
static int C[N][N];

int main() {
    // Initialise A and B with a cheap deterministic pattern (lots of lw/sw).
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            A[i][j] = (i + j) & 0xff;
            B[i][j] = (i ^ j) & 0xff;
        }
    }

    // The multiply itself: the inner loop is loads (lw) + multiply-add (add).
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            int sum = 0;
            for (int k = 0; k < N; k++) {
                sum += A[i][k] * B[k][j];   // lw A, lw B, mul, add
            }
            C[i][j] = sum;
        }
    }

    // Reduce C to a checksum so the work can't be optimised away and we get a
    // stable return value.
    int checksum = 0;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            checksum += C[i][j];

    return checksum & 0xff;
}