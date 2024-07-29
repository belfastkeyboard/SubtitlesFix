#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#define MAX_MIC 1000000
#define MAX_SEC 60
#define MAX_MIN 60
#define MAX_HOUR 24

#define MIC_IN_HOUR 3600000000
#define MIC_IN_MIN 60000000
#define MIC_IN_SEC 1000000

#define EXT_LEN 4
#define TS_LEN 13

typedef unsigned long useconds8_t;

typedef enum
{
    Resync,
    Overlap,
    Error
} Tool;

typedef struct
{
    int hour;
    int min;
    int sec;
    int mic;
} ts_stamp;

typedef struct
{
    ts_stamp first;
    ts_stamp second;
} ts_pair;

static regex_t re_ts;
static const char *pattern_ts = "^[0-9]{2}:[0-9]{2}:[0-9]{2},[0-9]{3} --> [0-9]{2}:[0-9]{2}:[0-9]{2},[0-9]{3}";

useconds8_t fit_time(ts_stamp *time)
{
    int hour = time->hour;
    int min = time->min;
    int sec = time->sec;
    int mic = time->mic;
    useconds8_t microseconds = 0;

    if (mic < 0)
    {
        sec -= (mic * -1) / MAX_MIC + 1;
        mic = MAX_MIC - (mic * -1) % MAX_MIC;
    }
    else
    {
        sec += mic / MAX_MIC;
        mic = mic % MAX_MIC;
    }
    microseconds += (useconds8_t)mic;

    if (sec < 0)
    {
        min -= (sec * -1) / MAX_SEC + 1;
        sec = MAX_SEC - (sec * -1) % MAX_SEC;
    }
    else
    {
        min += sec / MAX_SEC;
        sec = sec % MAX_SEC;
    }
    microseconds += (useconds8_t)sec * MIC_IN_SEC;

    if (min < 0)
    {
        hour -= (min * -1) / MAX_MIN + 1;
        min = MAX_MIN - (min * -1) % MAX_MIN;
    }
    else
    {
        hour += min / MAX_MIN;
        min = min % MAX_MIN;
    }
    microseconds += (useconds8_t)min * MIC_IN_MIN;

    if (hour < 0)
        hour = MAX_HOUR - (hour * -1) % MAX_HOUR;
    else
        hour = hour % MAX_HOUR;
    microseconds += (useconds8_t)hour * MIC_IN_HOUR;

    time->mic = mic;
    time->sec = sec;
    time->min = min;
    time->hour = hour;

    return microseconds;
}

char* time_repr(ts_stamp stamp, char* message)
{
    snprintf(message, TS_LEN, "%02d:%02d:%02d,%03d", stamp.hour, stamp.min, stamp.sec, stamp.mic / 1000);

    return message;
}

ts_stamp create_timestamp(char *time)
{
    ts_stamp stamp = { 0 };

    size_t i = 0;
    char *delim_t = strtok(time, ":");
    while (delim_t != NULL)
    {
        if (i == 0)
            stamp.hour = (int)strtol(delim_t, NULL, 10);
        else if (i == 1)
            stamp.min = (int)strtol(delim_t, NULL, 10);
        else if (i == 2)
        {
            char *dec = NULL;
            stamp.sec = (int)strtol(delim_t, &dec, 10);
            *dec = '.';
            float f = strtof(delim_t, NULL);
            stamp.mic = (int)roundf((f - floorf(f)) * MIC_IN_SEC);
        }

        delim_t = strtok(NULL, ":");
        ++i;
    }

    return stamp;
}

ts_pair get_stamp_pair(char *line)
{
    char *stamps[2];

    stamps[0] = strtok(line, " -->");
    stamps[1] = strtok(NULL, " -->\r\n");

    ts_stamp stamp1 = create_timestamp(stamps[0]);
    ts_stamp stamp2 = create_timestamp(stamps[1]);

    return (ts_pair){ stamp1, stamp2 };
}

bool is_timestamp(const char *line)
{
    regmatch_t match[1];

    return regexec(&re_ts, line, 1, match, 0) == 0;
}

bool is_counter(const char *line)
{
    const char *c = line;

    while (*c >= '0' && *c <= '9')
    {
        c++;
    }

    return (c != line && (*c == '\r' || *c == '\n'));
}

bool cmp_ts(ts_stamp *a, ts_stamp *b)
{
    useconds8_t a_ms = fit_time(a);
    useconds8_t b_ms = fit_time(b);

    return a_ms < b_ms;
}

int get_file(char *string, char **file, char *cmp)
{
    char ext[5] = { 0 };

    if ((strlen(string) < EXT_LEN) || (!strchr(string, '.')) || (cmp && strcmp(string, cmp) == 0) || *file)
        return EXIT_SUCCESS;

    size_t b = strlen(string);
    strncpy(ext, string + b-EXT_LEN, EXT_LEN);
    if (strcmp(ext, ".srt") != 0)
    {
        fprintf(stderr, "Not .srt file: %s.\n", string);
        return EXIT_FAILURE;
    }

    *file = string;
    return EXIT_SUCCESS;
}

