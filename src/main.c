/* FINDEN is part of a learning project;
 * toq 2025  LICENSE: BSD 2-Clause "Simplified"
 *
 *
 * Mit ICON:
 * gcc $(pkg-config --cflags gtk4 libadwaita-1) -o fullscreen main.c free.basti.oledsaver.gresource.c $(pkg-config --libs gtk4 libadwaita-1)
 *
 * Ohne ICON:
 * gcc $(pkg-config --cflags gtk4 libadwaita-1) -o finden main.c $(pkg-config --libs gtk4 libadwaita-1)
 *
 * Please note:
 * The Use of this code and execution of the applications is at your own risk, I accept no liability!
 * 
 * Version 0.9.1  free.basti.oledsaver (Festerbasis von finden v0.6.1)
 */
#include <glib.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include "icon-gresource.h" //h zu binäres Icon
#include <signal.h>
#include <locale.h> 
#include <glib/gi18n.h>
//#include "finden.gresource.h"   /* enthält resources_get_resource() */

/* globale Referenz, wird beim UI-Aufbau gesetzt */
static GtkCheckButton *fullscr_check = NULL;
static gboolean dialog_finished = FALSE;
static pid_t inhibit_pid = 0;  // PID des systemd-inhibit Prozesses

/* Hindergrundprozess wird gestartet um Standby zu vermeiden */
// Startet systemd-inhibit im Hintergrund
static void start_standby_prevention(void) {
    if (inhibit_pid == 0) {               // Prozess starten wenn nicht schon einer läuft! 
        pid_t pid = fork();               // fork erzeugt Kindprozess, wenn inhibit_pid nicht 1 ist!
        if (pid == 0) {
            execlp("systemd-inhibit",     // Kindprozess startet systemd-inhibit sleep infinity
                   "systemd-inhibit",
                   "--what=idle:sleep:shutdown",
                   "--who=OLED-Saver",
                   "--why=Prevent Standby and Screen Lock",
                   "sleep", "infinity",
                   NULL);
            _exit(1); // falls execlp fehlschlägt
        } else if (pid > 0) {
            inhibit_pid = pid;
            g_print("Standby blockiert von Prozess (PID: %d)\n", inhibit_pid);
        } else {
            g_warning("Fehler beim Erstellen des Kindprozesses!\n");
        }
    }
}


/* Hindergrundprozess eliminieren */
// Beenden des systemd-inhibit Prozesses beim App-Ende
static void stop_standby_prevention(void) {
    if (inhibit_pid > 0) {
        g_print(_("Kill Prozess (PID: %d )\n"), (int)inhibit_pid);
        if (kill(inhibit_pid, SIGTERM) == -1) {
            g_warning("Fehler beim Beenden von PID %d\n", (int)inhibit_pid);
        }
        inhibit_pid = 0;
    }
}

// TEST STANDBY NEUE METHODE
static void new_start_standby_prevention(void) {
    system("systemd-inhibit --what=idle:sleep --why='OLED-Saver aktiv' sleep infinity &");
}


/* --- Mausbewegung beendet Fullscreen Fenster, 
  reaktiviert von enable_mouse_exit_after_delay() */
static gboolean
on_mouse_move_exit_fullscreen(GtkEventControllerMotion *controller,
                              gdouble x, gdouble y,
                              gpointer user_data)
{
    GtkWindow *window = GTK_WINDOW(user_data);

    g_idle_add((GSourceFunc)gtk_window_destroy, window);
        g_print (_("Motionkontroller hat Fullscreen unterbrochen \n"));
    g_signal_handlers_disconnect_by_func(controller,
                                         on_mouse_move_exit_fullscreen,
                                         user_data);
    return GDK_EVENT_STOP;
}


