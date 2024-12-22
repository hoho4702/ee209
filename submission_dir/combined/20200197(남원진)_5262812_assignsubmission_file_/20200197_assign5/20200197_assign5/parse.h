#ifndef _PARSE_H_
#define _PARSE_H_

enum {first, mid, last};
struct args {
    char **cmd;
    char *infile;
    char *outfile;
    struct args *pipe_dest;
    int bg;
    int locpipe;
    // the project need not to implement (cmd) & (cmd).
    // struct args *bg_next;
};

struct args *parser();
void printparse();
void cleanparse();

#endif