int resync(const char *read, char *write, float offset)
{
    FILE* readfrom = NULL;
    FILE* writeto = NULL;
    char *line = NULL;
    char *newline = NULL;
    size_t len = 0;

    readfrom = fopen(read, "r");
    if (!readfrom)
    {
        fprintf(stderr, "Cannot open file.\n");
        return EXIT_FAILURE;
    }

    if (!write)
    {
        size_t r_len = strlen(read) - EXT_LEN;
        write = calloc(r_len + 5 + EXT_LEN, sizeof(char));
        strncpy(write, read, r_len);
        strncat(write + r_len, "_copy.srt", strlen("_copy.srt") + 1);

        writeto = fopen(write, "w");

        free(write);
    }
    else
    {
        writeto = fopen(write, "w");
    }
    if (!writeto)
    {
        fprintf(stderr, "Cannot open file.\n");
        fclose(readfrom);
        return EXIT_FAILURE;
    }

    int offset_s = (int)offset;
    int offset_u = (int)roundf((offset - (float)(int)(offset)) * MIC_IN_SEC);

    assert(regcomp(&re_ts, pattern_ts, REG_EXTENDED) == 0);

    while (getline(&line, &len, readfrom) != -1)
    {
        if (!newline)
        {
            char *c = line + strlen(line)-1;

            while (*c == '\r' || *c == '\n')
                --c;

            newline = calloc(strlen(c), sizeof(char));
            strcpy(newline, ++c);
        }

        if (is_timestamp(line))
        {
            ts_pair pair = get_stamp_pair(line);

            pair.first.sec += offset_s;
            pair.first.mic += offset_u;

            pair.second.sec += offset_s;
            pair.second.mic += offset_u;

            fit_time(&pair.first);
            fit_time(&pair.second);

            char *msg1[TS_LEN], msg2[TS_LEN];
            snprintf(
                line, TS_LEN + TS_LEN + strlen(newline) + strlen(" --> "), "%s --> %s%s",
                time_repr(pair.first, (char*)msg1), time_repr(pair.second, (char*)msg2), newline
            );
        }

        fwrite(line, sizeof(char), strlen(line), writeto);
    }

    regfree(&re_ts);
    free(newline);
    free(line);
    fclose(readfrom);
    fclose(writeto);

    return EXIT_SUCCESS;
}

int overlap(const char* read)
{
    FILE* readfrom = NULL;
    char *line = NULL;
    size_t len = 0;

    readfrom = fopen(read, "r");
    if (!readfrom)
    {
        fprintf(stderr, "Cannot open file.\n");
        return EXIT_FAILURE;
    }

    assert(regcomp(&re_ts, pattern_ts, REG_EXTENDED) == 0);

    ts_stamp curr = { 0 };
    ts_stamp prev = { 0 };
    unsigned int counter = 0;
    char* msg1[TS_LEN], msg2[TS_LEN];

    while (getline(&line, &len, readfrom) != -1)
    {
        if (is_counter(line))
            counter = (int)strtol(line, NULL, 10);

        if (!is_timestamp(line))
            continue;

        ts_pair pair = get_stamp_pair(line);
        if (cmp_ts(&pair.second, &pair.first))
            printf("1 %d: %s overlaps %s.\n",
               counter, time_repr(pair.first, (char*)msg1), time_repr(pair.second, (char*)msg2)
           );

        prev = curr;
        curr = pair.second;

        if (cmp_ts(&pair.first, &prev))
            printf("2 %d: %s overlaps %s.\n",
               counter, time_repr(pair.first, (char*)msg1), time_repr(prev, (char*)msg2)
            );
    }

    regfree(&re_ts);

    free(line);
    fclose(readfrom);
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    Tool tool = Resync;
    char* read = NULL;
    char* write = NULL;
    float amount = 0.0f;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--tool") == 0)
        {
            if (strcmp(argv[i+1], "resync") == 0)
                tool = Resync;
            else if (strcmp(argv[i+1], "overlap") == 0)
                tool = Overlap;
            else
                tool = Error;
        }
        else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "-file") == 0)
        {
            get_file(argv[i+1], &read, NULL);
        }
        else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "-output") == 0)
        {
            get_file(argv[i+1], &write, NULL);
        }
        else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "-amount") == 0)
        {
            amount = strtof(argv[i+1], NULL);
        }
    }

    if (read == NULL)
    {
        for (int i = 1; i < argc; i++)
        {
            get_file(argv[i], &read, NULL);
        }

        if (read == NULL)
        {
            fprintf(stderr, "File not specified.\n");
            return EXIT_FAILURE;
        }
    }

    if (tool == Resync && write == NULL)
    {
        for (int i = 1; i < argc; i++)
        {
            get_file(argv[i], &write, read);
        }
    }

    if (tool == Resync && amount == 0.0f)
    {
        for (int i = 1; i < argc; i++)
        {
            float n = strtof(argv[i], NULL);
            if (n && !amount)
            {
                amount = n;
            }
            else if (n && amount)
            {
                fprintf(stderr, "Ambiguous amount argument.\n");
                return EXIT_FAILURE;
            }
        }
    }

    switch (tool)
    {
        case Resync:
            resync(read, write, amount);
            break;
        case Overlap:
            overlap(read);
            break;
        default:
            fprintf(stderr, "Incorrect tool type.\n");
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
