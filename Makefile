CC      = gcc
AR      = ar
ARFLAGS = rcs

TARGET_EXEC = app.exe
TARGET_LIB  = libzipnet.a
TARGET_DLL  = libzipnetdll.dll

SRC = main.c
OBJ = $(SRC:.c=.o)

CFLAGS = -Wall -Wextra -DWIN32_LEAN_AND_MEAN -std=c17 -O3 -fno-plt -ffunction-sections -fdata-sections -fno-ident -fstack-protector-strong -Wimplicit-function-declaration -w -DNDEBUG -I. -I./wolfssl -DWOLFSSL_USER_SETTINGS

INCLUDES = -I.

LIBS = -lwolfssl -lz -lopus -lminiaudio -lws2_32 -lcrypt32 -lwinmm

.PHONY: all clean exec lib dll

all: lib

exec: $(TARGET_EXEC)

$(TARGET_EXEC): $(SRC)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC) -o $@ $(LIBS)
	@echo "[+] Executable successfully built: $(TARGET_EXEC)"
	./$(TARGET_EXEC)

lib: $(TARGET_LIB)

$(TARGET_LIB): CFLAGS += -DBUILD_AS_LIBRARY
$(TARGET_LIB): $(OBJ)
	$(AR) $(ARFLAGS) $@ $<
	@echo "[+] Static library successfully built: $(TARGET_LIB)"

dll: $(TARGET_DLL)

$(TARGET_DLL): CFLAGS += -DBUILD_AS_LIBRARY -D_BUILD_DLL
$(TARGET_DLL): $(SRC)
	$(CC) -shared $(CFLAGS) $(INCLUDES) $(SRC) -o $@ $(LIBS)
	@echo "[+] Shared library successfully built: $(TARGET_DLL)"

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET_EXEC) $(TARGET_LIB) $(TARGET_DLL)
	@echo "[+] Clean complete."
