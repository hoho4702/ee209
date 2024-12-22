#include <stdio.h>
#include <ctype.h>
int main(void){
    char ipch; //인풋으로 들어올 글자들 저장용
    int lines=0, words=0, chars=0; //새로운 줄 단어 문자 세는 용도
    int bl=1,li=1,blc=1; //블럭 주석, 라인 주석 확인과 블럭주석 닫힘 확인
    char pre_ch=' ';// 이전의 문자 추적용  
    int k;//오류 라인 확인용
    char idx[1024];//전처리용
    int j=0,i=0;//j 확인용 i 넣기용   
    while((ipch=getchar())!=EOF){ //표준입력을 받아 개수 세는 함수
        if(!bl){//블록 주석 닫힘 확인
            if((ipch=='/' && pre_ch=='*')&&li){
                bl=1;
                blc=1;
                chars--;
                ipch=' ';    
            }
        }
        if((ipch=='*' && pre_ch=='/') && li ){// 블록 주석 시작확인
            bl=0;
            blc=0;
        }
        if ((ipch=='/' && pre_ch=='/')&& bl ){ //라인주석확인
            li=0;
            chars--;
        }
        if (ipch=='\n'){//줄바꿈 확인
            li=1;
            lines++;
            if (!blc)chars++;   
        }
        if(li){//주석 내부가 아닐 때만 문자 수 새기
            if(bl){
                chars++; //문자 수 세기
            }
            if((isspace(pre_ch)&&!isspace(ipch))&&ipch!='/'&&blc)words++;
        }
        if(!bl || !li){// idx로 새로이 저장해 주석으로 시작하는 부분만 남는지 확인
            if(ipch=='\n'){
                idx[i]='\n';
            } 
            else{
                idx[i]=' ';
            }
        }
        else{
            idx[i]=ipch;
        }
        i++;
        pre_ch = ipch;
    }
    idx[i]='\0';
    if(blc){
        printf("%d %d %d\n",lines, words, chars);
    }
    else{   
        while(j){
            if(idx[j]=='\n'){
                k++;}
            if (idx[j+1]=='*'&&idx[j]=='/'){
                fprintf(stderr,"Error: line %d: unterminated comment\n", k);
            }
            if (j==1024){
                return 0;   
            }
        
        } 
    }
    return 0;
}