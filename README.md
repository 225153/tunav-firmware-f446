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

### 3. Module Cellulaire (EC200U via UART4)
- **Interface** : `UART4`
- **Broches** :
  - `PA0` (TX vers EC200U)
  - `PA1` (RX depuis EC200U)
- **Configuration** :
  - Baudrate : `115200`
  - Data bits : `8`
  - Parity : `None`
  - Stop bits : `1`

## 📡 Commandes AT Supportées

Le firmware implémente une boucle de test avec les commandes AT suivantes pour valider la connectivité du modem **Quectel EC200U-EU** sur le réseau **Orange Tunisia** :

### Diagnostic Modem
- `AT` - Vérifier la présence du modem
- `AT+CPIN?` - Vérifier le statut de la carte SIM
- `AT+CREG?` - Vérifier l'enregistrement sur le réseau 2G/3G/4G
- `AT+CGREG?` - Vérifier l'enregistrement sur le réseau GPRS/LTE

### Activation GPRS (Orange)
- `AT+QICSGP=1,1,"weborange","","",0` - Configurer le contexte de données avec l'APN Orange
- `AT+QIACT=1` - Activer le contexte GPRS
- `AT+QIACT?` - Vérifier l'activation et l'adresse IP obtenue

### Transmission TCP
- `AT+QIOPEN=1,0,"TCP","41.226.24.13",5000,0,0` - Ouvrir une connexion TCP vers le serveur
- `AT+QISEND=0,5` - Préparer l'envoi de 5 octets
- `AT+QICLOSE=0` - Fermer la connexion TCP

### Qualité du Signal
- `AT+CSQ` - Vérifier la puissance du signal (RSSI)

## 🔧 Utilisation

### Première Utilisation
1. Connectez le **ST-Link V2** à la carte via les broches SWD (SWCLK, SWDIO, GND).
2. Ouvrez le projet dans **VS Code** avec **PlatformIO IDE**.
3. Compilez et téléversez le code :
   ```bash
   pio run --target upload
   ```
4. **⚠️ IMPORTANT** : Après le téléversement, appuyez sur le bouton **RESET** situé près du MCU. Ceci redémarrera le microcontrôleur et lancera le firmware.
5. Ouvrez le **Moniteur Série** pour voir les journaux :
   ```bash
   pio run --target monitor
   ```

### Redémarrage du Firmware
Appuyez sur le bouton **RESET** (à côté du connecteur ST-Link) pour relancer les tests AT à tout moment.

## ⚙️ Détails Techniques
- **Framework** : STM32Cube HAL
- **Horloge** : HSI (Interne) à 16 MHz (Configuré pour la stabilité sur PCB personnalisé).
- **Outil de Flash** : ST-Link V2 avec gestion du Reset matériel (`connect_assert_srst`).
- **Modem** : Quectel EC200U-EU (Réseau cellulaire 4G/2G).
- **Opérateur** : Orange Tunisia (APN : `weborange`).
