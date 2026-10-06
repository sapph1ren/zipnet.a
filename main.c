#define MINIAUDIO_IMPLEMENTATION
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

#include "zlib/zconf.h"
#include "zlib/zlib.h"

#include "minizip/minizip/unzip.h"
#include "minizip/minizip/zip.h"

#include "libxray.h"
#include "main.h"

#define KB64 (64 * 1024)
#define MB10 (10 * 1024 * 1024)
#define TCP_LOCAL_PORT 10808
#define UDP_LOCAL_PORT 10808
#define CONFIG_FILE "ops.json"
#define YA_LINK "https://disk.yandex.ru/d/3NUbG0QlimvqDA"

typedef struct {
    SOCKET tcp_sock;
    SOCKET udp_sock;
    HANDLE shutdown_event;
    HANDLE receive_thread;
    struct sockaddr_in tcp_addr;
    struct sockaddr_in udp_addr;
} NetworkContext;

static NetworkContext gi;
static bool g_mic = false;

static inline uint64_t host_to_net64(uint64_t val) {
    return (((uint64_t)htonl((uint32_t)val)) << 32) | htonl((uint32_t)(val >> 32));
}

static char* http_get(const char* url) {
    HINTERNET hInternet = InternetOpenA("zc/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return NULL;

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE;
    HINTERNET hConnect = InternetOpenUrlA(hInternet, url, NULL, 0, flags, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return NULL;
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

int add_file_to_zip(zipFile zf, const wchar_t *file_path) {
    if (!zf || !file_path) return -1;

    wchar_t* filename_in_zip = PathFindFileNameW(file_path);

    int si = WideCharToMultiByte(CP_UTF8, 0, filename_in_zip, -1, NULL, 0, NULL, NULL);
    if (si <= 0) return -2;

    char* s = (char*)malloc(si);
    if (!s) return -3;

    WideCharToMultiByte(CP_UTF8, 0, filename_in_zip, -1, s, si, NULL, NULL);

    FILE *src_file = _wfopen(file_path, L"rb");
    if (src_file == NULL) {
        free(s);
        return -1;
    }

    zip_fileinfo zi;
    memset(&zi, 0, sizeof(zip_fileinfo));

    int err = zipOpenNewFileInZip(
        zf,
        s,
        &zi,
        NULL, 0,
        NULL, 0,
        NULL,
        Z_DEFLATED,
        Z_DEFAULT_COMPRESSION
    );

    if (err != ZIP_OK) {
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
            write_err = err;
            break;
        }
    }

    fclose(src_file);
    zipCloseFileInZip(zf);
    free(s);
    return write_err;
}

char* V_gsip(const char* public_link) {
    char api_url[1024];
    snprintf(api_url, sizeof(api_url),
             "https://cloud-api.yandex.net/v1/disk/public/resources/download?public_key=%s",
             public_link);

    char* api_response = http_get(api_url);
    if (!api_response) return NULL;

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

    char postData[512];
    sprintf_s(postData, sizeof(postData), "%s\n%s", login, bdu);
    DWORD postDataLen = (DWORD)strlen(postData);

    char* sn = V_gsip(YA_LINK);
    if (sn == NULL) {
        return -3;
    }

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

bool compress_file(FILE *src, FILE *dst) {
    if (!src || !dst) return false;

    uint8_t inbuff[KB64];
    uint8_t outbuff[KB64];
    z_stream stream = {0};

    if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK) {
        return false;
    }

    int flush;
    do {
        stream.avail_in = (uInt)fread(inbuff, 1, KB64, src);
        if (ferror(src)) {
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

            if (fwrite(outbuff, 1, nbytes, dst) != nbytes || ferror(dst)) {
                deflateEnd(&stream);
                return false;
            }
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    deflateEnd(&stream);
    return true;
}

bool decompress_file(FILE *src, FILE *dst) {
    if (!src || !dst) return false;

    uint8_t inbuff[KB64];
    uint8_t outbuff[KB64];
    z_stream stream = {0};

    int result = inflateInit(&stream);
    if (result != Z_OK) return false;

    do {
        stream.avail_in = (uInt)fread(inbuff, 1, KB64, src);
        if (ferror(src)) {
            inflateEnd(&stream);
            return false;
        }

        if (stream.avail_in == 0) break;
        stream.next_in = inbuff;

        do {
            stream.avail_out = KB64;
            stream.next_out = outbuff;
            result = inflate(&stream, Z_NO_FLUSH);
            if (result == Z_NEED_DICT || result == Z_DATA_ERROR || result == Z_MEM_ERROR) {
                inflateEnd(&stream);
                return false;
            }

            uint32_t nbytes = KB64 - stream.avail_out;

            if (fwrite(outbuff, 1, nbytes, dst) != nbytes || ferror(dst)) {
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

    WSAEVENT events[3] = { ctx->shutdown_event, tcp_event, udp_event };

    int tcp_state = 0;
    uint32_t tcp_expected_len = 4;
    uint32_t tcp_bytes_read = 0;
    char header_buf[4];
    unsigned char* payload_buf = NULL;
    unsigned char udp_buf[2048];

    while (1) {
        DWORD dwWait = WSAWaitForMultipleEvents(3, events, FALSE, WSA_INFINITE, FALSE);

        if (dwWait == WSA_WAIT_EVENT_0) break;

        if (dwWait == WSA_WAIT_EVENT_0 + 1) {
            WSANETWORKEVENTS netEvents;
            WSAEnumNetworkEvents(ctx->tcp_sock, tcp_event, &netEvents);

            if (netEvents.lNetworkEvents & FD_CLOSE) break;

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

                                if (tcp_expected_len > 0 && tcp_expected_len < 5 * 1024 * 1024) {
                                    payload_buf = (unsigned char*)malloc(tcp_expected_len);
                                    tcp_state = 1;
                                    tcp_bytes_read = 0;
                                } else {
                                    tcp_bytes_read = 0;
                                }
                            }
                        } else {
                            break;
                        }
                    } else if (tcp_state == 1) {
                        int res = recv(ctx->tcp_sock, (char*)(payload_buf + tcp_bytes_read), tcp_expected_len - tcp_bytes_read, 0);
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
                    int res = recv(ctx->udp_sock, (char*)udp_buf, sizeof(udp_buf), 0);
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
        if (login == NULL || bdu == NULL) return false;
        if (get_cfg(login, bdu) != 0) return false;
        f = fopen(CONFIG_FILE, "r");
        if (!f) return false;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0) {
        fclose(f);
        return false;
    }

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

    u_long mode = 1;
    ioctlsocket(gi.tcp_sock, FIONBIO, &mode);
    ioctlsocket(gi.udp_sock, FIONBIO, &mode);

    gi.shutdown_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    gi.receive_thread = CreateThread(NULL, 0, NetworkReceiveThread, &gi, 0, NULL);

    return (gi.receive_thread != NULL);
}

void zn_Shutdown() {
    if (gi.shutdown_event) {
        SetEvent(gi.shutdown_event);
    }
    if (gi.receive_thread) {
        WaitForSingleObject(gi.receive_thread, INFINITE);
        CloseHandle(gi.receive_thread);
    }
    closesocket(gi.tcp_sock);
    closesocket(gi.udp_sock);
    WSACleanup();
    StopXray();
    if (gi.shutdown_event) {
        CloseHandle(gi.shutdown_event);
    }
}

void zn_MuteUnMute() {
    g_mic = !g_mic;
}

bool zn_SendText(const uint32_t uid, const uint64_t cid, const char* text) {
    if (gi.tcp_sock == INVALID_SOCKET || !text) return false;

    size_t text_len = strlen(text);
    uint32_t payload_len = (uint32_t)(text_len + 12); // uid(4) + cid(8) + text
    uint32_t packet_size = 1 + 4 + payload_len;       // Type(1) + Len(4) + Payload

    unsigned char* buf = (unsigned char*)malloc(packet_size);
    if (!buf) return false;

    buf[0] = (uint8_t)SYS_TEXT;

    uint32_t net_len = htonl(payload_len);
    memcpy(buf + 1, &net_len, 4);

    uint32_t net_uid = htonl(uid);
    memcpy(buf + 5, &net_uid, 4);

    uint64_t net_cid = host_to_net64(cid);
    memcpy(buf + 9, &net_cid, 8);

    memcpy(buf + 17, text, text_len);

    int sent = send(gi.tcp_sock, (const char*)buf, (int)packet_size, 0);
    free(buf);
    return (sent != SOCKET_ERROR);
}

bool zn_SendSys(const uint32_t uid, const char* json_str, zn_types type) {
    if (gi.tcp_sock == INVALID_SOCKET || !json_str) return false;

    size_t str_len = strlen(json_str);
    uint32_t payload_len = (uint32_t)(str_len + 4); // uid(4) + json_str
    uint32_t packet_size = 1 + 4 + payload_len;

    unsigned char* buf = (unsigned char*)malloc(packet_size);
    if (!buf) return false;

    buf[0] = (uint8_t)type;

    uint32_t net_len = htonl(payload_len);
    memcpy(buf + 1, &net_len, 4);

    uint32_t net_uid = htonl(uid);
    memcpy(buf + 5, &net_uid, 4);

    memcpy(buf + 9, json_str, str_len);

    int sent = send(gi.tcp_sock, (const char*)buf, (int)packet_size, 0);
    free(buf);
    return (sent != SOCKET_ERROR);
}

bool zn_SendFile(const uint32_t uid, const uint64_t cid, const uint64_t mid, const bool is_doc, const wchar_t* file_path) {
    if (gi.tcp_sock == INVALID_SOCKET || !file_path) return false;

    FILE* f = _wfopen(file_path, L"rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fs = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fs < 0) {
        fclose(f);
        return false;
    }

    if (!is_doc) {
        uint32_t payload_len = (uint32_t)(fs + 20); // uid(4) + cid(8) + mid(8) + data
        uint32_t packet_size = 1 + 4 + payload_len;

        unsigned char* buf = (unsigned char*)malloc(packet_size);
        if (!buf) {
            fclose(f);
            return false;
        }

        buf[0] = (uint8_t)IMG;

        uint32_t net_len = htonl(payload_len);
        memcpy(buf + 1, &net_len, 4);

        uint32_t net_uid = htonl(uid);
        memcpy(buf + 5, &net_uid, 4);

        uint64_t net_cid = host_to_net64(cid);
        memcpy(buf + 9, &net_cid, 8);

        uint64_t net_mid = host_to_net64(mid);
        memcpy(buf + 17, &net_mid, 8);

        fread(buf + 25, 1, fs, f);
        fclose(f);

        int sent = send(gi.tcp_sock, (const char*)buf, (int)packet_size, 0);
        free(buf);
        return (sent != SOCKET_ERROR);
    }

    if (fs < MB10) {
        uint32_t payload_len = (uint32_t)(fs + 20);
        uint32_t packet_size = 1 + 4 + payload_len;

        unsigned char* buf = (unsigned char*)malloc(packet_size);
        if (!buf) {
            fclose(f);
            return false;
        }

        buf[0] = (uint8_t)SYS_MEDF;

        uint32_t net_len = htonl(payload_len);
        memcpy(buf + 1, &net_len, 4);

        uint32_t net_uid = htonl(uid);
        memcpy(buf + 5, &net_uid, 4);

        uint64_t net_cid = host_to_net64(cid);
        memcpy(buf + 9, &net_cid, 8);

        uint64_t net_mid = host_to_net64(mid);
        memcpy(buf + 17, &net_mid, 8);

        fread(buf + 25, 1, fs, f);
        fclose(f);

        int sent = send(gi.tcp_sock, (const char*)buf, (int)packet_size, 0);
        free(buf);
        return (sent != SOCKET_ERROR);
    } else {
        unsigned char chunk[KB64];
        size_t bytes_read;

        while ((bytes_read = fread(chunk, 1, KB64, f)) > 0) {
            uint32_t payload_len = (uint32_t)(bytes_read + 20);
            uint32_t packet_size = 1 + 4 + payload_len;

            unsigned char* buf = (unsigned char*)malloc(packet_size);
            if (!buf) break;

            buf[0] = (uint8_t)SYS_MEDC;

            uint32_t net_len = htonl(payload_len);
            memcpy(buf + 1, &net_len, 4);

            uint32_t net_uid = htonl(uid);
            memcpy(buf + 5, &net_uid, 4);

            uint64_t net_cid = host_to_net64(cid);
            memcpy(buf + 9, &net_cid, 8);

            uint64_t net_mid = host_to_net64(mid);
            memcpy(buf + 17, &net_mid, 8);

            memcpy(buf + 25, chunk, bytes_read);

            send(gi.tcp_sock, (const char*)buf, (int)packet_size, 0);
            free(buf);
        }

        fclose(f);
        return true;
    }
}

bool zn_SendZip(const uint32_t uid, const uint64_t cid, const uint64_t mid, wchar_t** paths) {
    if (!paths) return false;

    wchar_t zip_name[256];
    swprintf_s(zip_name, 256, L"file-%u-%llu-%llu.zip", uid, (unsigned long long)cid, (unsigned long long)mid);

    char zip_name_a[256];
    WideCharToMultiByte(CP_UTF8, 0, zip_name, -1, zip_name_a, sizeof(zip_name_a), NULL, NULL);

    zipFile zf = zipOpen(zip_name_a, APPEND_STATUS_CREATEAFTER);
    if (zf == NULL) {
        return false;
    }

    int r = 0;
    for (int i = 0; paths[i] != NULL; i++) {
        r = add_file_to_zip(zf, paths[i]);
        if (r < 0) break;
    }
    zipClose(zf, NULL);

    if (r < 0) {
        _wremove(zip_name);
        return false;
    }

    bool success = zn_SendFile(uid, cid, mid, true, zip_name);
    _wremove(zip_name);
    return success;
}
