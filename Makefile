.PHONY: configure build dev test lint frontend-install backend-install
OPENCV_DIR ?= $(shell if command -v brew >/dev/null 2>&1; then printf "%s/lib/cmake/opencv4" "$$(brew --prefix opencv@4)"; fi)
configure:
	cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release $(if $(OPENCV_DIR),-DOpenCV_DIR=$(OPENCV_DIR))
frontend-install:
	npm --prefix frontend ci
backend-install:
	./scripts/install_onnxruntime_macos.sh
	./scripts/install_hand_models.sh
build: configure
	cmake --build backend/build --parallel
	npm --prefix frontend run build
dev:
	./scripts/dev.sh
test:
	ctest --test-dir backend/build --output-on-failure
	npm --prefix frontend test
	node --test scripts/tests/*.test.mjs
lint:
	npm --prefix frontend run lint
	node scripts/check-repository.mjs

.PHONY: format format-check
format:
	npm --prefix frontend run format
	node scripts/format-native.mjs --write
format-check:
	npm --prefix frontend run format:check
	node scripts/format-native.mjs --check

.PHONY: integration-test
integration-test:
	node scripts/test-integration.mjs

.PHONY: docker-build docker-up docker-down
docker-build:
	docker build -t dualview-lab:local .
docker-up:
	docker compose up --build
docker-down:
	docker compose down

.PHONY: evaluate
evaluate:
	cmake --build backend/build --target geometry_evaluation
	node scripts/evaluate-geometry.mjs
