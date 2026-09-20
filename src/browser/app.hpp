#pragma once

#include <gtkmm.h>

class HoneyBeeApp : public Gtk::Application {
public:
    HoneyBeeApp();

protected:
    void on_activate() override;
};
