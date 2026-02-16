=================
    PROJET : SE
=================

-----------------------------------------------------------
1. DESCRIPTION
-----------------------------------------------------------

Application en ligne de commande qui s'auto-installe et lance un processus en arrière-plan. Elle utilise un module noyau (LKM), compilé à la volée, pour rendre ce processus invisible aux outils système standards.

-----------------------------------------------------------
2. PRÉREQUIS
-----------------------------------------------------------

L'étape d'installation intégrée au programme nécessite les paquets suivants :
- build-essential (gcc, make)
- linux-headers-$(uname -r)

Commande d'installation des dépendances (Debian/Ubuntu) :
sudo apt-get update 
sudo apt-get install build-essential linux-headers-$(uname -r)

-----------------------------------------------------------
3. INSTRUCTIONS
-----------------------------------------------------------
1. RENDRE LE PROCESSSUS EXECUTABLE
	`chmod +x my_hidden_process`

2.  INSTALLER ET LANCER :
    Pour l'exécution : `sudo` déclenche l'installation complète et lance le processus pour la première fois.
    `sudo ./my_hidden_process`


3.  DÉSINSTALLER :
    Pour supprimer proprement le programme
    `sudo ./my_hidden_process --uninstall`