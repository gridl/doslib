#include "minx86dec/types.h"
#include "minx86dec/state.h"
#include "minx86dec/opcodes.h"
#include "minx86dec/coreall.h"
#include "minx86dec/opcodes_str.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

#include <hw/dos/exehdr.h>
#include <hw/dos/exenehdr.h>
#include <hw/dos/exenepar.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

struct dec_label {
    uint16_t                    seg_v,ofs_v;
    char*                       name;
};

void cstr_free(char **l) {
    if (l != NULL) {
        if (*l != NULL) free(*l);
        *l = NULL;
    }
}

void cstr_copy(char **l,const char *s) {
    cstr_free(l);

    if (s != NULL) {
        const size_t len = strlen(s);
        *l = malloc(len+1);
        if (*l != NULL)
            strcpy(*l,s);
    }
}

void dec_label_set_name(struct dec_label *l,const char *s) {
    cstr_copy(&l->name,s);
}

struct dec_label*               dec_label = NULL;
size_t                          dec_label_count = 0;
size_t                          dec_label_alloc = 0;
unsigned long                   dec_ofs;
uint16_t                        dec_cs;

uint8_t                         dec_buffer[256];
uint8_t*                        dec_read;
uint8_t*                        dec_end;
char                            arg_c[101];
struct minx86dec_state          dec_st;
struct minx86dec_instruction    dec_i;
minx86_read_ptr_t               iptr;
uint16_t                        entry_cs,entry_ip;
uint16_t                        start_cs,start_ip;
uint32_t                        start_decom,end_decom,entry_ofs;
uint32_t                        current_offset;

struct exe_dos_header           exehdr;

char*                           label_file = NULL;

char*                           src_file = NULL;
int                             src_fd = -1;

uint32_t current_offset_minus_buffer() {
    return current_offset - (uint32_t)(dec_end - dec_buffer);
}

static void minx86dec_init_state(struct minx86dec_state *st) {
	memset(st,0,sizeof(*st));
}

static void minx86dec_set_buffer(struct minx86dec_state *st,uint8_t *buf,int sz) {
	st->fence = buf + sz;
	st->prefetch_fence = dec_buffer + sizeof(dec_buffer) - 16;
	st->read_ip = buf;
}

void help() {
    fprintf(stderr,"dosdasm [options]\n");
    fprintf(stderr,"MS-DOS COM/EXE/SYS decompiler\n");
    fprintf(stderr,"    -i <file>        File to decompile\n");
    fprintf(stderr,"    -lf <file>       Text file to define labels\n");
}

int parse_argv(int argc,char **argv) {
    char *a;
    int i=1;

    while (i < argc) {
        a = argv[i++];

        if (*a == '-') {
            do { a++; } while (*a == '-');

            if (!strcmp(a,"i")) {
                src_file = argv[i++];
                if (src_file == NULL) return 1;
            }
            else if (!strcmp(a,"lf")) {
                label_file = argv[i++];
                if (label_file == NULL) return 1;
            }
            else if (!strcmp(a,"h") || !strcmp(a,"help")) {
                help();
                return 1;
            }
            else {
                fprintf(stderr,"Unknown switch %s\n",a);
                return 1;
            }
        }
        else {
            fprintf(stderr,"Unknown arg %s\n",a);
            return 1;
        }
    }

    if (src_file == NULL) {
        fprintf(stderr,"Must specify -i source file\n");
        return 1;
    }

    return 0;
}

void reset_buffer() {
    current_offset = 0;
    dec_read = dec_end = dec_buffer;
}

int refill() {
    const size_t flush = sizeof(dec_buffer) / 2;
    const size_t padding = 16;
    size_t dlen;

    if (dec_end > dec_read)
        dlen = (size_t)(dec_end - dec_read);
    else
        dlen = 0;

    if (dec_read >= (dec_buffer+flush)) {
        assert((dec_read+dlen) <= (dec_buffer+sizeof(dec_buffer)-padding));
        if (dlen != 0) memmove(dec_buffer,dec_read,dlen);
        dec_read = dec_buffer;
        dec_end = dec_buffer + dlen;
    }
    {
        unsigned char *e = dec_buffer + sizeof(dec_buffer) - padding;

        if (dec_end < e) {
            unsigned long clen = end_decom - current_offset;
            dlen = (size_t)(e - dec_end);
            if ((unsigned long)dlen > clen)
                dlen = (size_t)clen;

            if (dlen != 0) {
                int rd = read(src_fd,dec_end,dlen);
                if (rd > 0) {
                    dec_end += rd;
                    current_offset += (unsigned long)rd;
                }
            }
        }
    }

    return (dec_read < dec_end);
}

