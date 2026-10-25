/* Game Manager class */

#include <sstream>
#include <fstream>
#include <unistd.h>
#include <algorithm>
#include <stdlib.h>
#include <systemctrl.h>
#include "system_mgr.h"
#include "gamemgr.h"
#include "music_player.h"
#include "osk.h"
#include "lang.h"
#include "texteditor.h"
#include "usb.h"
#include <psppower.h>

static GameManager* self = NULL;

static bool loadingData = false;

ARKConfig* ark_config;

GameManager* GameManager::getInstance(){
    if (self == NULL) self = new GameManager();
    return self;
}

GameManager::GameManager(){
    // set the global self variable as this instance for the threads to use it
    self = this;

    scroll.w = 0;
    this->use_categories = true;
    this->scanning = true;
    this->optionsmenu = NULL;
    this->dock_focus = -1;
    this->cc_focus = -1;

    // initialize the categories
    this->selectedCategory = (common::getConf()->main_menu)? -2 : -1;
    for (int i=0; i<MAX_CATEGORIES; i++){
        this->categories[i] = new Menu((EntryType)i);
    }

    // start the multithreaded icon loading
    this->maxDraw = MAX_CATEGORIES;
    this->dynamicIconRunning = ICONS_LOADING;
    this->iconSema = sceKernelCreateSema("icon0_sema",  0, 1, 1, NULL);
    this->iconThread = sceKernelCreateThread("icon0_thread", GameManager::loadIcons, 0x10, 0x20000, PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU, NULL);
    sceKernelStartThread(this->iconThread,  0, NULL);
}

GameManager::~GameManager(){
    for (int i=0; i<MAX_CATEGORIES; i++){
        delete this->categories[i];
    }
}

int GameManager::loadIcons(SceSize _args, void *_argp){

    while (self->dynamicIconRunning != ICONS_STOPPED){
        // check UMD status
        sceKernelDelayThread(0);
        std::vector<Entry*>* game_entries = self->categories[GAME]->getVector();
        bool has_umd = UMD::isUMD();
        bool umd_loaded = game_entries->size() > 0 && string("UMD") == game_entries->at(0)->getType();
        if (has_umd && !umd_loaded && self->selectedCategory >= 0){ // UMD inserted but not loaded
            SystemMgr::pauseDraw();
            game_entries->insert(game_entries->begin(), new UMD());
            SystemMgr::resumeDraw();
            common::playMenuSound();
        }
        else if (umd_loaded && !has_umd){ // UMD loaded but not inserted
            SystemMgr::pauseDraw();
            UMD* umd = (UMD*)game_entries->at(0);
            game_entries->erase(game_entries->begin());
            delete umd;
            if (game_entries->size() == 0 && self->selectedCategory == GAME){
                self->selectedCategory = HOMEBREW;
            }
            SystemMgr::resumeDraw();
            common::playMenuSound();
        }
        // load icons
        if (self->selectedCategory < 0){
            if (self->selectedCategory == -1) self->findEntries();
            continue;
        }
        for (int i=0; i<MAX_CATEGORIES; i++){
            sceKernelWaitSema(self->iconSema, 1, NULL);
            self->categories[i]->loadIconsDynamic(i == self->selectedCategory);
            sceKernelSignalSema(self->iconSema, 1);
            sceKernelDelayThread(0);
        }
    }

    sceKernelExitDeleteThread(0);
    
    return 0;
}

void GameManager::pauseIcons(){
    if (self->dynamicIconRunning == ICONS_PAUSED) return; // already paused
    for (int i=0; i<MAX_CATEGORIES; i++)
        categories[i]->waitIconsLoad(i == selectedCategory, true);
    sceKernelWaitSema(iconSema, 1, NULL);
    self->dynamicIconRunning = ICONS_PAUSED;
}

void GameManager::resumeIcons(){
    if (self->dynamicIconRunning == ICONS_LOADING) return; // already running
    for (int i=0; i<MAX_CATEGORIES; i++)
        categories[i]->resumeIconLoading();
    sceKernelSignalSema(iconSema, 1);
    sceKernelDelayThread(0);
    self->dynamicIconRunning = ICONS_LOADING;
}

bool GameManager::waitIconsLoad(bool forceQuit){
    for (int i = 0; i<MAX_CATEGORIES; i++){
        if (!this->categories[i]->waitIconsLoad(i == selectedCategory, forceQuit))
            return false;
    }
    return true;
}

Menu* GameManager::getMenu(EntryType t){
    return this->categories[(int)t];
}

