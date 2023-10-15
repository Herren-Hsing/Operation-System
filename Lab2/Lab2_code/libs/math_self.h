int pow2(int exponent)
{
    if (exponent < 0)
    {
        return 0;
    }
    else if (exponent >= 31)
    {
        return 0x7FFFFFFF;
    }
    else
    {
        return 1 << exponent;
    }
}

int log2_floor(int x)
{
    int count = 0;
    while (x > 1)
    {
        x >>= 1; // 右移一位，相当于除以2
        count++;
    }
    return count;
}

int log2_ceil(int x)
{
    int result = 0;
    x--;

    while (x > 0)
    {
        x >>= 1;
        result++;
    }

    return result;
}

int is_power_of_2(int x)
{
    return !(x & (x - 1));
}

int max_special(int a, int b)
{
    if (a == b && a == 255)
    {
        return 255;
    }
    if (a == 255)
        a = -1;
    if (b == 255)
        b = -1;
    return (a > b) ? a : b;
}