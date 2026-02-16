===================
    PROJET : SE
===================

-----------------------------------------------------------
1. DESCRIPTION
-----------------------------------------------------------

Application avec une interface graphique (GTK) qui démontre comment cacher un processus sous Linux en utilisant un module noyau (LKM) compilé à la volée.

-----------------------------------------------------------
2. PRÉREQUIS
-----------------------------------------------------------

Les paquets suivants sont nécessaires :
- build-essential (gcc, make)
- libgtk-3-dev
- linux-headers-$(uname -r)

Commandes d'installation (Debian/Ubuntu) :
sudo apt-get update
sudo apt-get install build-essential libgtk-3-dev linux-headers-$(uname -r)

-----------------------------------------------------------
3. INSTRUCTIONS
-----------------------------------------------------------
1. RENDRE LE PROCESSSUS EXECUTABLE
	`chmod +x gui_process_hider`
	
2.  EXÉCUTER (avec droits root) :
    `sudo ./gui_process_hider`

3.  DÉSINSTALLER :
    `sudo ./gui_process_hider --uninstall`