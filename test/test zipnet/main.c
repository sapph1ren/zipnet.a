#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "libxray.h"
#include "main.h"
#include <wchar.h>
#include <wchar.h>

#ifdef _WIN32
#include <windows.h>
#define sys_sleep(ms) Sleep(ms)
#else
#include <unistd.h>
#define sys_sleep(ms) usleep((ms) * 1000)
#endif

void process_tcp_data(const unsigned char* data, int length) {
    printf("[CLIENT TCP IN] Получено %d байт\n", length);
}

void process_udp_data(const unsigned char* data, int length) {
    printf("[CLIENT UDP IN] Получено %d байт\n", length);
}

int main() {
    wprintf(L"Инициализация клиента...\n");
    
    if (!zn_Init(100, "kopatyc", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")) {
        printf("Ошибка инициализации zn_Init!\n");
        return 1;
    }
    
    wprintf(L"Подключение успешно. Отправка тестовых пакетов...\n");
    sys_sleep(2000);

    wprintf(L"Отправка SYS_TEXT...\n");
    zn_SendSys(100, "{\"tipa\":\"json\", \"pon\":\"da? doooon\"}", SYS_TEXT);

    wprintf(L"Отправка TEXT 1...\n");
    zn_SendText(100, 999111, "Hello from C Client! Packet 1");

    wprintf(L"Отправка TEXT 2...\n");
    zn_SendText(100, 999111, "Hello from C Client! Packet 2");

    wprintf(L"Активация микрофона (Mute/Unmute)...\n");
    zn_MuteUnMute();

    wprintf(L"Все тестовые пакеты отправлены. Вход в режим ожидания.\n");
    
    while (1) {
        sys_sleep(1000);
    }
    
    zn_Shutdown();
    return 0;
}

