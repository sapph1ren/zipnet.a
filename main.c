#define MINIAUDIO_IMPLEMENTATION
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#include <zlib.h>
#include <opus/opus.h>
#include "miniaudio.h"
#include "netlib.h"

// --- СТАТИЧЕСКАЯ ПАМЯТЬ (< 10 MB) ---
#define MAX_CHUNK_SIZE 60000
#define RECV_BUFFER_SIZE 65536
static uint8_t g_tcp_recv_buf[RECV_BUFFER_SIZE];
static uint8_t g_udp_recv_buf[RECV_BUFFER_SIZE];
static uint8_t g_zlib_out_buf[MAX_CHUNK_SIZE];

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ СОСТОЯНИЯ ---
static SOCKET g_tcp_sock = INVALID_SOCKET;
static SOCKET g_udp_sock = INVALID_SOCKET;
static WOLFSSL_CTX* g_tls_ctx = NULL;
static WOLFSSL* g_tls = NULL;
static WOLFSSL_CTX* g_dtls_ctx = NULL;
static WOLFSSL* g_dtls = NULL;

static HANDLE g_network_thread = NULL;
static WSAEVENT g_events[3]; // 0: TCP, 1: UDP, 2: Shutdown
static bool g_is_running = false;
static uint32_t g_my_user_id = 0;
static CRITICAL_SECTION g_send_cs; // Защита от гонок при отправке

// --- АУДИО (Miniaudio + Opus) ---
static ma_device g_audio_device;
static OpusEncoder* g_opus_encoder = NULL;
static bool g_mic_muted = true;
#define SAMPLE_RATE 48000
#define CHANNELS 1
#define OPUS_FRAME_SIZE 960 // 20ms

// wchar_t в UTF-8
char* WcharToUtf8(const wchar_t* wstr) {
    if (!wstr) return NULL;
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    char* utf8 = (char*)malloc(size);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf8, size, NULL, NULL);
    return utf8;
} // free()

static void AudioDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pOutput;
    if (g_mic_muted || !g_is_running || g_dtls == NULL || pInput == NULL) return;

    unsigned char opus_data[1000];
    int bytes = opus_encode(g_opus_encoder, (const opus_int16*)pInput, frameCount, opus_data, sizeof(opus_data));
    
    if (bytes > 0) {
        PktAudio pkt;
        pkt.type = 0x04;
        pkt.length = (uint16_t)(4 + bytes);
        pkt.user_id = g_my_user_id;

        uint8_t send_buf[1500];
        memcpy(send_buf, &pkt, sizeof(PktAudio));
        memcpy(send_buf + sizeof(PktAudio), opus_data, bytes);

        // Отправка через DTLS (Шифрованный UDP)
        wolfSSL_write(g_dtls, send_buf, sizeof(PktAudio) + bytes);
    }
}

static DWORD WINAPI NetworkThread(LPVOID lpParam) {
    while (g_is_running) {
        DWORD dwEvent = WSAWaitForMultipleEvents(3, g_events, FALSE, WSA_INFINITE, FALSE);
        int event_idx = dwEvent - WSA_WAIT_EVENT_0;

        if (event_idx == 2) break; // Сигнал вкл

        if (event_idx == 0) { // TCP
            WSANETWORKEVENTS netEvents;
            WSAEnumNetworkEvents(g_tcp_sock, g_events[0], &netEvents);
            
            if (netEvents.lNetworkEvents & FD_READ) {
                int bytes_read = wolfSSL_read(g_tls, g_tcp_recv_buf, RECV_BUFFER_SIZE);
                if (bytes_read > 0) {
                    uint8_t type = g_tcp_recv_buf[0];
                    OnNetworkPacketReceived(type, g_tcp_recv_buf, bytes_read);
                }
            }
            if (netEvents.lNetworkEvents & FD_CLOSE) {
                g_is_running = false; // Сервер разорвал соединение
            }
        }

        if (event_idx == 1) { // UDP 
            WSANETWORKEVENTS netEvents;
            WSAEnumNetworkEvents(g_udp_sock, g_events[1], &netEvents);
            
            if (netEvents.lNetworkEvents & FD_READ) {
                int bytes_read = wolfSSL_read(g_dtls, g_udp_recv_buf, RECV_BUFFER_SIZE);
                if (bytes_read > 0) {
                    uint8_t type = g_udp_recv_buf[0];
                    OnNetworkPacketReceived(type, g_udp_recv_buf, bytes_read);
                }
            }
        }
    }
    return 0;
}

