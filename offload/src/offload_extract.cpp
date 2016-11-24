/*
    Copyright (c) 2014-2015 Intel Corporation.  All Rights Reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

      * Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
      * Neither the name of Intel Corporation nor the names of its
        contributors may be used to endorse or promote products derived
        from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include "offload_extract.h"
#include "liboffload_error_codes.h"

#include <malloc.h>
#ifndef TARGET_WINNT
#include <alloca.h>
#include <elf.h>
#else
#include <windows.h>
#endif // TARGET_WINNT
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef HOST_WINNT
typedef Elf64_Ehdr      Elf_Ehdr;
typedef Elf64_Shdr      Elf_Shdr;
typedef Elf64_Sym       Elf_Sym;
#endif // HOST_WINNT

static const char* image_symbol_name[] = {
    "__offload_target_image",
    "__gfx_offload_target_image"
};

// Simple function that opens a file to see if it exists
static bool file_exists(const char * filename)
{
    FILE *      fp;
    if (filename != NULL) {
        fp = fopen(filename, "r");
        if (fp != NULL) {
            fclose(fp);
            return true;
        }
    }
    return false;
}

#ifndef HOST_WINNT
static bool get_elf_header(FILE * infile, Elf_Ehdr *ehdr)
{
    int           read_size;
    int           size_read;

    read_size = sizeof(Elf_Ehdr);
    size_read = fread((unsigned char *) ehdr, 1, read_size, infile);
    if (size_read != read_size) {
        return false;
    }
    return true;
}

static void disk_seek(FILE * infile, unsigned long loc)
{
    fseek(infile, loc, SEEK_SET);
}

static unsigned long get_value(unsigned char *p, int size)
{
    int i;
    unsigned long offset = 0;
    for (i = (size - 1); i >= 0; i--) {
        offset = (offset << 8) + p[i];
    }
    return offset;
}

static bool get_symtab_section(
    FILE * infile,
    Elf_Shdr *symtab,
    unsigned long soffset,
    int scount
)
{
    int           read_size;
    int           i;
    int           stype;
    int           size_read;

    disk_seek(infile, soffset);
    read_size = sizeof(Elf_Shdr);
    for (i = 0; i < scount; i++) {
        size_read = fread((unsigned char *) symtab, 1, read_size, infile);
        if (size_read != read_size) {
            return false;
        }
        stype = get_value((unsigned char *) &(symtab->sh_type), 4);
        if (stype == SHT_SYMTAB) {
            return true;
        }
    }
    return false;
}

static bool get_mic_str_index(
    FILE * infile,
    Elf_Shdr *symtab,
    unsigned long soffset,
    int *mic_str_index,
    const char *symbol_name)
{
    int           str_sec_id;
    int           read_size;
    int           size_read;
    int           j;
    int           str_length;
    unsigned long str_offset;
    unsigned long str_size;
    unsigned long k;
    Elf_Shdr      strtab;
    char*         buffer;

    // Get string section used by Symbol table
    str_sec_id = get_value((unsigned char *) &(symtab->sh_link), 4);
    disk_seek(infile, soffset + (sizeof(Elf_Shdr) * str_sec_id));
    read_size = sizeof(Elf_Shdr);
    size_read = fread((unsigned char *) &strtab, 1, read_size, infile);
    if (size_read != read_size) {
        return false;
    }

    // Find the string index representing the MIC image name.
    str_offset = get_value((unsigned char *) &strtab.sh_offset, 8);
    str_size = get_value((unsigned char *) &strtab.sh_size, 8);
    str_length = strlen(symbol_name);
    buffer = (char *) malloc(str_length + 2);
    if (buffer == NULL)
        LIBOFFLOAD_ERROR(c_malloc);
    disk_seek(infile, str_offset);
    k = 0;
    j = 0;
    while (k < str_size) {
        size_read = fread(buffer+j, 1, 1, infile);
        if (buffer[j] == '\0') {
            if (!strncmp(buffer, symbol_name, str_length)) {
                *mic_str_index = k - j;
                return true;
            }
            j = 0;
        }
        else {
            if (j >str_length) {
                while (buffer[0] != '\0') {
                    size_read = fread(buffer, 1, 1, infile);
                    k++;
                }
                j = 0;
            }
            else
                j++;
        }
        k++;
    }
    return false;
}

static bool get_mic_image_loc(
    FILE * infile,
    Elf_Shdr *symtab,
    int mic_str_index,
    unsigned long *mic_image_loc,
    unsigned long base_offset
)
{
    int           read_size;
    int           size_read;
    int           sym_entries;
    int           i;
    int           str_index;
    unsigned long sym_size;
    unsigned long sym_sec_size;
    unsigned long symoffset;
    Elf_Sym       sym_entry;

    // Size of Symbol table
    sym_sec_size = sizeof(Elf_Sym);

    // Total size in bytes the entries in Symbol table
    sym_size = get_value((unsigned char *) &(symtab->sh_size), 8);

    // Number of entries in symbol table
    sym_entries = sym_size / sym_sec_size;

    // Offset of the 1st symbol table entry
    symoffset = get_value((unsigned char *) &(symtab->sh_offset), 8);
    disk_seek(infile, symoffset);

    // Read each symbol and check if it is MIC image name
    read_size = sizeof(Elf_Sym);
    for (i=0; i<sym_entries; i++) {
        size_read = fread((unsigned char *) &sym_entry, 1, read_size, infile);
        str_index = get_value((unsigned char *) &sym_entry.st_name, 4);
        if (str_index == mic_str_index) {
             *mic_image_loc = get_value(
                     (unsigned char *) &sym_entry.st_value, 8) - base_offset;
             return true;
        }
    }

    return false;
}


// Check library that is passed in for fatness and existence.
//  Input:  fully qualified library (dir + name)
static bool lib_is_fat_and_valid(const char * library_name)
{
    Elf_Ehdr      ehdr;
    Elf_Shdr      symtab;
    unsigned long soffset;
    int           scount;
    int           mic_str_index;
    unsigned long mic_image_loc;
    unsigned long base_offset = 0;
    int           image_type;
    int           image_class;
    const char   *symbol_name = image_symbol_name[0];
    FILE         *infile;

    if (library_name == NULL || library_name[0] == '\0') {
        return false;
    }

    infile = fopen(library_name, "rb");
    if (infile == NULL) {
        // failed to open the file
        return false;
    }

    // get the elf header
    if (!get_elf_header(infile, &ehdr)) {
        return false;
    }

    image_class = ehdr.e_ident[4];

    // 32-bit images are not valid for MIC
    if (image_class == 1) {
        return false;
    }

    // Get section offset and the number of sections
    soffset = get_value((unsigned char *) &ehdr.e_shoff, 8);
    scount  = get_value((unsigned char *) &ehdr.e_shnum, 2);

    // get image type
    image_type = get_value((unsigned char *) &ehdr.e_type, 2);

    // We only care about .so files.  If we were to expand to other types of
    // files, that would start here testing other image types.
    if (image_type != ET_DYN) {
        return false;
    }

    // get symtab section
    if (!get_symtab_section(infile, &symtab, soffset, scount)) {
        return false;
    }

    // get the index of MIC string image from the string table
    if (!get_mic_str_index(infile, &symtab, soffset, &mic_str_index,
                           symbol_name))
    {
        return false;
    }

    // get the location of MIC image
    if (!get_mic_image_loc(infile, &symtab, mic_str_index,
                           &mic_image_loc, base_offset))
    {
        return false;
    }

    return true;
}
#endif // !HOST_WINNT

static char * lib_basename(const char * fullname) {
    char *    s1;
    char *    s2;
    int       dirlen;
    char *    cp_fullname;

    cp_fullname = (char*)malloc(strlen(fullname) + 1);
    if (cp_fullname == NULL)
      LIBOFFLOAD_ERROR(c_malloc);
    strcpy(cp_fullname, fullname);

    dirlen = strlen(cp_fullname);
    if (cp_fullname[dirlen - 1] == '/') {
        cp_fullname[dirlen - 1] = '\0';
    }

    s1 = strrchr(cp_fullname, '\\');
    s2 = strrchr(cp_fullname, '/');
    s1 = (s1 > s2) ? s1 : s2;

    if (s1 || (s1 = strrchr(cp_fullname, ':')))
        s1 += 1;
    else
        s1 = cp_fullname;

    return s1;
}

// Check to see if the library passed in which references the Host shared
// object is valid and is fat (host+offload).  We search LD_LIBRARY_PATH
// settings and return the full name including directory in the fullname
// parameter.
extern "C" bool offload_verify_library(const char * in_name, char ** fullname)
{
    bool   is_fat_and_valid = false;
#ifndef HOST_WINNT
    char * ld_library_path = NULL;
    char * ld_library_path_t = NULL;
    char * name = NULL;

    // make sure there is no leading directory to the file
    name = lib_basename(in_name);
    if (name == NULL || name[0] == '\0') {
        return false;
    }

    // resolve the name based on known LD_LIBRARY_PATH settings.
    ld_library_path_t = getenv("LD_LIBRARY_PATH");

    // Add ./ to the search paths
    if (ld_library_path_t) {
        ld_library_path = (char *)malloc(strlen(ld_library_path_t) + 5);
        if (ld_library_path == NULL)
            LIBOFFLOAD_ERROR(c_malloc);
        strcpy(ld_library_path, "./:");
        strcat(ld_library_path, ld_library_path_t);
    }
    else {
        ld_library_path = "./";
    }

    if (ld_library_path != NULL && *ld_library_path) {
        char * cur_lib_dir;
        cur_lib_dir = strtok(ld_library_path, ":");
        while (cur_lib_dir && !is_fat_and_valid) {
            char * t_fullname = NULL;
            int    lib_dir_len = 0;
            lib_dir_len = strlen(cur_lib_dir);

            // Make the full name
            t_fullname = (char*)malloc(strlen(name) + lib_dir_len + 2);
            if (t_fullname == NULL)
                LIBOFFLOAD_ERROR(c_malloc);
            strcpy(t_fullname, cur_lib_dir);
            if (lib_dir_len > 0 && cur_lib_dir[lib_dir_len-1] == '/') {
                strcat(t_fullname, name);
            }
            else {
                strcat(t_fullname, "/");
                strcat(t_fullname, name);
            }

            // check to see if this name exists and if it is valid
            if (file_exists(t_fullname) && lib_is_fat_and_valid(t_fullname)) {
                *fullname = t_fullname;
                is_fat_and_valid = true;
            }
            else {
                free(t_fullname);
            }
            cur_lib_dir = strtok(NULL, ":");
        }
    }
#else
    // For Windows, we just want to pass through and maintain existing
    // functionality
    is_fat_and_valid = TRUE;
#endif // !HOST_WINNT
    return is_fat_and_valid;
}

