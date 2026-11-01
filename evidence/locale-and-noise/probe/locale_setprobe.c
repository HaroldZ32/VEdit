#include <locale.h>
#include <stdio.h>
int main(void) {
    const char *p = setlocale(LC_ALL, "");
    printf("setlocale=%s\n", p ? p : "(null)");
    return 0;
}
