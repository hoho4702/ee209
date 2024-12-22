# 학생 ID를 STUDENT_ID 파일에서 읽어옵니다.
STUDENT_ID := $(shell cat STUDENT_ID)

# 제출 디렉토리 및 파일 설정
SUBMIT_DIR := $(STUDENT_ID)_assign5
SUBMIT_FILES := $(wildcard *.c) $(wildcard *.h) Makefile readme EthicsOath.pdf
SUBMIT := $(STUDENT_ID)_assign5.tar.gz

# 타겟 실행 파일 이름
TARGET = ish

# 모든 .c 파일을 소스 파일로 지정
SRCS = $(wildcard *.c)

# 소스 파일을 객체 파일로 변환
OBJS = $(patsubst %.c, %.o, $(SRCS))

# 컴파일러 및 컴파일 옵션 설정
CC = gcc209
CFLAGS = -Wall -Werror -g -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_GNU_SOURCE

# 기본 규칙: 모든 타겟을 빌드
all: $(TARGET)

# 타겟 실행 파일을 객체 파일로부터 링크
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# 각 소스 파일을 객체 파일로 컴파일
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 제출 규칙
submit:
	# 제출 디렉토리 생성
	mkdir -p $(SUBMIT_DIR)
	# 제출할 파일 복사
	cp $(SUBMIT_FILES) $(SUBMIT_DIR)
	# 제출 디렉토리를 tar.gz로 압축
	tar -czf $(SUBMIT) $(SUBMIT_DIR)
	# 임시 디렉토리 삭제
	rm -rf $(SUBMIT_DIR)

# 청소 규칙: 빌드된 파일 및 객체 파일 삭제
clean:
	rm -rf $(TARGET) *.o

# .PHONY 타겟 선언
.PHONY: all clean submit
