#include <string.h>
       void *memmove(void *s1, const void *s2, size_t n);

int main() {
        int x[10] = {1,2,3,4,5,6,7,8,9,10};
	int *dest = x;
	int *source = x+2;
	printf("source (before): %d, %d, %d, %d, %d\n",
	       source[0], source[1], source[2], source[3], source[4]);
        memmove(dest,source,5*sizeof(int));
	printf("dest (after): %d, %d, %d, %d, %d\n",
	       dest[0], dest[1], dest[2], dest[3], dest[4]);
        return 0;
}

