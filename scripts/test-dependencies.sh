#!/bin/bash

# Script de test des dépendances pour ESPAsyncWebClient
# Teste la compatibilité avec différentes versions d'AsyncTCP

set -e  # Arrêter en cas d'erreur

echo "=== Test des dépendances ESPAsyncWebClient ==="

# Nettoyer et créer le répertoire de test
echo "Préparation de l'environnement de test..."
rm -rf /tmp/dep_test
mkdir -p /tmp/dep_test/lib/ESPAsyncWebClient

# Copier les fichiers de la bibliothèque
echo "Copie des fichiers de la bibliothèque..."
cp -r /workspaces/ESPAsyncWebClient/* /tmp/dep_test/lib/ESPAsyncWebClient/

# Aller dans le répertoire de test
cd /tmp/dep_test/lib/ESPAsyncWebClient

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
cd /workspaces/ESPAsyncWebClient
rm -rf /tmp/dep_test

echo "Nettoyage terminé."
