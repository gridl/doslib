
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#ifndef O_BINARY
#define O_BINARY (0)
#endif

const char *omf_rectype_to_str_long(unsigned char rt) {
    switch (rt) {
        case 0x80:  return "Translator Header Record";
        case 0x82:  return "Library Module Header Record";
        case 0x88:  return "Comment Record";
        case 0x8A:  return "Module End Record";
        case 0x8B:  return "Module End Record (32-bit)";
        case 0x8C:  return "External Names Definition Record";
        case 0x90:  return "Public Names Definition Record";
        case 0x91:  return "Public Names Definition Record (32-bit)";
        case 0x94:  return "Line Numbers Record";
        case 0x95:  return "Line Numbers Record (32-bit)";
        case 0x96:  return "List of Names Record";
        case 0x98:  return "Segment Definition Record";
        case 0x99:  return "Segment Definition Record (32-bit)";
        case 0x9A:  return "Group Definition Record";
        case 0x9C:  return "Fixup Record";
        case 0x9D:  return "Fixup Record (32-bit)";
        case 0xA0:  return "Logical Enumerated Data Record";
        case 0xA1:  return "Logical Enumerated Data Record (32-bit)";
        case 0xA2:  return "Logical Iterated Data Record";
        case 0xA3:  return "Logical Iterated Data Record (32-bit)";
        case 0xB0:  return "Communal Names Definition Record";
        case 0xB2:  return "Backpatch Record";
        case 0xB3:  return "Backpatch Record (32-bit)";
        case 0xB4:  return "Local External Names Definition Record";
        case 0xB6:  return "Local Public Names Definition Record";
        case 0xB7:  return "Local Public Names Definition Record (32-bit)";
        case 0xB8:  return "Local Communal Names Definition Record";
        case 0xBC:  return "COMDAT External Names Definition Record";
        case 0xC2:  return "Initialized Communal Data Record";
        case 0xC3:  return "Initialized Communal Data Record (32-bit)";
        case 0xC4:  return "Symbol Line Numbers Record";
        case 0xC5:  return "Symbol Line Numbers Record (32-bit)";
        case 0xC6:  return "Alias Definition Record";
        case 0xC8:  return "Named Backpatch Record";
        case 0xC9:  return "Named Backpatch Record (32-bit)";
        case 0xCA:  return "Local Logical Names Definition Record";
        case 0xCC:  return "OMF Version Number Record";
        case 0xCE:  return "Vendor-specific OMF Extension Record";
        case 0xF0:  return "Library Header Record";
        case 0xF1:  return "Library End Record";
        default:    break;
    }

    return "?";
}

const char *omf_rectype_to_str(unsigned char rt) {
    switch (rt) {
        case 0x80:  return "THEADR";
        case 0x82:  return "LHEADR";
        case 0x88:  return "COMENT";
        case 0x8A:  return "MODEND";
        case 0x8B:  return "MODEND32";
        case 0x8C:  return "EXTDEF";
        case 0x90:  return "PUBDEF";
        case 0x91:  return "PUBDEF32";
        case 0x94:  return "LINNUM";
        case 0x95:  return "LINNUM32";
        case 0x96:  return "LNAMES";
        case 0x98:  return "SEGDEF";
        case 0x99:  return "SEGDEF32";
        case 0x9A:  return "GRPDEF";
        case 0x9C:  return "FIXUPP";
        case 0x9D:  return "FIXUPP32";
        case 0xA0:  return "LEDATA";
        case 0xA1:  return "LEDATA32";
        case 0xA2:  return "LIDATA";
        case 0xA3:  return "LIDATA32";
        case 0xB0:  return "COMDEF";
        case 0xB2:  return "BAKPAT";
        case 0xB3:  return "BAKPAT32";
        case 0xB4:  return "LEXTDEF";
        case 0xB6:  return "LPUBDEF";
        case 0xB7:  return "LPUBDEF32";
        case 0xB8:  return "LCOMDEF";
        case 0xBC:  return "CEXTDEF";
        case 0xC2:  return "COMDAT";
        case 0xC3:  return "COMDAT32";
        case 0xC4:  return "LINSYM";
        case 0xC5:  return "LINSYM32";
        case 0xC6:  return "ALIAS";
        case 0xC8:  return "NBKPAT";
        case 0xC9:  return "NBKPAT32";
        case 0xCA:  return "LLNAMES";
        case 0xCC:  return "VERNUM";
        case 0xCE:  return "VENDEXT";
        case 0xF0:  return "LIBHEAD"; // I made up this name
        case 0xF1:  return "LIBEND"; // I also made up this name
        default:    break;
    }

    return "?";
}

