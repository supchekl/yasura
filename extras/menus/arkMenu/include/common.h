#ifndef COMMON_H
#define COMMON_H

#include "debug.h"
#include <pspgu.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psprtc.h>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <time.h>
#include <ark.h>
#include "gfx.h"
#include "mp3.h"
#include "conf.h"

#define THREAD_DELAY 1000

// Colors
enum colors {
    RED =    0xFF0000FF,
    GREEN =    0xFF00FF00,
    BLUE =    0xFFFF0000,
    WHITE =    0xFFFFFFFF,
    LITEGRAY = 0xFFEFEFEF,
    GRAY =  0xFF7F7F7F,
    DARKGRAY = 0xFF3F3F3F,        
    BLACK = 0xFF000000,
    YELLOW = 0xFF00FFFF,
    WEIRD_ORANGE = 0xFF2AB1FF
};

#define MAX_WALLPAPERS 6
#define WALLPAPER_PHOTO 5

/* optional images of the Yasura theme */
enum {
    THEME_PHOTOS,
    THEME_VIDEO,
    THEME_CLOCK, // digit strip for the lock screen clock: 0-9 and ':'
    THEME_CC, // glyph strip for the control center
    MAX_THEME_EXTRAS
};

/* glyphs in CCICONS.PNG, 32px cells */
enum { GLYPH_PLAY, GLYPH_USB, GLYPH_MOON, GLYPH_REFRESH, GLYPH_MUSIC, GLYPH_BATTERY, GLYPH_CARD, GLYPH_BOLT, GLYPH_LOCK,
       GLYPH_SEARCH, GLYPH_CAMERA, GLYPH_IMAGE, GLYPH_GAME };

enum images {
    IMAGE_BG,
    IMAGE_WAITICON,
    IMAGE_NOICON,
    IMAGE_GAME,
    IMAGE_SETTINGS,
    IMAGE_BROWSER,
    IMAGE_FTP,
    IMAGE_DIALOG,
    IMAGE_LOADING,
    IMAGE_SPRITE,
    IMAGE_EXIT,
    IMAGE_PLUGINS,
    MAX_IMAGES
};

enum {
    FOLDER,
    FILE_BIN,
    FILE_TXT,
    FILE_PBP,
    FILE_PRX,
    FILE_ISO,
    FILE_ZIP,
    FILE_MUSIC,
    FILE_PICTURE,
    MAX_FILE_TYPES,
};

typedef struct TextScroll{
    float x;
    float y;
    float tmp;
    float w;
}TextScroll;

#define SIZE_TINY 0.4f
#define SIZE_LITTLE 0.51f
#define SIZE_MEDIUM 0.6f
#define SIZE_BIG 0.7f
#define SIZE_HUGE 1.5f

#define THEME_NAME "THEME.ARK"

#define MS0_PATH 0x3A30736D // 'ms0:' as u32
#define EF0_PATH 0x3A306665 // 'ef0:' as u32

extern struct tm today;

namespace common{

    extern bool is_recovery;
    extern ARKConfig* getArkConfig();
    extern int getArgc();
    extern char** getArgv();
    extern int getPspModel();
    extern struct tm getDateTime();
    extern bool has_suffix(const std::string &str, const std::string &suffix);
    SceOff findPkgOffset(const char* filename, unsigned* size = NULL, const char* pkgpath=NULL, void (*missinghandler)(const char*) = NULL);
    extern void* readFromPKG(const char* filename, unsigned* size = NULL, const char* pkgpath=NULL);
    extern u32 getMagic(const char* filename, unsigned int offset);
    extern void loadData(int ac, char** av, int recovery);
    extern void deleteData();
    extern void loadTheme();
    extern void deleteTheme();
    extern void startLoadingThread();
    extern void stopLoadingThread();
    extern void setThemePath(char* path = NULL);
    extern bool isFolder(SceIoDirent* dit);
    extern bool fileExists(const std::string &path);
    extern bool folderExists(const std::string &path);
    extern long fileSize(const std::string &path);
    extern u64 deviceSize(const std::string path);
    extern string beautifySize(u64 size);
    extern Image* getImage(int which);
    extern Image* getIcon(int which);
    extern Image* getPlate(); // optional selection backdrop, may be NULL
    extern Image* getRim(); // optional selection outline, may be NULL
    extern void drawSelection(int x, int y, int w, int h); // highlight behind a selected row
    extern Image* getDock(); // optional home screen dock backdrop, may be NULL
    extern Image* getCfwIcon(); // icon of the CFW settings app, falls back to the settings icon
    extern Image* getThemeImage(int which); // optional Yasura images (THEME_*), may be NULL
    extern Image* fitImage(Image* src, int w, int h, bool cover); // resample any 8888 image, works past the 512px GU limit
    extern void drawSub(Image* img, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh); // part of a texture, smooth
    extern void previewWallpaper(int which); // show this wallpaper on the next frame only (settings preview)
    extern void reloadWallpaper();
    extern bool photoWallpaperMissing();

    /* animation helpers (the draw thread runs at the display rate, about 60 fps) */
    extern float easeOut(float t); // cubic ease out, t in 0..1
    extern float easeBack(float t); // ease out with a small overshoot, for springy pops
    extern float approach(float cur, float target, float k=0.3f); // exponential glide toward target
    extern float since(clock_t start, float seconds); // 0..1 progress of an animation started at start
    extern void setAlpha(int a); // fade nine-slice panels, drawSub and app icons, 255 = opaque
    extern u32 withAlpha(u32 color, int a);

    /* iOS style banners that slide down from the top */
    extern void notify(std::string text, int glyph);
    extern void drawNotification();

    extern bool saveScreenshot(); // PNG of the screen into PICTURE/Yasura
    extern bool mediaChanged(bool clear); // new pictures since the Photos app last looked
    extern bool setPhotoWallpaper(Image* src); // save a photo as the custom wallpaper and switch to it
    extern Image* makeAppIcon(Image* src); // square iOS style app icon (squircle alpha) from an ICON0
    extern void drawAppIcon(Image* img, int x, int y, int size); // smooth scaled draw plus theme gloss
    extern void drawHints(string spec); // bottom button hint bar, spec is "X:Label;O:Label;..."
    extern Image* getCheckbox(int which);
    extern bool isSharedImage(Image* img);
    extern intraFont* getFont();
    extern MP3* getMP3Sound();
    extern void saveConf();
    extern t_conf* getConf();
    extern void resetConf();
    extern void playMenuSound();
    extern void printText(float x, float y, const char *text, u32 color=GRAY_COLOR, float size=SIZE_LITTLE, int glow=0, TextScroll* scroll=NULL, int translate=1);
    extern int calcTextWidth(const char* text, float size=SIZE_LITTLE, int translate=1);
    extern void clearScreen(u32 color = CLEAR_COLOR);
    extern void drawBorder();
    extern void drawScreen();
    extern bool canDrawBackground();
    extern void flipScreen();
    extern void upperString(char* text);
    extern int maxString(string* strings, int n_strings);
    extern std::string getExtension(std::string path);
    extern void launchRecovery(const char* path);
}

#endif
