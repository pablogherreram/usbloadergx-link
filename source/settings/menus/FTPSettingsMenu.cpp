/* USB Loader GX Link FTP settings UI. */
#include "FTPSettingsMenu.hpp"
#include "settings/CSettings.h"
#include "network/networkops.h"
#include "network/ftp/FTPServer.h"
#include "prompts/PromptWindows.h"
#include "language/gettext.h"
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <string.h>

FTPSettingsMenu::FTPSettingsMenu() : SettingsMenu(tr("FTP Settings"), &GuiOptions, MENU_NONE) { SetOptionNames(); SetOptionValues(); }
FTPSettingsMenu::~FTPSettingsMenu() { Settings.Save(); }

void FTPSettingsMenu::SetOptionNames() {
    int i=0;
    Options->SetName(i++, "%s", tr("FTP server"));
    Options->SetName(i++, "%s", tr("Status"));
    Options->SetName(i++, "%s", tr("IP address"));
    Options->SetName(i++, "%s", tr("Port"));
    Options->SetName(i++, "%s", tr("Username"));
    Options->SetName(i++, "%s", tr("Password"));
    Options->SetName(i++, "%s", tr("Change password"));
    Options->SetName(i++, "%s", tr("Generate new password"));
    Options->SetName(i++, "%s", tr("Write access"));
    Options->SetName(i++, "%s", tr("Accessible root"));
}
void FTPSettingsMenu::SetOptionValues() {
    int i=0;
    Options->SetValue(i++, "%s", Settings.FTPEnabled ? tr("On") : tr("Off"));
    Options->SetValue(i++, "%s", FTPServer::GetStatusText());
    Options->SetValue(i++, "%s", IsNetworkInit() ? GetNetworkIP() : tr("Not connected"));
    Options->SetValue(i++, "%d", Settings.FTPPort);
    Options->SetValue(i++, "wii");
    Options->SetValue(i++, "%s", Settings.FTPPassword);
    Options->SetValue(i++, " ");
    Options->SetValue(i++, " ");
    Options->SetValue(i++, "%s", Settings.FTPWriteAccess ? tr("On") : tr("Off"));
    Options->SetValue(i++, "sd:/");
}
void FTPSettingsMenu::GeneratePassword() {
    static const char chars[]="ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    u64 seed=gettime();
    for(int i=0;i<10;i++){ seed=seed*6364136223846793005ULL+1; Settings.FTPPassword[i]=chars[(seed>>32)%(sizeof(chars)-1)]; }
    Settings.FTPPassword[10]=0;
}
int FTPSettingsMenu::GetMenuInternal() {
    int ret=optionBrowser->GetClickedOption(); if(ret<0){SetOptionValues();return MENU_NONE;}
    if(ret==0){
        if(!Settings.FTPEnabled && !Settings.FTPPassword[0]) GeneratePassword();
        Settings.FTPEnabled = !Settings.FTPEnabled;
        Settings.Save();
        FTPServer::Restart();
    } else if(ret==6){
        char entered[33]; strlcpy(entered,Settings.FTPPassword,sizeof(entered));
        if(OnScreenKeyboard(entered,sizeof(entered),0,false,true)){
            if(strlen(entered)<8) WindowPrompt(tr("Invalid password"),tr("The FTP password must contain at least 8 characters."),tr("OK"));
            else { strlcpy(Settings.FTPPassword,entered,sizeof(Settings.FTPPassword)); Settings.Save(); FTPServer::Restart(); }
        }
    } else if(ret==7){
        if(WindowPrompt(tr("Generate new password"),tr("Replace the current FTP password? Existing clients will need the new password."),tr("Yes"),tr("Cancel"))==1){GeneratePassword();Settings.Save();FTPServer::Restart();}
    } else if(ret==8){Settings.FTPWriteAccess=!Settings.FTPWriteAccess;Settings.Save();}
    SetOptionValues(); return MENU_NONE;
}