void GameManager::findEntries(){

    this->scanning = true;

    int ms_is_ef = sctrlKernelMsIsEf();

    // clear entries
    this->categories[0]->clearEntries();
    this->categories[1]->clearEntries();
    this->categories[2]->clearEntries();
    
    // scan eboots
    this->findEboots("ms0:/PSP/VHBL/");
    this->findEboots("ms0:/PSP/APPS/");
    this->findEboots("ms0:/PSP/GAME/");
    this->findEboots("ms0:/PSP/GAME150/");
    if (!ms_is_ef) {
        this->findEboots("ef0:/PSP/VHBL/");
        this->findEboots("ef0:/PSP/APPS/");
        this->findEboots("ef0:/PSP/GAME/");
    }
    // scan ISOs
    this->findISOs("ms0:/ISO/");
    if (!ms_is_ef) this->findISOs("ef0:/ISO/");
    // scan saves
    if (common::getConf()->scan_save){
        this->findSaveEntries("ms0:/PSP/SAVEDATA/");
        if (!ms_is_ef) this->findSaveEntries("ef0:/PSP/SAVEDATA/");
    }

    if (common::getConf()->sort_entries){
        std::sort(this->categories[0]->getVector()->begin(), this->categories[0]->getVector()->end(), Entry::cmpEntriesForSort);
        std::sort(this->categories[1]->getVector()->begin(), this->categories[1]->getVector()->end(), Entry::cmpEntriesForSort);
        std::sort(this->categories[2]->getVector()->begin(), this->categories[2]->getVector()->end(), Entry::cmpEntriesForSort);
    }

    // add recovery menu
    if (common::getConf()->show_recovery){
        string recovery_path = string(common::getArkConfig()->arkpath) + ARK_RECOVERY;
        string recovery_prx = string(common::getArkConfig()->arkpath) + RECOVERY_PRX;
        if (common::fileExists(recovery_path)){
            Eboot* recovery_menu = new Eboot(recovery_path);
            recovery_menu->setName("Recovery Menu");
            this->categories[HOMEBREW]->getVector()->insert(this->categories[HOMEBREW]->getVector()->begin(), recovery_menu);
        }
        else if (common::fileExists(recovery_prx)){
            Eboot* recovery_menu = new Eboot(string(common::getArkConfig()->arkpath) + VBOOT_PBP); // fake entry
            recovery_menu->setName("Recovery Menu");
            this->categories[HOMEBREW]->getVector()->insert(this->categories[HOMEBREW]->getVector()->begin(), recovery_menu);
        }
    }

    // find the first category with entries
    this->selectedCategory = -2;
    for (int i=0; i<MAX_CATEGORIES && selectedCategory < 0; i++){
        if (!this->categories[i]->empty())
            this->selectedCategory = i;
    }

    this->scanning = false;
}

void GameManager::findEboots(const char* path){ 

    struct dirent* dit;
    DIR* dir = opendir(path);
    
    if (dir == NULL)
        return;
        
    while ((dit = readdir(dir))){
        if (strstr(dit->d_name, "%") != NULL) continue; // ignore 1.50 kxploit format
        if (strcmp(dit->d_name, ".") == 0) continue; // ignore "cur dir"
        if (strcmp(dit->d_name, "..") == 0) continue; // ignore "parent dir"
        if (!FIO_SO_ISDIR(dit->d_stat.st_attr)) continue; // ignore files
        if (dit->d_name[0] == '.' && !common::getConf()->show_hidden) continue; // ignore hidden?
        if (strcmp(dit->d_name, "NPUZ01234") == 0
            || strcmp(dit->d_name, "SCPS10084") == 0
            || strcmp(dit->d_name, "ARK_Loader") == 0)
            continue; // ignore ARK launchers
        
        string fullpath = Eboot::fullEbootPath(path, dit->d_name, common::getConf()->show_dlc);
        if (fullpath == ""){
            if (common::getConf()->scan_cat){
                findEboots((string(path) + dit->d_name + "/").c_str());
            }
            continue;
        }

        Eboot* e = new Eboot(fullpath);
        switch (Eboot::getEbootType(fullpath.c_str())){
        case TYPE_PSN:         this->categories[GAME]->addEntry(e);        break;
        case TYPE_POPS:        this->categories[POPS]->addEntry(e);        break;
        default:               this->categories[HOMEBREW]->addEntry(e);    break;
        }
    }
    closedir(dir);
}

