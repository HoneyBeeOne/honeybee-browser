#include "app.hpp"
#include "window.hpp"

HoneyBeeApp::HoneyBeeApp() : Gtk::Application("com.honeybee.browser") {}

void HoneyBeeApp::on_activate() {
    auto window = new HoneyBeeWindow();
    add_window(*window);
    window->present();
}
