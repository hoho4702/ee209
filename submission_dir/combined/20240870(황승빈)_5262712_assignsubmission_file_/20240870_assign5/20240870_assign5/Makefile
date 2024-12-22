# Makefile for Assignment 5: A Unix Shell (ish)

# 컴파일러 설정
CC = gcc209

# 컴파일 플래그 설정
CFLAGS = -std=c99 -Wall -Wextra -pedantic -D_DEFAULT_SOURCE -D_GNU_SOURCE

# 소스 파일 목록
SRCS = ish.c dynarray.c token.c util.c lexsyn.c

# 객체 파일 목록
OBJS = $(SRCS:.c=.o)

# 최종 실행 파일 이름
TARGET = ish

# 학생 ID 설정
STUDENT_ID := 20240870

# 제출 디렉토리 및 파일 설정
SUBMIT_DIR := ${STUDENT_ID}_assign5
SUBMIT_FILES := ish.c dynarray.c token.c util.c lexsyn.c dynarray.h token.h util.h lexsyn.h Makefile readme EthicsOath.pdf
SUBMIT := ${STUDENT_ID}_assign5.tar.gz

# 기본 빌드 규칙
all: $(TARGET)

# 실행 파일 빌드 규칙
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# 객체 파일 빌드 규칙
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 클린 규칙
clean:
	rm -f $(TARGET) *.o

# 제출 규칙
submit: clean
	rm -rf ${SUBMIT_DIR}
	mkdir -p ${SUBMIT_DIR}
	cp -r ${SUBMIT_FILES} ${SUBMIT_DIR}
	tar -czf ${SUBMIT} ${SUBMIT_DIR}
	rm -rf ${SUBMIT_DIR}

# Phony Targets
.PHONY: all clean submit
