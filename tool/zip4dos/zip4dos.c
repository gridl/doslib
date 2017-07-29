#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "zlib.h"
#include "iconv.h"

static void help(void) {
    fprintf(stderr,"zip4dos [options] <files to archive>\n");
    fprintf(stderr,"A very simple ZIP archiver with MS-DOS/PKUNZIP.EXE in mind.\n");
    fprintf(stderr,"\n");
    fprintf(stderr,"  --zip <path>             ZIP archive to write.\n");
    fprintf(stderr,"  -0                       Store only\n");
    fprintf(stderr,"  -1                       Fast compression\n");
    fprintf(stderr,"  -9                       Better compression\n");
    fprintf(stderr,"  -r                       Recurse into directories\n");
    fprintf(stderr,"  -s <size>                Generate spanning ZIP archives\n");
    fprintf(stderr,"  -ic <charset>            File names on host use this charset\n");
    fprintf(stderr,"  -oc <charset>            File names for target use this charset\n");
    fprintf(stderr,"\n");
    fprintf(stderr,"Spanning size can be specified in bytes, or with K, M, G, suffix.\n");
    fprintf(stderr,"With spanning, the zip file must have .zip suffix, which will be changed\n");
    fprintf(stderr,"per fragment to z01, z02, etc.\n");
    fprintf(stderr,"\n");
    fprintf(stderr,"No changes are made to the filename if neither -ic or -oc are specified.\n");
    fprintf(stderr,"If only -oc is specified, -ic is assumed from your locale.\n");
    fprintf(stderr,"If targeting US MS-DOS systems, you can use -oc CP437.\n");
    fprintf(stderr,"If targeting Japanese PC-98, use -oc CP932 or -oc SHIFT-JIS.\n");
    fprintf(stderr,"You will need to specify -ic and/or -oc before listing files to archive.\n");
    fprintf(stderr,"\n");
    fprintf(stderr,"For simplistic reasons, this code only supports Deflate compression\n");
    fprintf(stderr,"with no password protection.\n");
}

void clear_string(char **a) {
    if (*a != NULL) {
        free(*a);
        *a = NULL;
    }
}

char *set_string(char **a,const char *s) {
    clear_string(a);

    if (s != NULL) {
        assert(*a == NULL);
        *a = strdup(s);
    }

    return *a;
}

struct in_file {
    char*               in_path;
    char*               zip_name;
    unsigned long       file_size;
    struct in_file*     next;
} in_file;

struct in_file *in_file_alloc(void) {
    return (struct in_file*)calloc(1,sizeof(struct in_file));
}

void in_file_set_in_path(struct in_file *f,char *s) {
    set_string(&f->in_path,s);
}

void in_file_set_zip_name(struct in_file *f,char *s) {
    set_string(&f->zip_name,s);
}

void in_file_free(struct in_file *f) {
    clear_string(&f->in_path);
    clear_string(&f->zip_name);
    f->file_size = 0;
}

char *zip_path = NULL;
unsigned long spanning_size = 0;
int deflate_mode = 5; /* default deflate mode */
_Bool recurse = 0;
char *codepage_in = NULL;
char *codepage_out = NULL;

int parse_unit_amount(unsigned long *out,const char *s) {
    if (!isdigit(*s))
        return 0;

    /* value */
    *out = strtoull(s,(char**)(&s),10);

    /* suffix, if any */
    if (*out != 0) {
        char c = tolower(*out++);

        if (*out != 0)
            return 0;

        if (c == 'k')
            *out <<= 10ULL;
        else if (c == 'm')
            *out <<= 20ULL;
        else if (c == 'g')
            *out <<= 30ULL;
        return 0;
    }

    return 1;
}

static int parse(int argc,char **argv) {
    iconv_t ic = (iconv_t)-1;
    char *a;
    int i;

    if (argc <= 1) {
        help();
        return 1;
    }

    for (i=1;i < argc;) {
        a = argv[i++];

        if (*a == '-') {
            do { a++; } while (*a == '-');

            if (!strcmp(a,"h") || !strcmp(a,"help")) {
                help();
                return 1;
            }
            else if (!strcmp(a,"zip")) {
                if (zip_path != NULL) return 1;
                zip_path = strdup(argv[i++]);
                if (zip_path == NULL) return 1;
            }
            else if (!strcmp(a,"r")) {
                recurse = 1;
            }
            else if (!strcmp(a,"s")) {
                a = argv[i++];
                if (a == NULL) return 1;
                if (!parse_unit_amount(&spanning_size,a)) return 1;
            }
            else if (!strcmp(a,"ic")) {
                a = argv[i++];
                if (a == NULL) return 1;
                set_string(&codepage_in,a);

                if (ic != (iconv_t)-1) {
                    iconv_close(ic);
                    ic = (iconv_t)-1;
                }
            }
            else if (!strcmp(a,"oc")) {
                a = argv[i++];
                if (a == NULL) return 1;
                set_string(&codepage_out,a);

                if (ic != (iconv_t)-1) {
                    iconv_close(ic);
                    ic = (iconv_t)-1;
                }
            }
            else if (isdigit(*a)) {
                deflate_mode = (int)strtol(a,(char**)(&a),10);
                if (deflate_mode < 0 || deflate_mode > 9) return 1;
                if (*a != 0) return 1;
            }
            else {
                fprintf(stderr,"Unknown switch %s\n",a);
                return 1;
            }
        }
        else {
            /* file to archive */
            if (ic == (iconv_t)-1 && (codepage_out != NULL || codepage_in != NULL)) {
                if (codepage_out != NULL && codepage_in == NULL) {
                    codepage_in = strdup("UTF-8");
                    if (codepage_in == NULL) return 1;
                }

                ic = iconv_open(codepage_out,codepage_in);
                if (ic == (iconv_t)-1) {
                    fprintf(stderr,"Unable to open character encoding conversion from '%s' to '%s', '%s'\n",codepage_in,codepage_out,strerror(errno));
                    return 1;
                }
            }
        }
    }

    if (ic != (iconv_t)-1) {
        iconv_close(ic);
        ic = (iconv_t)-1;
    }

    if (zip_path == NULL) {
        fprintf(stderr,"Must specify ZIP file: --zip\n");
        return 1;
    }

    fprintf(stderr,"Writing to ZIP archive: %s (deflate level %d)\n",zip_path,deflate_mode);

    return 0;
}

int main(int argc,char **argv) {
    if (parse(argc,argv))
        return 1;

    return 0;
}