/* ----- Message / Alert-Dialog Generisch,  show_alert_dialog (parent,*Titel, *Inhalttext) ----- */
static void on_alert_dialog_response (AdwAlertDialog *dialog,
                          const char     *response,
                          gpointer        user_data)
{
    if (g_strcmp0 (response, "ok") == 0)
        g_print ("Dialog btn - ok\n");
    else
        g_print ("Dialog btn - cancel\n");

    /* **Wichtig:** hier kein g_object_unref(dialog) ! */
}

/* ----- Callback Alert-Dialog anzeigen (generisch) ----- */
static void
show_alert_dialog (GtkWindow   *parent,
                   const char  *title,
                   const char  *body)
{
    if (!parent || !GTK_IS_WINDOW (parent)) {
        g_warning (_("Kein gültiges Elternfenster für Alert-Dialog \n"));
        return;
    }

    /* Dialog erzeugen – Titel und Body werden übergeben */
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (title, body));

    /* Buttons hinzufügen */
    adw_alert_dialog_add_response (dialog, "cancel", _("Abbrechen"));
    adw_alert_dialog_add_response (dialog, "ok",     _("OK"));
    adw_alert_dialog_set_default_response (dialog, "ok");

    /* Antwort‑Signal verbinden */
    g_signal_connect (dialog, "response",
                      G_CALLBACK (on_alert_dialog_response), NULL);

    /* Dialog präsentieren */
    adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (parent));
}

/* ---- Callback: About-Dialog öffnen ----- */
static void show_about (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    AdwApplication *app = ADW_APPLICATION (user_data);
    /* About‑Dialog anlegen */
    AdwAboutDialog *about = ADW_ABOUT_DIALOG (adw_about_dialog_new ());
    //adw_about_dialog_set_body(about, "Hierbei handelt es sich um ein klitzekleines Testprojekt."); //nicht in meiner adw Version?
    adw_about_dialog_set_application_name (about, "OLED-Saver");
    adw_about_dialog_set_version (about, "0.9.1");
    adw_about_dialog_set_developer_name (about, "toq");
    adw_about_dialog_set_website (about, "https://github.com/super-toq");

    /* Lizenz – BSD2 wird als „custom“ angegeben */
    adw_about_dialog_set_license_type (about, GTK_LICENSE_CUSTOM);
    adw_about_dialog_set_license (about,
        "BSD 2-Clause License\n\n"
        "Copyright (c) 2025, toq\n"
        "Redistribution and use in source and binary forms, with or without "
        "modification, are permitted provided that the following conditions are met: "
        "1. Redistributions of source code must retain the above copyright notice, this "
        "list of conditions and the following disclaimer.\n"
        "2. Redistributions in binary form must reproduce the above copyright notice, "
        "this list of conditions and the following disclaimer in the documentation"
        "and/or other materials provided with the distribution.\n\n"
        "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ''AS IS'' "
        "AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE "
        "IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE "
        "DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE "
        "FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL "
        "DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR "
        "SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER "
        "CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, "
        "OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE "
        "OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n\n"
        "Application Icon by SVG. \n"
        "https://www.svgrepo.com \n"
        "Respect and thanks to SVG for free use. \n"
        "LICENSE for the icon: \n"
        "CC Attribution License \n"
        "Follow the link to view details of the CC Attribution License: \n"
        "https://www.svgrepo.com/page/licensing/#CC%20Attribution \n");

//    adw_about_dialog_set_translator_credits (about, "toq: deutsch\n toq: englisch");
//    adw_about_dialog_set_application_icon (about, "free.basti.oledsaver");   //IconName

    /* Dialog modal zum aktiven Fenster zeigen */
    GtkWindow *parent = gtk_application_get_active_window (GTK_APPLICATION (app));
    adw_dialog_present (ADW_DIALOG (about), GTK_WIDGET (parent));
}
/* ----- Callback Beenden-Button ----- */
static void on_quitbutton_clicked (GtkButton *button, gpointer user_data)
{
    g_application_quit (G_APPLICATION (user_data));
}

