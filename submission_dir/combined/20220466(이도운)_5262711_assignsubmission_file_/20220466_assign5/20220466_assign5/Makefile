#--------------------------------------------------------------------
# Makefile
# Student ID: 20220466
#
# This Makefile builds the ish shell program and provides utilities
# for submission. It uses gcc209 with appropriate flags and supports
# cleaning build artifacts and creating submission archives.
#--------------------------------------------------------------------

# Compiler and flags
# CC: The compiler to use (gcc209 for this assignment)
# CFLAGS: Compilation flags including warnings and necessary defines
CC = gcc209
CFLAGS = -Wall -Wextra -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_GNU_SOURCE -g

# Target executable
# TARGET: The name of the final executable to be built
TARGET = ish

# Source files and object files
# SRCS: All .c source files in the current directory
# OBJS: Object files derived from source files
SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

# Submission variables
# STUDENT_ID: Read from STUDENT_ID file
# SUBMIT_DIR: Directory name for submission
# SUBMIT_FILES: Files to include in submission
# SUBMIT: Name of final submission archive
STUDENT_ID := $(shell cat STUDENT_ID)
SUBMIT_DIR := $(STUDENT_ID)_assign5
SUBMIT_FILES := $(SRCS) *.h Makefile readme EthicsOath.pdf
SUBMIT := $(SUBMIT_DIR).tar.gz

# Default rule: build the executable
# Creates the target executable from object files
all: $(TARGET)

# Links object files to create the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Rule to clean up build artifacts
# Removes all generated files for a clean build
clean:
	rm -f $(TARGET) $(OBJS)

# Rule for submission
# Creates a submission archive containing all necessary files
# Checks for STUDENT_ID file existence
# Creates a temporary directory, copies files, creates archive, and cleans up
submit:
	@if [ -z "$(STUDENT_ID)" ]; then \
		echo "Error: STUDENT_ID file is missing or empty. Please create a STUDENT_ID file with your ID."; \
		exit 1; \
	fi
	@echo "Preparing submission archive: $(SUBMIT)"
	mkdir -p $(SUBMIT_DIR)
	cp $(SUBMIT_FILES) $(SUBMIT_DIR)
	tar -czf $(SUBMIT) $(SUBMIT_DIR)
	rm -rf $(SUBMIT_DIR)
	@echo "Submission archive created successfully."

# Phony targets
# Declares targets that don't represent actual files
.PHONY: all clean submit