void GameManager::findISOs(const char* path){

    int dir = sceIoDopen(path);
    
    SceIoDirent entry;
    SceIoDirent* dit = &entry;
    memset(&entry, 0, sizeof(SceIoDirent));

    if (dir < 0)
        return;
        
    while (sceIoDread(dir, dit) > 0){

        if (strcmp(dit->d_name, ".") == 0) continue;
        if (strcmp(dit->d_name, "..") == 0) continue;
        if (dit->d_name[0] == '.' && !common::getConf()->show_hidden) continue;

        string fullpath = string(path)+string(dit->d_name);

        if (FIO_SO_ISDIR(dit->d_stat.st_attr)){
            if (common::getConf()->scan_cat && string(dit->d_name) != string("VIDEO")){
                findISOs((string(path) + dit->d_name + "/").c_str());
            }
            continue;
        }
        if (Iso::isISO(fullpath.c_str())) this->categories[GAME]->addEntry(new Iso(fullpath));
    }
    sceIoDclose(dir);
}

void GameManager::findSaveEntries(const char* path){
    struct dirent* dit;
    DIR* dir = opendir(path);
    
    if (dir == NULL)
        return;
        
    while ((dit = readdir(dir))){

        if (strcmp(dit->d_name, ".") == 0) continue;
        if (strcmp(dit->d_name, "..") == 0) continue;
        if (FIO_SO_ISDIR(dit->d_stat.st_attr)){
            struct dirent* savedit;
            string fullpath = string(path)+string(dit->d_name);
            DIR* savedir = opendir(fullpath.c_str());
            if (savedir == NULL)
                continue;
            while ((savedit = readdir(savedir))){
                if (strcmp(savedit->d_name, ".") == 0) continue;
                if (strcmp(savedit->d_name, "..") == 0) continue;
                string fullentrypath = fullpath + "/" + string(savedit->d_name);
                if (Iso::isISO(fullentrypath.c_str())) this->categories[GAME]->addEntry(new Iso(fullentrypath));
                else if (Eboot::isEboot(fullentrypath.c_str())){
                    Eboot* e = new Eboot(fullentrypath);
                    switch (Eboot::getEbootType(fullentrypath.c_str())){
                    case TYPE_PSN:         this->categories[GAME]->addEntry(e);        break;
                    case TYPE_POPS:        this->categories[POPS]->addEntry(e);        break;
                    default:               this->categories[HOMEBREW]->addEntry(e);    break;
                    }
                }
            }
            closedir(savedir);
        }
    }
    closedir(dir);
}

Entry* GameManager::getEntry(){
    if (selectedCategory < 0)
        return NULL;
    return this->categories[this->selectedCategory]->getEntry();
}

int GameManager::getPreviousCategory(int current){
    do { 
        if (current > 0){
            current--;
        }
        else{
            current = MAX_CATEGORIES-1;
        }
    } while (this->categories[current]->empty());
    return current;
}

int GameManager::getNextCategory(int current){
    do {
        if (current < MAX_CATEGORIES-1){
            current++;
        }
        else{
            current = 0;
        }
    } while (this->categories[current]->empty());
    return current;
}

void GameManager::moveLeft(){
    if (selectedCategory < 0)
        return;

    int auxCategory = getPreviousCategory(selectedCategory);

    if (auxCategory != selectedCategory){
        this->categories[selectedCategory]->animStart(2);
        this->categories[auxCategory]->animStart(2);
        this->selectedCategory = auxCategory;
        common::playMenuSound();
    }
    sceKernelDelayThread(100000);
}

void GameManager::moveRight(){
    if (selectedCategory < 0)
        return;
    
    int auxCategory = getNextCategory(selectedCategory);
    
    if (auxCategory != selectedCategory){
        this->categories[selectedCategory]->animStart(2);
        this->categories[auxCategory]->animStart(2);
        this->selectedCategory = auxCategory;
        common::playMenuSound();
    }
    sceKernelDelayThread(100000);
}

void GameManager::moveUp(){
    if (selectedCategory < 0)
        return;
    scroll.tmp = scroll.x; // reset text scroll
    this->categories[this->selectedCategory]->moveUp();
}

void GameManager::moveDown(){
    if (selectedCategory < 0)
        return;
    scroll.tmp = scroll.x; // reset text scroll
    this->categories[this->selectedCategory]->moveDown();
}

void GameManager::stopFastScroll(){
    if (selectedCategory < 0)
        return;
    this->categories[this->selectedCategory]->stopFastScroll();
}

string GameManager::getFooter(){
    return "";
}