/* Motion-Handler, Wartezeit */
static gboolean
enable_mouse_exit_after_delay(gpointer user_data)
{
    GtkEventController *motion = GTK_EVENT_CONTROLLER(user_data);
    
    /* Motion-Handler aktivieren */
    gtk_event_controller_set_propagation_phase(motion, GTK_PHASE_TARGET);
    
    return G_SOURCE_REMOVE;  // Timer nur einmal ausführen
}

/* Fullscreen-Button */
static void
on_fullscreen_button_clicked(GtkButton *button, gpointer user_data)
{
   /* 1. Blackscreen erzeugen, hier als Fenster */
    GtkApplication *app = GTK_APPLICATION(user_data);

    /* CSS-Provider mit schwarzem Hintergrund */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        ".fullscreen-window { background-color: black; }");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);

    /* Vollbild-Fenster */
    GtkWidget *fullscreen_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(fullscreen_window), "Vollbild");
    gtk_widget_add_css_class(fullscreen_window, "fullscreen-window");
    gtk_window_fullscreen(GTK_WINDOW(fullscreen_window));
    gtk_window_present(GTK_WINDOW(fullscreen_window));

    /* Motion-Controller erstellen */
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion",
                     G_CALLBACK(on_mouse_move_exit_fullscreen), fullscreen_window);
    gtk_widget_add_controller(fullscreen_window, motion);

    /* Motion-Handler erst deaktivieren */
    gtk_event_controller_set_propagation_phase(motion, GTK_PHASE_NONE);

    /* Sekunden-Verzögerung zur Aktivierung des Motion-Handlers */
    g_timeout_add_seconds(1, enable_mouse_exit_after_delay, motion);

    /* 2. Standbyzeit */
    start_standby_prevention();

}

