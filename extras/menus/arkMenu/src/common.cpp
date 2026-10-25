#include "common.h"
#include <ctime>
#include <cmath>
#include <pspgum.h>
#include <sstream>
#include <dirent.h>
#include <algorithm>
#include <unistd.h>
#include <sys/stat.h>
#include "controller.h"
#include <systemctrl.h>
#include <pspiofilemgr.h>
#include <kubridge.h>
#include "animations.h"
#include "system_mgr.h"
#include "lang.h"
#include "browser.h"
#include "iso.h"
#include "eboot.h"

#define RESOURCES_LOAD_PLACE YA2D_PLACE_VRAM

using namespace common;

bool common::is_recovery = false;

extern "C"{
    int kuKernelGetModel();
}

struct tm today;

static ARKConfig ark_config = {0};
static Image* images[MAX_IMAGES];
/* Common browser images */
static Image* checkbox[2];
static Image* icons[MAX_FILE_TYPES];

extern float text_size;
extern int altFontId;
extern intraFont* altFont;
extern intraFont* font;
static MP3* sound_mp3 = NULL;
static int argc;
static char **argv;
static int currentFont = 0;
static int currentLang = 0;
static int currentApp = 0; // Games
/* Instance of the animations that are drawn on the menu */
static Anim* animations[ANIM_COUNT];
/* Optional theme images for the selected entry (PLATE.PNG behind it, RIM.PNG over it) */
static Image* plate = NULL;
static Image* rim = NULL;
/* Optional home screen images: DOCK.PNG behind the system apps, GLOSS.PNG over every app icon */
static Image* dock = NULL;
static Image* gloss = NULL;
static Image* cfw_icon = NULL; // optional CFW.PNG, so the two settings apps look different
static const char* extra_files[MAX_THEME_EXTRAS] = {"PHOTOS.PNG", "VIDEO.PNG", "CLOCK.PNG", "CCICONS.PNG"};
static Image* extras[MAX_THEME_EXTRAS] = {NULL};
static int wall_loaded = 0; // wallpaper index in images[IMAGE_BG]
static bool wall_photo_missing = false; // "My photo" picked but no photo saved yet
/* Optional button glyphs for the hint bar, matched by key in drawHints() */
#define HINT_BUTTONS 11
static const char* hint_keys[HINT_BUTTONS] = {"X", "O", "T", "S", "L", "R", "ST", "SE", "UD", "LR", "DP"};
static const char* hint_files[HINT_BUTTONS] = {
    "BTN_X.PNG", "BTN_O.PNG", "BTN_T.PNG", "BTN_S.PNG", "BTN_L.PNG", "BTN_R.PNG",
    "BTN_ST.PNG", "BTN_SE.PNG", "BTN_UD.PNG", "BTN_LR.PNG", "BTN_DP.PNG"
};
static Image* hint_btns[HINT_BUTTONS] = {NULL};

static bool flipControl = false;

static int psp_model;

static string theme_path = THEME_NAME;

char* fonts[] = {
    "FONT.PGF",
    "flash0:/font/ltn0.pgf",
    "flash0:/font/ltn1.pgf",
    "flash0:/font/ltn2.pgf",
    "flash0:/font/ltn3.pgf",
    "flash0:/font/ltn4.pgf",
    "flash0:/font/ltn5.pgf",
    "flash0:/font/ltn6.pgf",
    "flash0:/font/ltn7.pgf",
    "flash0:/font/ltn8.pgf",
    "flash0:/font/ltn9.pgf",
    "flash0:/font/ltn10.pgf",
    "flash0:/font/ltn11.pgf",
    "flash0:/font/ltn12.pgf",
    "flash0:/font/ltn13.pgf",
    "flash0:/font/ltn14.pgf",
    "flash0:/font/ltn15.pgf",
    //"flash0:/font/jpn0.pgf",
    //"flash0:/font/kr0.pgf"
};

extern unsigned char language_selection;
// custom languages
static char* lang_files[] = {
    "en",
    "es",
    "de",
    "fr",
    "pt",
    "it",
    "nl",
    "ru",
    "ukr",
    "ro",
    "lat",
    "jp",
    "ko",
    "cht",
    "chs",
    "pol",
    "latgr",
    "tr",
    //"grk",
    //"thai",
};

// system languages
static char* system_lang[] = {
    "jp",
    "en",
    "fr",
    "es",
    "de",
    "it",
    "nl",
    "pt",
    "ru",
    "ko",
    "cht",
    "chs"
};

static t_conf config;

static volatile bool do_loading_thread = false;
static volatile SceUID load_thread_id = -1;

static void dummyMissingHandler(const char* filename){

}

static char* getLangFile(){
    static char file[20];
    char* lang = NULL;
    if (config.syslang){
        int id; sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &id);
        lang = system_lang[id];
    }
    else {
        lang = lang_files[config.language];
    }
    sprintf(file, "lang_%s.json", lang);
    return file;
}

void setArgs(int ac, char** av){
    argc = ac;
    argv = av;
}

void loadConfig(){
    FILE* fp = fopen(MENU_SETTINGS, "rb");
    if (fp == NULL){
        resetConf();
        return;
    }
    memset(&config, 0, sizeof(t_conf));
    fseek(fp, 0, SEEK_SET);
    fread(&config, 1, sizeof(t_conf), fp);
    fclose(fp);

    if (config.syslang){
        language_selection = 0;
    }
    else {
        language_selection = config.language+1;
    }

    if (today.tm_mday == 1 && today.tm_mon == 3)
        config.language = 10;
}

ARKConfig* common::getArkConfig(){
    if (ark_config.magic != ARK_CONFIG_MAGIC){
        sctrlArkGetConfig(&ark_config);
    }
    return &ark_config;
}

