# Makefile for minishell.c

CC=gcc
CFLAGS=-Wall -g
TARGET=minishell

$(TARGET): minishell.c
	$(CC) $(CFLAGS) minishell.c -o $(TARGET)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)