/* --------------------------------------------------------------------------- */
/*       Aktivierungshandler                                                   */
/* ----- CALLBACK-Funktion wird aufgerufen wenn Anwendung aktiviert wird ----- */
static void on_activate (AdwApplication *app, gpointer)
{
    /* ----- Adwaita-Fenster ------------------------ */
    AdwApplicationWindow *win = ADW_APPLICATION_WINDOW (adw_application_window_new (GTK_APPLICATION (app))); 

    gtk_window_set_title (GTK_WINDOW(win), "OLED-Saver");   // Fenstertitel
    gtk_window_set_default_size (GTK_WINDOW(win), 380, 400);  // Standard-Fenstergröße
    gtk_window_present (GTK_WINDOW(win));                     // Fenster anzeigen lassen

    /* ----- ToolbarView (Root‑Widget) erstellt und als Inhalt des Fensters festgelegt -- */
    AdwToolbarView *toolbar_view = ADW_TOOLBAR_VIEW (adw_toolbar_view_new ());
    adw_application_window_set_content (win, GTK_WIDGET (toolbar_view));

    /* ----- HeaderBar mit TitelWidget erstellt und dem ToolbarView hinzugefügt ------------ */
    AdwHeaderBar *header = ADW_HEADER_BAR (adw_header_bar_new());
    /* Label mit Pango‑Markup erzeugen */
    GtkLabel *title_label = GTK_LABEL(gtk_label_new (NULL));
    gtk_label_set_markup (title_label, "<b>Basti's OLED-Saver</b>");                  // Fenstertitel in Markup
    gtk_label_set_use_markup (title_label, TRUE);                        //Markup‑Parsing aktivieren
    adw_header_bar_set_title_widget (header, GTK_WIDGET (title_label)); //Label als Title‑Widget einsetzen
    adw_toolbar_view_add_top_bar (toolbar_view, GTK_WIDGET (header));  //Header‑Bar zur Toolbar‑View hinzuf

    /* --- Hamburger‑Button innerhalb der Headerbar --- */
    GtkMenuButton *menu_btn = GTK_MENU_BUTTON (gtk_menu_button_new ());
    gtk_menu_button_set_icon_name (menu_btn, "open-menu-symbolic");
    adw_header_bar_pack_start (header, GTK_WIDGET (menu_btn));

    /* --- Popover‑Menu im Hamburger --- */
    GMenu *menu = g_menu_new ();
    g_menu_append (menu, _("Über OLED-Saver"), "app.show-about");
    GtkPopoverMenu *popover = GTK_POPOVER_MENU (
        gtk_popover_menu_new_from_model (G_MENU_MODEL (menu)));
    gtk_menu_button_set_popover (menu_btn, GTK_WIDGET (popover));

    /* --- Action die den About‑Dialog öffnet --- */
    const GActionEntry entries[] = {
        { "show-about", show_about, NULL, NULL, NULL }
    };
    g_action_map_add_action_entries (G_ACTION_MAP (app), entries, G_N_ELEMENTS (entries), app);


    /* ---- Haupt‑Box erstellen ----------------------------------------------------------- */
    GtkBox *main_box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 12));
    gtk_widget_set_margin_top    (GTK_WIDGET (main_box), 20);
    gtk_widget_set_margin_bottom (GTK_WIDGET (main_box), 20);
    gtk_widget_set_margin_start  (GTK_WIDGET (main_box), 20);
    gtk_widget_set_margin_end    (GTK_WIDGET (main_box), 20);
    gtk_widget_set_hexpand (GTK_WIDGET (main_box), TRUE);
    gtk_widget_set_vexpand (GTK_WIDGET (main_box), TRUE);

    /* ----- Text-Label 1 erstellen  ----- */
    GtkWidget *label1 = gtk_label_new(_("Blackscreen statt Burnin! \n"));
    gtk_widget_set_halign (label1, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (label1, GTK_ALIGN_CENTER);

    /* ----- Label als Inhalt zur hinzufügen ----- */ 
    gtk_box_append (main_box, label1);

    // Icon erstellen weather-clear-night-large
//    GtkWidget *icon = gtk_image_new_from_icon_name("weather-clear-night-large");
    GtkWidget *icon = gtk_image_new_from_resource("/free/basti/oledsaver/icon"); //alias in xml !
    // Icon horizontal zentrieren
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 128);
    gtk_box_append(GTK_BOX(main_box), icon);

    /* ----- Text-Label 2 erstellen ----- */
    GtkWidget *label2 = gtk_label_new(_("Bei Fullscreen kein Standby."));
    gtk_widget_set_halign (label2, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (label2, GTK_ALIGN_CENTER);
    gtk_box_append (main_box, label2);
    /* ----- Checkbutton oberhalb der Schaltfläche (horizontal) ----- */
    static GtkWidget *chbx_box   = NULL;   /* Gtk-Box */

     /* Kontrollkästchen/Checkbox mit Namen "..." */
    if (!fullscr_check) {
        fullscr_check = GTK_CHECK_BUTTON (gtk_check_button_new_with_label ("checkbox"));
        gtk_check_button_set_active (fullscr_check, FALSE);
    }

    /* Horizontales Box‑Widget nur einmal erzeugen */
    if (!chbx_box) {
        chbx_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_hexpand (chbx_box, TRUE);
        /* Beide Widgets nur hier einfügen – danach besitzen sie einen Eltern‑Container */
        gtk_box_append (GTK_BOX (chbx_box), GTK_WIDGET (fullscr_check));
           /* Checkbox ist unsichtbar !! */
        gtk_widget_set_visible(GTK_WIDGET(fullscr_check), FALSE);
    }
    
    /* button_box erstellen, wo Schaltfläche hineinkommen */
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    /* Das bereits vorbereitete horizontale Box‑Widget in die vertikale Haupt‑Box einfügen */
    gtk_box_append (main_box, chbx_box);

    /* --- Schaltfläche-Fullscreen:  ------------------------------------------------- */
    GtkWidget *setfullscreen_button = gtk_button_new_with_label (_("Fullscreen"));
    gtk_widget_set_halign (setfullscreen_button, GTK_ALIGN_CENTER);
    g_signal_connect (setfullscreen_button, "clicked",
                  G_CALLBACK (on_fullscreen_button_clicked), app);

    /* ----- Schaltfläche Beenden erzeugen ----- */
    GtkWidget *quit_button = gtk_button_new_with_label(_(" Beenden "));
    gtk_widget_set_halign(quit_button, GTK_ALIGN_CENTER);

    /* ----- Callback auslösen ----- */
    g_signal_connect(quit_button, "clicked", G_CALLBACK(on_quitbutton_clicked), app);

    /* ----- Schaltflächen der button_box hinzufügen ----- */
    gtk_box_append(GTK_BOX(button_box), quit_button);
    gtk_box_append(GTK_BOX(button_box), setfullscreen_button);


    /* ----- button_box der Hauptbox (box) hinzufügen ----- */
    gtk_widget_set_valign(button_box, GTK_ALIGN_END);    // Ausrichtung nach unten
    gtk_widget_set_halign(button_box, GTK_ALIGN_CENTER); // Ausrichtung mittig
    gtk_box_append(GTK_BOX(main_box), button_box);
    gtk_widget_set_vexpand(button_box, TRUE);            // Platz über Buttons ausdehnen
    
    /* -----  Box zur ToolbarView hinzufügen ------------ */
    adw_toolbar_view_set_content(toolbar_view, GTK_WIDGET(main_box));

    /* ----- System-Icon ----- */

    /* ----- Fenster desktop‑konform anzeigen ----- */
    gtk_window_present(GTK_WINDOW(win));
    

}