static char                 tempstr[257];
static unsigned char        tempstr_len;

static unsigned char        omf_record[16384];
static unsigned char        omf_rectype = 0;
static unsigned long        omf_recoffs = 0;
static unsigned int         omf_reclen = 0; // NTS: Does NOT include leading checksum byte
static unsigned int         omf_recpos = 0; // where we are parsing

static inline unsigned int omfrec_eof(void) {
    return (omf_recpos >= omf_reclen);
}

static inline unsigned int omfrec_avail(void) {
    return (omf_reclen - omf_recpos);
}

void omfrec_end(void) {
    omf_recpos = omf_reclen;
}

static inline unsigned char omfrec_gb(void) {
    return omf_record[omf_recpos++];
}

static inline unsigned int omfrec_gw(void) {
    unsigned int v = *((unsigned short*)(omf_record+omf_recpos));
    omf_recpos += 2;
    return v;
}

static inline unsigned long omfrec_gd(void) {
    unsigned long v = *((uint32_t*)(omf_record+omf_recpos));
    omf_recpos += 4;
    return v;
}

void omfrec_read(char *dst,unsigned int len) {
    if (len > 0) {
        memcpy(dst,omf_record+omf_recpos,len);
        omf_recpos += len;
    }
}

void omfrec_checkrange(void) {
    if (omf_recpos > omf_reclen) {
        fprintf(stderr,"ERROR: Overread occurred\n");
        abort();
    }
}

static char*                in_file = NULL;   

static void help(void) {
    fprintf(stderr,"omfdump [options]\n");
    fprintf(stderr,"  -i <file>    OMF file to dump\n");
}

static int omf_lib_next_block(int fd,unsigned long checkofs) {
    unsigned long endoff;

    if (lseek(fd,checkofs,SEEK_SET) != checkofs)
        return 0;

    /* "Libraries under MS-DOS are always multiples of 512-byte blocks" */
    checkofs = (checkofs + 0x1FFUL) & (~0x1FFUL);
    endoff = lseek(fd,0,SEEK_END);
    if (checkofs > endoff)
        return 0;

    if (lseek(fd,checkofs,SEEK_SET) != checkofs)
        return 0;

    return 1;
}

static int read_omf_record(int fd) {
    unsigned char sum = 0;
    unsigned int i;

    omf_recoffs = lseek(fd,0,SEEK_CUR);
    if (read(fd,omf_record,3) != 3)
        return 0;

    omf_rectype = omf_record[0];
    omf_reclen = *((uint16_t*)(omf_record+1));
    if (omf_reclen == 0)
        return 0;
    if (omf_reclen > sizeof(omf_record))
        return 0;

    for (i=0;i < 3;i++)
        sum += omf_record[i];

    if ((unsigned int)read(fd,omf_record,omf_reclen) != omf_reclen)
        return 0;

    if (omf_record[omf_reclen-1]/* checksum */ != 0) {
        for (i=0;i < omf_reclen;i++)
            sum += omf_record[i];

        if (sum != 0) {
            fprintf(stderr,"* checksum failed sum=0x%02x\n",sum);
            return 0;
        }
    }
    omf_recpos=0;
    omf_reclen--; // exclude checksum byte
    return 1;
}