string GameManager::getInfo(){
    if (selectedCategory >= 0){
        Entry* e = getEntry();
        if (common::getConf()->show_path){
            return e->getName() + " <" + e->getPath() + ">"; 
        }
        return e->getName();
    }
    else if (selectedCategory == -1) return "Loading games...";
    else if (selectedCategory == -2) return "No games available";
    return "Unknown Menu State";
}

static const char* category_names[MAX_CATEGORIES] = {"Games", "Apps", "PS1"};

/* Segmented control in the status bar: one tab per non-empty section */
void GameManager::drawTabs(){
    int x = 8;
    for (int i=0; i<MAX_CATEGORIES; i++){
        if (categories[i]->empty()) continue;
        string name = TR(category_names[i]);
        int w = common::calcTextWidth(name.c_str(), SIZE_LITTLE, 0) + 16;
        if (i == selectedCategory){
            common::drawSelection(x, 2, w, 16);
            common::printText(x+8, 13, name.c_str(), WHITE, SIZE_LITTLE, 0, NULL, 0);
        }
        else {
            common::printText(x+8, 13, name.c_str(), 0xC0FFFFFF, SIZE_LITTLE, 0, NULL, 0);
        }
        x += w + 2;
    }
}

/* Glass dock with every other launcher app: files, network, settings, plugins, exit */
void GameManager::drawDock(){
    int n = 0;
    SystemEntry* apps[12];
    for (int i=0; i<12; i++){
        SystemEntry* e = SystemMgr::getSystemEntry(i);
        if (e == NULL) break;
        if (e == this) continue;
        apps[n++] = e;
    }
    if (n == 0) return;
    if (dock_focus >= n) dock_focus = n-1;

    const int size = 38, step = 50, y = 192, h = 52;
    int w = n*step + 14;
    int x = (480-w)/2;
    Image* dock = common::getDock();
    if (dock) dock->draw_scale(x, y, w, h);
    else common::getImage(IMAGE_DIALOG)->draw_scale(x, y, w, h);

    for (int i=0; i<n; i++){
        int cx = x + 7 + i*step + step/2;
        int s = (i == dock_focus)? size+6 : size;
        if (i == dock_focus){
            Image* plate = common::getPlate();
            if (plate) plate->draw_scale(cx - s/2 - 4, y + h/2 - s/2 - 4, s+8, s+8);
        }
        apps[i]->getIcon()->draw_scale(cx - s/2, y + h/2 - s/2, s, s);
    }

    // name of the focused app, where the page dots usually are
    if (dock_focus >= 0){
        string name = TR(apps[dock_focus]->getName());
        int tw = common::calcTextWidth(name.c_str(), SIZE_LITTLE, 0);
        common::printText((480-tw)/2, 186, name.c_str(), WHITE, SIZE_LITTLE, 0, NULL, 0);
    }
}

void GameManager::draw(){
    if (this->selectedCategory >= 0){
        if (this->selectedCategory < this->maxDraw)
            this->categories[this->selectedCategory]->drawGrid(dock_focus < 0 && !optionsmenu);
        if (this->maxDraw > 0)
            drawDock();
        if (optionsmenu) optionsmenu->draw();
        if (cc_focus >= 0) drawControlCenter();
    }
    else if (this->selectedCategory == -2){
        string s = TR("No games available");
        common::printText((480-common::calcTextWidth(s.c_str(), SIZE_MEDIUM, 0))/2, 110, s.c_str(), WHITE, SIZE_MEDIUM, 0, NULL, 0);
        drawDock();
    }
    if (loadingData || selectedCategory == -1){
        static float angle = 1.0;
        Image* img = common::getImage(IMAGE_WAITICON);
        img->draw_rotate(
            (480-img->getTexture()->width)/2,
            (272-img->getTexture()->height)/2,
            angle
        );
        angle+=0.2;
    }
}

void GameManager::endAllThreads(){
    dynamicIconRunning = ICONS_STOPPED;
    sceKernelWaitThreadEnd(iconThread, 0);
}


