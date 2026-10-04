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
#include "main.h"
#include "libxray.h"

#define MAX_CHUNK_SIZE 60000
#define RECV_BUFFER_SIZE 65536
#define SOCKS_PORT 10808

static uint8_t g_tcp_recv_buf[RECV_BUFFER_SIZE];
static uint8_t g_udp_recv_buf[RECV_BUFFER_SIZE];

static SOCKET g_tcp_sock = INVALID_SOCKET;
static SOCKET g_udp_sock = INVALID_SOCKET;

static WOLFSSL_CTX* g_dtls_ctx = NULL;
static WOLFSSL* g_dtls = NULL;

static HANDLE g_network_thread = NULL;
static WSAEVENT g_events[3]; // 0: TCP (Xray/Ozon), 1: UDP (DTLS/OK-Call), 2: Shutdown
static bool g_is_running = false;
static uint32_t g_my_user_id = 0;
static CRITICAL_SECTION g_send_cs;

static ma_device g_audio_device;
static OpusEncoder* g_opus_encoder = NULL;
// ДОБАВЛЕНО: Глобальный декодер для использования в OnNetworkPacketReceived
OpusDecoder* g_opus_decoder = NULL; 

static bool g_mic_muted = true;
#define SAMPLE_RATE 48000
#define CHANNELS 1
#define OPUS_FRAME_SIZE 960

static bool Socks5HandshakeTCP(SOCKET sock, const char* target_ip, uint16_t target_port) {
    char req1[] = { 0x05, 0x01, 0x00 };
    send(sock, req1, 3, 0);
    char resp1[2];
    recv(sock, resp1, 2, 0);
    if (resp1[1] != 0x00) return false;

    uint8_t req2[10] = { 0x05, 0x01, 0x00, 0x01 };
    inet_pton(AF_INET, target_ip, &req2[4]);
    uint16_t port_n = htons(target_port);
    memcpy(&req2[8], &port_n, 2);

    send(sock, (char*)req2, 10, 0);
    char resp2[10];
    recv(sock, resp2, 10, 0);
    return resp2[1] == 0x00;
}

static void AudioDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    if (pOutput != NULL) {
        int16_t* outSamples = (int16_t*)pOutput;
        ma_uint32 totalSamples = frameCount * pDevice->playback.channels;
        
        memset(outSamples, 0, totalSamples * sizeof(int16_t));

    }
    if (g_mic_muted || !g_is_running || g_dtls == NULL || pInput == NULL || g_opus_encoder == NULL) {
        return;
    }

    if (frameCount == 0 || frameCount > 2880) return;

    unsigned char opus_data[1500];
    
    int bytes = opus_encode(g_opus_encoder, (const opus_int16*)pInput, frameCount, opus_data, sizeof(opus_data));
    
    if (bytes > 0) {
        PktAudio pkt;
        pkt.type = 0x04;
        pkt.length = (uint16_t)(4 + bytes);
        pkt.user_id = g_my_user_id;

        uint8_t send_buf[1500];
        if (sizeof(PktAudio) + bytes <= sizeof(send_buf)) {
            memcpy(send_buf, &pkt, sizeof(PktAudio));
            memcpy(send_buf + sizeof(PktAudio), opus_data, bytes);

            wolfSSL_write(g_dtls, send_buf, sizeof(PktAudio) + bytes);
        }
    }
}
static char* EscapeJson(const char* input) {
    size_t len = strlen(input);
    char* output = (char*)malloc(len * 2 + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (input[i] == '"') {
            output[j++] = '\\';
            output[j++] = '"';
        } else if (input[i] == '\n' || input[i] == '\r') {
            // Игнорируем переносы строк
        } else if (input[i] == '\\') {
            output[j++] = '\\';
            output[j++] = '\\';
        } else {
            output[j++] = input[i];
        }
    }
    output[j] = '\0';
    return output;
}

char* RunXray(const char* raw_xray_json) {
    char* escaped_json = EscapeJson(raw_xray_json);
    
    const char* fmt = "{\"apiVersion\":2,\"method\":\"runXray\",\"payload\":{\"xrayJson\":\"%s\"}}";
    int req_len = snprintf(NULL, 0, fmt, escaped_json);
    char* request = (char*)malloc(req_len + 1);
    snprintf(request, req_len + 1, fmt, escaped_json);

    printf("[REQ] %s\n\n", request);

    // Вызов CGoInvoke из libxray
    char* response = CGoInvoke(request);

    free(escaped_json);
    free(request);
    return response;
}

static char* StopXray() {
    const char* stop_req = "{\"apiVersion\":2,\"method\":\"stopXray\",\"payload\":{}}";
    return CGoInvoke((char*)stop_req);
}