unsigned int omfrec_get_remstr(char * const dst,const size_t dstmax) {
    unsigned int len;

    if (dstmax == 0)
        return 0;

    dst[0] = 0;
    if (omfrec_eof())
        return 0;

    len = omfrec_avail();
    if (len == 0 || len > 255)
        return 0;

    omfrec_read(dst,len);
    dst[len] = 0;
    return len;
}

unsigned int omfrec_get_lenstr(char * const dst,const size_t dstmax) {
    unsigned char len;

    if (dstmax == 0)
        return 0;

    dst[0] = 0;
    if (omfrec_eof())
        return 0;

    len = omfrec_gb();
    if (len > omfrec_avail() || (len+1/*NUL*/) > dstmax) {
        omfrec_end();
        return 0;
    }

    if (len > 0) {
        omfrec_read(dst,len);
        dst[len] = 0;
    }

    return len;
}

void dump_THEADR(void) {
    /* omf_record+0: length of the string */
    /* omf_record+1: ASCII string */
    omfrec_get_lenstr(tempstr,sizeof(tempstr));
    printf("    Name of the object: \"%s\"\n",tempstr);
}

void dump_LHEADR(void) {
    /* omf_record+0: length of the string */
    /* omf_record+1: ASCII string */
    omfrec_get_lenstr(tempstr,sizeof(tempstr));
    printf("    Name of the object in library: \"%s\"\n",tempstr);
}

const char *omfrec_COMENT_cclass_to_str(unsigned char cc) {
    switch (cc) {
        case 0x00:  return "Translator";
        case 0x01:  return "Intel copyright";
        case 0x81:  return "Library specifier";
        case 0x9C:  return "MS-DOS version";
        case 0x9D:  return "Memory model";
        case 0x9E:  return "DOSSEG";
        case 0x9F:  return "Default library search name";
        case 0xA0:  return "OMF extension";
        case 0xA1:  return "New OMF extension";
        case 0xA2:  return "Link Pass Separator";
        case 0xA3:  return "Library module comment record";
        case 0xA4:  return "Executable string";
        case 0xA6:  return "Incremental compilation error";
        case 0xA7:  return "No segment padding";
        case 0xA8:  return "Weak extern record";
        case 0xA9:  return "Lazy extern record";
        case 0xDA:  return "Comment";
        case 0xDB:  return "Compiler (pragma)";
        case 0xDC:  return "Date (pragma)";
        case 0xDD:  return "Timestamp (pragma)";
        case 0xDF:  return "User (pragma)";
        case 0xE9:  return "Dependency file";
        default:    break;
    };

    return "?";
}

void dump_COMENT(void) {
    unsigned char ctype,cclass;

    /* omf_record+0: Comment Type
     * omf_record+1: Comment Class
     * omf_record+2: byte string until end of record */
    if (omfrec_avail() < 2) return;
    ctype = omfrec_gb();
    cclass = omfrec_gb();

    printf("    COMENT Type=0x%02x ( ",ctype);
    if (ctype & 0x80) printf("No-purge ");
    if (ctype & 0x40) printf("No-list ");
    printf(") class=0x%02x (%s): ",cclass,omfrec_COMENT_cclass_to_str(cclass));
    printf("\n");

    if (cclass == 0x9F) {
        /* the rest is the string (no length byte) */
        omfrec_get_remstr(tempstr,sizeof(tempstr));
        printf("        Library name: %s\n",tempstr);
    }
}

void dump_LNAMES(void) {
    /* string records, one after the other, until the end of the record */
    printf("    LNAMES:");

    while (!omfrec_eof()) {
        omfrec_get_lenstr(tempstr,sizeof(tempstr));
        printf(" \"%s\"",tempstr);
    }

    printf("\n");
}

