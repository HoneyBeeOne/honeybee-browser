#include <gtkmm.h>
#include <iostream>

class HoneyBeeWindow : public Gtk::Window {
public:
    HoneyBeeWindow() {
        set_title("HoneyBee Browser");
        set_default_size(1024, 768);

        m_label.set_text("HoneyBee Browser\n\nv0.1.0 — Phase 0\n\nThe browser will render here.");
        m_label.set_justify(Gtk::Justification::CENTER);
        m_label.set_margin(24);

        set_child(m_label);
    }

private:
    Gtk::Label m_label;
};

class HoneyBeeApp : public Gtk::Application {
public:
    HoneyBeeApp() : Gtk::Application("com.honeybee.browser") {}

protected:
    void on_activate() override {
        auto window = new HoneyBeeWindow();
        add_window(*window);
        window->present();
    }
};

int main(int argc, char* argv[]) {
    std::cout << "HoneyBee Browser v0.1.0\n";
    std::cout << "Starting GTK 4 application...\n";

    auto app = HoneyBeeApp();
    return app.run(argc, argv);
}