/* ---------------------------------------------------------------------------
 * Anwendungshauptteil, main()
 * --------------------------------------------------------------------------- */
int main (int argc, char **argv)
{
    char *app_dir = g_get_current_dir();  // Ermit. den aktuellen Arbeitsverzeichnis-Pfad
    const char *locale_path = NULL;
    const char *flatpak_id = getenv("FLATPAK_ID"); //flatpak string free.toq.finden anderenfalls NULL !

    /* Resource‑Bundle (....gresource) registrieren um den Inhalt verfügbar zu machen */
    g_resources_register (resources_get_resource ()); // reicht für Icon innerhalb der App

    /*  Icon aus GResource für AnwendungsIcon laden */
/*    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_resource("/free/basti/oledsaver/icon", &error);
    if (error != NULL) {
        g_print("Fehler beim Laden des Icons: %s\n", error->message);
        g_error_free(error);
    pixbuf = NULL;
     }
*/    

    /* ----- Erstelle den Pfad zu den locale-Dateien ----------------------------------- */
    setlocale(LC_ALL, "");
    textdomain("oledsaver");
    bind_textdomain_codeset("oledsaver", "UTF-8"); // Basisverzeichnis für Übersetzungen
    if (flatpak_id != NULL && flatpak_id[0] != '\0')  // Wenn ungleich NULL:
    {
        locale_path = "/app/share/locale"; // Flatpakumgebung /app/share/locale
    } else {
        locale_path = "/usr/share/locale"; // Native Hostumgebung /usr/share/locale
    }
    bindtextdomain("oledsaver", locale_path);
//    g_print (_("Lokalisierung in: %s \n"), locale_path); // testen

    g_autoptr (AdwApplication) app =                        // Instanz erstellen + App-ID + Default-Flags;
        adw_application_new ("free.basti.oledsaver", G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL); // Signal mit on_activate verbinden
    g_signal_connect(app, "shutdown", G_CALLBACK(stop_standby_prevention), NULL);
    /* --- g_application_run startet Anwendung u. wartet auf Ereignis --- */
    return g_application_run (G_APPLICATION (app), argc, argv);
}