/* Move around the icon grid; going down from the last row lands in the dock */
void GameManager::gridMove(int dx, int dy){
    Menu* m = categories[selectedCategory];
    int size = m->getVectorSize();
    int idx = m->getIndex();
    int start = (idx/GRID_PAGE)*GRID_PAGE;
    int pos = idx - start;
    scroll.tmp = scroll.x;

    if (dx){
        m->setIndex(idx + dx); // reading order, flows onto the next or previous page
    }
    else if (dy < 0){
        if (pos >= GRID_COLS) m->setIndex(idx - GRID_COLS);
        else if (start > 0) m->setIndex(min(idx - GRID_COLS, size-1)); // bottom row of the previous page
    }
    else if (dy > 0){
        if (pos < GRID_COLS && idx + GRID_COLS < size) m->setIndex(idx + GRID_COLS);
        else if (pos < GRID_COLS && start + GRID_COLS < size) m->setIndex(size-1);
        else {
            dock_focus = 0;
            common::playMenuSound();
        }
    }
    sceKernelDelayThread(90000); // held buttons repeat every frame, keep it walkable
}

enum { CC_USB, CC_SLEEP, CC_REFRESH, CC_MUSIC, CC_BUTTONS };
/* glyphs in CCICONS.PNG, 32px cells */
enum { GLYPH_PLAY, GLYPH_USB, GLYPH_MOON, GLYPH_REFRESH, GLYPH_MUSIC, GLYPH_BATTERY, GLYPH_CARD, GLYPH_BOLT, GLYPH_LOCK };

static void ccGlyph(int glyph, int x, int y, int size){
    Image* cc = common::getThemeImage(THEME_CC);
    if (cc) common::drawSub(cc, glyph*32, 0, 32, 32, x, y, size, size);
}

static void deviceSpace(const char* dev, u64* total, u64* free){
    struct {
        u32 maxClusters, freeClusters, maxSectors, sectorSize, sectorCount;
    } ds;
    void* cmd = &ds;
    memset(&ds, 0, sizeof(ds));
    sceIoDevctl(dev, 0x02425818, &cmd, sizeof(cmd), NULL, 0);
    u64 cluster = (u64)ds.sectorSize * ds.sectorCount;
    *total = (u64)ds.maxClusters * cluster;
    *free = (u64)ds.freeClusters * cluster;
}

