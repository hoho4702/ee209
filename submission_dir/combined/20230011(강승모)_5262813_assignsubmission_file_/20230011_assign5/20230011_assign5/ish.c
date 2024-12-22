#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include "dynarray.h"
#include "lexsyn.h"
#include "token.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

static time_t g_tQuitTime = 0;  
static int    g_bFirstQuit = 1; 

static void shellHelper(const char *inLine);
static void processIshrc(void);
static void sigintHandler(int signo);
static void sigquitHandler(int signo);

static int  buildPipeline(DynArray_T oTokens, DynArray_T oCommands[]);
static void doPipeline(DynArray_T oCommands[], int nCmds);
static int  handleRedirection(DynArray_T oTokens);
static int  doBuiltin(enum BuiltinType btype, DynArray_T oTokens);
static void doExternalCmd(DynArray_T oTokens);



static void sigintHandler(int signo)
{
    (void)signo;

}


static void sigquitHandler(int signo)
{
    (void)signo;
    if (g_bFirstQuit) {
        fprintf(stdout, "Type Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        g_tQuitTime = time(NULL);
        g_bFirstQuit = 0;
    }
    else {
        time_t now = time(NULL);
        if (difftime(now, g_tQuitTime) <= 5.0) {
            exit(EXIT_SUCCESS);
        } else {
            g_bFirstQuit = 1;
        }
    }
}

static void processIshrc(void)
{
    char *homeDir = getenv("HOME");
    if (homeDir == NULL) {
        return;
    }

    char rcPath[1024];
    snprintf(rcPath, sizeof(rcPath), "%s/.ishrc", homeDir);

    FILE *fp = fopen(rcPath, "r");
    if (!fp) {
        /* If .ishrc doesn't exist or is unreadable, ignore */
        return;
    }

    char acLine[MAX_LINE_SIZE + 2];
    while (fgets(acLine, MAX_LINE_SIZE, fp) != NULL) {
        /* Print line with prompt before execution */
        fprintf(stdout, "%% %s", acLine); 
        fflush(stdout);

        shellHelper(acLine);
    }
    fclose(fp);
}


static int buildPipeline(DynArray_T oTokens, DynArray_T oCommands[])
{
    int i, startIdx = 0;
    int cmdCount = 0;

    int length = DynArray_getLength(oTokens);
    for (i = 0; i < length; i++) {
        struct Token *t = DynArray_get(oTokens, i);

        if (t->eType == TOKEN_PIPE) {
            if (i == startIdx) {
                return -1; 
            }
            DynArray_T cmdTokens = DynArray_new(0);
            if (!cmdTokens) return -1;

            {
                int j;
                for (j = startIdx; j < i; j++) {
                    DynArray_add(cmdTokens, DynArray_get(oTokens, j));
                }
            }
            oCommands[cmdCount++] = cmdTokens;
            startIdx = i + 1;
        }
    }

    if (startIdx >= length) {
        return -1;
    }
    {
        DynArray_T cmdTokens = DynArray_new(0);
        if (!cmdTokens) return -1;
        int j;
        for (j = startIdx; j < length; j++) {
            DynArray_add(cmdTokens, DynArray_get(oTokens, j));
        }
        oCommands[cmdCount++] = cmdTokens;
    }

    return cmdCount;
}


static void doPipeline(DynArray_T oCommands[], int nCmds)
{
    int i;
    int (*pipes)[2] = NULL; 

    if (nCmds > 1) {
        pipes = calloc((size_t)(nCmds - 1), sizeof(int[2]));
        if (!pipes) {
            errorPrint("Cannot allocate memory for pipes", FPRINTF);
            return;
        }
        for (i = 0; i < nCmds - 1; i++) {
            if (pipe(pipes[i]) < 0) {
                errorPrint("pipe() error", PERROR);
                free(pipes);
                return;
            }
        }
    }

    for (i = 0; i < nCmds; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            errorPrint("fork() error", PERROR);
            continue; 
        }
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);

            if (i > 0) {
                if (dup2(pipes[i-1][0], STDIN_FILENO) < 0) {
                    errorPrint("dup2 error", PERROR);
                    exit(EXIT_FAILURE);
                }
            }
            if (i < nCmds - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    errorPrint("dup2 error", PERROR);
                    exit(EXIT_FAILURE);
                }
            }

            {
                int closei, closemax = (nCmds - 1);
                for (closei = 0; closei < closemax; closei++) {
                    close(pipes[closei][0]);
                    close(pipes[closei][1]);
                }
            }

            if (handleRedirection(oCommands[i]) < 0) {
                exit(EXIT_FAILURE);
            }

            {
                int n = DynArray_getLength(oCommands[i]);
                char **argv = calloc(n + 1, sizeof(char*));
                if (!argv) {
                    errorPrint("Cannot allocate memory for argv", FPRINTF);
                    exit(EXIT_FAILURE);
                }
                {
                    int k, idx = 0;
                    for (k = 0; k < n; k++) {
                        struct Token *t = DynArray_get(oCommands[i], k);
                        if (t->eType == TOKEN_WORD) {
                            argv[idx++] = t->pcValue;
                        }
                    }
                    argv[idx] = NULL;
                }

            execvp(argv[0], argv);
            errorPrint(argv[0], PERROR);
            free(argv);
            exit(EXIT_FAILURE);
            }
        }
    }

    if (nCmds > 1) {
        for (i = 0; i < nCmds - 1; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        free(pipes);
    }

    for (i = 0; i < nCmds; i++) {
        int status;
        wait(&status);
    }
}

