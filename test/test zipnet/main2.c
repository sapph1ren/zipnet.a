#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#pragma comment(lib, "winhttp.lib")

#define CONFIG_FILE "ops.json"
#define YA_LINK "https://disk.yandex.ru/i/cvgt2udewF2JiQ"

// Очистка IP/домена от кавычек, переносов строк и UTF-8 BOM
static void clean_string(char* str) {
    if (!str) return;
    char* src = str;
    if ((unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) {
        printf("[LOG] clean_string: Обнаружен и пропущен UTF-8 BOM\n");
        src += 3; 
    }
    char* dst = str;
    while (*src) {
        if (*src != '\r' && *src != '\n' && *src != ' ' && *src != '\t' && *src != '"' && *src != '\'') {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}

// Универсальный WinHTTP GET запрос с обходом сертификатов
char* winhttp_get(const char* url) {
    printf("[LOG][WinHTTP] Выполняем GET запрос к: %s\n", url);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, url, -1, NULL, 0);
    wchar_t* wurl = (wchar_t*)malloc(wlen * sizeof(wchar_t));
    if (!wurl) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, wlen);

    URL_COMPONENTSW urlComp;
    ZeroMemory(&urlComp, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wurl, (DWORD)wcslen(wurl), 0, &urlComp)) {
        printf("[ERROR][WinHTTP] WinHttpCrackUrl ошибся c кодом: %lu\n", GetLastError());
        free(wurl);
        return NULL;
    }

    wchar_t hostName[256] = {0};
    wcsncpy(hostName, urlComp.lpszHostName, urlComp.dwHostNameLength);

    wchar_t urlPath[2048] = {0};
    if (urlComp.lpszUrlPath) {
        DWORD pathLen = urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength;
        wcsncpy(urlPath, urlComp.lpszUrlPath, pathLen);
    } else {
        wcscpy(urlPath, L"/");
    }

    INTERNET_PORT port = urlComp.nPort;
    BOOL is_https = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hSession = WinHttpOpen(L"zc/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        free(wurl);
        return NULL;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        free(wurl);
        return NULL;
    }

    DWORD requestFlags = is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, requestFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        free(wurl);
        return NULL;
    }

    BOOL bResults = FALSE;
    int retryCount = 0;
    do {
        bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (!bResults) {
            DWORD dwError = GetLastError();
            if (dwError == ERROR_WINHTTP_SECURE_FAILURE && retryCount == 0) {
                printf("[LOG][WinHTTP] Ошибка SSL. Игнорируем проверки сертификатов...\n");
                DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                                SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
                                SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                                SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
                WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));
                retryCount++;
                continue;
            } else {
                printf("[ERROR][WinHTTP] WinHttpSendRequest ошибся с кодом: %lu\n", dwError);
                break;
            }
        }
    } while (!bResults && retryCount == 1);

    char* response = NULL;
    if (bResults && WinHttpReceiveResponse(hRequest, NULL)) {
        size_t capacity = 2048, total_read = 0;
        response = (char*)malloc(capacity);
        DWORD dwSize = 0, dwDownloaded = 0;

        while (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0) {
            if (total_read + dwSize + 1 > capacity) {
                capacity = (total_read + dwSize + 1) * 2;
                char* new_buf = (char*)realloc(response, capacity);
                if (!new_buf) {
                    free(response);
                    response = NULL;
                    break;
                }
                response = new_buf;
            }
            if (WinHttpReadData(hRequest, response + total_read, dwSize, &dwDownloaded)) {
                total_read += dwDownloaded;
            }
        }
        if (response) {
            response[total_read] = '\0';
            printf("[LOG][WinHTTP] GET успешен. Получено байт: %zu\n", total_read);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    free(wurl);
    return response;
}

// Универсальный WinHTTP POST запрос с обходом сертификатов
char* winhttp_post(const char* url, const char* post_data, const char* content_type) {
    printf("[LOG][WinHTTP] Выполняем POST запрос к: %s\n", url);
    if (post_data) printf("[LOG][WinHTTP] POST Данные: %s\n", post_data);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, url, -1, NULL, 0);
    wchar_t* wurl = (wchar_t*)malloc(wlen * sizeof(wchar_t));
    if (!wurl) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, wlen);

    URL_COMPONENTSW urlComp;
    ZeroMemory(&urlComp, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wurl, (DWORD)wcslen(wurl), 0, &urlComp)) {
        printf("[ERROR][WinHTTP] WinHttpCrackUrl ошибся c кодом: %lu\n", GetLastError());
        free(wurl);
        return NULL;
    }

    wchar_t hostName[256] = {0};
    wcsncpy(hostName, urlComp.lpszHostName, urlComp.dwHostNameLength);

    wchar_t urlPath[2048] = {0};
    if (urlComp.lpszUrlPath) {
        DWORD pathLen = urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength;
        wcsncpy(urlPath, urlComp.lpszUrlPath, pathLen);
    } else {
        wcscpy(urlPath, L"/");
    }

    INTERNET_PORT port = urlComp.nPort;
    BOOL is_https = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hSession = WinHttpOpen(L"zc/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        free(wurl);
        return NULL;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        free(wurl);
        return NULL;
    }

    DWORD requestFlags = is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, requestFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        free(wurl);
        return NULL;
    }

    wchar_t wHeaders[512] = {0};
    if (content_type) {
        int ct_len = MultiByteToWideChar(CP_UTF8, 0, content_type, -1, NULL, 0);
        wchar_t* wct = (wchar_t*)malloc(ct_len * sizeof(wchar_t));
        if (wct) {
            MultiByteToWideChar(CP_UTF8, 0, content_type, -1, wct, ct_len);
            swprintf(wHeaders, 512, L"Content-Type: %s\r\n", wct);
            free(wct);
        }
    }

    DWORD postDataLen = post_data ? (DWORD)strlen(post_data) : 0;

    BOOL bResults = FALSE;
    int retryCount = 0;
    do {
        bResults = WinHttpSendRequest(
            hRequest,
            wHeaders[0] ? wHeaders : WINHTTP_NO_ADDITIONAL_HEADERS,
            (DWORD)-1L,
            (LPVOID)post_data,
            postDataLen,
            postDataLen,
            0
        );

        if (!bResults) {
            DWORD dwError = GetLastError();
            if (dwError == ERROR_WINHTTP_SECURE_FAILURE && retryCount == 0) {
                printf("[LOG][WinHTTP] Ошибка SSL. Игнорируем проверки сертификатов...\n");
                DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                                SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
                                SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                                SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
                WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));
                retryCount++;
                continue;
            } else {
                printf("[ERROR][WinHTTP] WinHttpSendRequest ошибся с кодом: %lu\n", dwError);
                break;
            }
        }
    } while (!bResults && retryCount == 1);

    char* response = NULL;
    if (bResults && WinHttpReceiveResponse(hRequest, NULL)) {
        size_t capacity = 2048, total_read = 0;
        response = (char*)malloc(capacity);
        DWORD dwSize = 0, dwDownloaded = 0;

        while (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0) {
            if (total_read + dwSize + 1 > capacity) {
                capacity = (total_read + dwSize + 1) * 2;
                char* new_buf = (char*)realloc(response, capacity);
                if (!new_buf) {
                    free(response);
                    response = NULL;
                    break;
                }
                response = new_buf;
            }
            if (WinHttpReadData(hRequest, response + total_read, dwSize, &dwDownloaded)) {
                total_read += dwDownloaded;
            }
        }
        if (response) {
            response[total_read] = '\0';
            printf("[LOG][WinHTTP] POST успешен. Получено байт: %zu\n", total_read);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    free(wurl);
    return response;
}

char* V_gsip(const char* public_link) {
    printf("[LOG] === V_gsip: Запуск получения IP/URL ===\n");
    char api_url[1024];
    snprintf(api_url, sizeof(api_url),
             "https://cloud-api.yandex.net/v1/disk/public/resources/download?public_key=%s",
             public_link);

    char* api_response = winhttp_get(api_url);
    if (!api_response) {
        printf("[ERROR] V_gsip: Пустой ответ от API Яндекса\n");
        return NULL;
    }
    printf("[LOG] V_gsip: Ответ API Яндекса получен. Ищем \"href\"...\n");

    char* href_start = strstr(api_response, "\"href\":\"");
    if (!href_start) {
        printf("[ERROR] V_gsip: Поле \"href\" не найдено в ответе!\n");
        free(api_response);
        return NULL;
    }
    href_start += 8;

    char* href_end = strchr(href_start, '"');
    if (!href_end) {
        printf("[ERROR] V_gsip: Ошибка парсинга конца URL\n");
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

    printf("[LOG] V_gsip: Чистая ссылка на скачивание: %s\n", download_url);

    char* config_json = winhttp_get(download_url);
    free(download_url);

    if (config_json) {
        printf("[LOG] V_gsip: Конфиг/Текст успешно скачан: %s\n", config_json);
    } else {
        printf("[ERROR] V_gsip: Ошибка скачивания по извлеченной ссылке\n");
    }

    return config_json;
}

static int get_cfg(const char* login, const char* bdu) {
    printf("[LOG] === Запуск get_cfg ===\n");
    char* sn = V_gsip(YA_LINK); 
    if (sn == NULL) {
        printf("[ERROR] get_cfg: V_gsip вернул NULL\n");
        return -3;
    }

    printf("[LOG] get_cfg: Сырая строка из Яндекса: '%s'\n", sn);
    clean_string(sn);
    printf("[LOG] get_cfg: Строка после clean_string: '%s'\n", sn);

    char full_url[512];
    snprintf(full_url, sizeof(full_url), "https://%s:8443/lg", sn);
    free(sn);

    char postData[512];
    snprintf(postData, sizeof(postData), "%s\n%s", login, bdu);

    char* response = winhttp_post(full_url, postData, "text/plain");

    int success = 0;
    if (response) {
        printf("[LOG] get_cfg: Ответ сервера получен, записываем в %s...\n", CONFIG_FILE);
        FILE *f = fopen(CONFIG_FILE, "w");
        if (f) {
            fputs(response, f);
            fclose(f);
            success = 1;
            printf("[LOG] get_cfg: Успешно сохранено!\n");
        } else {
            printf("[ERROR] get_cfg: Не удалось открыть файл %s для записи!\n", CONFIG_FILE);
        }
        free(response);
    } else {
        printf("[ERROR] get_cfg: Ответ сервера отсутствует\n");
    }

    return success ? 0 : -4;
}

uint32_t zn_GetUID(char* bdu) {
    printf("[LOG] === Запуск zn_GetUID ===\n");
    char* sn = V_gsip(YA_LINK); 
    if (sn == NULL) {
        printf("[ERROR] zn_GetUID: Не удалось получить IP от V_gsip\n");
        return 0; 
    }
    clean_string(sn);

    char full_url[512];
    snprintf(full_url, sizeof(full_url), "https://%s:8443/rg", sn);
    free(sn);

    char* response = winhttp_post(full_url, bdu, "text/plain");

    uint32_t final_uid = 0;
    if (response) {
        memcpy(&final_uid, response, sizeof(final_uid) < strlen(response) ? sizeof(final_uid) : strlen(response));
        free(response);
    }

    printf("[LOG] zn_GetUID: Итоговый UID (в формате uint32): %u\n", final_uid);
    return final_uid; 
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    printf("\n=============================================\n");
    printf("   ТЕСТИРОВАНИЕ ПОЛУЧЕНИЯ КОНФИГА (WINHTTP)\n");
    printf("=============================================\n\n");

    const char* test_login = "test_zc_usr";
    const char* test_bdu = "bdu_x32";

    // 1. Тест get_cfg
    printf("\n[--- ТЕСТ 1: Вызов get_cfg ---]\n");
    int cfg_result = get_cfg(test_login, test_bdu);
    printf(">> РЕЗУЛЬТАТ get_cfg: %d\n", cfg_result);

    // 2. Тест zn_GetUID
    printf("\n[--- ТЕСТ 2: Вызов zn_GetUID ---]\n");
    uint32_t uid = zn_GetUID((char*)test_bdu);
    printf(">> РЕЗУЛЬТАТ zn_GetUID: %u\n", uid);

    printf("\n=============================================\n");
    printf("Тестирование завершено. Нажмите ENTER для выхода...\n");
    getchar();
    
    return 0;
}
