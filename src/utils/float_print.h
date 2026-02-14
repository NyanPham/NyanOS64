static void reverse(char *str, int len)
{
    int i = 0, j = len - 1, temp;
    while (i < j)
    {
        temp = str[i];
        str[i] = str[j];
        str[j] = temp;
        i++;
        j--;
    }
}

static int int_to_str(int x, char str[], int d)
{
    int i = 0;
    int sign = x;
    if (x < 0)
        x = -x;

    while (x)
    {
        str[i++] = (x % 10) + '0';
        x = x / 10;
    }
    if (i == 0)
        str[i++] = '0';
    if (sign < 0)
        str[i++] = '-';

    while (i < d)
        str[i++] = '0';

    str[i] = '\0';
    reverse(str, i);
    return i;
}

void ftoa(float n, char *res, int afterpoint)
{
    int ipart = (int)n;
    float fpart = n - (float)ipart;

    if (n < 0)
    {
        *res++ = '-';
        n = -n;
        ipart = -ipart;
        fpart = -fpart;
    }

    int i = int_to_str(ipart, res, 0);

    if (afterpoint != 0)
    {
        res[i] = '.';
        fpart = fpart * 1.0f;

        float pow10 = 1.0f;
        for (int k = 0; k < afterpoint; k++)
            pow10 *= 10.0f;

        fpart = fpart * pow10;

        int_to_str((int)fpart, res + i + 1, afterpoint);
    }
}