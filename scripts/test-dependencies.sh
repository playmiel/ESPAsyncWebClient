#!/bin/bash

# Script de test des dépendances pour ESPAsyncWebClient
# Teste la compatibilité avec différentes versions d'AsyncTCP

set -e  # Arrêter en cas d'erreur

echo "=== Test des dépendances ESPAsyncWebClient ==="

# Déterminer le chemin du dépôt indépendamment de l'environnement
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ORIG_PWD="$(pwd)"
WORKDIR="/tmp/dep_test"
TARGET_DIR="${WORKDIR}/lib/ESPAsyncWebClient"

# Nettoyer et créer le répertoire de test
echo "Préparation de l'environnement de test..."
rm -rf "${WORKDIR}"
mkdir -p "${TARGET_DIR}"

# Copier les fichiers de la bibliothèque
echo "Copie des fichiers de la bibliothèque..."
cp -r "${REPO_ROOT}/." "${TARGET_DIR}/"

# Aller dans le répertoire de test
cd "${TARGET_DIR}"

echo "=== Test 1: AsyncTCP version dev (master) ==="
pio run -e esp32dev_asynctcp_dev
if [ $? -eq 0 ]; then
    echo "✅ Test AsyncTCP dev: RÉUSSI"
else
    echo "❌ Test AsyncTCP dev: ÉCHEC"
    exit 1
fi

echo "=== Test 2: AsyncTCP version stable ==="
pio run -e test_asynctcp_stable
if [ $? -eq 0 ]; then
    echo "✅ Test AsyncTCP stable: RÉUSSI"
else
    echo "❌ Test AsyncTCP stable: ÉCHEC"
    exit 1
fi

echo "=== Test 3: Compilation basique ==="
pio run -e compile_test
if [ $? -eq 0 ]; then
    echo "✅ Test compilation: RÉUSSI"
else
    echo "❌ Test compilation: ÉCHEC"
    exit 1
fi

echo ""
echo "🎉 Tous les tests de dépendances sont passés avec succès!"
echo "✅ AsyncTCP dev (master): Compatible"
echo "✅ AsyncTCP stable: Compatible"
echo "✅ Compilation basique: Compatible"

# Nettoyer
cd "${ORIG_PWD}"
rm -rf "${WORKDIR}"

echo "Nettoyage terminé."