void dump_EXTDEF(void) {
    unsigned int typidx;

    printf("    EXTDEF:\n");
    while (!omfrec_eof()) {
        omfrec_get_lenstr(tempstr,sizeof(tempstr));

        if (omfrec_avail() < 1) return;
        typidx = omfrec_gb();

        printf("        '%s' typidx=%u\n",tempstr,typidx);
    }
}

void dump_LEXTDEF(const unsigned char b32) {
    unsigned int typidx;

    printf("    LEXTDEF:\n");
    while (!omfrec_eof()) {
        omfrec_get_lenstr(tempstr,sizeof(tempstr));

        if (omfrec_avail() < 1) return;
        typidx = omfrec_gb();

        printf("        '%s' typidx=%u\n",tempstr,typidx);
    }
}

void dump_PUBDEF(const unsigned char b32) {
    unsigned int base_segment_index = 0;
    unsigned int base_group_index = 0;
    unsigned int base_frame = 0;
    unsigned long puboff;
    unsigned int typidx;

    if (omfrec_avail() < 2) return;
    base_group_index = omfrec_gb();
    base_segment_index = omfrec_gb();

    if (omfrec_avail() < 2) return;
    if (base_segment_index == 0)
        base_frame = omfrec_gw();

    printf("    PUBDEF%u: basegroupidx=%u basesegidx=%u baseframe=%u\n",
        b32?32:16,base_group_index,base_segment_index,base_frame);

    while (!omfrec_eof()) {
        omfrec_get_lenstr(tempstr,sizeof(tempstr));

        if (b32) {
            if (omfrec_avail() < (4+1)) break;
            puboff = omfrec_gd();
            typidx = omfrec_gb();
        }
        else {
            if (omfrec_avail() < (2+1)) break;
            puboff = omfrec_gw();
            typidx = omfrec_gb();
        }

        printf("        '%s' offset=%lu typeidx=%u\n",tempstr,puboff,typidx);
    }
}

void dump_LPUBDEF(const unsigned char b32) {
    unsigned int base_segment_index = 0;
    unsigned int base_group_index = 0;
    unsigned int base_frame = 0;
    unsigned long puboff;
    unsigned int typidx;

    if (omfrec_avail() < 2) return;
    base_group_index = omfrec_gb();
    base_segment_index = omfrec_gb();

    if (omfrec_avail() < 2) return;
    if (base_segment_index == 0)
        base_frame = omfrec_gw();

    printf("    LPUBDEF%u: basegroupidx=%u basesegidx=%u baseframe=%u\n",
        b32?32:16,base_group_index,base_segment_index,base_frame);

    while (!omfrec_eof()) {
        omfrec_get_lenstr(tempstr,sizeof(tempstr));

        if (b32) {
            if (omfrec_avail() < (4+1)) break;
            puboff = omfrec_gd();
            typidx = omfrec_gb();
        }
        else {
            if (omfrec_avail() < (2+1)) break;
            puboff = omfrec_gw();
            typidx = omfrec_gb();
        }

        printf("        '%s' offset=%lu typeidx=%u\n",tempstr,puboff,typidx);
    }
}

