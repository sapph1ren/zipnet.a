#include <stdio.h>
#include <stdlib.h>
#include "argon2.h"
#include <string.h>
#include <windows.h>

#define SystemFunction036 NTAPI SystemFunction036
#define RtlGenRandom SystemFunction036
BOOLEAN WINAPI RtlGenRandom(PVOID RandomBuffer, ULONG RandomBufferLength);

int gr(BYTE *buffer, ULONG size){
    if (RtlGenRandom(buffer, size)) {
        printf("WINAPI uspeshno gen rand\n");
        printf("\n");
        return 0;
    } else {
        fprintf(stderr, "WINAPI gen rand kaka :((()))\n");
        return 1;
    }
}

int main(){
	printf("\t\t\t-=C VERSIYA TESTA=-\n");
    // Настройки Argon2id
    uint32_t t_cost = 3;           // 3 итерации
    uint32_t m_cost = 65536;       // 64 MB памяти
    uint32_t parallelism = 2;      // 2 потока
    size_t salt_len = 16;
    size_t hash_len = 32;
    char buffer[128];
    
    printf("Vvedi do 128 bayt na hesh: ");
    if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
        buffer[strcspn(buffer, "\n")] = 0;
        printf("GOVNO TAKOE: %s\n", buffer);
    }
    
    unsigned char salt[16];
    if (gr(salt, sizeof(salt)) != 0) {
        return 1; 
    }
    
    size_t encoded_len = argon2_encodedlen(t_cost, m_cost, parallelism, salt_len, hash_len, Argon2_id);
    char *encoded_hash = malloc(encoded_len);
    if (encoded_hash == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    int rc = argon2id_hash_encoded(t_cost, m_cost, parallelism, buffer, strlen(buffer), salt, salt_len, hash_len, encoded_hash, encoded_len);
    if (rc != ARGON2_OK) {
        fprintf(stderr, "Oshipka prirodi - Argon2: %s\n", argon2_error_message(rc));
        free(encoded_hash);
        return 1;
    }
    
    printf("Vyvod govna: %s\n", encoded_hash);
    char ch;
	ch = getchar();
	
    free(encoded_hash);
    return 0;
}