/* iOS style control center: battery and memory widgets, quick toggles */
void GameManager::drawControlCenter(){
    ya2d_draw_rect(0, 0, 480, 272, 0x80000000, 1);
    Image* box = common::getImage(IMAGE_DIALOG);
    const int x0 = 44, y0 = 30, wide = 192, gap = 8;

    // battery widget
    box->draw_scale(x0, y0, wide, 100);
    ccGlyph(GLYPH_BATTERY, x0+12, y0+10, 24);
    common::printText(x0+42, y0+27, TR("Battery").c_str(), 0xFFDDDDDD, SIZE_LITTLE, 0, NULL, 0);
    if (scePowerIsBatteryExist()){
        int pct = scePowerGetBatteryLifePercent();
        char big[8];
        snprintf(big, sizeof(big), "%d%%", (pct < 0)? 0 : pct);
        common::printText(x0+14, y0+64, big, WHITE, 2.0f, 0, NULL, 0);
        bool charging = scePowerIsBatteryCharging();
        if (charging) ccGlyph(GLYPH_BOLT, x0+wide-36, y0+40, 24);
        int mins = scePowerGetBatteryLifeTime();
        char line[48];
        if (charging) snprintf(line, sizeof(line), "%s", TR("Charging").c_str());
        else if (mins > 0) snprintf(line, sizeof(line), "%s %d:%02d", TR("Left").c_str(), mins/60, mins%60);
        else line[0] = 0;
        common::printText(x0+14, y0+82, line, 0xFFDDDDDD, SIZE_LITTLE, 0, NULL, 0);
        int temp = scePowerGetBatteryTemp();
        if (temp > 0 && temp < 100){
            snprintf(line, sizeof(line), "%d°C", temp);
            common::printText(x0+wide-14-common::calcTextWidth(line, SIZE_LITTLE, 0), y0+82, line, 0xFFDDDDDD, SIZE_LITTLE, 0, NULL, 0);
        }
        // level bar
        ya2d_draw_rect(x0+14, y0+88, wide-28, 3, 0x40FFFFFF, 1);
        u32 col = (pct <= 15)? 0xFF3C3CFF : (charging? 0xFF64D94C : WHITE);
        ya2d_draw_rect(x0+14, y0+88, (wide-28)*max(0, pct)/100, 3, col, 1);
    }
    else common::printText(x0+14, y0+64, TR("No battery").c_str(), WHITE, SIZE_BIG, 0, NULL, 0);

    // memory stick widget
    int x1 = x0 + wide + gap;
    box->draw_scale(x1, y0, wide, 100);
    ccGlyph(GLYPH_CARD, x1+12, y0+10, 24);
    common::printText(x1+42, y0+27, TR("Memory Stick").c_str(), 0xFFDDDDDD, SIZE_LITTLE, 0, NULL, 0);
    static u64 total = 0, freeb = 0;
    static clock_t checked = 0;
    if (checked == 0 || clock() - checked > 5*CLOCKS_PER_SEC || clock() < checked){
        deviceSpace("ms0:", &total, &freeb);
        checked = clock();
    }
    string fr = common::beautifySize(freeb);
    common::printText(x1+14, y0+62, fr.c_str(), WHITE, SIZE_BIG, 0, NULL, 0);
    string of = TR("free of") + " " + common::beautifySize(total);
    common::printText(x1+14, y0+80, of.c_str(), 0xFFDDDDDD, SIZE_LITTLE, 0, NULL, 0);
    ya2d_draw_rect(x1+14, y0+88, wide-28, 3, 0x40FFFFFF, 1);
    if (total) ya2d_draw_rect(x1+14, y0+88, (int)((wide-28)*(total-freeb)/total), 3, 0xFFFFC85A, 1);

    // toggles
    const int bw = (2*wide + gap - 3*gap)/4, bh = 72, by = y0 + 100 + gap;
    bool music = MusicPlayer::isPlaying();
    const char* labels[CC_BUTTONS] = {"USB", "Sleep", "Refresh", "Music"};
    int glyphs[CC_BUTTONS] = {GLYPH_USB, GLYPH_MOON, GLYPH_REFRESH, GLYPH_MUSIC};
    for (int i=0; i<CC_BUTTONS; i++){
        int bx = x0 + i*(bw+gap);
        bool on = (i == CC_USB && USB::is_enabled) || (i == CC_MUSIC && music);
        if (i == cc_focus){
            Image* plate = common::getPlate();
            if (plate) plate->draw_scale(bx-4, by-4, bw+8, bh+8);
        }
        box->draw_scale(bx, by, bw, bh);
        if (on) common::drawSelection(bx, by, bw, bh);
        ccGlyph(glyphs[i], bx + bw/2 - 14, by + 10, 28);
        string l = TR(labels[i]);
        common::printText(bx + bw/2 - common::calcTextWidth(l.c_str(), SIZE_LITTLE, 0)/2, by+52, l.c_str(), WHITE, SIZE_LITTLE, 0, NULL, 0);
        const char* state = NULL;
        if (i == CC_USB) state = (USB::is_enabled)? "On" : "Off";
        else if (i == CC_MUSIC) state = (music)? "Pause" : "Not playing";
        if (state){
            string st = TR(state);
            common::printText(bx + bw/2 - common::calcTextWidth(st.c_str(), SIZE_LITTLE, 0)/2, by+65, st.c_str(), (on)? 0xFF64D94C : 0xFFAAAAAA, SIZE_LITTLE, 0, NULL, 0);
        }
    }
}

void GameManager::controlCenter(Controller* pad){
    if (pad->decline() || pad->select()){
        cc_focus = -1;
        common::playMenuSound();
    }
    else if (pad->left()){
        if (cc_focus > 0){ cc_focus--; common::playMenuSound(); }
        sceKernelDelayThread(90000);
    }
    else if (pad->right()){
        if (cc_focus < CC_BUTTONS-1){ cc_focus++; common::playMenuSound(); }
        sceKernelDelayThread(90000);
    }
    else if (pad->accept()){
        common::playMenuSound();
        switch (cc_focus){
            case CC_USB:
                if (USB::is_enabled) USB::disable();
                else USB::enable();
                break;
            case CC_SLEEP:
                cc_focus = -1;
                scePowerRequestSuspend();
                break;
            case CC_REFRESH:
                cc_focus = -1;
                this->waitIconsLoad();
                GameManager::updateGameList(NULL);
                this->waitIconsLoad();
                break;
            case CC_MUSIC:
                if (MusicPlayer::isPlaying()) MusicPlayer::pauseResume();
                break;
        }
    }
}

