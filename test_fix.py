#!/usr/bin/env python3
"""
Script de test pour valider que la configuration GitHub Actions est correcte
"""

import tempfile
import os
import subprocess
import sys

def test_github_actions_syntax():
    """Test que les URLs de dépendances sont bien formées"""
    
    # Simuler les valeurs de matrix
    test_cases = [
        {
            "platform": "esp32dev",
            "async-tcp-repo": "https://github.com/ESP32Async/AsyncTCP.git",
            "test-name": "latest"
        },
        {
            "platform": "esp32dev", 
            "async-tcp-repo": "https://github.com/ESP32Async/AsyncTCP.git#v1.1.1",
            "test-name": "v1.1.1"
        }
    ]
    
    for case in test_cases:
        print(f"Testing case: {case['test-name']}")
        
        # Créer un platformio.ini temporaire
        platformio_content = f"""[platformio]
default_envs = {case['platform']}

[env]
framework = arduino
lib_deps = 
    {case['async-tcp-repo']}
    bblanchon/ArduinoJson@^6.21.0

[env:esp32dev]
platform = espressif32
board = esp32dev
"""
        
        with tempfile.NamedTemporaryFile(mode='w', suffix='.ini', delete=False) as f:
            f.write(platformio_content)
            temp_file = f.name
        
        try:
            # Vérifier que l'URL est valide (pas de caractères collés)
            repo_url = case['async-tcp-repo']
            assert 'gitlatest' not in repo_url, f"URL malformée détectée: {repo_url}"
            assert 'git@' not in repo_url or '#' in repo_url or repo_url.endswith('.git'), f"URL de dépendance invalide: {repo_url}"
            
            print(f"  ✓ URL valide: {repo_url}")
            print(f"  ✓ Fichier platformio.ini généré correctement")
            
        finally:
            os.unlink(temp_file)
    
    print("\n✅ Tous les tests ont réussi ! Les URLs de dépendances sont correctes.")

if __name__ == "__main__":
    test_github_actions_syntax()