static DWORD WINAPI NetworkThread(LPVOID lpParam) {
    while (g_is_running) {
        DWORD dwEvent = WSAWaitForMultipleEvents(3, g_events, FALSE, WSA_INFINITE, FALSE);
        int event_idx = dwEvent - WSA_WAIT_EVENT_0;

        if (event_idx == 2) break;

        if (event_idx == 0) { // TCP поток (Xray -> ozon.ru)
            WSANETWORKEVENTS netEvents;
            WSAEnumNetworkEvents(g_tcp_sock, g_events[0], &netEvents);
            
            if (netEvents.lNetworkEvents & FD_READ) {
                int bytes_read = recv(g_tcp_sock, (char*)g_tcp_recv_buf, RECV_BUFFER_SIZE, 0);
                if (bytes_read > 0) {
                    uint8_t type = g_tcp_recv_buf[0];
                    OnNetworkPacketReceived(type, g_tcp_recv_buf, bytes_read);
                }
            }
            if (netEvents.lNetworkEvents & FD_CLOSE) {
                g_is_running = false;
            }
        }

        if (event_idx == 1) { // UDP поток (DTLS -> calls.okcdn.ru)
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


bool zn_Init(const char* xray_json_config, const char* target_server_ip, uint16_t tcp_port, uint16_t udp_port, uint32_t my_user_id) {
    FILE* f = fopen("ZN_LOG.txt", "w");
    if(f) { fputs("1. Start zn_Init\n", f); fclose(f); }

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    wolfSSL_Init();
    InitializeCriticalSection(&g_send_cs);
    g_my_user_id = my_user_id;

    if(f) { fputs("2. Before RunXray\n", f); fclose(f); }

    // 1. Запуск ядра Xray REALITY
    char* xray_err = RunXray(xray_json_config);
    
    if(f) { fputs("3. After RunXray\n", f); fclose(f); }

    if (xray_err != NULL) {
        CGoFree(xray_err);
        return false;
    }

    // 2. Настройка TCP сокета к Xray SOCKS5
    g_tcp_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in socks_addr = {0};
    socks_addr.sin_family = AF_INET;
    socks_addr.sin_port = htons(SOCKS_PORT);
    inet_pton(AF_INET, "127.0.0.1", &socks_addr.sin_addr);

    if (connect(g_tcp_sock, (struct sockaddr*)&socks_addr, sizeof(socks_addr)) != 0) return false;
    if (!Socks5HandshakeTCP(g_tcp_sock, target_server_ip, tcp_port)) return false;

    // 3. Инициализация DTLS под видом WebRTC/Звонка MAX/OK
    g_dtls_ctx = wolfSSL_CTX_new(wolfDTLSv1_3_client_method());
    g_udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    struct sockaddr_in udp_addr = {0};
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(udp_port);
    inet_pton(AF_INET, target_server_ip, &udp_addr.sin_addr);

    connect(g_udp_sock, (struct sockaddr*)&udp_addr, sizeof(udp_addr));

    g_dtls = wolfSSL_new(g_dtls_ctx);
    wolfSSL_set_fd(g_dtls, g_udp_sock);
    
    // Подмена SNI в DTLS ClientHello на сервер голосовых звонков VK/OK/MAX
    wolfSSL_UseSNI(g_dtls, WOLFSSL_SNI_HOST_NAME, "calls.okcdn.ru", 15);
    if (wolfSSL_connect(g_dtls) != WOLFSSL_SUCCESS) return false;

    // 4. Подготовка событий
    g_events[0] = WSACreateEvent();
    g_events[1] = WSACreateEvent();
    g_events[2] = WSACreateEvent();

    WSAEventSelect(g_tcp_sock, g_events[0], FD_READ | FD_CLOSE);
    WSAEventSelect(g_udp_sock, g_events[1], FD_READ | FD_CLOSE);

	// int opus_err;
	// g_opus_encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, &opus_err);
	// // ДОБАВЛЕНО: Создание стандартного декодера вместо custom
	// g_opus_decoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &opus_err); 

	// if (opus_err != OPUS_OK || !g_opus_encoder || !g_opus_decoder) {
	// 	printf("Ошибка инициализации кодека Opus!\n");
	// 	return false;
	// }

	// ma_device_config config = ma_device_config_init(ma_device_type_duplex); // Обязательно duplex для звонков
	// config.capture.format   = ma_format_s16;
	// config.capture.channels = 1;              // Моно для микрофона
	// config.playback.format  = ma_format_s16;
	// config.playback.channels = 2;             // Стерео для наушников/динамиков
	// config.sampleRate       = 48000;          // Opus нативно работает с 48 кГц
	// config.dataCallback     = AudioDataCallback;
	// config.periodSizeInFrames = 960;          // Жестко фиксируем 20мс пакеты (960 семплов при 48кГц)

	// if (ma_device_init(NULL, &config, &g_audio_device) == MA_SUCCESS) {
	// 	ma_device_start(&g_audio_device);
	// }
	
    g_is_running = true;
    g_network_thread = CreateThread(NULL, 0, NetworkThread, NULL, 0, NULL);

    return true;
}

bool zn_SendText(uint64_t chat_id, uint64_t msg_id, const char* text) {
    if (g_tcp_sock == INVALID_SOCKET) return false;
    uint16_t text_len = (uint16_t)strlen(text);
    
    PktText pkt = {0};
    pkt.type = 0x01;
    pkt.length = 20 + text_len;
    pkt.user_id = g_my_user_id;
    pkt.msg_id = msg_id;
    pkt.chat_id = chat_id;

    uint8_t buf[2048];
    memcpy(buf, &pkt, sizeof(PktText));
    memcpy(buf + sizeof(PktText), text, text_len);

    EnterCriticalSection(&g_send_cs);
    int ret = send(g_tcp_sock, (const char*)buf, sizeof(PktText) + text_len, 0);
    LeaveCriticalSection(&g_send_cs);
    return ret > 0;
}

bool zn_SendSystem(const char* json_str) {
    if (g_tcp_sock == INVALID_SOCKET) return false;
    uint32_t json_len = (uint32_t)strlen(json_str);

    PktSystem pkt;
    pkt.type = 0x02;
    pkt.length = json_len;

    uint8_t buf[4096];
    memcpy(buf, &pkt, sizeof(PktSystem));
    memcpy(buf + sizeof(PktSystem), json_str, json_len);

    EnterCriticalSection(&g_send_cs);
    int ret = send(g_tcp_sock, (const char*)buf, sizeof(PktSystem) + json_len, 0);
    LeaveCriticalSection(&g_send_cs);
    return ret > 0;
}

bool zn_SendMediaFile(uint64_t chat_id, uint64_t msg_id, bool is_doc, const wchar_t* file_path) {
    if (g_tcp_sock == INVALID_SOCKET) return false;

    FILE* fp = _wfopen(file_path, L"rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t read_buf[32768];
    uint32_t chunk_idx = 0;
    uint32_t approx_chunks = (file_size / 32768) + 1; 

    // ИСПРАВЛЕНИЕ: Выделяем память под сжатые данные локально (защита от Data Race)
    uint8_t* zlib_out_buf = (uint8_t*)malloc(MAX_CHUNK_SIZE);
    if (!zlib_out_buf) {
        fclose(fp);
        return false;
    }

    z_stream strm = {0};
    deflateInit(&strm, Z_DEFAULT_COMPRESSION);

    int flush;
    do {
        strm.avail_in = (uInt)fread(read_buf, 1, sizeof(read_buf), fp);
        if (ferror(fp)) { 
            deflateEnd(&strm); 
            fclose(fp); 
            free(zlib_out_buf); 
            return false; 
        }
        flush = feof(fp) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = read_buf;

        do {
            strm.avail_out = MAX_CHUNK_SIZE;
            strm.next_out = zlib_out_buf; // Пишем в локальный буфер
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
                pkt.total_chunks = approx_chunks;

                // ИСПРАВЛЕНИЕ: Динамическое выделение буфера (защита от переполнения стека Stack Overflow)
                uint8_t* send_buf = (uint8_t*)malloc(MAX_CHUNK_SIZE + sizeof(PktMediaChunk));
                if (send_buf) {
                    memcpy(send_buf, &pkt, sizeof(PktMediaChunk));
                    memcpy(send_buf + sizeof(PktMediaChunk), zlib_out_buf, compressed_bytes);

                    EnterCriticalSection(&g_send_cs);
                    send(g_tcp_sock, (const char*)send_buf, sizeof(PktMediaChunk) + compressed_bytes, 0);
                    LeaveCriticalSection(&g_send_cs);
                    
                    free(send_buf);
                }
            }
        } while (strm.avail_out == 0);
    } while (flush != Z_FINISH);

    deflateEnd(&strm);
    fclose(fp);
    free(zlib_out_buf);
    
    return true;
}

void zn_SetMicrophoneMute(bool mute) {
    g_mic_muted = mute;
}

void zn_Shutdown() {
    if (!g_is_running) return;
    g_is_running = false;
    
    // Остановка Xray ядра
    char* stop_res = StopXray();
    if (stop_res) CGoFree(stop_res);

    WSASetEvent(g_events[2]); 
    WaitForSingleObject(g_network_thread, INFINITE);
    CloseHandle(g_network_thread);

    if (g_dtls) { wolfSSL_shutdown(g_dtls); wolfSSL_free(g_dtls); }
    if (g_dtls_ctx) wolfSSL_CTX_free(g_dtls_ctx);
    
    if (g_tcp_sock != INVALID_SOCKET) closesocket(g_tcp_sock);
    if (g_udp_sock != INVALID_SOCKET) closesocket(g_udp_sock);

    for (int i = 0; i < 3; i++) WSACloseEvent(g_events[i]);
    DeleteCriticalSection(&g_send_cs);

	// ma_device_uninit(&g_audio_device);
    
    // if (g_opus_encoder) opus_encoder_destroy(g_opus_encoder);
    // if (g_opus_decoder) opus_decoder_destroy(g_opus_decoder);
    
    wolfSSL_Cleanup();
    WSACleanup();
}