void GameManager::control(Controller* pad){

    if (cc_focus >= 0){
        controlCenter(pad);
        return;
    }

    if (selectedCategory == -2 && dock_focus < 0) dock_focus = 0; // no games, only the dock
    if (selectedCategory == -1) return;

    if (dock_focus >= 0){
        if (pad->left()){
            if (dock_focus > 0){ dock_focus--; common::playMenuSound(); }
        }
        else if (pad->right()){
            dock_focus++; // clamped by drawDock()
            common::playMenuSound();
        }
        if (pad->left() || pad->right()) sceKernelDelayThread(90000);
        else if ((pad->up() || pad->decline()) && selectedCategory >= 0){
            dock_focus = -1;
            common::playMenuSound();
        }
        else if (pad->accept()){
            int n = 0;
            for (int i=0; i<12; i++){
                SystemEntry* e = SystemMgr::getSystemEntry(i);
                if (e == NULL) break;
                if (e == this) continue;
                if (n++ == dock_focus){
                    common::playMenuSound();
                    SystemMgr::openEntry(i);
                    break;
                }
            }
        }
        return;
    }

    if (pad->down()) this->gridMove(0, 1);
    else if (pad->up()) this->gridMove(0, -1);
    else if (pad->left()) this->gridMove(-1, 0);
    else if (pad->right()) this->gridMove(1, 0);
    else if (pad->LT()) this->moveLeft();
    else if (pad->RT()) this->moveRight();
    else if (pad->accept()){
        if (selectedCategory >= 0){
            common::playMenuSound();
            this->execApp();
        }
    }
    else if (pad->start()){
        if (selectedCategory >= 0 && !categories[selectedCategory]->isAnimating()){
            this->startBoot();
        }
    }
    else if (pad->select()){
        if (selectedCategory != -1){
            common::playMenuSound();
            cc_focus = 0;
        }
    }
    else if (pad->square()){
        if (selectedCategory >= 0){
            common::playMenuSound();
            this->gameOptionsMenu();
        }
    }
}

void GameManager::updateGameList(const char* path){
    if (path == NULL 
        || strncmp(path, "ms0:/PSP/GAME/", 14) == 0 || strncmp(path, "ms0:/ISO/", 9) == 0
        || strncmp(path, "ef0:/PSP/GAME/", 14) == 0 || strncmp(path, "ef0:/ISO/", 9) == 0
        || strncmp(path, "ms0:/PSP/VHBL/", 14) == 0 || strncmp(path, "ms0:/PSP/APPS/", 14) == 0
        || strncmp(path, "ef0:/PSP/VHBL/", 14) == 0 || strncmp(path, "ef0:/PSP/APPS/", 14) == 0
        || strncmp(path, "ms0:/PSP/GAME150/", 17) == 0
      ){
        int icon_status = self->dynamicIconRunning;
        if (icon_status == ICONS_LOADING){
            self->pauseIcons();
        }
        SystemMgr::pauseDraw();
        self->selectedCategory = -1;
        for (int i=0; i<MAX_CATEGORIES; i++){
            self->categories[i]->clearEntries();
        }
        SystemMgr::resumeDraw();
        if (icon_status == ICONS_LOADING){
            self->resumeIcons();
        }
    }
}

void GameManager::execApp(){
    if (common::getConf()->fast_gameboot){
        this->endAllThreads();
        this->getEntry()->execute();
    }

    loadingData = true;
    this->waitIconsLoad();
    this->getEntry()->loadPics();
    loadingData = false;
    if (this->getEntry()->pmfPrompt()){
        this->endAllThreads();
        this->getEntry()->execute();
    }
    this->getEntry()->freeTempData();
    sceKernelDelayThread(0);
}

