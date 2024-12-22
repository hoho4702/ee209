/*--------------------------------------------------------------------*/
/* Name: Changyong Eom                                                */
/* Student ID: 20190383                                               */
/* File description:                                                  */
/* Shell Helper function header file                                  */
/* for initialization, termination and interactive op                 */
/*--------------------------------------------------------------------*/

#ifndef SHELL_H
#define SHELL_H

#include "dynarray.h"
#include "lexsyn.h"
#include "util.h"
#include "exec.h"

void shellHelper(const char *inLine);
/* Print a prompt, which is consisting of a percent
   sign followed by a space, to the standard output 
   stream. Read a line from the standard input stream. 
   Lexically analyze the line to form an array of 
   tokens. Syntactically analyze (i.e. parse) the 
   token array to form a command. Execute the command.
*/

void shellInitializer();
/* Read and interpret lines from the file .ishrc 
   when it is first lauched. This function look at 
   the .ishrc file in the HOME directory, and print 
   the commands from .ishrc with % sign and generate
   output that results from executing that command. 
   There is no parameter and return value.
*/

void shellUserInter();
/* Read and interpret lines from the user, not file,
   then generate output that results from executing
   that command. There is no parameter and return 
   value.
*/

#endif