bool NetLib_Init(const char* server_ip, uint16_t tcp_port, uint16_t udp_port, const wchar_t* ca_cert_path, uint32_t my_user_id) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    wolfSSL_Init();
    InitializeCriticalSection(&g_send_cs);
    g_my_user_id = my_user_id;

    g_tls_ctx = wolfSSL_CTX_new(wolfTLSv1_3_client_method());
    
    char* cert_path_utf8 = WcharToUtf8(ca_cert_path);
    wolfSSL_CTX_load_verify_locations(g_tls_ctx, cert_path_utf8, NULL);
    free(cert_path_utf8);

    wolfSSL_CTX_SetOuterServerName(g_tls_ctx, "ozon.ru");

    g_tcp_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in tcp_addr = {0};
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_port = htons(tcp_port);
    inet_pton(AF_INET, server_ip, &tcp_addr.sin_addr);

    if (connect(g_tcp_sock, (struct sockaddr*)&tcp_addr, sizeof(tcp_addr)) != 0) return false;

    g_tls = wolfSSL_new(g_tls_ctx);
    wolfSSL_set_fd(g_tls, g_tcp_sock);
    wolfSSL_UseSNI(g_tls, WOLFSSL_SNI_HOST_NAME, "ozon.ru", 7);
    
    if (wolfSSL_connect(g_tls) != WOLFSSL_SUCCESS) return false;


	
    g_dtls_ctx = wolfSSL_CTX_new(wolfDTLSv1_2_client_method());
    g_udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in udp_addr = tcp_addr;
    udp_addr.sin_port = htons(udp_port);
    connect(g_udp_sock, (struct sockaddr*)&udp_addr, sizeof(udp_addr));

    g_dtls = wolfSSL_new(g_dtls_ctx);
    wolfSSL_set_fd(g_dtls, g_udp_sock);
    wolfSSL_connect(g_dtls);

    g_events[0] = WSACreateEvent(); // TCP
    g_events[1] = WSACreateEvent(); // UDP
    g_events[2] = WSACreateEvent(); // off

    WSAEventSelect(g_tcp_sock, g_events[0], FD_READ | FD_CLOSE);
    WSAEventSelect(g_udp_sock, g_events[1], FD_READ | FD_CLOSE);

    int opus_err;
    g_opus_encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, &opus_err);
    
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format   = ma_format_s16;
    config.capture.channels = CHANNELS;
    config.sampleRate       = SAMPLE_RATE;
    config.dataCallback     = AudioDataCallback;
    config.periodSizeInFrames = OPUS_FRAME_SIZE;

    if (ma_device_init(NULL, &config, &g_audio_device) == MA_SUCCESS) {
        ma_device_start(&g_audio_device);
    }

    // 5. Запуск сетевого потока
    g_is_running = true;
    g_network_thread = CreateThread(NULL, 0, NetworkThread, NULL, 0, NULL);

    return true;
}

bool NetLib_SendText(uint64_t chat_id, uint64_t msg_id, const char* text) {
    if (!g_tls) return false;
    uint16_t text_len = (uint16_t)strlen(text);
    
    PktText pkt = {0};
    pkt.type = 0x01;
    pkt.length = 20 + text_len; // 20 = sizeof(user_id) + msg_id + chat_id
    pkt.user_id = g_my_user_id;
    pkt.msg_id = msg_id;
    pkt.chat_id = chat_id;

    uint8_t buf[2048];
    memcpy(buf, &pkt, sizeof(PktText));
    memcpy(buf + sizeof(PktText), text, text_len);

    EnterCriticalSection(&g_send_cs);
    int ret = wolfSSL_write(g_tls, buf, sizeof(PktText) + text_len);
    LeaveCriticalSection(&g_send_cs);
    return ret > 0;
}

