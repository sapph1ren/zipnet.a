<<<<<<< HEAD
#define MINIAUDIO_IMPLEMENTATION
=======
>>>>>>> d45bdc1da5939b87d53de9cf45f8958dc58780b9
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winreg.h>
#include <wincrypt.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wininet.h>
#define INTERNET_SCHEME       WINHTTP_INTERNET_SCHEME
#define LPINTERNET_SCHEME     WINHTTP_LPINTERNET_SCHEME
#define URL_COMPONENTS        WINHTTP_URL_COMPONENTS
#define LPURL_COMPONENTS      WINHTTP_LPURL_COMPONENTS
#define URL_COMPONENTSW       WINHTTP_URL_COMPONENTSW
#define LPURL_COMPONENTSW     WINHTTP_LPURL_COMPONENTSW
#define HTTP_VERSION_INFO     WINHTTP_HTTP_VERSION_INFO
#define LPHTTP_VERSION_INFO   WINHTTP_LPHTTP_VERSION_INFO
#include <winhttp.h>
#undef INTERNET_SCHEME
#undef LPINTERNET_SCHEME
#undef URL_COMPONENTS
#undef LPURL_COMPONENTS
#undef URL_COMPONENTSW
#undef LPURL_COMPONENTSW
#undef HTTP_VERSION_INFO
#undef LPHTTP_VERSION_INFO
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <shlwapi.h>

<<<<<<< HEAD
#include "zlib/zconf.h"
#include "zlib/zlib.h"

#include "minizip/minizip/unzip.h"
#include "minizip/minizip/zip.h"

#include "libxray.h"


#define KB64 64*1024
#define MB10 10*1024*1024
#define TCP_LOCAL_PORT 10808
#define UDP_LOCAL_PORT 10808
#define CONFIG_FILE "ops.json"
#define YA_LINK "https://disk.yandex.ru/d/3NUbG0QlimvqDA"
#define TEXT 0
#define REG 1
#define AUTH 2
#define GMI 3
#define MEDf 4
#define MEDc 5
#define AUDIO 6
#define IMG 7
typedef struct {
    SOCKET tcp_sock;
    SOCKET udp_sock;
    HANDLE shutdown_event;
    HANDLE receive_thread;
    struct sockaddr_in tcp_addr;
    struct sockaddr_in udp_addr;
} NetworkContext;
typedef enum {
    SYS_TEXT = 0, 
    SYS_REG = 1, 
    SYS_AUTH = 2, 
    SYS_GMI = 3, 
    SYS_MEDF = 4, 
    SYS_MEDC = 5, 
    SYS_AUDIO = 6  
} zn_types;
static NetworkContext gi;
static bool g_mic = false;

