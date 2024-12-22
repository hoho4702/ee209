/*--------------------------------------------------------------------*/
/* Name: Changyong Eom                                                */
/* Student ID: 20190383                                               */
/* File description:                                                  */
/* Command execution Helper function header file                      */
/* for redirection, piplining command and so on...                    */
/*--------------------------------------------------------------------*/

#ifndef EXEC_H
#define EXEC_H

#include "dynarray.h"
#include "util.h"
#include "token.h"

void procRedir(DynArray_T oTokens);
/* Handle redirection and its parameter is dynarray.
   No return value, but change the stdin and stdout
   into given file.
*/

void procPipe(DynArray_T oTokens);
/* Handle piplining and its parameter is dynarray.
   No return value, but change the stdin and stdout
   into given file and handle mutiple command
*/

void execCommand(DynArray_T oTokens);
/* Execute not built-in commands. and its parameter 
   is dynarray.Redirection & piplining can occurs 
   here. No return value, but can change stdin & 
   stdout for these command utilities.
*/

void execBuiltIn(DynArray_T oTokens);
/* Execute four built-in commands. and its 
   parameter is dynarray. Depending on the commands, 
   it execute required operation. No return value. 
   No change to stdin & stdout.
*/

#endif