#ifndef NETLIB_H
#define NETLIB_H

#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>

#pragma pack(push, 1)

// 0x01: Текст
typedef struct {
    uint8_t  type;      // 0x01
    uint16_t length;
    uint32_t user_id;
    uint64_t msg_id;
    uint64_t chat_id;
    // Дальше идут байты текста (без null-терминатора, длина = length - 20)
} PktText;

// 0x02: Системное (JSON)
typedef struct {
    uint8_t  type;      // 0x02
    uint32_t length;
    // Дальше JSON данные (длина = length)
} PktSystem;

// 0x03: Медиа Чанк (zlib)
typedef struct {
    uint8_t  type;      // 0x03
    uint16_t length;
    uint32_t user_id;
    uint64_t msg_id;
    uint64_t chat_id;
    uint8_t  is_doc;
    uint32_t chunk_idx;
    uint32_t total_chunks;
    // Дальше сжатые данные (длина = length - 29)
} PktMediaChunk;

// 0x04: Аудио (UDP/DTLS)
typedef struct {
    uint8_t  type;      // 0x04
    uint16_t length;
    uint32_t user_id;
    // Дальше Opus фрейм (длина = length - 4)
} PktAudio;

#pragma pack(pop)

// --- ВНЕШНИЙ КОЛБЕК (Реализуешь ТЫ в основном коде) ---
// Сюда прилетает все, что расшифровано и готово к обработке
extern void OnNetworkPacketReceived(uint8_t type, const uint8_t* payload, uint32_t len);


// --- API БИБЛИОТЕКИ ---

// Инициализация всего (Сеть, TLS, DTLS, Audio, шифрование).
// server_ip - IP сервера, ca_cert_path - путь до сертификата (кириллица поддерживается)
bool NetLib_Init(const char* server_ip, uint16_t tcp_port, uint16_t udp_port, const wchar_t* ca_cert_path, uint32_t my_user_id);

// Полная очистка и остановка потоков (без утечек)
void NetLib_Shutdown();

// Вкл/Выкл микрофона
void NetLib_SetMicrophoneMute(bool mute);

// Функции отправки (потокобезопасные)
bool NetLib_SendText(uint64_t chat_id, uint64_t msg_id, const char* text);
bool NetLib_SendSystem(const char* json_str);
bool NetLib_SendMediaFile(uint64_t chat_id, uint64_t msg_id, bool is_doc, const wchar_t* file_path);

#endif // NETLIB_H

