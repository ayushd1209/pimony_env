int main() {
    int a[4] = {10, 20, 30, 40};   // array -> generates lw loads
    int sum = 0;
    for (int i = 0; i < 4; i++)
        sum += a[i];               // load (lw) + add
    return sum;                    // returns 100
}
