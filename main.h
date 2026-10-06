#ifndef NETLIB_H
#define NETLIB_H

#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>


#pragma pack(push, 1)


typedef enum {
    SYS_TEXT = 0, 
    SYS_REG = 1, 
    SYS_AUTH = 2, 
    SYS_GMI = 3, 
    SYS_MEDF = 4, 
    SYS_MEDC = 5, 
    SYS_AUDIO = 6  
} zn_types;

#define TEXT 0
#define REG 1
#define AUTH 2
#define GMI 3
#define MEDf 4
#define MEDc 5
#define AUDIO 6
#define IMG 7

bool compress_file(FILE *src, FILE*dst);
bool decompress_file(FILE *src, FILE *dst);

extern void process_tcp_data(const unsigned char* data, int length);
extern void process_udp_data(const unsigned char* data, int length);


bool zn_Init(uint32_t uid, const char* login, const char* bdu);

void zn_Shutdown();

void zn_MuteUnMute();

bool zn_SendText(const uint32_t uid, const uint64_t cid, const char* text);
bool zn_SendSys(const uint32_t uid, const char* json_str, zn_types type);
bool zn_SendFile(const uint32_t uid, const uint64_t cid, const uint64_t mid, const bool is_doc, const wchar_t* file_path);
bool zn_SendZip(const uint32_t uid, const uint64_t cid, const uint64_t mid, wchar_t**paths);

#endif

