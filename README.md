# Tunav Firmware - STM32F446RET6

Ce projet est un firmware de base stabilisé pour une carte personnalisée (Custom PCB) basée sur le microcontrôleur **STM32F446RET6**. Il a été migré de STM32CubeIDE vers **PlatformIO** pour une meilleure gestion du développement sous VS Code.

## 🚀 Fonctionnalités actuelles
- **Diagnostic LED** : Clignotement matériel pour vérifier que le MCU est vivant.
- **Console Série** : Sortie de débogage pour surveiller l'état du système.
- **Récupération Automatique** : Configuration OpenOCD optimisée pour éviter les verrouillages (lockout) du port SWD.

## 📌 Configuration Hardware

### 1. LED de Diagnostic
- **Broche** : `PC13`
- **Comportement** : Clignote toutes les 500ms.

### 2. Moniteur Série (UART)
- **Interface** : `USART2`
- **Broches** : 
  - `PA2` (TX)
  - `PA3` (RX)
- **Configuration** :
  - Baudrate : `115200`
  - Data bits : `8`
  - Parity : `None`
  - Stop bits : `1`

## 🛠 Installation & Utilisation

1. Installez l'extension **PlatformIO IDE** dans VS Code.
2. Clonez ce dépôt.
3. Ouvrez le dossier racine dans VS Code.
4. Pour compiler et téléverser :
   - Cliquez sur la flèche `PlatformIO: Upload` en bas de VS Code.
   - Si la carte est verrouillée, maintenez le bouton **RESET** physique, lancez l'upload, et relâchez le bouton dès que `Uploading` apparaît.
5. Pour voir les messages :
   - Cliquez sur l'icône de prise `PlatformIO: Serial Monitor`.

## ⚙️ Détails Techniques
- **Framework** : STM32Cube HAL
- **Horloge** : HSI (Interne) à 16 MHz (Configuré pour la stabilité sur PCB personnalisé).
- **Outil de Flash** : ST-Link V2 avec gestion du Reset matériel (`connect_assert_srst`).