static char* http_get(const char* url) {
    HINTERNET hInternet = InternetOpenA("zc/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return NULL;

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE;
    HINTERNET hConnect = InternetOpenUrlA(hInternet, url, NULL, 0, flags, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return NULL;
=======
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
>>>>>>> d45bdc1da5939b87d53de9cf45f8958dc58780b9
    }

    size_t capacity = 2048;
    size_t total_read = 0;
    char* buffer = (char*)malloc(capacity);
    if (!buffer) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return NULL;
    }

    DWORD bytes_read = 0;
    char chunk[512];

    while (InternetReadFile(hConnect, chunk, sizeof(chunk), &bytes_read) && bytes_read > 0) {
        if (total_read + bytes_read + 1 > capacity) {
            capacity *= 2;
            char* new_buf = (char*)realloc(buffer, capacity);
            if (!new_buf) {
                free(buffer);
                InternetCloseHandle(hConnect);
                InternetCloseHandle(hInternet);
                return NULL;
            }
            buffer = new_buf;
        }
        memcpy(buffer + total_read, chunk, bytes_read);
        total_read += bytes_read;
    }

    buffer[total_read] = '\0';

    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    return buffer;
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

<<<<<<< HEAD
int add_file_to_zip(zipFile zf, const wchar_t *file_path) {
	wchar_t* filename_in_zip = PathFindFileNameW(file_path); 
	
	int si = WideCharToMultiByte(CP_UTF8, 0, filename_in_zip, -1, NULL, 0, NULL, NULL);
    char* s = malloc(si * sizeof(char));
    if (!s) {
        free(s);
        return -3;
    }
    WideCharToMultiByte(CP_UTF8, 0, filename_in_zip, -1, s, si, NULL, NULL);
	
	
	FILE *src_file = _wfopen(file_path, L"rb");
    if (src_file == NULL) {
		free(s);
		return -1;
    }

    zip_fileinfo zi;
    memset(&zi, 0, sizeof(zip_fileinfo));
=======
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
>>>>>>> d45bdc1da5939b87d53de9cf45f8958dc58780b9

    int err = zipOpenNewFileInZip(
        zf, 
        s, 
        &zi,
        NULL, 0,    // Локальные экстра-поля
        NULL, 0,    // Глобальные экстра-поля
        NULL,       // Комментарий к файлу
        Z_DEFLATED, // Способ сжатия
        Z_DEFAULT_COMPRESSION
    );

    if (err != ZIP_OK) {
        printf("Ошибка создания записи в ZIP: %d\n", err);
        fclose(src_file);
		free(s);
		return err;
    }

    char buffer[KB64];
    size_t bytes_read;
    int write_err = ZIP_OK;

    while ((bytes_read = fread(buffer, 1, KB64, src_file)) > 0) {
        err = zipWriteInFileInZip(zf, buffer, (unsigned int)bytes_read);
        if (err < 0) {
            printf("Ошибка записи данных в ZIP: %d\n", err);
            write_err = err;
			free(s);
			break;
        }
    }

    fclose(src_file);
    zipCloseFileInZip(zf);
	free(s);
    return write_err;
}

<<<<<<< HEAD
char* V_gsip(const char* public_link) {
    char api_url[1024];
    snprintf(api_url, sizeof(api_url), 
             "https://cloud-api.yandex.net/v1/disk/public/resources/download?public_key=%s", 
             public_link);
=======
bool zn_SendText(uint64_t chat_id, uint64_t msg_id, const char* text) {
    if (g_tcp_sock == INVALID_SOCKET) return false;
    uint16_t text_len = (uint16_t)strlen(text);
    
    PktText pkt = {0};
    pkt.type = 0x01;
    pkt.length = 20 + text_len;
    pkt.user_id = g_my_user_id;
    pkt.msg_id = msg_id;
    pkt.chat_id = chat_id;
>>>>>>> d45bdc1da5939b87d53de9cf45f8958dc58780b9

    char* api_response = http_get(api_url);
    if (!api_response) return NULL;

<<<<<<< HEAD
    char* href_start = strstr(api_response, "\"href\":\"");
    if (!href_start) {
        free(api_response);
        return NULL;
    }
    href_start += 8;

    char* href_end = strchr(href_start, '"');
    if (!href_end) {
        free(api_response);
        return NULL;
    }

    size_t raw_len = href_end - href_start;
    char* download_url = (char*)malloc(raw_len + 1);
    if (!download_url) {
        free(api_response);
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < raw_len; i++) {
        if (href_start[i] == '\\' && href_start[i + 1] == '/') {
            continue;
        }
        download_url[j++] = href_start[i];
    }
    download_url[j] = '\0';
    free(api_response);

    char* config_json = http_get(download_url);
    free(download_url);

    return config_json;
}

static int get_cfg(const char* login, const char* bdu) {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    BOOL bResults = FALSE;
    DWORD dwDownloaded = 0;
    char response[4096] = {0};
    size_t response_offset = 0;
=======
    EnterCriticalSection(&g_send_cs);
    int ret = send(g_tcp_sock, (const char*)buf, sizeof(PktText) + text_len, 0);
    LeaveCriticalSection(&g_send_cs);
    return ret > 0;
}

bool zn_SendSystem(const char* json_str) {
    if (g_tcp_sock == INVALID_SOCKET) return false;
    uint32_t json_len = (uint32_t)strlen(json_str);
>>>>>>> d45bdc1da5939b87d53de9cf45f8958dc58780b9

    char postData[512];
    sprintf_s(postData, sizeof(postData), "%s\n%s", login, bdu);
    DWORD postDataLen = (DWORD)strlen(postData);

    char* sn = V_gsip(YA_LINK);
    if (sn == NULL) {
        return -3;
    }

<<<<<<< HEAD
    int si = MultiByteToWideChar(CP_UTF8, 0, sn, -1, NULL, 0);
    wchar_t* wsn = (wchar_t*)malloc(si * sizeof(wchar_t));
    if (!wsn) {
        free(sn);
        return -3;
    }
    MultiByteToWideChar(CP_UTF8, 0, sn, -1, wsn, si);

    hSession = WinHttpOpen(L"zc/1.0",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);

    if (hSession) {
        hConnect = WinHttpConnect(hSession, wsn, 8443, 0);
    }

    if (hConnect) {
        hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/post",
                                      NULL, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      WINHTTP_FLAG_SECURE);
    }

    if (hRequest) {
        const wchar_t* headers = L"Content-Type: text/plain\r\n";
        WinHttpAddRequestHeaders(hRequest, headers, -1, WINHTTP_ADDREQ_FLAG_ADD);
        bResults = WinHttpSendRequest(hRequest,
                                      WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      (LPVOID)postData, postDataLen,
                                      postDataLen, 0);
    }

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }

    if (bResults) {
        DWORD dwSize = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;

            LPSTR pszOutBuffer = (LPSTR)malloc(dwSize + 1);
            if (!pszOutBuffer) break;

            ZeroMemory(pszOutBuffer, dwSize + 1);

            if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                if (response_offset + dwDownloaded < sizeof(response) - 1) {
                    memcpy(response + response_offset, pszOutBuffer, dwDownloaded);
                    response_offset += dwDownloaded;
                    response[response_offset] = '\0';
                }
            }
            free(pszOutBuffer);
        } while (dwSize > 0);
    }

    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    free(wsn);
    free(sn);

    int success = 0;
    FILE *f = fopen(CONFIG_FILE, "w");
    if (f) {
        fputs(response, f);
        fclose(f);
        success = 1;
    }

    return success ? 0 : -4;
}

