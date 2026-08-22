#define WOLFSSL_DEVELOPER

/* Обязательные системные настройки для Windows (MinGW) */
#define WOLFSSL_MINGW
#define WOLF_ALIGN_BLOCK
#define WOLFSSL_ANY_RECENT_WINDOWS

/* Активация совместимости с OpenSSL и кастомных расширений */
#define OPENSSL_EXTRA
#define OPENSSL_ALL
#define WOLFSSL_CUSTOM_EXT    /* ИСПРАВЛЕНИЕ: Решает ошибку с custom_ext_*_cb */

/* Принудительно включает типы данных для кастомных расширений TLS (custom_ext) */
#define WOLFSSL_OPENVPN
#define HAVE_SUPPORTED_CURVES

/* Активация SNI (Server Name Indication) и расширений TLS */
#define HAVE_SNI
#define HAVE_TLS_EXTENSIONS

/* Активация ECH (Encrypted Client Hello) и зависимостей */
#define HAVE_ECH              /* Активирует wolfSSL_CTX_set_outer_server_name */
#define HAVE_HPKE
#define WOLFSSL_BASE64_DECODE

/* Активация протоколов TLS 1.3 и DTLS */
#define WOLFSSL_TLS13
#define WOLFSSL_DTLS
#define WOLFSSL_DTLS13

/* Дополнительные крипто-алгоритмы, необходимые для ECH/HPKE */
#define HAVE_HKDF
#define HAVE_ECC
#define HAVE_AESGCM
#define HAVE_CHACHA
#define HAVE_POLY1305
#define HAVE_DH
#define HAVE_RSA
#define HAVE_HASHDRBG

/* Настройки оптимизации памяти и генерации ключей */
#define WOLFSSL_LOW_MEMORY
#define WOLFSSL_SMALL_STACK
#define WOLFSSL_STATIC_MEMORY
#define WOLFSSL_KEY_GEN
#define WOLFSSL_SHA512
#define WOLFSSL_SHA256

/* Тайминговая защита / Убирает предупреждение settings.h:4791 */
#define TFM_TIMING_RESISTANT
#define ECC_TIMING_RESISTANT