static int handleRedirection(DynArray_T oTokens)
{
    int i = 0;
    while (i < DynArray_getLength(oTokens)) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_REDIN) {
            if (i+1 >= DynArray_getLength(oTokens)) {
                return -1;
            }
            struct Token *fnameTok = DynArray_get(oTokens, i+1);
            int fd = open(fnameTok->pcValue, O_RDONLY);
            if (fd < 0) {
                errorPrint(fnameTok->pcValue, PERROR);
                return -1;
            }
            if (dup2(fd, STDIN_FILENO) < 0) {
                errorPrint("dup2 error", PERROR);
                close(fd);
                return -1;
            }
            close(fd);
            DynArray_removeAt(oTokens, i+1);
            DynArray_removeAt(oTokens, i);
        }
        else if (t->eType == TOKEN_REDOUT) {
            if (i+1 >= DynArray_getLength(oTokens)) {
                return -1;
            }
            struct Token *fnameTok = DynArray_get(oTokens, i+1);
            int fd = open(fnameTok->pcValue, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd < 0) {
                errorPrint(fnameTok->pcValue, PERROR);
                return -1;
            }
            if (dup2(fd, STDOUT_FILENO) < 0) {
                errorPrint("dup2 error", PERROR);
                close(fd);
                return -1;
            }
            close(fd);
            DynArray_removeAt(oTokens, i+1);
            DynArray_removeAt(oTokens, i);
        }
        else {
            i++;
        }
    }
    return 0;
}


static int doBuiltin(enum BuiltinType btype, DynArray_T oTokens)
{
    int i;
    for (i = 1; i < DynArray_getLength(oTokens); i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) {
            errorPrint("Redirection with builtin command", FPRINTF);
            return 1;
        }
    }

    switch (btype) {
        case B_CD:
        {
            if (DynArray_getLength(oTokens) == 1) {
                char *home = getenv("HOME");
                if (!home) {
                    errorPrint("HOME not set", FPRINTF);
                    return 1;
                }
                if (chdir(home) != 0) {
                    errorPrint(home, PERROR);
                }
            }
            else {
                struct Token *dirToken = DynArray_get(oTokens, 1);
                if (chdir(dirToken->pcValue) != 0) {
                    errorPrint(dirToken->pcValue, PERROR);
                }
            }
            return 1;
        }
        case B_EXIT:
        {
            exit(EXIT_SUCCESS);
        }
        case B_SETENV:
        {
            if (DynArray_getLength(oTokens) == 1) {
                errorPrint("Usage: setenv var [value]", FPRINTF);
                return 1;
            }
            else {
                struct Token *varToken = DynArray_get(oTokens, 1);
                if (DynArray_getLength(oTokens) >= 3) {
                    struct Token *valToken = DynArray_get(oTokens, 2);
                    setenv(varToken->pcValue, valToken->pcValue, 1);
                }
                else {
                    setenv(varToken->pcValue, "", 1);
                }
            }
            return 1;
        }
        case B_USETENV:
        {
            if (DynArray_getLength(oTokens) == 1) {
                errorPrint("Usage: unsetenv var", FPRINTF);
                return 1;
            }
            else {
                struct Token *varToken = DynArray_get(oTokens, 1);
                unsetenv(varToken->pcValue);
            }
            return 1;
        }
        case B_FG:
        {
            errorPrint("fg is not implemented in this sample", FPRINTF);
            return 1;
        }
        case B_ALIAS:
        {
            errorPrint("alias is not implemented in this sample", FPRINTF);
            return 1;
        }
        default:
            return 0;
    }
}


