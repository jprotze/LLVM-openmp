//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//


#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===========================================================================
// Bring in the static string table and the enumerations for indexing into
// it.
// ===========================================================================

#include "liboffload_msg.h"

# define DYNART_STDERR_PUTS(__message_text__) fputs((__message_text__),stderr)

// ===========================================================================
// Now the code for accessing the message catalogs
// ===========================================================================


    void write_message(FILE * file, int msgCode, va_list args_p) {
        va_list args;
        char buf[1024];

        va_copy(args, args_p);
        buf[0] = '\n';
        vsnprintf(buf + 1, sizeof(buf) - 2,
            MESSAGE_TABLE_NAME[ msgCode ], args);
        strcat(buf, "\n");
        va_end(args);
        fputs(buf, file);
        fflush(file);
    }

    char const *offload_get_message_str(int msgCode) {
        return MESSAGE_TABLE_NAME[ msgCode ];
    }
