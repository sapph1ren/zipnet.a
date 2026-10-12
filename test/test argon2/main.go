package main

import (
	"crypto/rand"
	"fmt"
	"log"
	"bufio"
	"os"
	"encoding/base64"
	"golang.org/x/crypto/argon2"
)

const (
	memory      = 64 * 1024 // 64 MB
	iterations  = 3
	parallelism = 2
	saltLength  = 16
	keyLength   = 32
)

func generateSalt(n uint32) ([]byte, error) {
	b := make([]byte, n)
	_, err := rand.Read(b)
	if err != nil {
		return nil, err
	}
	return b, nil
}


func main(){
	fmt.Printf("\t\t\t-=GO VERSIYA TESTA=-\n")
	scanner := bufio.NewScanner(os.Stdin)
	fmt.Print("Vvedi do 128 bayt na hesh: ")

	var input string
	
	if scanner.Scan() {
		input = scanner.Text()
	}
	if err := scanner.Err(); err != nil {
		fmt.Fprintln(os.Stderr, "ASHIPKA PRIRODAH:", err)
	}
	
	fmt.Printf("GOVNO TAKOE: %s\n", input)
	
	
	salt, err := generateSalt(saltLength)
	if err != nil {
		log.Fatal(err)
	}
	
	hash := argon2.IDKey([]byte(input), salt, iterations, memory, parallelism, keyLength)
	b64Salt := base64.RawStdEncoding.EncodeToString(salt)
	b64Hash := base64.RawStdEncoding.EncodeToString(hash)
	fullEncodedHash := fmt.Sprintf("$argon2id$v=%d$m=%d,t=%d,p=%d$%s$%s", argon2.Version, memory, iterations, parallelism, b64Salt, b64Hash)
	fmt.Printf(fullEncodedHash)

	scanner1 := bufio.NewScanner(os.Stdin)
	if !scanner1.Scan(){
		
	}
	
}
