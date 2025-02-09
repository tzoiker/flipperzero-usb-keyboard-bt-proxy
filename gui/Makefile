PROJECT_NAME := f0_usb_keyboard_bt_proxy
PROJECT_PATH := $(PROJECT_NAME)

all:
	@echo "make clean              - Clean some generated files"
	@echo "make purge              - Remove virtual env"
	@echo "make develop            - Create virtual env"
	@echo "make pre-commit         - Install pre-commit hooks"
	@echo "make pre-commit-remove  - Remove pre-commit hooks"
	@echo "make lint               - Run linters"
	@echo "make format             - Format code"
	@echo "make build              - Build wheel"
	@echo "make publish            - Publish wheel to PyPI"
	@echo "make release            - Update version and changelog"
	@exit 0

clean:
	rm -fr *.egg-info .tox dist
	find . -iname '*.pyc' -delete

purge: clean
	rm -rf ./.venv

develop: clean
	poetry install
	poetry run pre-commit install

pre-commit:
	poetry run pre-commit install

pre-commit-remove:
	poetry run pre-commit uninstall

mypy:
	poetry run mypy

ruff:
	poetry run ruff check $(PROJECT_PATH)

lint: mypy ruff

format:
	poetry run ruff check $(PROJECT_PATH) --fix-only
	poetry run black $(PROJECT_PATH)
	poetry run ruff check $(PROJECT_PATH)

release:
	@if [[ -z "$(version)" ]]; then \
		echo "Must run the command with the version parameter: make release version=<version>"; \
	else \
		poetry run cz bump --changelog --files-only $(version); \
	fi

build:
	poetry build

publish: build
	poetry publish