static void doExternalCmd(DynArray_T oTokens)
{
    pid_t pid = fork();
    if (pid < 0) {
        errorPrint("fork() error", PERROR);
        return;
    }
    if (pid == 0) {
        /* Child process */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);

        if (handleRedirection(oTokens) < 0) {
            exit(EXIT_FAILURE);
        }

        {
            int n = DynArray_getLength(oTokens);
            char **argv = (char**)calloc(n + 1, sizeof(char*));
            if (!argv) {
                errorPrint("Cannot allocate memory for argv", FPRINTF);
                exit(EXIT_FAILURE);
            }

            {
                int i, idx = 0;
                for (i = 0; i < n; i++) {
                    struct Token *t = DynArray_get(oTokens, i);
                    if (t->eType == TOKEN_WORD) {
                        argv[idx++] = t->pcValue;
                    }
                }
                argv[idx] = NULL;
            }

            execvp(argv[0], argv);
            errorPrint(argv[0], PERROR);
            free(argv);
            exit(EXIT_FAILURE);
        }
    }
    else {
        /* Parent process */
        int status;
        waitpid(pid, &status, 0);
    }
}

static void shellHelper(const char *inLine)
{
    DynArray_T oTokens = DynArray_new(0);
    if (!oTokens) {
        errorPrint("Cannot allocate memory", FPRINTF);
        exit(EXIT_FAILURE);
    }

    enum LexResult lexcheck = lexLine(inLine, oTokens);
    switch (lexcheck) {
        case LEX_SUCCESS:
        {
            if (DynArray_getLength(oTokens) == 0) {
                DynArray_free(oTokens);
                return;
            }
            dumpLex(oTokens);

            enum SyntaxResult syncheck = syntaxCheck(oTokens);
            if (syncheck == SYN_SUCCESS) {
                int pipeCount = countPipe(oTokens);
                if (pipeCount == 0) {
                    enum BuiltinType btype = checkBuiltin(DynArray_get(oTokens, 0));
                    if (btype != NORMAL) {
                        (void)doBuiltin(btype, oTokens);
                    }
                    else {
                        doExternalCmd(oTokens);
                    }
                }
                else {
                    DynArray_T commands[64];
                    int nCmds = buildPipeline(oTokens, commands);
                    if (nCmds < 0) {
                        errorPrint("Missing command name", FPRINTF);
                    }
                    else {
                        doPipeline(commands, nCmds);
                        {
                            int i;
                            for (i = 0; i < nCmds; i++) {
                                DynArray_free(commands[i]);
                            }
                        }
                    }
                }
            }
            else if (syncheck == SYN_FAIL_NOCMD)
                errorPrint("Missing command name", FPRINTF);
            else if (syncheck == SYN_FAIL_MULTREDOUT)
                errorPrint("Multiple redirection of standard out", FPRINTF);
            else if (syncheck == SYN_FAIL_NODESTOUT)
                errorPrint("Standard output redirection without file name", FPRINTF);
            else if (syncheck == SYN_FAIL_MULTREDIN)
                errorPrint("Multiple redirection of standard input", FPRINTF);
            else if (syncheck == SYN_FAIL_NODESTIN)
                errorPrint("Standard input redirection without file name", FPRINTF);
            else if (syncheck == SYN_FAIL_INVALIDBG)
                errorPrint("Invalid use of background", FPRINTF);
            break;
        }
        case LEX_QERROR:
            errorPrint("Unmatched quote", FPRINTF);
            break;
        case LEX_NOMEM:
            errorPrint("Cannot allocate memory", FPRINTF);
            break;
        case LEX_LONG:
            errorPrint("Command is too large", FPRINTF);
            break;
        default:
            errorPrint("lexLine needs to be fixed", FPRINTF);
            exit(EXIT_FAILURE);
    }

    DynArray_map(oTokens, freeToken, NULL);
    DynArray_free(oTokens);
}


int main(int argc, char* argv[])
{
    (void)argc; 
    errorPrint(argv[0], SETUP);

    signal(SIGINT,  sigintHandler);
    signal(SIGQUIT, sigquitHandler);

    processIshrc();

    char acLine[MAX_LINE_SIZE + 2];
    while (1) {
        fprintf(stdout, "%% ");
        fflush(stdout);

        if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
            printf("\n");
            exit(EXIT_SUCCESS);
        }
        shellHelper(acLine);
    }
    return 0; 
}
