#ifndef SYSTEM_H
#define SYSTEM_H

#include "system_entry.h"

namespace SystemMgr{
    extern void initMenu(SystemEntry**, int);
    extern void startMenu();
    extern void flushFiles();
    extern void stopMenu();
    extern void endMenu();
    extern void drawScreen();
    extern void pauseDraw();
    extern void resumeDraw();
    extern void enterFullScreen();
    extern void exitFullScreen();
    extern void changeMenuState();
    extern SystemEntry* getSystemEntry(unsigned index);
    extern void openEntry(unsigned index); // switch to another launcher app, like picking it in the triangle menu
    void setSystemEntry(SystemEntry* entry, unsigned index);
};

#endif