struct tm common::getDateTime(){
    struct tm  ts;
    time_t now = sceKernelLibcTime(NULL);
    ts = *localtime(&now);
    return ts;
}

static void loadFont(){
    unsigned offset=0, size=0;
    if (config.font == 0){
        offset = findPkgOffset(fonts[0], &size, "LANG.ARK", &dummyMissingHandler);
        if (offset && size){
            fonts[0] = "LANG.ARK"; // found font in lang package
        }
        else if ((offset = findPkgOffset(fonts[0], &size, "THEME.ARK", &dummyMissingHandler)) != 0 ){
            fonts[0] = "THEME.ARK"; // found font in theme package
        }
        else if (!fileExists(fonts[0])){
            if (altFont){
                intraFont* aux = font;
                if (aux) intraFontUnload(aux);
                font = altFont;
                config.font = altFontId;
                return;
            }
            else{
                config.font = 1;
            }
        }
    }

    // offload current font
    intraFont* aux = font;
    if (aux) intraFontUnload(aux);
    // load new font
    if (config.font == 0 && !altFont) altFont = intraFontLoadEx(fonts[1], INTRAFONT_CACHE_ALL, 0, 0);
    font = intraFontLoadEx(fonts[config.font], INTRAFONT_CACHE_ALL, offset, size);
    intraFontSetEncoding(font, INTRAFONT_STRING_UTF8);
    // set alt font
    if (altFont) intraFontSetAltFont(font, altFont);
    currentFont = config.font;
}

void common::saveConf(){

    SystemMgr::pauseDraw();

    // reload language
    if (currentLang != language_selection){
        if (language_selection == 0) {
            config.syslang = 1;
            config.language = 0;
        }
        else {
            config.syslang = 0;
            config.language = language_selection-1;
        }
        if (!Translations::loadLanguage(getLangFile()) && language_selection > 1){
            config.syslang = 1;
        }
        currentLang = language_selection;
    }

    // reload font
    if (currentFont != config.font || font == NULL){
        loadFont();
    }

    // swap apps
    if (!is_recovery && currentApp != config.main_menu){
        SystemEntry* ent0 = SystemMgr::getSystemEntry(0);
        SystemEntry* ent1 = SystemMgr::getSystemEntry(1);
        SystemMgr::setSystemEntry(ent1, 0);
        SystemMgr::setSystemEntry(ent0, 1);
        currentApp = config.main_menu;
    }

    SystemMgr::resumeDraw();

    strcpy(config.browser_dir, Browser::getInstance()->getCWD());
    
    FILE* fp = fopen(MENU_SETTINGS, "wb");
    fwrite(&config, 1, sizeof(t_conf), fp);
    fclose(fp);
}

t_conf* common::getConf(){
    return &config;
}

void common::resetConf(){
    memset(&config, 0, sizeof(config));
    config.syslang = 1;
    config.font = 1;
    config.sort_entries = 1;
    config.show_recovery = 1;
    config.text_glow = 0;
    config.animation = 10; // no background animation, keeps the glass theme clean
    config.screensaver = 4; // lock screen after 30 s, like iOS auto-lock
    config.standby = 4; // sleep after 10 idle minutes
    config.redirect_ms0 = 1;
    config.menusize = 2;
    config.browser_icon0 = 1;
    language_selection = 0;
}

void common::launchRecovery(const char* path){
    string fakent = string(common::getArkConfig()->arkpath) + VBOOT_PBP;
    if (fakent != path && common::fileExists(path)){
        struct SceKernelLoadExecVSHParam param;
        
        memset(&param, 0, sizeof(param));
        
        int runlevel = HOMEBREW_RUNLEVEL;
        
        param.args = strlen(path) + 1;
        param.argp = (char*)path;
        param.key = "game";
        sctrlKernelLoadExecVSHWithApitype(runlevel, path, &param);
    }
    else {
        string recovery_prx = string(common::getArkConfig()->arkpath) + RECOVERY_PRX;
        SceUID modid = kuKernelLoadModule(recovery_prx.c_str(), 0, NULL);
        if (modid >= 0){
            int res = sceKernelStartModule(modid, recovery_prx.size() + 1, (void*)recovery_prx.c_str(), NULL, NULL);
            if (res >= 0){
                while (1){sceKernelDelayThread(1000000);}; // wait for recovery to finish
            }
        }
    }
}

static void missingFileHandler(const char* filename){

    if (load_thread_id >= 0){
        stopLoadingThread();
    }

    if (!font){
        font = intraFontLoad(fonts[1], INTRAFONT_CACHE_ASCII);
        intraFontSetEncoding(font, INTRAFONT_STRING_UTF8);
    }
    
    static char msg[64];
    snprintf(msg, 64, "Error, missing file %s", filename);
    
    Controller pad;
    
    while (true){
    
        common::clearScreen(0);
        common::printText(10, 10, msg, GRAY_COLOR, SIZE_BIG, true);
        common::printText(10, 30, "Press cross or circle to launch recovery", GRAY_COLOR, SIZE_LITTLE, false);
        common::printText(10, 50, "Press triangle to exit", GRAY_COLOR, SIZE_LITTLE, false);
        common::flipScreen();
    
        pad.update();
        if (pad.cross() || pad.circle()){
            string recovery_path = string(ark_config.arkpath) + ARK_RECOVERY;
            common::launchRecovery(recovery_path.c_str());
        }
        else if (pad.triangle())
            sctrlKernelExitVSH(NULL);
    }
}

