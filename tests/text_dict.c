/* Expose the in-memory blob only to the independent format tests. */
#include "xdb.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
	unsigned char *data = NULL;
	uint32_t size = 0;
	scws_status_t status;
	FILE *output;
	int failed;
	if (argc != 3) return 2;
	status = scws_text_load(argv[1], &data, &size);
	if (status) return (int) status;
	output = fopen(argv[2], "wb");
	if (!output) { free(data); return 2; }
	failed = fwrite(data, 1, size, output) != size;
	if (fclose(output)) failed = 1;
	free(data);
	return failed;
}
