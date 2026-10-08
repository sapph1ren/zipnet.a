package main

import "C"
import (
	"fmt"
	"os"
	"github.com/xtls/xray-core/core"
	_ "github.com/xtls/xray-core/main/distro/all"
)

var xrayServer *core.Instance

//export StartXray
func StartXray(configPath *C.char) C.int {
	pathStr := C.GoString(configPath)

	file, err := os.Open(pathStr)
	if err != nil {
		fmt.Printf("[Xray Go] Ошибка открытия файла конфига (%s): %v\n", pathStr, err)
		return -1
	}
	defer file.Close() // Файл закроется автоматически при выходе из функции

	config, err := core.LoadConfig("json", file)
	if err != nil {
		fmt.Printf("[Xray Go] Ошибка парсинга JSON: %v\n", err)
		return -1
	}

	server, err := core.New(config)
	if err != nil {
		fmt.Printf("[Xray Go] Ошибка создания инстанса: %v\n", err)
		return -1
	}
	xrayServer = server

	if err := xrayServer.Start(); err != nil {
		fmt.Printf("[Xray Go] Ошибка запуска Xray: %v\n", err)
		return -1
	}

	fmt.Printf("[Xray Go] Сервер успешно запущен из файла: %s\n", pathStr)
	return 0
}

//export StopXray
func StopXray() {
	if xrayServer != nil {
		if err := xrayServer.Close(); err != nil {
			fmt.Printf("[Xray Go] Ошибка при остановке: %v\n", err)
		} else {
			fmt.Println("[Xray Go] Сервер остановлен")
		}
		xrayServer = nil
	}
}

func main() {}