SceOff common::findPkgOffset(const char* filename, unsigned* size, const char* pkgpath, void (*missinghandler)(const char*)){
    
    if (pkgpath == NULL)
        pkgpath = theme_path.c_str();

    if (missinghandler == NULL)
        missinghandler = &missingFileHandler;

    FILE* pkg = fopen(pkgpath, "rb");
    if (pkg == NULL)
        return 0;
     
    fseek(pkg, 0, SEEK_END);
     
    unsigned pkgsize = ftell(pkg);
    unsigned size2 = 0;
     
    fseek(pkg, 0, SEEK_SET);

    if (size != NULL)
        *size = 0;

    unsigned offset = 0;
    char name[64];
           
    while (offset != 0xFFFFFFFF){
        fread(&offset, 1, 4, pkg);
        if (offset == 0xFFFFFFFF){
            fclose(pkg);
            missinghandler(filename);
            return 0;
        }
        unsigned namelength;
        fread(&namelength, 1, 4, pkg);
        fread(name, 1, namelength+1, pkg);
                   
        if (!strncmp(name, filename, namelength)){
            fread(&size2, 1, 4, pkg);
    
            if (size2 == 0xFFFFFFFF)
                size2 = pkgsize;

            if (size != NULL)
                *size = size2 - offset;
     
            fclose(pkg);
            return offset;
        }
    }
    missinghandler(pkgpath);
    return 0;
}

void* common::readFromPKG(const char* filename, unsigned* size, const char* pkgpath){

    unsigned mySize;
    
    if (size == NULL)
        size = &mySize;

    if (pkgpath == NULL)
        pkgpath = theme_path.c_str();

    unsigned offset = findPkgOffset(filename, size, pkgpath, &dummyMissingHandler);
    
    FILE* fp = fopen(pkgpath, "rb");
    
    if (offset == 0 || fp == NULL){
        fclose(fp);
        *size = 0;
        return NULL;
    }
    
    void* data = malloc(*size);
    fseek(fp, offset, SEEK_SET);
    fread(data, 1, *size, fp);
    fclose(fp);
    return data;
}


int common::getArgc(){
    return argc;
}

char** common::getArgv(){
    return argv;
}

int common::getPspModel(){
    return psp_model;
}

