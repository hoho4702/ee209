#include"signal.h"
#include<stdlib.h>
#include<stdio.h>
#include<unistd.h>
static int within_5=0,sigquit_cnt=0;
void sigquit_handler(int sig){
  if(within_5==0){
    sigquit_cnt=0;
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
  }
  sigquit_cnt++;
  if(within_5==1 && sigquit_cnt>=2)exit(0);
  within_5=1;
  alarm(5);
}

void sigalarm_handler(int sig){
  within_5=0;
}