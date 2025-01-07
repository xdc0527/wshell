CC = gcc
CFLAGS-common = -Wall -Wextra -Werror -pedantic -std=gnu18
CFLAGS = $(CFLAGS-common) -O2
CFLAGS-dbg = $(CFLAGS-common) -Og -g
TARGET = wsh
SRC = $(TARGET).c
LOGIN = daocheng_xu
SUBMITPATH = @rockhopper-01.cs.wisc.edu:~cs537-1/handin/$(LOGIN)

all: $(TARGET) $(TARGET)-dbg

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@

$(TARGET)-dbg: $(SRC)
	$(CC) $(CFLAGS-dbg) $< -o $@

clean:
	rm -f $(TARGET) $(TARGET)-dbg

submit:
	scp -r .. $(LOGIN)$(SUBMITPATH)