void dump_FIXUPP(const unsigned char b32) {
    unsigned char c;

    printf("    FIXUPP%u:\n",b32?32:16);

    /* whoo, dense structures */
    while (!omfrec_eof()) {
        c = omfrec_gb();

        if ((c&0xA0/*0x80+0x20*/) == 0x00/* 0 D 0 x x x x x */) {
            printf("        * error THREAD records not supported\n");
            break;
        }
        else if (c&0x80) {
            if (omfrec_eof()) break;

            {
                unsigned long target_disp;
                unsigned char fix_f,fix_frame,fix_t,fix_p,fix_target;
                unsigned char fixdata;
                unsigned char segrel_fixup = (c >> 6) & 1;
                unsigned char locat = (c >> 2) & 0xF;
                unsigned int recoff = ((c & 3U) << 8U) + (unsigned int)omfrec_gb();

                printf("        FIXUP %s loctofix=",segrel_fixup?"SEG-RELATIVE":"SELF-RELATIVE");
                switch (locat) {
                    case 0: printf("LOBYTE"); break;
                    case 1: printf("16BIT-OFFSET"); break;
                    case 2: printf("16BIT-SEGBASE"); break;
                    case 3: printf("16SEG:16OFS-FAR-POINTER"); break;
                    case 4: printf("HIBYTE"); break;
                    case 5: printf("16BIT-LOADER-OFFSET"); break;
                    case 9: printf("32BIT-OFFSET"); break;
                    case 11:printf("16SEG:32OFS-FAR-POINTER"); break;
                    case 13:printf("32BIT-LOADER-OFFSET"); break;
                };
                printf(" datarecofs=%lu",recoff);
                printf("\n");

                // FIXME: Is "Fix Data" conditional? If so, when does it happen??
                //        The OMF spec doesn't say! This is a part of the spec
                //        where the denseness and conditional nature is VERY
                //        problematic because the OMF spec is very bad at explaining
                //        WHEN these conditional fields appear.
                if (omfrec_eof()) break;
                fixdata = omfrec_gb();

                fix_f = (fixdata >> 7) & 1;     /* [7:7] */
                fix_frame = (fixdata >> 4) & 7; /* [6:4] */
                fix_t = (fixdata >> 3) & 1;     /* [3:3] */
                fix_p = (fixdata >> 2) & 1;     /* [2:2] */
                fix_target = (fixdata & 3);     /* [1:0] */

                printf("            ");
                if (fix_f)
                    printf("F=1 frame=%u",fix_frame);
                else
                    printf("F=0 framemethod=%u",fix_frame);

                if (!fix_f) {
                    // again, if the OMF spec doesn't explain this and I have to read VirtualBox source code
                    // to get this that says how reliable your spec is.
                    switch (fix_frame) {
                        case 0: // F0: segment
                            printf(" F0:frame=segment");
                            break;
                        case 1: // F1: group
                            printf(" F1:frame=group");
                            break;
                        case 2: // F2: external symbol
                            printf(" F2:frame=external-symbol");
                            break;
                        case 4: // F4: frame = source
                            printf(" F4:frame=source");
                            break;
                        case 5: // F5: frame = target
                            printf(" F5:frame=target");
                            break;
                    }
                }

                if (!fix_f && fix_frame < 4) {
                    if (omfrec_eof()) break;

                    c = omfrec_gb();
                    printf(" framedatum=%u",c);
                }

                /* FIXME: WHEN is the Target Datum field prsent???? This is a shitty guess! The OMF spec doesn't say! */
                // NTS: To the people who wrote the OMF spec: your doc is confusing. The fact I had to read VirtualBox
                //      source code for clarification means your spec needs clarification.
                if (fix_t) {
                    printf(" target=%u",fix_target);
                }
                else {
                    if (omfrec_eof()) break;

                    switch (fix_target) {
                        case 0: // T0/T4: Target = segment
                            printf(" T0:target=segment");
                            break;
                        case 1: // T1/T5: Target = segment group
                            printf(" T0:target=segment-group");
                            break;
                        case 2: // T2/T6: Target = external symbol
                            printf(" T0:target=extern-sym");
                            break;
                    };

                    c = omfrec_gb();
                    printf(" targetdatum=%u",c);
                }

                if (!fix_p) {
                    if (b32) {
                        if (omfrec_avail() < 4) break;
                        target_disp = omfrec_gd();
                    }
                    else {
                        if (omfrec_avail() < 4) break;
                        target_disp = omfrec_gw();
                    }

                    printf(" targetdisp=%lu",target_disp);
                }

                printf("\n");
            }
        }
        else {
            printf("        * error unknown record\n");
            break;
        }
    }
}