extern void process_tcp_data(const unsigned char* data, int length);
extern void process_udp_data(const unsigned char* data, int length);
=======
    EnterCriticalSection(&g_send_cs);
    int ret = send(g_tcp_sock, (const char*)buf, sizeof(PktSystem) + json_len, 0);
    LeaveCriticalSection(&g_send_cs);
    return ret > 0;
}

bool zn_SendMediaFile(uint64_t chat_id, uint64_t msg_id, bool is_doc, const wchar_t* file_path) {
    if (g_tcp_sock == INVALID_SOCKET) return false;
>>>>>>> d45bdc1da5939b87d53de9cf45f8958dc58780b9


bool compress_file(FILE *src, FILE*dst)
{
    uint8_t inbuff[KB64];
    uint8_t outbuff[KB64];
    z_stream stream = {0};

<<<<<<< HEAD
    if(deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK)
    {
        return false;
    }

    int flush;
    do {
        stream.avail_in = fread(inbuff, 1, KB64, src);
        if(ferror(src))
        {
            deflateEnd(&stream);
            return false;
        }

        flush = feof(src) ? Z_FINISH : Z_NO_FLUSH;
        stream.next_in = inbuff;

        do {
            stream.avail_out = KB64;
            stream.next_out = outbuff;
            deflate(&stream, flush);
            uint32_t nbytes = KB64 - stream.avail_out;

            if(fwrite(outbuff, 1, nbytes, dst) != nbytes ||
               ferror(dst))
            {
                deflateEnd(&stream);
                return false;
=======
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
>>>>>>> d45bdc1da5939b87d53de9cf45f8958dc58780b9
            }
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

<<<<<<< HEAD
    deflateEnd(&stream);
=======
    deflateEnd(&strm);
    fclose(fp);
    free(zlib_out_buf);
    
>>>>>>> d45bdc1da5939b87d53de9cf45f8958dc58780b9
    return true;
}

/* Декомпрессия */
bool decompress_file(FILE *src, FILE *dst)
{
    uint8_t inbuff[KB64];
    uint8_t outbuff[KB64];
    z_stream stream = { 0 };

    int result = inflateInit(&stream);
    if(result != Z_OK)
    {
        fprintf(stderr, "inflateInit(...) failed!\n");
        return false;
    }

    do {
        stream.avail_in = fread(inbuff, 1, KB64, src);
        if(ferror(src))
        {
            fprintf(stderr, "fread(...) failed!\n");
            inflateEnd(&stream);
            return false;
        }

        if(stream.avail_in == 0)
            break;

        stream.next_in = inbuff;

        do {
            stream.avail_out = KB64;
            stream.next_out = outbuff;
            result = inflate(&stream, Z_NO_FLUSH);
            if(result == Z_NEED_DICT || result == Z_DATA_ERROR ||
                result == Z_MEM_ERROR)
            {
                fprintf(stderr, "inflate(...) failed: %d\n", result);
                inflateEnd(&stream);
                return false;
            }

            uint32_t nbytes = KB64 - stream.avail_out;

            if(fwrite(outbuff, 1, nbytes, dst) != nbytes ||
               ferror(dst))
            {
                fprintf(stderr, "fwrite(...) failed!\n");
                inflateEnd(&stream);
                return false;
            }
        } while (stream.avail_out == 0);
    } while (result != Z_STREAM_END);

    inflateEnd(&stream);
    return result == Z_STREAM_END;
}

static DWORD WINAPI NetworkReceiveThread(LPVOID lpParam) {
    NetworkContext* ctx = (NetworkContext*)lpParam;
    
    WSAEVENT tcp_event = WSACreateEvent();
    WSAEVENT udp_event = WSACreateEvent();

    WSAEventSelect(ctx->tcp_sock, tcp_event, FD_READ | FD_CLOSE);
    WSAEventSelect(ctx->udp_sock, udp_event, FD_READ | FD_CLOSE);

    WSAEVENT events[3];
    events[0] = ctx->shutdown_event;
    events[1] = tcp_event;
    events[2] = udp_event;

    int tcp_state = 0;
    uint32_t tcp_expected_len = 4;
    uint32_t tcp_bytes_read = 0;
    char header_buf[4];
    unsigned char* payload_buf = NULL;
    unsigned char udp_buf[2048];

    while (1) {
        DWORD dwWait = WSAWaitForMultipleEvents(3, events, FALSE, WSA_INFINITE, FALSE);
        
        if (dwWait == WSA_WAIT_EVENT_0) {
            break;
        }

        if (dwWait == WSA_WAIT_EVENT_0 + 1) {
            WSANETWORKEVENTS netEvents;
            WSAEnumNetworkEvents(ctx->tcp_sock, tcp_event, &netEvents);
            
            if (netEvents.lNetworkEvents & FD_CLOSE) {
                printf("[TCP Thread] Сервер закрыл соединение.\n");
                break;
            }

            if (netEvents.lNetworkEvents & FD_READ) {
                while (1) {
                    if (tcp_state == 0) {
                        int res = recv(ctx->tcp_sock, header_buf + tcp_bytes_read, tcp_expected_len - tcp_bytes_read, 0);
                        if (res > 0) {
                            tcp_bytes_read += res;
                            if (tcp_bytes_read == 4) {
                                uint32_t net_len;
                                memcpy(&net_len, header_buf, 4);
                                tcp_expected_len = ntohl(net_len);
                                
                                if (tcp_expected_len > 0 && tcp_expected_len < 5*1024*1024) {
                                    payload_buf = (char*)malloc(tcp_expected_len);
                                    tcp_state = 1;
                                    tcp_bytes_read = 0;
                                } else {
                                    tcp_bytes_read = 0;
                                }
                            }
                        } else {
                            break;
                        }
                    } 
                    else if (tcp_state == 1) {
                        int res = recv(ctx->tcp_sock, payload_buf + tcp_bytes_read, tcp_expected_len - tcp_bytes_read, 0);
                        if (res > 0) {
                            tcp_bytes_read += res;
                            if (tcp_bytes_read == tcp_expected_len) {
                                process_tcp_data(payload_buf, tcp_expected_len);
                                free(payload_buf);
                                payload_buf = NULL;
                                tcp_state = 0;
                                tcp_bytes_read = 0;
                                tcp_expected_len = 4;
                            }
                        } else {
                            break;
                        }
                    }
                }
            }
        }

        if (dwWait == WSA_WAIT_EVENT_0 + 2) {
            WSANETWORKEVENTS netEvents;
            WSAEnumNetworkEvents(ctx->udp_sock, udp_event, &netEvents);

            if (netEvents.lNetworkEvents & FD_READ) {
                while (1) {
                    int res = recv(ctx->udp_sock, udp_buf, sizeof(udp_buf), 0);
                    if (res > 0) {
                        process_udp_data(udp_buf, res);
                    } else {
                        break;
                    }
                }
            }
        }
    }

    if (payload_buf) free(payload_buf);
    WSAEventSelect(ctx->tcp_sock, tcp_event, 0);
    WSAEventSelect(ctx->udp_sock, udp_event, 0);
    WSACloseEvent(tcp_event);
    WSACloseEvent(udp_event);
    return 0;
}

bool zn_Init(uint32_t uid, const char* login, const char* bdu) {
    FILE* f = fopen(CONFIG_FILE, "r");
    if (!f) {
        if (login == NULL || bdu == NULL) {
            return false;
        }
        if (get_cfg(login, bdu) != 0) {
            return false;
        }
        f = fopen(CONFIG_FILE, "r");
        if (!f) return false;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *config_str = (char*)malloc(fsize + 1);
    if (!config_str) {
        fclose(f);
        return false;
    }
    fread(config_str, 1, fsize, f);
    fclose(f);
    config_str[fsize] = '\0';

    if (StartXray(config_str) != 0) {
        free(config_str);
        return false;
    }
    free(config_str);
    Sleep(1500);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    gi.tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    gi.tcp_addr.sin_family = AF_INET;
    gi.tcp_addr.sin_port = htons(TCP_LOCAL_PORT);
    gi.tcp_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    gi.udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    gi.udp_addr.sin_family = AF_INET;
    gi.udp_addr.sin_port = htons(UDP_LOCAL_PORT);
    gi.udp_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Синхронное подключение к локальному Xray
    if (connect(gi.tcp_sock, (struct sockaddr*)&gi.tcp_addr, sizeof(gi.tcp_addr)) != 0) {
        closesocket(gi.tcp_sock);
        closesocket(gi.udp_sock);
        return false;
    }

    if (connect(gi.udp_sock, (struct sockaddr*)&gi.udp_addr, sizeof(gi.udp_addr)) != 0) {
        closesocket(gi.tcp_sock);
        closesocket(gi.udp_sock);
        return false;
    }

    // Переводим сокеты в неблокирующий режим ПОСЛЕ соединения
    u_long mode = 1;
    ioctlsocket(gi.tcp_sock, FIONBIO, &mode);
    ioctlsocket(gi.udp_sock, FIONBIO, &mode);

    gi.shutdown_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    gi.receive_thread = CreateThread(NULL, 0, NetworkReceiveThread, &gi, 0, NULL);
    
    if (gi.receive_thread == NULL) {
        return false;
    }

    return true;
}

void zn_Shutdown() {
<<<<<<< HEAD
    if (gi.shutdown_event) {
        SetEvent(gi.shutdown_event);
    }
    if (gi.receive_thread) {
        WaitForSingleObject(gi.receive_thread, INFINITE);
        CloseHandle(gi.receive_thread);
    }
    closesocket(gi.tcp_sock);
    closesocket(gi.udp_sock);
=======
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
>>>>>>> d45bdc1da5939b87d53de9cf45f8958dc58780b9
    WSACleanup();
    StopXray();
    if (gi.shutdown_event) {
        CloseHandle(gi.shutdown_event);
    }
}

void zn_MuteUnMute() {
    g_mic = !g_mic;
}


bool zn_SendText(const uint32_t uid, const uint64_t cid, const char* text){
	if(gi.tcp_sock == INVALID_SOCKET){return false;}

	uint16_t strl = (uint16_t)strlen(text) + 17; // uid 4, cid 8 
	unsigned char* otpr = malloc(strl);
	uint8_t a=0;
	sprintf_s(otpr, strl, "%u%u%u%u%s", a, strl-5, uid, cid, text);
	
	send(gi.tcp_sock, otpr, strl, 0);
	free(otpr);
	return true;
}

bool zn_SendSys(const uint32_t uid, const char* json_str, zn_types type){
	if(gi.tcp_sock == INVALID_SOCKET){return false;}

	uint16_t strl = (uint16_t)strlen(json_str) + 9; // uid 4 
	unsigned char* otpr = malloc(strl);
	uint8_t t = type;
	sprintf_s(otpr, strl, "%u%u%u%s", t, strl-5, uid, otpr);
	
	send(gi.tcp_sock, otpr, strl, 0);
	free(otpr);
	return true;	
}

bool zn_SendFile(const uint32_t uid, const uint64_t cid, const uint64_t mid, const bool is_doc, const wchar_t* file_path){
	FILE* f = _wfopen(file_path, L"rb");

	fseek(f, 0, SEEK_END);
	long fs = ftell(f);
	fseek(f, 0, SEEK_SET);

	uLongf cs = compressBound(fs);

	if(!is_doc){
		unsigned char* a = malloc(fs);
		fread(a, 1, fs, f);
		unsigned char* bb = malloc(fs+25);
		sprintf_s(bb, fs+25, "%u%u%u%u%u%.*s", fs+21, IMG,  uid, cid, mid, fs, a);
		fclose(f);
		send(gi.tcp_sock, bb, fs, 0);
		free(a);
		free(bb);
		return true;
	}
	
	if(cs > fs){
		if(fs < MB10){
			unsigned char* a = malloc(fs);
			fread(a, 1, fs, f);
			unsigned char* bb = malloc(fs+25);
			sprintf_s(bb, fs+25, "%u%u%u%u%u%.*s", fs+21, MEDf,  uid, cid, mid, fs, a);
			fclose(f);
			send(gi.tcp_sock, bb, fs, 0);
			free(a);
			free(bb);
			return true;
		}
		unsigned char* b = malloc(KB64);
		unsigned char* bb = malloc(KB64+21);
		uint16_t ss;
		while((ss = fread(b, 1, KB64, f))>0){
			sprintf_s(bb, KB64+21, "%u%u%u%u%u%.*s", KB64+21, MEDc, uid, cid, mid, KB64, b);
			send(gi.tcp_sock, b, fs, 0);
		}
		free(b);
		free(bb);
		fclose(f);
		
		return true;
	}
	uint8_t inbuff[KB64];
    uint8_t outbuff[KB64];
    z_stream stream = {0};
	unsigned char* bb = malloc(KB64+21);
    if(deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK)
    {
        return false;
    }

    int flush;
    do {
        stream.avail_in = fread(inbuff, 1, KB64, f);
        if(ferror(f))
        {
            deflateEnd(&stream);
            return false;
        }

        flush = feof(f) ? Z_FINISH : Z_NO_FLUSH;
        stream.next_in = inbuff;

        do {
            stream.avail_out = KB64;
            stream.next_out = outbuff;
            deflate(&stream, flush);
            uint32_t nbytes = KB64 - stream.avail_out;

			sprintf_s(bb, nbytes, "%u%u%u%u%u%.*s", KB64+21, MEDc, uid, cid, mid, nbytes, outbuff);
			send(gi.tcp_sock, bb, fs, 0);

			
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);
	free(bb);
    deflateEnd(&stream);
    return true;
}

bool zn_SendZip(const uint32_t uid, const uint64_t cid, const uint64_t mid, wchar_t**paths){
	char* zn = malloc(64);
	sprintf_s(zn, 64, L"file-%u:%u=%u.zip", uid, cid, mid);
	zipFile zf = zipOpen(zn, APPEND_STATUS_CREATEAFTER);
	if (zf == NULL) {
        return false;
    }
	int r;
	for (int i = 0; paths[i] != NULL; i++) {
		r = add_file_to_zip(zf, paths[i]);
    }
	zipClose(zf, NULL);
	if(r <0){return false;}

	zn_SendFile(uid, cid, mid, true, zn);

	_wremove(zn);
	free(zn);
	return true;
}
