CC = gcc
AR = ar
ARFLAGS = rcs
TARGET_LIB = libzipnet.a

SRC = main.c
OBJ = $(SRC:.c=.o)

CFLAGS = -Wall -Wextra -s -DWIN32_LEAN_AND_MEAN -std=c17 -O3 \
         -fno-plt -ffunction-sections -fdata-sections -fno-ident \
         -fstack-protector-strong -Wimplicit-function-declaration -w \
         -DNDEBUG -I. -I./wolfssl -DWOLFSSL_USER_SETTINGS -DBUILD_AS_LIBRARY

INCLUDES = -I.

# Пути к вашим статическим библиотекам (измените путь к папке, если они лежат не в libs/)
STATIC_LIBS = libs/libwolfssl.a libs/libz.a libs/libopus.a libs/libminizip.a libs/libxray.a 


.PHONY: all clean

all: $(TARGET_LIB)

$(TARGET_LIB): $(OBJ)
	@echo "CREATE $@" > ar.mac
	@echo "ADDMOD $(OBJ)" >> ar.mac
	@$(foreach lib,$(STATIC_LIBS),echo "ADDLIB $(lib)" >> ar.mac;)
	@echo "SAVE" >> ar.mac
	@echo "END" >> ar.mac
	$(AR) -M < ar.mac
	rm -f ar.mac
	@mkdir -p build
	cp $(TARGET_LIB) build

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o -lwinhttp -lwininet -lws2_32 -ladvapi32 -lcrypt32 -lshlwapi -lpthread -lm $@

clean:
	rm -f $(OBJ) $(TARGET_LIB) ar.mac
	rm -rf build
