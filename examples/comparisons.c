#include <stdio.h>

int main() {
    int y = 2;
    int z = 3;
    if(y > 0) {
        z++;
    } 

    if(y < 0) {
        z--;
    }

    if(y <= 0) {
        z = 0;
    }

    if(y >= 0) {
        z = 3;
    }

    return 0;
}