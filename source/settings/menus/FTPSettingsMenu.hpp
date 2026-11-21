#ifndef FTP_SETTINGS_MENU_HPP
#define FTP_SETTINGS_MENU_HPP
#include "SettingsMenu.hpp"
class FTPSettingsMenu : public SettingsMenu {
public:
    FTPSettingsMenu();
    virtual ~FTPSettingsMenu();
protected:
    void SetOptionNames();
    void SetOptionValues();
    int GetMenuInternal();
    void GeneratePassword();
    OptionList GuiOptions;
};
#endif