void GameManager::gameOptionsMenu(){
    t_options_entry options_entries[] = {
        {OPTIONS_CANCELLED, "Cancel"},
        {0, "View Info"},
        {1, "Rename"},
        {2, "Delete"},
    };
    optionsmenu = new OptionsMenu("Game Options", sizeof(options_entries)/sizeof(t_options_entry), options_entries);
    int ret = optionsmenu->control();
    OptionsMenu* aux = optionsmenu;
    optionsmenu = NULL;
    delete aux;

    switch (ret){

    case 0:{
        // create a new options menu but each entry is some info about the game
        Entry* e = this->getEntry();
        string path = e->getPath();
        SfoInfo info = e->getSfoInfo();
        string game_info[] = {
            info.title,
            info.gameid,
            path,
            common::beautifySize(common::fileSize(path))
        };
        string fullname = TR("Name") + " - " + game_info[0];
        string gameid = TR("Game ID") + " - " + game_info[1];
        string fullpath = TR("Path") + " - " + game_info[2];
        string size = TR("Size") + " - " + game_info[3];
        t_options_entry gameinfo_entries[] = {
            {-1, "Cancel"},
            {0, (char*)fullname.c_str()},
            {1, (char*)gameid.c_str()},
            {3, (char*)size.c_str()},
            {2, (char*)fullpath.c_str()},
        };
        optionsmenu = new OptionsMenu("Game Info", sizeof(gameinfo_entries)/sizeof(t_options_entry), gameinfo_entries);
        int res = optionsmenu->control();
        OptionsMenu* aux = optionsmenu;
        optionsmenu = NULL;
        delete aux;
        if (res >= 0){
            TextEditor::clipboard = game_info[res];
        }
    } break;

    case 1:{
        // rename the ISO or Eboot folder name
        SystemMgr::pauseDraw();
        Entry* e = this->getEntry();
        string name = e->getName();
        OSK osk;

        char parent[128];
        strcpy(parent, e->getPath().c_str());
        char* pname = strstr(parent, name.c_str());
        *pname = 0;
        
        osk.init("New name for Game", name.c_str(), 50);
        osk.loop();
        if(osk.getResult() != OSK_CANCEL)
        {
            char tmpText[51];
            osk.getText((char*)tmpText);
            string newpath = (string(parent)+string(tmpText));
            string oldpath = (string(parent)+name);
            
            sceIoRename(oldpath.c_str(), newpath.c_str());
            e->setName(tmpText);
            e->setPath(newpath);
        }
        osk.end();
        SystemMgr::resumeDraw();
    } break;

    case 2:{
        // remove current entry from list (adjusting index and selectedCategory accordingly), delete file or folder depending on ISO/EBOOT
        // make checks to prevent deleting stuff like "UMD Drive" and "Recovery" entries
        t_options_entry opts[] = {
            {OPTIONS_CANCELLED, "Cancel"},
            {0, "Confirm"},
        };
        optionsmenu = new OptionsMenu("Confirm Deletion?", sizeof(opts)/sizeof(t_options_entry), opts);
        int pret = optionsmenu->control();
        OptionsMenu* aux = optionsmenu;
        optionsmenu = NULL;
        delete aux;

        if (pret == OPTIONS_CANCELLED) return;

        Entry* e = this->getEntry();
        string name = e->getName();
        if (name != "UMD Drive" && name != "Recovery Menu"){
            // pause drawing so we don't crash due to race condition
            SystemMgr::pauseDraw();
            // get current menu and it's game list
            Menu* category = categories[selectedCategory];
            std::vector<Entry*>* entries = category->getVector();
            // remove entry from list
            entries->erase(entries->begin()+category->getIndex());
            // adjust selectedCategory
            int retries = 0;
            while (categories[selectedCategory]->getVectorSize() == 0){
                if (selectedCategory < POPS) selectedCategory++;
                else selectedCategory = GAME;
                if (++retries >= 4){ // can't find a valid menu
                    selectedCategory = -2; // no games available
                    break;
                }
            }
            // resume drawing
            SystemMgr::resumeDraw();

            // delete file/folder
            if (e->getType() == string("ISO")){
                sceIoRemove(e->getPath().c_str());
            }
            else if (e->getType() == string("EBOOT")){
                string path = e->getPath();
                if (strstr(path.c_str(), "/PSP/SAVEDATA/") != NULL){
                    // remove eboot only
                    sceIoRemove(e->getPath().c_str());
                }
                else{
                    string path = e->getPath();
                    // remove entire folder
                    string folder = path.substr(0, path.rfind("/")+1);
                    Browser::recursiveFolderDelete(folder);
                    if(strstr(path.c_str(), "%/") != NULL) {
                        // remove 1.50 kxploit folder
                        path.erase(path.find("%/"));
                        path += '/';
                        folder = path;
                        Browser::recursiveFolderDelete(folder);
                    }
                }
            }
            // free resources
            delete e;
        }
    }break;

    default: break;
    }
}

void GameManager::startBoot(){
    switch (common::getConf()->startbtn){
    case 0: { // Default Start Button (Current)
        this->endAllThreads();
        this->getEntry()->execute();
    } break;
    case 1: { // Last game
        const char* last_game = common::getConf()->last_game;
        if (last_game[0] == 0) break;
        if (Eboot::isEboot(last_game)){
            this->endAllThreads();
            Eboot* eboot = new Eboot(last_game);
            eboot->execute();
        }
        else if (Iso::isISO(last_game)){
            this->endAllThreads();
            Iso* iso = new Iso(last_game);
            iso->execute();
        }
    } break;
    case 2: { // Random ISO
        if (this->categories[GAME]->getVectorSize() > 0){
            this->endAllThreads();
            int rand_idx = rand() % this->categories[GAME]->getVectorSize();
            Entry* e = this->categories[GAME]->getEntry(rand_idx);
            e->execute();
        }
    } break;
    default: break;
    }
}
