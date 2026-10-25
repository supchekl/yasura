#ifndef MENU_H
#define MENU_H

#include <vector>
#include <cstdlib>
#include "iso.h"
#include "eboot.h"
#include "gfx.h"

#define MAX_DRAW_ENTRIES 5
#define MAX_LOADED_ICONS 11

/* iOS style home screen grid */
#define GRID_COLS 5
#define GRID_ROWS 2
#define GRID_PAGE (GRID_COLS*GRID_ROWS)

typedef enum {
    GAME = 0,
    HOMEBREW = 1,
    POPS = 2
}EntryType;

using namespace std;

class Menu{

    private:
        EntryType type;
        int index;
        int threadIndex;
        int animating;
        float animState;
        bool fastScrolling;
        bool animDelay;
        bool initLoad;
        bool stopLoading;
        vector<Entry*>* entries;

        /* home screen animation state */
        float pop[GRID_PAGE]; // 0 = resting icon, 1 = selected and grown
        float plate_x, plate_y, plate_s; // selection glass, glides between icons
        bool plate_valid;
        int shown_page, page_from;
        float page_off; // horizontal slide while turning a page
        clock_t appear_time; // icons pop in one after another
        float dot_x; // the wide page dot glides too
        void drawPage(int page, int xoff, bool focused, bool with_plate);
        
        void freeIcons();
        bool checkIconsNeeded(bool isSelected);
        void checkIndex();
        
    public:
        Menu(EntryType t);
        ~Menu();
        
        void draw(bool selected);
        void drawGrid(bool focused); // one page of rounded app icons with labels and page dots
        void setIndex(int i);
        void startAppear(); // replay the pop-in animation (section switch, coming back home)
        int getPageCount();
        
        void loadIconsDynamic(bool isSelected);
        
        bool waitIconsLoad(bool isSelected, bool forceQuit=false);
        
        void resumeIconLoading();
        
        void addEntry(Entry* e);
        Entry* getEntry();
        Entry* getEntry(int index);
        void clearEntries();
        size_t getVectorSize();
        vector<Entry*>* getVector();
        int getIndex(){ return index; };
        
        bool isAnimating();
        bool empty();
        
        void animStart(int direction);
        void moveUp();
        void moveDown();
        void stopFastScroll();
};
        
#endif