void dump_GRPDEF(const unsigned char b32) {
    unsigned int grpnamidx;
    unsigned char index;
    unsigned char segdefidx;

    if (omfrec_eof()) return;
    grpnamidx = omfrec_gb();
    printf("    GRPDEF nameidx=%u:\n",grpnamidx);

    while (!omfrec_eof()) {
        index = omfrec_gb();

        if (index == 0xFF) {
            /* segment def index */
            segdefidx = omfrec_gb();
            printf("        SEGDEF index=%u\n",segdefidx);
        }
        else {
            printf("* Whoops, don't know how to handle type 0x%02x\n",index);
            break;
        }
    }
}

void dump_SEGDEF(const unsigned char b32) {
    unsigned char align_f;
    unsigned char comb_f;
    unsigned char use32;
    unsigned char big;
    unsigned short frame_number;
    unsigned char offset;
    unsigned long seg_length;
    unsigned short segnamidx;
    unsigned short classnamidx;
    unsigned short ovlnamidx;
    unsigned char c;

    if (omfrec_eof()) return;
    c = omfrec_gb();
    align_f = (c >> 5) & 7; /* [7:5] = alignment code */
    comb_f = (c >> 2) & 7;  /* [4:2] = combination code */
    big = (c >> 1) & 1;     /* [1:1] = "BIG" meaning the segment is 64K or 4GB exactly and seg length == 0 */
    use32 = (c >> 0) & 1;   /* [0:0] = if set, 32-bit code/data  if clear, 16-bit code/data */

    if (align_f == 0) {
        if (omfrec_avail() < (2+1)) return;
        frame_number = omfrec_gw();
        offset = omfrec_gb();
    }

    if (b32) {
        if (omfrec_avail() < (4+1+1+1)) return;
        seg_length = omfrec_gd();
        segnamidx = omfrec_gb();
        classnamidx = omfrec_gb();
        ovlnamidx = omfrec_gb();
    }
    else {
        if (omfrec_avail() < (2+1+1+1)) return;
        seg_length = omfrec_gw();
        segnamidx = omfrec_gb();
        classnamidx = omfrec_gb();
        ovlnamidx = omfrec_gb();
    }

    printf("    SEGDEF%u: USE%u",b32?32:16,use32?32:16);
    if (big && seg_length == 0) printf(" length=%s(max)",b32?"4GB":"64KB");
    else printf(" length=%lu",seg_length);
    printf(" nameidx=%u classidx=%u ovlidx=%u",
        segnamidx,classnamidx,ovlnamidx);
    printf("\n");

    switch (align_f) {
        case 0:
            printf("        ABSOLUTE frameno=%u offset=%u\n",frame_number,offset);
            break;
        case 1:
            printf("        RELOCATABLE, BYTE ALIGNMENT\n");
            break;
        case 2:
            printf("        RELOCATABLE, WORD (16-bit) ALIGNMENT\n");
            break;
        case 3:
            printf("        RELOCATABLE, PARAGRAPH (16-byte) ALIGNMENT\n");
            break;
        case 4:
            printf("        RELOCATABLE, PAGE ALIGNMENT\n");
            break;
        case 5:
            printf("        RELOCATABLE, DWORD (32-bit) ALIGNMENT\n");
            break;
    };

    switch (comb_f) {
        case 0:
            printf("        PRIVATE, do not combine with any other segment\n");
            break;
        case 2:
        case 4:
        case 7:
            printf("        PUBLIC, combine by appending at aligned offset\n");
            break;
        case 5:
            printf("        STACK, combine by appending at aligned offset\n");
            break;
        case 6:
            printf("        COMMON, combine by overlaying using max size\n");
            break;
    };
}