bool common::has_suffix(const std::string &str, const std::string &suffix)
{
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

u32 common::getMagic(const char* filename, unsigned int offset){
    FILE* fp = fopen(filename, "rb");
    if (fp == NULL)
        return 0;
    u32 magic;
    fseek(fp, offset, SEEK_SET);
    fread(&magic, 4, 1, fp);
    fclose(fp);
    return magic;
}

static int loading_thread(SceSize argc, void* argp){
    float angle = 1.0;
    while (do_loading_thread){
        common::clearScreen(CLEAR_COLOR);
        images[IMAGE_BG]->draw(0, 0);
        images[IMAGE_WAITICON]->draw_rotate(
            (480 - images[IMAGE_WAITICON]->getTexture()->width)/2,
            (272 - images[IMAGE_WAITICON]->getTexture()->height)/2,
            angle
        );
        angle+=0.2;
        common::flipScreen();
        sceKernelDelayThread(0);
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

void common::startLoadingThread(){
    do_loading_thread = true;
    load_thread_id = sceKernelCreateThread("theme_thread", &loading_thread, 0x10, 0x8000, PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU, NULL);
    sceKernelStartThread(load_thread_id, 0, NULL);
}

void common::stopLoadingThread(){
    do_loading_thread = false;
    sceKernelWaitThreadEnd(load_thread_id, NULL);
    sceKernelDeleteThread(load_thread_id);
    load_thread_id = -1;
}

static Image* loadRawWallpaper(string path);

/* Wallpaper: 0 = theme DEFBG.PNG, 1-4 = theme WALL1-4.PNG, 5 = the user's photo (WALL.RAW, or the old BG.PNG) */
static Image* loadWallpaper(int which){
    SceIoStat stat;
    Image* img = NULL;
    if (which == WALLPAPER_PHOTO){
        string raw = string(ark_config.arkpath) + "WALL.RAW";
        string png = string(ark_config.arkpath) + "BG.PNG";
        img = loadRawWallpaper(raw);
        if (!img && sceIoGetstat(png.c_str(), &stat) >= 0) img = new Image(png.c_str());
        if (img && img->is_system_image){ delete img; img = NULL; } // failed to load
        wall_photo_missing = (img == NULL);
    }
    else if (which > 0 && which < MAX_WALLPAPERS){
        char name[16];
        snprintf(name, sizeof(name), "WALL%d.PNG", which);
        SceOff off = common::findPkgOffset(name, NULL, NULL, &dummyMissingHandler);
        if (off) img = new Image(theme_path, RESOURCES_LOAD_PLACE, off);
        if (img && !img->getTexture()){ delete img; img = NULL; }
    }
    if (img == NULL)
        img = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("DEFBG.PNG"));
    return img;
}

/* Loads files, so only call it from the main/control thread (the draw thread has no working dir) */
static void swapWallpaper(int which, bool lock){
    Image* img = loadWallpaper(which);
    img->swizzle();
    img->is_system_image = true;
    if (lock) SystemMgr::pauseDraw();
    Image* old = images[IMAGE_BG];
    images[IMAGE_BG] = img;
    wall_loaded = which;
    if (lock) SystemMgr::resumeDraw();
    if (old){
        old->is_system_image = false;
        delete old;
    }
}

void common::previewWallpaper(int which){
    int want = (which >= 0 && which < MAX_WALLPAPERS)? which : config.wallpaper;
    if (want >= MAX_WALLPAPERS) want = 0;
    if (want != wall_loaded) swapWallpaper(want, true);
}

bool common::photoWallpaperMissing(){
    return wall_photo_missing;
}

void common::reloadWallpaper(){
    wall_loaded = -1;
}

void common::loadTheme(){
    images[IMAGE_BG] = loadWallpaper(config.wallpaper);
    wall_loaded = config.wallpaper;
    images[IMAGE_WAITICON] = new Image(theme_path, RESOURCES_LOAD_PLACE, findPkgOffset("WAIT.PNG"));

    images[0]->swizzle();
    images[1]->swizzle();

    startLoadingThread();

    images[IMAGE_LOADING] = new Image(theme_path, RESOURCES_LOAD_PLACE, findPkgOffset("LOADING.PNG"));
    images[IMAGE_SPRITE] = new Image(theme_path, RESOURCES_LOAD_PLACE, findPkgOffset("SPRITE.PNG"));
    images[IMAGE_NOICON] = new Image(theme_path, RESOURCES_LOAD_PLACE, findPkgOffset("NOICON.PNG"));
    images[IMAGE_GAME] = new Image(theme_path, RESOURCES_LOAD_PLACE, findPkgOffset("GAME.PNG"));
    images[IMAGE_FTP] = new Image(theme_path, RESOURCES_LOAD_PLACE, findPkgOffset("FTP.PNG"));
    images[IMAGE_SETTINGS] = new Image(theme_path, RESOURCES_LOAD_PLACE, findPkgOffset("SETTINGS.PNG"));
    images[IMAGE_BROWSER] = new Image(theme_path, RESOURCES_LOAD_PLACE, findPkgOffset("BROWSER.PNG"));
    images[IMAGE_DIALOG] = new Image(theme_path, RESOURCES_LOAD_PLACE, findPkgOffset("BOX.PNG"));
    images[IMAGE_EXIT] = new Image(theme_path, RESOURCES_LOAD_PLACE, findPkgOffset("EXIT.PNG"));
    images[IMAGE_PLUGINS] = new Image(theme_path, RESOURCES_LOAD_PLACE, findPkgOffset("PLUGINS.PNG"));

    icons[FOLDER] = new Image(theme_path, YA2D_PLACE_VRAM, common::findPkgOffset("FOLDER.PNG"));
    icons[FILE_BIN] = new Image(theme_path, YA2D_PLACE_VRAM, common::findPkgOffset("FILE.PNG"));
    icons[FILE_TXT] = new Image(theme_path, YA2D_PLACE_VRAM, common::findPkgOffset("TXT.PNG"));
    icons[FILE_PBP] = new Image(theme_path, YA2D_PLACE_VRAM, common::findPkgOffset("PBP.PNG"));
    icons[FILE_PRX] = new Image(theme_path, YA2D_PLACE_VRAM, common::findPkgOffset("PRX.PNG"));    
    icons[FILE_ISO] = new Image(theme_path, YA2D_PLACE_VRAM, common::findPkgOffset("ISO.PNG"));
    icons[FILE_ZIP] = new Image(theme_path, YA2D_PLACE_VRAM, common::findPkgOffset("ZIP.PNG"));
    icons[FILE_MUSIC] = new Image(theme_path, YA2D_PLACE_VRAM, common::findPkgOffset("MUSIC.PNG"));
    icons[FILE_PICTURE] = new Image(theme_path, YA2D_PLACE_VRAM, common::findPkgOffset("PICTURE.PNG"));

    images[IMAGE_DIALOG]->slice = 24;

    SceOff extra = findPkgOffset("PLATE.PNG", NULL, NULL, &dummyMissingHandler);
    if (extra){
        plate = new Image(theme_path, RESOURCES_LOAD_PLACE, extra);
        if (plate->getTexture()) plate->slice = 24;
        else { delete plate; plate = NULL; }
    }
    extra = findPkgOffset("RIM.PNG", NULL, NULL, &dummyMissingHandler);
    if (extra){
        rim = new Image(theme_path, RESOURCES_LOAD_PLACE, extra);
        if (rim->getTexture()) rim->slice = 16;
        else { delete rim; rim = NULL; }
    }

    extra = findPkgOffset("DOCK.PNG", NULL, NULL, &dummyMissingHandler);
    if (extra){
        dock = new Image(theme_path, RESOURCES_LOAD_PLACE, extra);
        if (dock->getTexture()) dock->slice = 24;
        else { delete dock; dock = NULL; }
    }
    extra = findPkgOffset("CFW.PNG", NULL, NULL, &dummyMissingHandler);
    if (extra){
        cfw_icon = new Image(theme_path, RESOURCES_LOAD_PLACE, extra);
        if (!cfw_icon->getTexture()){ delete cfw_icon; cfw_icon = NULL; }
    }
    for (int i=0; i<MAX_THEME_EXTRAS; i++){
        SceOff off = findPkgOffset(extra_files[i], NULL, NULL, &dummyMissingHandler);
        if (!off) continue;
        extras[i] = new Image(theme_path, RESOURCES_LOAD_PLACE, off);
        if (!extras[i]->getTexture()){ delete extras[i]; extras[i] = NULL; }
    }
    extra = findPkgOffset("GLOSS.PNG", NULL, NULL, &dummyMissingHandler);
    if (extra){
        gloss = new Image(theme_path, RESOURCES_LOAD_PLACE, extra);
        if (!gloss->getTexture()){ delete gloss; gloss = NULL; }
    }

    for (int i=0; i<HINT_BUTTONS; i++){
        SceOff off = findPkgOffset(hint_files[i], NULL, NULL, &dummyMissingHandler);
        if (!off) continue;
        hint_btns[i] = new Image(theme_path, YA2D_PLACE_VRAM, off);
        if (!hint_btns[i]->getTexture()){
            delete hint_btns[i];
            hint_btns[i] = NULL;
        }
    }

    checkbox[1] = new Image(theme_path, YA2D_PLACE_VRAM, common::findPkgOffset("CHECK.PNG"));
    checkbox[0] = new Image(theme_path, YA2D_PLACE_VRAM, common::findPkgOffset("UNCHECK.PNG"));
    
    for (int i=2; i<MAX_IMAGES; i++){
        images[i]->swizzle();
        images[i]->is_system_image = true;
    }
    
    unsigned mp3_size;
    void* mp3_buffer = readFromPKG("SOUND.MP3", &mp3_size);
    sound_mp3 = new MP3(mp3_buffer, mp3_size);

}

void common::loadData(int ac, char** av, int recovery){

    argc = ac;
    argv = av;

    today = common::getDateTime();

    psp_model = kuKernelGetModel();

    sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    sceUtilityLoadModule(PSP_MODULE_AV_MP3);
    
    animations[0] = new PixelAnim();
    animations[1] = new Waves();
    animations[2] = new Sprites();
    animations[3] = new Fire();
    animations[4] = new Tetris();
    animations[5] = new Matrix();
    animations[6] = new Hacker();
    animations[7] = new BSoD();
    animations[8] = new SnowAnim();
    animations[9] = new GoLAnim();
    animations[10] = new NoAnim();
    
    loadConfig();
    if (config.wallpaper >= MAX_WALLPAPERS) config.wallpaper = 0;

    // check to run last game
    const char* last_game = common::getConf()->last_game;
    if (last_game[0] != 0) {
        Controller pad;
        pad.update(1);
        if (!recovery && (pad.LT() || config.app_autoboot)){
            common::stopLoadingThread();
            if (Eboot::isEboot(last_game)){
                Eboot* eboot = new Eboot(last_game);
                eboot->execute(true);
            }
            else if (Iso::isISO(last_game)){
                Iso* iso = new Iso(last_game);
                iso->execute(true);
            }
        }
    }

    loadTheme();

    currentFont = config.font;
    currentLang = language_selection;
    currentApp = config.main_menu;

    Translations::loadLanguage(getLangFile());
    
    loadFont();

    if (currentFont != config.font){
        currentFont = config.font;
    }
    
}

void common::deleteTheme(){
    for (int i=0; i<MAX_IMAGES; i++){
        delete images[i];
    }
    for (int i=0; i<MAX_FILE_TYPES; i++){
        delete icons[i];
    }
    delete checkbox[0];
    delete checkbox[1];
    delete sound_mp3;
}

void common::deleteData(){
    deleteTheme();
    intraFontUnload(font);
}

void common::setThemePath(char* path){
    if (path == NULL) theme_path = THEME_NAME;
    else theme_path = path;
}

bool common::isFolder(SceIoDirent* dit){
    return FIO_SO_ISDIR(dit->d_stat.st_attr) || FIO_S_ISDIR(dit->d_stat.st_mode);
}

bool common::fileExists(const std::string &path){
    struct stat sb;
    return (stat(path.c_str(), &sb) == 0 && S_ISREG(sb.st_mode));
}

bool common::folderExists(const std::string &path){
    struct stat sb;
    return (stat(path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode));
}

long common::fileSize(const std::string &path){
    struct stat stat_buf;
    int rc = stat(path.c_str(), &stat_buf);
    return rc == 0 ? stat_buf.st_size : 0;
}

u64 common::deviceSize(const std::string path){
    struct DeviceSize {
        u32 maxClusters;
        u32 freeClusters;
        u32 maxSectors;
        u32 sectorSize;
        u32 sectorCount;
    } devsize;
    void* command = (void*)&devsize;
    memset(&devsize, 0, sizeof(devsize));
    string devpath = path.substr(0, path.find(":")+1);
    sceIoDevctl(devpath.c_str(), 0x02425818, &command, sizeof(command), NULL, 0);
    return (u64)devsize.freeClusters*(u64)devsize.sectorSize*(u64)devsize.sectorCount;
}

string common::beautifySize(u64 size){
    ostringstream txt;

    if (size < 1024)
        txt<<size<<" B";
    else if (1024 < size && size < 1048576)
        txt<<round(float(size*100)/1024.f)/100.f<<" KB";
    else if (1048576 < size && size < 1073741824)
        txt<<round(float(size*100)/1048576.f)/100.f<<" MB";
    else
        txt<<round(float(size*100)/1073741824.f)/100.f<<" GB";
    return txt.str();
}

Image* common::getImage(int which){
    return (which < MAX_IMAGES)? images[which] : images[IMAGE_LOADING];
}

Image* common::getIcon(int which){
    return (which < MAX_FILE_TYPES)? icons[which] : images[IMAGE_LOADING];
}

Image* common::getCheckbox(int which){
    return checkbox[which&1];
}

Image* common::getPlate(){
    return plate;
}

Image* common::getRim(){
    return rim;
}

Image* common::getDock(){
    return dock;
}

Image* common::getThemeImage(int which){
    return (which >= 0 && which < MAX_THEME_EXTRAS)? extras[which] : NULL;
}

Image* common::getCfwIcon(){
    return (cfw_icon)? cfw_icon : images[IMAGE_SETTINGS];
}

/* Read one texel of an 8888 texture, swizzled or not */
static inline u32 texel(ya2d_texture* t, int x, int y){
    if (x < 0) x = 0; else if (x >= t->width) x = t->width-1;
    if (y < 0) y = 0; else if (y >= t->height) y = t->height-1;
    u8* data = (u8*)t->data;
    if (!t->swizzled)
        return *(u32*)(data + y*t->stride + x*4);
    // swizzled blocks are 16 bytes wide and 8 rows tall
    int bx = x*4;
    u32 off = ((y>>3)*(t->stride>>4) + (bx>>4))*128 + (y&7)*16 + (bx&15);
    return *(u32*)(data + off);
}

static inline u32 mix(u32 a, u32 b, int w){ // w in 0..256, per channel
    u32 out = 0;
    for (int s=0; s<32; s+=8){
        int ca = (a>>s)&0xFF, cb = (b>>s)&0xFF;
        out |= (u32)((ca*(256-w) + cb*w)>>8) << s;
    }
    return out;
}

static u32 sample(ya2d_texture* t, float fx, float fy){
    int x = (int)fx, y = (int)fy;
    int wx = (int)((fx-x)*256), wy = (int)((fy-y)*256);
    u32 top = mix(texel(t, x, y), texel(t, x+1, y), wx);
    u32 bot = mix(texel(t, x, y+1), texel(t, x+1, y+1), wx);
    return mix(top, bot, wy);
}

#define APP_ICON 64
static u8* squircle = NULL;

/* Coverage of an iOS style superellipse (n=5), 4x4 supersampled, built once */
static void buildSquircle(){
    squircle = (u8*)malloc(APP_ICON*APP_ICON);
    const float r = APP_ICON/2.f;
    for (int y=0; y<APP_ICON; y++){
        for (int x=0; x<APP_ICON; x++){
            int hits = 0;
            for (int sy=0; sy<4; sy++){
                for (int sx=0; sx<4; sx++){
                    float u = fabsf((x + (sx+0.5f)/4.f - r)/r);
                    float v = fabsf((y + (sy+0.5f)/4.f - r)/r);
                    float u2 = u*u, v2 = v*v;
                    if (u2*u2*u + v2*v2*v <= 1.f) hits++;
                }
            }
            squircle[y*APP_ICON+x] = hits*255/16;
        }
    }
}

Image* common::makeAppIcon(Image* src){
    if (!src || !src->getTexture() || src->getTexture()->pixel_format != GU_PSM_8888) return NULL;
    if (!squircle) buildSquircle();

    ya2d_texture* st = src->getTexture();
    ya2d_texture* tex = ya2d_create_texture(APP_ICON, APP_ICON, GU_PSM_8888, YA2D_PLACE_RAM);
    if (!tex) return NULL;

    // backdrop: the icon cropped to a square, averaged down to 4x4 and blown up again,
    // which gives a soft blur in the icon's own colors
    const int B = 4;
    u32 grid[B*B];
    int side = (st->width < st->height)? st->width : st->height;
    int ox = (st->width-side)/2, oy = (st->height-side)/2;
    for (int gy=0; gy<B; gy++){
        for (int gx=0; gx<B; gx++){
            u32 acc[4] = {0,0,0,0}; int n = 0;
            for (int y=oy+gy*side/B; y<oy+(gy+1)*side/B; y+=2){
                for (int x=ox+gx*side/B; x<ox+(gx+1)*side/B; x+=2){
                    u32 c = texel(st, x, y);
                    int a = c>>24;
                    for (int k=0; k<3; k++) acc[k] += ((c>>(k*8))&0xFF)*a/255;
                    n++;
                }
            }
            if (!n) n = 1;
            // darken a little so the foreground stands out
            grid[gy*B+gx] = 0xFF000000 | ((acc[2]*3/(4*n))<<16) | ((acc[1]*3/(4*n))<<8) | (acc[0]*3/(4*n));
        }
    }

    // foreground: the whole icon, fitted to the width with a slight zoom
    float scale = (float)st->width / (APP_ICON*1.12f);
    int fh = (int)(st->height/scale);
    if (fh > APP_ICON) fh = APP_ICON;
    int fy0 = (APP_ICON-fh)/2;
    float fx_off = (st->width - APP_ICON*scale)/2.f;

    u32* out = (u32*)tex->data;
    int stride = tex->stride/4;
    for (int y=0; y<APP_ICON; y++){
        for (int x=0; x<APP_ICON; x++){
            float gxf = (x+0.5f)*B/APP_ICON - 0.5f, gyf = (y+0.5f)*B/APP_ICON - 0.5f;
            if (gxf < 0) gxf = 0; if (gyf < 0) gyf = 0;
            int gx = (int)gxf, gy = (int)gyf;
            int gx1 = (gx+1 < B)? gx+1 : gx, gy1 = (gy+1 < B)? gy+1 : gy;
            int wx = (int)((gxf-gx)*256), wy = (int)((gyf-gy)*256);
            u32 c = mix(mix(grid[gy*B+gx], grid[gy*B+gx1], wx), mix(grid[gy1*B+gx], grid[gy1*B+gx1], wx), wy);
            if (y >= fy0 && y < fy0+fh){
                u32 f = sample(st, fx_off + (x+0.5f)*scale - 0.5f, (y-fy0+0.5f)*scale - 0.5f);
                c = mix(c, f|0xFF000000, (f>>24)*256/255);
            }
            out[y*stride+x] = (c & 0x00FFFFFF) | ((u32)squircle[y*APP_ICON+x] << 24);
        }
    }
    ya2d_flush_texture(tex);
    return new Image(tex);
}

Image* common::fitImage(Image* src, int w, int h, bool cover){
    if (!src || !src->getTexture() || src->getTexture()->pixel_format != GU_PSM_8888) return NULL;
    ya2d_texture* st = src->getTexture();
    float sx = (float)st->width / w, sy = (float)st->height / h;
    // cover fills the box and crops, contain fits inside it
    float s = (cover)? ((sx < sy)? sx : sy) : ((sx > sy)? sx : sy);
    int ow = (cover)? w : (int)(st->width / s), oh = (cover)? h : (int)(st->height / s);
    if (ow < 1) ow = 1; if (oh < 1) oh = 1;
    if (ow > w) ow = w; if (oh > h) oh = h;
    ya2d_texture* tex = ya2d_create_texture(ow, oh, GU_PSM_8888, YA2D_PLACE_RAM);
    if (!tex) return NULL;
    float x0 = (st->width - ow*s)/2.f, y0 = (st->height - oh*s)/2.f;
    u32* out = (u32*)tex->data;
    int stride = tex->stride/4;
    // average four samples per pixel when shrinking a lot, so photos don't sparkle
    float q = (s > 2.f)? s/4.f : 0.f;
    for (int y=0; y<oh; y++){
        for (int x=0; x<ow; x++){
            float fx = x0 + (x+0.5f)*s - 0.5f, fy = y0 + (y+0.5f)*s - 0.5f;
            u32 c;
            if (q > 0){
                u32 a = sample(st, fx-q, fy-q), b = sample(st, fx+q, fy-q);
                u32 d = sample(st, fx-q, fy+q), e = sample(st, fx+q, fy+q);
                c = mix(mix(a, b, 128), mix(d, e, 128), 128);
            }
            else c = sample(st, fx, fy);
            out[y*stride+x] = c;
        }
    }
    ya2d_flush_texture(tex);
    return new Image(tex);
}

/* The photo wallpaper is stored raw (480x272 RGBA), no encoder or decoder needed */
#define WALL_W 480
#define WALL_H 272

bool common::setPhotoWallpaper(Image* src){
    Image* fit = fitImage(src, WALL_W, WALL_H, true);
    if (!fit) return false;
    ya2d_texture* t = fit->getTexture();
    string path = string(ark_config.arkpath) + "WALL.RAW";
    int fd = sceIoOpen(path.c_str(), PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC, 0777);
    bool ok = false;
    if (fd >= 0){
        ok = true;
        for (int y=0; y<WALL_H && ok; y++){
            u32* row = (u32*)((u8*)t->data + y*t->stride);
            for (int x=0; x<WALL_W; x++) row[x] |= 0xFF000000;
            ok = (sceIoWrite(fd, row, WALL_W*4) == WALL_W*4);
        }
        sceIoClose(fd);
    }
    delete fit;
    if (ok){
        config.wallpaper = WALLPAPER_PHOTO;
        saveConf();
        swapWallpaper(WALLPAPER_PHOTO, true);
    }
    return ok;
}

static Image* loadRawWallpaper(string path){
    SceUID fd = sceIoOpen(path.c_str(), PSP_O_RDONLY, 0777);
    if (fd < 0) return NULL;
    ya2d_texture* t = ya2d_create_texture(WALL_W, WALL_H, GU_PSM_8888, YA2D_PLACE_RAM);
    bool ok = (t != NULL);
    for (int y=0; y<WALL_H && ok; y++)
        ok = (sceIoRead(fd, (u8*)t->data + y*t->stride, WALL_W*4) == WALL_W*4);
    sceIoClose(fd);
    if (!ok){
        if (t) ya2d_free_texture(t);
        return NULL;
    }
    ya2d_flush_texture(t);
    return new Image(t);
}

void common::drawSub(Image* img, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh){
    if (!img || !img->getTexture()) return;
    ya2d_set_texture(img->getTexture());
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    ya2d_vertex_2s3s* v = (ya2d_vertex_2s3s*)sceGuGetMemory(2*sizeof(ya2d_vertex_2s3s));
    v[0].u = sx; v[0].v = sy; v[0].x = dx; v[0].y = dy; v[0].z = 0;
    v[1].u = sx+sw; v[1].v = sy+sh; v[1].x = dx+dw; v[1].y = dy+dh; v[1].z = 0;
    sceGumDrawArray(GU_SPRITES, GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2, 0, v);
}

void common::drawAppIcon(Image* img, int x, int y, int size){
    if (!img || !img->getTexture()) return;
    ya2d_texture* t = img->getTexture();
    ya2d_set_texture(t);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR); // smooth when shrinking or growing the icon
    ya2d_vertex_2s3s* v = (ya2d_vertex_2s3s*)sceGuGetMemory(2*sizeof(ya2d_vertex_2s3s));
    v[0].u = 0; v[0].v = 0; v[0].x = x; v[0].y = y; v[0].z = 0;
    v[1].u = t->width; v[1].v = t->height; v[1].x = x+size; v[1].y = y+size; v[1].z = 0;
    sceGumDrawArray(GU_SPRITES, GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2, 0, v);
    if (gloss){
        ya2d_texture* g = gloss->getTexture();
        ya2d_set_texture(g);
        sceGuTexFilter(GU_LINEAR, GU_LINEAR);
        ya2d_vertex_2s3s* w = (ya2d_vertex_2s3s*)sceGuGetMemory(2*sizeof(ya2d_vertex_2s3s));
        w[0].u = 0; w[0].v = 0; w[0].x = x; w[0].y = y; w[0].z = 0;
        w[1].u = g->width; w[1].v = g->height; w[1].x = x+size; w[1].y = y+size; w[1].z = 0;
        sceGumDrawArray(GU_SPRITES, GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 2, 0, w);
    }
}

void common::drawSelection(int x, int y, int w, int h){
    if (plate) plate->draw_scale(x, y, w, h);
    else ya2d_draw_rect(x, y, w, h, 0x40FFFFFF, 1);
}

void common::drawHints(string spec){
    if (spec.empty()) return;

    const int max_items = 6;
    Image* btn[max_items];
    string key[max_items];
    string label[max_items];
    int tw[max_items];
    int n = 0;

    size_t pos = 0;
    while (pos < spec.size() && n < max_items){
        size_t end = spec.find(';', pos);
        if (end == string::npos) end = spec.size();
        string item = spec.substr(pos, end-pos);
        size_t colon = item.find(':');
        if (colon != string::npos){
            key[n] = item.substr(0, colon);
            label[n] = item.substr(colon+1);
            btn[n] = NULL;
            if (key[n] == "L+R") btn[n] = hint_btns[4]; // drawn as L then R
            for (int i=0; i<HINT_BUTTONS; i++){
                if (key[n] == hint_keys[i]){
                    btn[n] = hint_btns[i];
                    break;
                }
            }
            tw[n] = calcTextWidth(label[n].c_str(), SIZE_LITTLE, 1);
            n++;
        }
        pos = end+1;
    }
    if (n == 0) return;

    const int h = 20, pad = 10, gap = 14, icon_gap = 4;
    int w = 2*pad - gap;
    for (int i=0; i<n; i++){
        int bw = (btn[i])? btn[i]->getWidth() : calcTextWidth(key[i].c_str(), SIZE_LITTLE, 0);
        if (key[i] == "L+R" && btn[i] && hint_btns[5]) bw += 2 + hint_btns[5]->getWidth();
        w += bw + icon_gap + tw[i] + gap;
    }
    if (w > 476) w = 476;
    int x = (480-w)/2;
    int y = 272-h-3;

    getImage(IMAGE_DIALOG)->draw_scale(x, y, w, h);

    int cx = x+pad;
    for (int i=0; i<n; i++){
        int bw;
        if (btn[i]){
            bw = btn[i]->getWidth();
            btn[i]->draw(cx, y + (h - btn[i]->getHeight())/2);
            if (key[i] == "L+R" && hint_btns[5]){
                hint_btns[5]->draw(cx + bw + 2, y + (h - hint_btns[5]->getHeight())/2);
                bw += 2 + hint_btns[5]->getWidth();
            }
        }
        else {
            bw = calcTextWidth(key[i].c_str(), SIZE_LITTLE, 0);
            printText(cx, y+14, key[i].c_str(), WHITE, SIZE_LITTLE, 0, NULL, 0);
        }
        cx += bw + icon_gap;
        printText(cx, y+14, label[i].c_str(), WHITE, SIZE_LITTLE, 0, NULL, 1);
        cx += tw[i] + gap;
    }
}

bool common::isSharedImage(Image* img){

    if (img->is_system_image)
        return true;

    for (int i=0; i<MAX_IMAGES; i++){
        if (images[i] == img)
            return true;
    }
    return false;
}

intraFont* common::getFont(){
    return font;
}

MP3* common::getMP3Sound(){
    return sound_mp3;
}

void common::playMenuSound(){
    sound_mp3->play();
}

void common::printText(float x, float y, const char* text, u32 color, float size, int glow, TextScroll* scroll, int translate){

    if (font == NULL)
        return;

    extern bool non_latin_filenames;
    string translated = (translate)? TR(text) : text;
    intraFont* textFont = font;

    if (translated != text){
        size *= text_size;
    }
    
    if (!translate && altFont && !non_latin_filenames){
        textFont = altFont;
    }

    u32 secondColor = BLACK_COLOR;
    u32 arg5 = INTRAFONT_WIDTH_VAR;
    
    if (glow && config.text_glow){
        int val = 0;
        float t = (float)((float)(clock() % CLOCKS_PER_SEC)) / ((float)CLOCKS_PER_SEC);
        if(config.text_glow == 1) {
            val = (t < 0.5f) ? t*311 : (1.0f-t)*311;
        }
        else if(config.text_glow == 2) {
            val = (t < 0.5f) ? t*411 : (1.0f-t)*411;
        }
        else {
            val = (t < 0.5f) ? t*511 : (1.0f-t)*511;
        }
        secondColor = (0xFF<<24)+(val<<16)+(val<<8)+(val);
    }
    if (scroll){
        arg5 = INTRAFONT_SCROLL_LEFT;
    }
    
    intraFontSetStyle(textFont, size, color, secondColor, 0.f, arg5);
    if (altFont) intraFontSetStyle(altFont, size, color, secondColor, 0.f, arg5);

    if (scroll){
        if (x != scroll->x || y != scroll->y){
            scroll->x = x;
            scroll->tmp = x;
            scroll->y = y;
        }
        if (scroll->w <= 0 || scroll->w >= 480) scroll->w = 200;
        scroll->tmp = intraFontPrintColumn(textFont, scroll->tmp, y, scroll->w, translated.c_str());
    }
    else
        intraFontPrint(textFont, x, y, translated.c_str());
    
}

int common::calcTextWidth(const char* text, float size, int translate){
    string translated = (translate)? TR(text) : text;
    intraFont* textFont = font;
    if (translated != text){
        size *= text_size;
    }
    if (!translate && altFont){
        textFont = altFont;
    }
    intraFontSetStyle(textFont, size, 0, 0, 0.f, INTRAFONT_WIDTH_VAR);
    float w = intraFontMeasureText(textFont, translated.c_str());
    return (int)ceil(w);
}

void common::clearScreen(u32 color){
    ya2d_start_drawing();
    ya2d_clear_screen(color);
    flipControl = true;
}

void common::drawBorder(){
    ya2d_draw_rect(10, 20, 80, 254, GRAY_COLOR, 0);
    ya2d_draw_rect(170, 20, 80, 254, GRAY_COLOR, 0);
    ya2d_draw_rect(330, 20, 80, 254, GRAY_COLOR, 0);
}

void common::drawScreen(){
    if (canDrawBackground())
        getImage(IMAGE_BG)->draw(0, 0);

    animations[getConf()->animation]->draw();
}

bool common::canDrawBackground(){
    return animations[getConf()->animation]->drawBackground();
}

void common::flipScreen(){
    if (!flipControl)
        return;

    sceDisplayWaitVblankStart();
    ya2d_finish_drawing();
    ya2d_swapbuffers();
    flipControl = false;
};

void common::upperString(char* str){
    while (*str){
        if (*str >= 'a' && *str <= 'z')
            *str -= 0x20;
        str++;
    }
}

int common::maxString(string* strings, int n_strings){
    int max = 0;
    for (int i = 0; i<n_strings; i++){
        int len = strings[i].length();
        if (len > max)
            max = len;
    }
    return max;
}

std::string common::getExtension(std::string path){
    std::string ext = path.substr(path.find_last_of(".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}
