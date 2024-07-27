#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void overlap(void);
void resync(void);

typedef enum
{
    Resync,
    Overlap,
    Error
} Tool;

int main(int argc, char *argv[])
{
    // get tool
    Tool tool = Resync;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--tool") != 0)
            continue;

        if (strcmp(argv[i+1], "resync") == 0)
            tool = Resync;
        else if (strcmp(argv[i+1], "overlap") == 0)
            tool = Overlap;
        else
            tool = Error;
    }

    switch (tool)
    {
        case Resync:
            fprintf(stderr, "Running resync!\n");
            break;
        case Overlap:
            fprintf(stderr, "Running overlap!\n");
            break;
        default:
            fprintf(stderr, "Incorrect tool type.\n");
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}