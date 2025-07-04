# Makefile pour ESPAsyncWebClient
# Facilite les tâches de développement et de test

.PHONY: help clean build test test-deps test-examples install-deps

# Aide par défaut
help:
	@echo "ESPAsyncWebClient - Commandes disponibles:"
	@echo ""
	@echo "  make build           - Compiler l'environnement par défaut"
	@echo "  make test            - Exécuter les tests de compilation"
	@echo "  make test-deps       - Tester avec différentes versions d'AsyncTCP"
	@echo "  make test-examples   - Compiler tous les exemples"
	@echo "  make clean           - Nettoyer les fichiers de build"
	@echo "  make install-deps    - Installer les dépendances"
	@echo "  make help            - Afficher cette aide"
	@echo ""

# Construire l'environnement par défaut
build:
	@echo "🔨 Compilation de l'environnement par défaut..."
	pio run -e esp32dev

# Test de compilation basique
test:
	@echo "🧪 Exécution des tests de compilation..."
	pio run -e compile_test

# Test des dépendances
test-deps:
	@echo "🔍 Test des dépendances AsyncTCP..."
	@./scripts/test-dependencies.sh

# Compiler tous les exemples
test-examples:
	@echo "📚 Compilation des exemples..."
	@for example in examples/*/; do \
		if [ -f "$$example"*.ino ]; then \
			echo "Compilation de $$example..."; \
			pio ci "$$example"*.ino --lib="." --board=esp32dev; \
		fi \
	done

# Nettoyer les fichiers de build
clean:
	@echo "🧹 Nettoyage..."
	pio run --target clean
	rm -rf .pio
	rm -rf /tmp/dep_test

# Installer les dépendances
install-deps:
	@echo "📦 Installation des dépendances..."
	pio pkg install

# Test complet (toutes les versions d'AsyncTCP)
test-all: test test-deps test-examples
	@echo "✅ Tous les tests terminés!"

# Test rapide (juste la compilation)
test-quick: test
	@echo "⚡ Test rapide terminé!"