struct dec_label *dec_label_malloc() {
    if (dec_label == NULL)
        return NULL;

    if (dec_label_count >= dec_label_alloc)
        return NULL;

    return dec_label + (dec_label_count++);
}

int dec_label_qsortcb(const void *a,const void *b) {
    const struct dec_label *as = (const struct dec_label*)a;
    const struct dec_label *bs = (const struct dec_label*)b;

    if (as->seg_v < bs->seg_v)
        return -1;
    else if (as->seg_v > bs->seg_v)
        return 1;

    if (as->ofs_v < bs->ofs_v)
        return -1;
    else if (as->ofs_v > bs->ofs_v)
        return 1;

    return 0;
}

void dec_label_sort() {
    if (dec_label == NULL || dec_label_count == 0)
        return;

    qsort(dec_label,dec_label_count,sizeof(*dec_label),dec_label_qsortcb);
}

int main(int argc,char **argv) {
    struct exe_ne_header_imported_name_table ne_imported_name_table;
    struct exe_ne_header_entry_table_table ne_entry_table;
    struct exe_ne_header_name_entry_table ne_nonresname;
    struct exe_ne_header_resource_table_t ne_resources;
    struct exe_ne_header_name_entry_table ne_resname;
    struct exe_ne_header_segment_table ne_segments;
    struct exe_ne_header ne_header;
    uint32_t ne_header_offset;
    struct dec_label *label;
    unsigned int segmenti;
    unsigned int labeli;
    uint32_t file_size;
    int c;

    assert(sizeof(ne_header) == 0x40);
    memset(&exehdr,0,sizeof(exehdr));
    exe_ne_header_segment_table_init(&ne_segments);
    exe_ne_header_resource_table_init(&ne_resources);
    exe_ne_header_name_entry_table_init(&ne_resname);
    exe_ne_header_name_entry_table_init(&ne_nonresname);
    exe_ne_header_entry_table_table_init(&ne_entry_table);
    exe_ne_header_imported_name_table_init(&ne_imported_name_table);

    if (parse_argv(argc,argv))
        return 1;

    dec_label_alloc = 4096;
    dec_label_count = 0;
    dec_label = malloc(sizeof(*dec_label) * dec_label_alloc);
    if (dec_label == NULL) {
        fprintf(stderr,"Failed to alloc label array\n");
        return 1;
    }

    src_fd = open(src_file,O_RDONLY|O_BINARY);
    if (src_fd < 0) {
        fprintf(stderr,"Unable to open %s, %s\n",src_file,strerror(errno));
        return 1;
    }

    file_size = lseek(src_fd,0,SEEK_END);
    lseek(src_fd,0,SEEK_SET);

    if (read(src_fd,&exehdr,sizeof(exehdr)) != (int)sizeof(exehdr)) {
        fprintf(stderr,"EXE header read error\n");
        return 1;
    }

    if (exehdr.magic != 0x5A4DU/*MZ*/) {
        fprintf(stderr,"EXE header signature missing\n");
        return 1;
    }

    if (!exe_header_can_contain_exe_extension(&exehdr)) {
        fprintf(stderr,"EXE header cannot contain extension\n");
        return 1;
    }

    /* go read the extension */
    if (lseek(src_fd,EXE_HEADER_EXTENSION_OFFSET,SEEK_SET) != EXE_HEADER_EXTENSION_OFFSET ||
        read(src_fd,&ne_header_offset,4) != 4) {
        fprintf(stderr,"Cannot read extension\n");
        return 1;
    }
    printf("    EXE extension (if exists) at: %lu\n",(unsigned long)ne_header_offset);
    if ((ne_header_offset+EXE_HEADER_NE_HEADER_SIZE) >= file_size) {
        printf("! NE header not present (offset out of range)\n");
        return 0;
    }

    /* go read the extended header */
    if (lseek(src_fd,ne_header_offset,SEEK_SET) != ne_header_offset ||
        read(src_fd,&ne_header,sizeof(ne_header)) != sizeof(ne_header)) {
        fprintf(stderr,"Cannot read NE header\n");
        return 1;
    }
    if (ne_header.signature != EXE_NE_SIGNATURE) {
        fprintf(stderr,"Not an NE executable\n");
        return 1;
    }

    /* load segment table */
    if (ne_header.segment_table_entries != 0 && ne_header.segment_table_offset != 0 &&
        (unsigned long)lseek(src_fd,ne_header.segment_table_offset + ne_header_offset,SEEK_SET) == ((unsigned long)ne_header.segment_table_offset + ne_header_offset)) {
        unsigned char *base;
        size_t rawlen;

        base = exe_ne_header_segment_table_alloc_table(&ne_segments,ne_header.segment_table_entries,ne_header.sector_shift);
        if (base != NULL) {
            rawlen = exe_ne_header_segment_table_size(&ne_segments);
            if (rawlen != 0) {
                if ((size_t)read(src_fd,base,rawlen) != rawlen) {
                    printf("    ! Unable to read segment table\n");
                    exe_ne_header_segment_table_free_table(&ne_segments);
                }
            }
        }
    }

    if (label_file != NULL) {
        FILE *fp = fopen(label_file,"r");
        if (fp == NULL) {
            fprintf(stderr,"Failed to open label file, %s\n",label_file);
            return 1;
        }

        while (!feof(fp) && !ferror(fp)) {
            char *s;

            memset(dec_buffer,0,sizeof(dec_buffer));
            if (fgets((char*)dec_buffer,sizeof(dec_buffer)-1,fp) == NULL)
                break;

            s = (char*)dec_buffer;
            {
                char *e = s + strlen(s) - 1;
                while (e > (char*)dec_buffer && (*e == '\r' || *e == '\n')) *e-- = 0;
            }

            while (*s == ' ') s++;
            if (*s == ';' || *s == '#') continue;

            // seg:off label
            if (isxdigit(*s)) {
                uint16_t so,oo;

                so = (uint16_t)strtoul(s,&s,16);
                if (*s == ':') {
                    s++;
                    oo = (uint16_t)strtoul(s,&s,16);
                    while (*s == '\t' || *s == ' ') s++;

                    if ((label=dec_label_malloc()) != NULL) {
                        dec_label_set_name(label,s);
                        label->seg_v =
                            so;
                        label->ofs_v =
                            oo;
                    }
                }
            }
        }

        fclose(fp);
    }

    printf("* Entry point %04X:%04X\n",
        ne_header.entry_cs,
        ne_header.entry_ip);

    if (ne_header.entry_cs >= 1 && ne_header.entry_cs <= ne_segments.length) {
        if ((label=dec_label_malloc()) != NULL) {
            dec_label_set_name(label,"Entry point NE .EXE");
            label->seg_v =
                ne_header.entry_cs;
            label->ofs_v =
                ne_header.entry_ip;
        }
    }

    // TODO first pass

    /* sort labels */
    dec_label_sort();

    /* second pass: decompilation */
    for (segmenti=0;segmenti < ne_segments.length;segmenti++) {
        const struct exe_ne_header_segment_entry *segent = ne_segments.table + segmenti;
        uint32_t segment_ofs = (uint32_t)segent->offset_in_segments << (uint32_t)ne_segments.sector_shift;
        uint32_t segment_sz;

        if (segent->offset_in_segments == 0)
            continue;

        segment_sz =
            (segent->length == 0 ? 0x10000UL : segent->length);
        dec_cs = segmenti + 1;
        dec_ofs = 0;

        if ((uint32_t)lseek(src_fd,segment_ofs,SEEK_SET) != segment_ofs)
            return 1;

        printf("* NE segment #%d (0x%lx bytes @0x%lx)\n",
            segmenti + 1,(unsigned long)segment_sz,(unsigned long)segment_ofs);

        labeli = 0;
        entry_ip = 0;
        reset_buffer();
        entry_cs = dec_cs;
        current_offset = segment_ofs;
        end_decom = segment_ofs + segment_sz;
        minx86dec_init_state(&dec_st);
        dec_read = dec_end = dec_buffer;
        dec_st.data32 = dec_st.addr32 = 0;

        if (segent->flags & EXE_NE_HEADER_SEGMENT_ENTRY_FLAGS_DATA) {
            printf("    * Skipping data segment\n");
            // TODO: Someday we should hexdump the data segment and point out where relocations affect it, imports, exports, etc.
        }
        else {
            do {
                uint32_t ofs = (uint32_t)(dec_read - dec_buffer) + current_offset_minus_buffer() - segment_ofs;
                uint32_t ip = ofs + entry_ip - dec_ofs;
                unsigned char reloc_ann = 0;
                unsigned char dosek = 0;
                size_t inslen;

                while (labeli < dec_label_count) {
                    label = dec_label + labeli;
                    if (label->seg_v != dec_cs) {
                        labeli++;
                        continue;
                    }
                    if (ip < label->ofs_v)
                        break;

                    labeli++;
                    ip = label->ofs_v;
                    dec_cs = label->seg_v;
                    ofs = segment_ofs + ip;

                    printf("Label '%s' at %04lx:%04lx @0x%08lx\n",
                            label->name ? label->name : "",
                            (unsigned long)label->seg_v,
                            (unsigned long)label->ofs_v,
                            (unsigned long)ofs);

                    label = dec_label + labeli;
                    dosek = 1;
                }

                if (dosek) {
                    reset_buffer();
                    current_offset = ofs;
                    if ((uint32_t)lseek(src_fd,current_offset,SEEK_SET) != current_offset)
                        return 1;
                }

                if (!refill()) break;

                minx86dec_set_buffer(&dec_st,dec_read,(int)(dec_end - dec_read));
                minx86dec_init_instruction(&dec_i);
                dec_st.ip_value = ip;
                minx86dec_decodeall(&dec_st,&dec_i);
                assert(dec_i.end >= dec_read);
                assert(dec_i.end <= (dec_buffer+sizeof(dec_buffer)));
                inslen = (size_t)(dec_i.end - dec_i.start);

                printf("%04lX:%04lX @0x%08lX ",(unsigned long)dec_cs,(unsigned long)dec_st.ip_value,(unsigned long)(dec_read - dec_buffer) + current_offset_minus_buffer());
                for (c=0,iptr=dec_i.start;iptr != dec_i.end;c++)
                    printf("%02X ",*iptr++);

                if (dec_i.rep != MX86_REP_NONE) {
                    for (;c < 6;c++)
                        printf("   ");

                    switch (dec_i.rep) {
                        case MX86_REPE:
                            printf("REP   ");
                            break;
                        case MX86_REPNE:
                            printf("REPNE ");
                            break;
                        default:
                            break;
                    };
                }
                else {
                    for (;c < 8;c++)
                        printf("   ");
                }
                printf("%-8s ",opcode_string[dec_i.opcode]);

                if (!reloc_ann) {
                    for (c=0;c < dec_i.argc;) {
                        minx86dec_regprint(&dec_i.argv[c],arg_c);
                        printf("%s",arg_c);
                        if (++c < dec_i.argc) printf(",");
                    }
                }
                if (dec_i.lock) printf("  ; LOCK#");
                printf("\n");

                dec_read = dec_i.end;
            } while(1);
        }
    }

    exe_ne_header_imported_name_table_free(&ne_imported_name_table);
    exe_ne_header_entry_table_table_free(&ne_entry_table);
    exe_ne_header_name_entry_table_free(&ne_nonresname);
    exe_ne_header_name_entry_table_free(&ne_resname);
    exe_ne_header_resource_table_free(&ne_resources);
    exe_ne_header_segment_table_free(&ne_segments);
    close(src_fd);
	return 0;
}