bool NetLib_SendSystem(const char* json_str) {
    if (!g_tls) return false;
    uint32_t json_len = (uint32_t)strlen(json_str);

    PktSystem pkt;
    pkt.type = 0x02;
    pkt.length = json_len;

    uint8_t buf[4096];
    memcpy(buf, &pkt, sizeof(PktSystem));
    memcpy(buf + sizeof(PktSystem), json_str, json_len);

    EnterCriticalSection(&g_send_cs);
    int ret = wolfSSL_write(g_tls, buf, sizeof(PktSystem) + json_len);
    LeaveCriticalSection(&g_send_cs);
    return ret > 0;
}

bool NetLib_SendMediaFile(uint64_t chat_id, uint64_t msg_id, bool is_doc, const wchar_t* file_path) {
    if (!g_tls) return false;

    FILE* fp = _wfopen(file_path, L"rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t read_buf[32768];
    uint32_t chunk_idx = 0;
    uint32_t approx_chunks = (file_size / 32768) + 1; 

    z_stream strm = {0};
    deflateInit(&strm, Z_DEFAULT_COMPRESSION);

    int flush;
    do {
        strm.avail_in = fread(read_buf, 1, sizeof(read_buf), fp);
        if (ferror(fp)) { deflateEnd(&strm); fclose(fp); return false; }
        flush = feof(fp) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = read_buf;

        do {
            strm.avail_out = MAX_CHUNK_SIZE;
            strm.next_out = g_zlib_out_buf;
            deflate(&strm, flush);
            
            uint32_t compressed_bytes = MAX_CHUNK_SIZE - strm.avail_out;
            
            if (compressed_bytes > 0) {
                PktMediaChunk pkt = {0};
                pkt.type = 0x03;
                pkt.length = (uint16_t)(29 + compressed_bytes);
                pkt.user_id = g_my_user_id;
                pkt.msg_id = msg_id;
                pkt.chat_id = chat_id;
                pkt.is_doc = is_doc ? 1 : 0;
                pkt.chunk_idx = chunk_idx++;
                pkt.total_chunks = approx_chunks; // Сервер должен собирать по EOF или Z_STREAM_END

                uint8_t send_buf[MAX_CHUNK_SIZE + sizeof(PktMediaChunk)];
                memcpy(send_buf, &pkt, sizeof(PktMediaChunk));
                memcpy(send_buf + sizeof(PktMediaChunk), g_zlib_out_buf, compressed_bytes);

                EnterCriticalSection(&g_send_cs);
                wolfSSL_write(g_tls, send_buf, sizeof(PktMediaChunk) + compressed_bytes);
                LeaveCriticalSection(&g_send_cs);
            }
        } while (strm.avail_out == 0);
    } while (flush != Z_FINISH);

    deflateEnd(&strm);
    fclose(fp);
    return true;
}

void NetLib_SetMicrophoneMute(bool mute) {
    g_mic_muted = mute;
}

void NetLib_Shutdown() {
    if (!g_is_running) return;
    g_is_running = false;
    
    WSASetEvent(g_events[2]); 
    WaitForSingleObject(g_network_thread, INFINITE);
    CloseHandle(g_network_thread);

    ma_device_uninit(&g_audio_device);
    if (g_opus_encoder) opus_encoder_destroy(g_opus_encoder);

    if (g_tls) { wolfSSL_shutdown(g_tls); wolfSSL_free(g_tls); }
    if (g_dtls) { wolfSSL_shutdown(g_dtls); wolfSSL_free(g_dtls); }
    if (g_tls_ctx) wolfSSL_CTX_free(g_tls_ctx);
    if (g_dtls_ctx) wolfSSL_CTX_free(g_dtls_ctx);
    
    if (g_tcp_sock != INVALID_SOCKET) closesocket(g_tcp_sock);
    if (g_udp_sock != INVALID_SOCKET) closesocket(g_udp_sock);

    for (int i=0; i<3; i++) WSACloseEvent(g_events[i]);
    DeleteCriticalSection(&g_send_cs);
    
    wolfSSL_Cleanup();
    WSACleanup();
}
