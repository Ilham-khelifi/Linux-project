🐧 Implémentation sous Linux – Dissimulation et Inspection de Processus
📌 Description du projet

Ce projet vise à explorer les mécanismes internes du système Linux liés à la gestion des processus, en mettant l’accent sur :

l’inspection des informations du Process Control Block (PCB),

la dissimulation avancée d’un processus au niveau du noyau,

la persistance automatique après redémarrage,

l’intégration avec un environnement graphique.

L’objectif est pédagogique et expérimental, dans un contexte d’étude des systèmes d’exploitation et de la sécurité système.

🎯 Problématique

Linux impose plusieurs contraintes architecturales importantes :

🔒 Accès aux structures internes

Les structures internes du noyau (ex. task_struct) ne sont pas accessibles depuis l’espace utilisateur. Il est donc nécessaire d’obtenir des informations détaillées sur les processus sans utiliser d’outils externes comme ps.

👻 Invisibilité réelle

Les outils de monitoring (ps, top, htop) utilisent la même source de vérité fournie par le noyau. Une simple modification du nom du processus est insuffisante.
Le défi est de modifier le comportement du noyau lui-même.

🔁 Persistance et privilèges

Le processus doit :

redémarrer automatiquement après un reboot,

fonctionner sans intervention humaine,

disposer des privilèges nécessaires pour interagir avec le noyau,

rester compatible avec une interface graphique.

🧠 Solutions techniques retenues
🔍 Inspection des processus

Parsing direct de /proc
Le système de fichiers virtuel /proc fournit une vue directe et standard des informations internes des processus (PID, UID, mémoire, état).

🕶️ Dissimulation du processus

LD_PRELOAD (rejetée)
Méthode simple mais facilement détectable et inefficace face aux exécutables statiques.

Loadable Kernel Module (LKM) – retenue
Insertion d’un module noyau permettant de modifier dynamiquement le comportement interne du système, notamment la visibilité des processus.

🔁 Persistance et automatisation

Systemd (rejetée)
Les services démarrent avant l’interface graphique.

XDG Autostart + Sudoers (retenue)

Lancement automatique à l’ouverture de la session graphique

Exécution avec privilèges élevés sans saisie de mot de passe

🖥️ Interface utilisateur

GTK+ (C)
Permet une visualisation en temps réel des informations du processus et le contrôle du module noyau via une interface graphique.

🏗️ Architecture et implémentation
1️⃣ Création et inspection du processus

Utilisation de fork() pour créer un processus enfant

Lecture et analyse de /proc/self/status pour extraire les données du PCB

2️⃣ Dissimulation via module noyau

Module noyau compilé dynamiquement à l’exécution

Code du module intégré directement dans l’application utilisateur

Hook des opérations du système de fichiers /proc

Filtrage des entrées correspondant au PID ciblé

✅ Résultat :
Le répertoire /proc/[PID] devient invisible, rendant le processus indétectable par les outils standards.

3️⃣ Persistance après redémarrage

Création automatique d’un fichier .desktop dans ~/.config/autostart

Lancement via un script intermédiaire

Configuration des règles de privilèges pour une exécution sans interaction
