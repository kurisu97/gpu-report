SHELL := /bin/sh

CMAKE ?= cmake
CTEST ?= ctest
BUILD_DIR ?= build
BUILD_TYPE ?= Release
PREFIX ?= $(HOME)/.local
JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)

.PHONY: all configure build install test clean help

all: build

configure:
	@echo "==> Настройка сборки ($(BUILD_TYPE)) в $(BUILD_DIR)"
	$(CMAKE) -S . -B "$(BUILD_DIR)" \
		-DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		-DBUILD_TESTING=ON

build: configure
	@echo "==> Сборка GPU Report"
	$(CMAKE) --build "$(BUILD_DIR)" --parallel "$(JOBS)"

install: build
	@echo "==> Установка GPU Report в $(PREFIX)"
	$(CMAKE) --install "$(BUILD_DIR)" --prefix "$(PREFIX)"
	@echo "==> Готово: $(PREFIX)/bin/gpu-report"

test: build
	@echo "==> Запуск тестов"
	$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure

clean:
	@echo "==> Удаление каталога $(BUILD_DIR)"
	$(CMAKE) -E remove_directory "$(BUILD_DIR)"

help:
	@printf '%s\n' \
		'make build                         Собрать проект' \
		'make install                       Установить в ~/.local' \
		'make install PREFIX=/usr/local     Установить системно (обычно через sudo)' \
		'make test                          Собрать и запустить тесты' \
		'make clean                         Удалить каталог сборки' \
		'' \
		'Дополнительные переменные: BUILD_DIR, BUILD_TYPE, PREFIX, JOBS, CMAKE, CTEST'
