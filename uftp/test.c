#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

void error(char *msg) {
	fprintf(stderr, "Error %s\n", msg);
	exit(-1);
}

int main() {
	FILE *fp;
	char buf[65000];
	for(int i = 0; i < 65000; i++) {
		buf[i] = 'a';
	}
	fp = fopen("test.txt", "wb");
	printf("%s\n", buf);
	fwrite(buf, 1, 65000, fp);
	fclose(fp);
}
