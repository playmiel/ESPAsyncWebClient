#!/bin/bash

# Script de test des dépendances pour ESPAsyncWebClient
# Teste la compatibilité avec différentes versions d'AsyncTCP

set -e  # Arrêt en cas d'erreur

# Couleurs pour l'output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
TEST_DIR="/tmp/dep_test"
LIB_DIR="$TEST_DIR/lib/ESPAsyncWebClient"

echo -e "${BLUE}=== Test des dépendances ESPAsyncWebClient ===${NC}"
echo "Date: $(date)"
echo "Répertoire de test: $TEST_DIR"
echo ""

# Fonction pour afficher les résultats
print_result() {
    local env_name="$1"
    local status="$2"
    local duration="$3"
    
    if [ "$status" = "SUCCESS" ]; then
        echo -e "${GREEN}✅ $env_name: RÉUSSI${NC} (${duration}s)"
    else
        echo -e "${RED}❌ $env_name: ÉCHEC${NC} (${duration}s)"
    fi
}

# Fonction pour nettoyer l'environnement de test
cleanup() {
    echo -e "${YELLOW}🧹 Nettoyage...${NC}"
    if [ -d "$TEST_DIR" ]; then
        rm -rf "$TEST_DIR"
    fi
}

# Nettoyage en cas d'interruption
trap cleanup EXIT

# Étape 1: Création de la structure de répertoires
echo -e "${YELLOW}📁 Création de la structure de test...${NC}"
mkdir -p "$LIB_DIR"

# Étape 2: Copie des fichiers de la bibliothèque
echo -e "${YELLOW}📋 Copie des fichiers de la bibliothèque...${NC}"
cp -r /workspaces/ESPAsyncWebClient/* "$LIB_DIR/"

# Vérification que les fichiers essentiels sont présents
if [ ! -f "$LIB_DIR/platformio.ini" ]; then
    echo -e "${RED}❌ Erreur: platformio.ini non trouvé${NC}"
    exit 1
fi

if [ ! -f "$LIB_DIR/test/compile_test.cpp" ]; then
    echo -e "${RED}❌ Erreur: test/compile_test.cpp non trouvé${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Structure de test créée avec succès${NC}"
echo ""

# Étape 3: Test des différents environnements
echo -e "${BLUE}🧪 Début des tests de compilation...${NC}"
echo ""

cd "$LIB_DIR"

# Définition des environnements à tester
declare -A environments=(
    ["compile_test"]="Test de compilation basique (sans réseau)"
    ["esp32dev_asynctcp_dev"]="AsyncTCP ESP32Async/main (développement)"
    ["test_asynctcp_stable"]="AsyncTCP ESP32Async stable"
)

# Variables pour le résumé
total_tests=0
passed_tests=0
failed_tests=0
declare -A results

echo -e "${YELLOW}Environnements à tester:${NC}"
for env in "${!environments[@]}"; do
    echo "  - $env: ${environments[$env]}"
    total_tests=$((total_tests + 1))
done
echo ""

# Exécution des tests
for env in "${!environments[@]}"; do
    echo -e "${BLUE}🔧 Test de l'environnement: $env${NC}"
    echo "Description: ${environments[$env]}"
    
    start_time=$(date +%s)
    
    if pio run -e "$env" --silent; then
        end_time=$(date +%s)
        duration=$((end_time - start_time))
        print_result "$env" "SUCCESS" "$duration"
        results[$env]="SUCCESS:$duration"
        passed_tests=$((passed_tests + 1))
    else
        end_time=$(date +%s)
        duration=$((end_time - start_time))
        print_result "$env" "FAILED" "$duration"
        results[$env]="FAILED:$duration"
        failed_tests=$((failed_tests + 1))
        
        # Affichage des dernières lignes de log en cas d'erreur
        echo -e "${YELLOW}📄 Dernières lignes du log d'erreur:${NC}"
        pio run -e "$env" 2>&1 | tail -10 | sed 's/^/  /'
    fi
    echo ""
done

# Étape 4: Résumé final
echo -e "${BLUE}📊 RÉSUMÉ DES TESTS${NC}"
echo "=================================="
echo "Total des tests: $total_tests"
echo -e "Tests réussis: ${GREEN}$passed_tests${NC}"
echo -e "Tests échoués: ${RED}$failed_tests${NC}"
echo ""

echo "Détails par environnement:"
for env in "${!environments[@]}"; do
    IFS=':' read -r status duration <<< "${results[$env]}"
    if [ "$status" = "SUCCESS" ]; then
        echo -e "  ${GREEN}✅${NC} $env (${duration}s)"
    else
        echo -e "  ${RED}❌${NC} $env (${duration}s)"
    fi
done

echo ""

# Vérification des versions AsyncTCP installées
echo -e "${BLUE}📦 VERSIONS DES DÉPENDANCES${NC}"
echo "=================================="

if [ -d ".pio/libdeps" ]; then
    for env_dir in .pio/libdeps/*/; do
        env_name=$(basename "$env_dir")
        echo "Environnement: $env_name"
        
        if [ -d "$env_dir/AsyncTCP" ]; then
            asynctcp_path="$env_dir/AsyncTCP"
            if [ -f "$asynctcp_path/.git/HEAD" ]; then
                commit=$(cd "$asynctcp_path" && git rev-parse --short HEAD 2>/dev/null || echo "unknown")
                echo "  AsyncTCP: commit $commit"
            else
                echo "  AsyncTCP: version inconnue"
            fi
        fi
        
        if [ -d "$env_dir/ArduinoJson" ]; then
            echo "  ArduinoJson: installé"
        fi
        echo ""
    done
fi

# Code de sortie final
if [ $failed_tests -eq 0 ]; then
    echo -e "${GREEN}🎉 Tous les tests ont réussi !${NC}"
    exit 0
else
    echo -e "${RED}⚠️  $failed_tests test(s) ont échoué${NC}"
    exit 1
fi