void dump_LEDATA(const unsigned char b32) {
    unsigned long enum_data_offset,doh;
    unsigned int segment_index;
    unsigned int len,i,colo;
    unsigned char tmp[16];

    if (b32) {
        if (omfrec_avail() < (1+4)) return;
        segment_index = omfrec_gb();
        enum_data_offset = omfrec_gd();
    }
    else {
        if (omfrec_avail() < (1+2)) return;
        segment_index = omfrec_gb();
        enum_data_offset = omfrec_gw();
    }

    printf("    LEDATA%u: segidx=%u data_offset=%lu\n",b32?32:16,segment_index,enum_data_offset);

    doh = enum_data_offset;
    while (!omfrec_eof()) {
        len = omfrec_avail();
        colo = (unsigned int)(doh&0xFUL);
        if (len > (16-colo)) len = (16-colo);
        omfrec_read((char*)tmp+colo,len);

        printf("    @0x%08lx: ",doh);
        for (i=0;i < colo;i++) printf("   ");
        for (   ;i < (colo+len);i++) printf("%02X ",tmp[i]);
        for (   ;i <  16;i++) printf("   ");
        printf("  ");
        for (i=0;i < colo;i++) printf(" ");
        for (   ;i < (colo+len);i++) {
            if (tmp[i] < 32 || tmp[i] >= 127)
                printf(".");
            else
                printf("%c",tmp[i]);
        }
        printf("\n");

        doh += (unsigned long)len;
    }
}

void dump_LIDATA_indent(unsigned int indent) {
    while (indent-- > 0) printf("    ");
}

int dump_LIDATA_datablock(const unsigned char b32,const unsigned int indent,unsigned long *doh) {
    unsigned long repeat_count;
    unsigned short block_count;

    if (b32) {
        if (omfrec_avail() < (4+2)) return 0;
        repeat_count = omfrec_gd();
        block_count = omfrec_gw();
    }
    else {
        if (omfrec_avail() < (2+2)) return 0;
        repeat_count = omfrec_gw();
        block_count = omfrec_gw();
    }

    dump_LIDATA_indent(indent);
    if (block_count == 0) {
        unsigned long repeat_iter;
        unsigned char rowstart=0;
        unsigned char count;
        unsigned char col;
        unsigned int i,j;
        unsigned char c;
        char row[16];

        if (omfrec_eof()) return 0;
        count = omfrec_gb();

        printf("<content len=%u x %lu>:\n",count,repeat_count);

        /* read into memory */
        if (count != 0) {
            if (omfrec_avail() < count) return 0;
            omfrec_read(tempstr,count);
        }

        for (repeat_iter=0;repeat_iter < repeat_count;repeat_iter++) {
            for (i=0,col=0;i < count;) {
                if (i == 0 || col == 0) {
                    j = (unsigned char)((*doh) & 0xFUL);
                    dump_LIDATA_indent(indent+1);
                    printf("@0x%08lx: ",*doh);
                    while (col < j) {
                        printf("   ");
                        col++;
                    }
                    rowstart = col;
                }

                row[col] = tempstr[i];
                printf("%02X ",row[col]);
                (*doh)++;
                col++;
                i++;

                if (col >= 16 || i == count) {
                    j = col;
                    while (j < 16) {
                        printf("   ");
                        j++;
                    }

                    j = 0;
                    while (j < rowstart) {
                        printf(" ");
                        j++;
                    }
                    while (j < col) {
                        c = (unsigned char)row[j];
                        j++;

                        if (c >= 32 && c < 127)
                            printf("%c",c);
                        else
                            printf(".");
                    }
                    printf("\n");

                    rowstart = 0;
                    col = 0;
                }
            }
        }
    }
    else {
        /* WARNING: untested! */
        printf("<block x %lu>:\n",repeat_count);
        return dump_LIDATA_datablock(b32,indent+1,doh);
    }

    return 1;
}

void dump_LIDATA(const unsigned char b32) {
    unsigned long enum_data_offset,doh;
    unsigned int segment_index;
    unsigned int len,i,colo;
    unsigned char tmp[16];

    if (b32) {
        if (omfrec_avail() < (1+4)) return;
        segment_index = omfrec_gb();
        enum_data_offset = omfrec_gd();
    }
    else {
        if (omfrec_avail() < (1+2)) return;
        segment_index = omfrec_gb();
        enum_data_offset = omfrec_gw();
    }

    printf("    LIDATA%u: segidx=%u data_offset=%lu\n",b32?32:16,segment_index,enum_data_offset);

    doh = enum_data_offset;
    dump_LIDATA_datablock(b32,2,&doh);
}

