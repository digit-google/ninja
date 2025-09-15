#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char kBytes[] = "Included\xa0";

// Example usage:
//
// ninja_fake_win32_compiler object.o source.c
//
int main(int argc, char** argv) {
    if (argc != 3) {
      fprintf(stderr, "This program only takes two arguments.\n");
      return 1;
    }

    // Write the output file.
    FILE* f = fopen(argv[1], "wt");
    fwrite("a\n", 1, 1, f);
    fclose(f);

    // Replace input file extension with .h
    char filename[128];
    snprintf(filename, sizeof(filename), "%s", argv[2]);
    size_t pos = strlen(filename);
    while (pos > 0 && filename[pos - 1] != '.')
      pos--;
    filename[pos] = 'h';

    // Write the msvc_deps_prefix line.
    _setmode(_fileno(stdout), _O_BINARY);
    fwrite(kBytes, sizeof(kBytes) - 1, 1, stdout);
    _setmode(_fileno(stdout), _O_TEXT);
    fprintf(stdout, "%s\n", filename);

    return 0;
}