int main(int argc,char **argv) {
    unsigned char lasttype;
    unsigned long lastofs;
    unsigned int lastlen;
    int i,fd;
    char *a;

    for (i=1;i < argc;) {
        a = argv[i++];

        if (*a == '-') {
            do { a++; } while (*a == '-');

            if (!strcmp(a,"i")) {
                in_file = argv[i++];
                if (in_file == NULL) return 1;
            }
            else {
                help();
                return 1;
            }
        }
        else {
            fprintf(stderr,"Unexpected arg %s\n",a);
            return 1;
        }
    }

    if (in_file == NULL) {
        help();
        return 1;
    }

    fd = open(in_file,O_RDONLY|O_BINARY);
    if (fd < 0) {
        fprintf(stderr,"Failed to open input file %s\n",strerror(errno));
        return 1;
    }

    do {
        while (read_omf_record(fd)) {
            lastlen = omf_reclen;
            lastofs = omf_recoffs;
            lasttype = omf_rectype;
            printf("OMF record type=0x%02x (%s: %s) length=%u offset=%lu\n",
                    omf_rectype,
                    omf_rectype_to_str(omf_rectype),
                    omf_rectype_to_str_long(omf_rectype),
                    omf_reclen,
                    omf_recoffs);

            switch (omf_rectype) {
                case 0x80:/* THEADR */
                    dump_THEADR();
                    break;
                case 0x82:/* LHEADR */
                    dump_LHEADR();
                    break;
                case 0x88:/* COMENT */
                    dump_COMENT();
                    break;
                case 0x8C:/* EXTDEF */
                    dump_EXTDEF();
                    break;
                case 0x90:/* PUBDEF */
                case 0x91:/* PUBDEF32 */
                    dump_PUBDEF(omf_rectype&1);
                    break;
                case 0x96:/* LNAMES */
                    dump_LNAMES();
                    break;
                case 0x98:/* SEGDEF */
                case 0x99:/* SEGDEF32 */
                    dump_SEGDEF(omf_rectype&1);
                    break;
                case 0x9A:/* GRPDEF */
                case 0x9B:/* GRPDEF32 */
                    dump_GRPDEF(omf_rectype&1);
                    break;
                case 0x9C:/* FIXUPP */
                case 0x9D:/* FIXUPP32 */
                    dump_FIXUPP(omf_rectype&1);
                    break;
                case 0xA0:/* LEDATA */
                case 0xA1:/* LEDATA32 */
                    dump_LEDATA(omf_rectype&1);
                    break;
                case 0xA2:/* LIDATA */
                case 0xA3:/* LIDATA32 */
                    dump_LIDATA(omf_rectype&1);
                    break;
                case 0xB4:/* LEXTDEF */
                case 0xB5:/* LEXTDEF32 */
                    dump_LEXTDEF(omf_rectype&1);
                    break;
                case 0xB6:/* LPUBDEF */
                case 0xB7:/* LPUBDEF32 */
                    dump_LPUBDEF(omf_rectype&1);
                    break;
            }

            omfrec_checkrange();
        }

        if (lasttype == 0x8A || lasttype == 0x8B) {
            /* if the last record as MODEND, then advance to the next block in the file
             * and try to read again (.LIB parsing case). clear lasttype to avoid infinite loops. */
            lasttype = 0;
            if (!omf_lib_next_block(fd,lastofs+3UL+(unsigned long)lastlen))
                break;

            printf("\n* Another .LIB object archive begins...\n\n");
        }
        else {
            break;
        }
    } while (1);

    close(fd);
    return 0;
}
