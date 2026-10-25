/* Category Class that defines each of the individual menus */

#include "menu.h"
#include "mp3.h"
#include "lang.h"

static SceUID iconSema = -1;

Menu::Menu(EntryType t){
    this->type = t;
    this->index = 0;
    this->threadIndex = 0;
    this->animating = 0;
    this->fastScrolling = false;
    this->animDelay = true;
    this->animState = 0.f;
    this->initLoad = false;
    this->stopLoading = false;
    this->entries = new vector<Entry*>();
    if (iconSema < 0)
        iconSema = sceKernelCreateSema("icon_sema",  0, 1, 1, NULL);
}


Menu::~Menu(){
    this->clearEntries();
    delete this->entries;
}

// the home screen shows one page at a time, so icons are kept for the current page only
static inline int pageStart(int index){
    return (index/GRID_PAGE)*GRID_PAGE;
}

void Menu::freeIcons(){
    int start = pageStart(this->threadIndex);
    for (int i = 0; i < start; i++)
        this->getEntry(i)->freeIcon();

    for (int i = start+GRID_PAGE; i<this->getVectorSize(); i++)
        this->getEntry(i)->freeIcon();
}

bool Menu::checkIconsNeeded(bool isSelected){
    int start = pageStart(this->threadIndex);
    for (int i = start; i < min((int)this->getVectorSize(), start+GRID_PAGE); i++){
        Entry* e = this->getEntry(i);
        if (e->getIcon() == common::getImage(IMAGE_WAITICON) || e->getAppIcon() == NULL)
            return true;
    }
    return false;
}

void Menu::checkIndex(){
    if (this->index>=this->getVectorSize()){
        this->index = this->getVectorSize()-1;
    }
    if (this->index<0){
        this->index = 0;
    }
}

void Menu::loadIconsDynamic(bool isSelected){

    if (this->fastScrolling || this->getVectorSize() == 0 || stopLoading)
        return; // we don't need to load any icons

    this->checkIndex();

    sceKernelWaitSema(iconSema, 1, NULL);

    this->stopLoading = false;
    this->threadIndex = this->index; // prevents our working index from changing
    freeIcons(); // delete any icon that won't be loaded
    if (!checkIconsNeeded(isSelected)){ // check if we need to load icons
        sceKernelSignalSema(iconSema, 1);
        return;
    }
    // start loading the needed icons
    int start = pageStart(this->threadIndex);
    for (int i = start; i < min((int)this->getVectorSize(), start+GRID_PAGE); i++){
        if (pageStart(this->index) != start || this->stopLoading)
            break; // stop loading the icons if the page has changed
        if (this->getEntry(i)->getIcon() == common::getImage(IMAGE_WAITICON))
            this->getEntry(i)->loadIcon();
        this->getEntry(i)->makeAppIcon();
    }
    initLoad = !this->stopLoading;
    sceKernelSignalSema(iconSema, 1);
}

bool Menu::waitIconsLoad(bool isSelected, bool forceQuit){
    if (this->getVectorSize() == 0) return true;
    this->stopLoading = forceQuit;
    sceKernelWaitSema(iconSema, 1, NULL); // wait for the thread to release the semaphore
    if (this->getEntry()->getIcon() == common::getImage(IMAGE_WAITICON))
        this->getEntry()->loadIcon();
    sceKernelSignalSema(iconSema, 1);
    return true;
}

void Menu::resumeIconLoading(){
    this->stopLoading = false;
}

void Menu::draw(bool selected){

    if (this->getVectorSize() == 0)
        return;

    this->checkIndex();

    int xoffset = ((int)this->type)*144 + ((int)this->type+1)*10 + 5;
    int yoffset = 20;
    int curentry_yoffset = 0;

    // draw scrollbar
    if (selected && fastScrolling){
        int height = 240/entries->size();
        int x = xoffset-7;
        int y = yoffset+10;
        ya2d_draw_rect(x+2, y, 3, height*entries->size(), DARKGRAY, 1);
        ya2d_draw_rect(x+1, y + index*height, 5, height, DARKGRAY, 1);
        ya2d_draw_rect(x+3, y, 1, height*entries->size(), LITEGRAY, 1);
        ya2d_draw_rect(x+2, y + index*height, 3, height, LITEGRAY, 1);
    }
    
    // draw icon, calculating whatever for animation
    float anim = 0.0f;
    if (animState >= 1.0f){
        if (animating == -1){
            if (this->index < this->getVectorSize()-1)
                this->index++;
            else
                animating = 0;
        }
        else if (animating == 1){
            if (this->index > 0)
                this->index--;
            else
                animating = 0;
        }
        if (fastScrolling){
            animState = 0.74f;
            animDelay = false;
        }
        else
            animating = 0;
    }
    
    if (animating){
        anim = (animating != 2)? animState : 0.f;
        if (!this->animDelay)
            animState += (animating == 2)? 0.5f : 0.25f;
        if (animating == -1)
            anim *= -1;
    }

    float scale = 1.f;
    if (animating){
        if (animState < 0.5f)
            scale = 0.5f;
        else if (animState < 1.f)
            scale = 0.75f;
    }

    for (int i=this->index-2; yoffset<272; i++){
        if (i < 0){
            yoffset+=40;
            continue;
        }

        if (i >= this->getVectorSize()){
            break;
        }
            
        if (selected && i == this->index){
            curentry_yoffset = yoffset;
            if (animating)
                yoffset += 70;
            else
                yoffset += 90;
            continue;
        }
        else{
            Image* e_icon = getEntry(i)->getIcon();
            if (animating == 1 && i == this->index-1 && !fastScrolling){
                e_icon->draw_scale(xoffset+2, yoffset+anim*40, 0.75f, 0.75f);
                yoffset += 60;
            }
            else if (animating == -1 && i == this->index+1 && !fastScrolling){
                e_icon->draw_scale(xoffset+2, yoffset+anim*40, 0.75f, 0.75f);
                yoffset += 60;
            }
            else{
                e_icon->draw_scale(xoffset, yoffset+anim*40, 0.5f, 0.5f);
                yoffset += 40;
            }
        }
    }

    if (selected){
        Image* e_icon = getEntry(this->index)->getIcon();
        int height = e_icon->getTexture()->height;
        if (height != 80)
            curentry_yoffset = (272-height)/2;
        
        Image* plate = common::getPlate();
        Image* rim = common::getRim();
        if (animating){
            int w = e_icon->getWidth()*3/4, h = e_icon->getHeight()*3/4;
            int x = xoffset+2, y = curentry_yoffset+5+anim*40;
            if (plate) plate->draw_scale(x-4, y-4, w+8, h+8);
            e_icon->draw_scale(x, y, 0.75f, 0.75f);
            if (rim) rim->draw_scale(x, y, w, h);
        }
        else {
            int w = e_icon->getWidth(), h = e_icon->getHeight();
            int x = xoffset+5, y = curentry_yoffset+5+anim*40;
            if (plate) plate->draw_scale(x-4, y-4, w+8, h+8);
            else {
                int cw = common::calcTextWidth("...", 1.f, 0);
                float factor = float(e_icon->getWidth())/float(cw);
                common::printText(xoffset, curentry_yoffset+(height/2), "...", WHITE_COLOR, factor, 1, NULL, 0);
            }
            e_icon->draw(x, y);
            if (rim) rim->draw_scale(x, y, w, h);
        }   
    }
}

void Menu::setIndex(int i){
    if (i < 0 || i >= (int)this->getVectorSize() || i == this->index) return;
    this->index = i;
    common::playMenuSound();
}

int Menu::getPageCount(){
    return (this->getVectorSize() + GRID_PAGE - 1) / GRID_PAGE;
}

// cut a label to fit under an icon
static string fitLabel(const string& name, int max_w){
    if (common::calcTextWidth(name.c_str(), SIZE_LITTLE, 0) <= max_w) return name;
    string s = name;
    while (s.size() > 1){
        // drop a whole UTF-8 character
        size_t cut = s.size()-1;
        while (cut > 0 && (s[cut] & 0xC0) == 0x80) cut--;
        s = s.substr(0, cut);
        while (!s.empty() && s[s.size()-1] == ' ') s = s.substr(0, s.size()-1);
        if (common::calcTextWidth((s+"...").c_str(), SIZE_LITTLE, 0) <= max_w) break;
    }
    return s + "...";
}

void Menu::drawGrid(bool focused){
    if (this->getVectorSize() == 0) return;
    this->checkIndex();

    const int size = 52, cell_w = 88, top = 26, row_h = 74;
    const int left = (480 - GRID_COLS*cell_w)/2;
    int start = pageStart(this->index);
    Image* wait = common::getImage(IMAGE_WAITICON);

    for (int k = 0; k < GRID_PAGE && start+k < (int)this->getVectorSize(); k++){
        Entry* e = this->getEntry(start+k);
        int col = k % GRID_COLS, row = k / GRID_COLS;
        int cx = left + col*cell_w + cell_w/2;
        int y = top + row*row_h;
        bool sel = focused && (start+k == this->index);
        int s = (sel)? size+8 : size;
        int x = cx - s/2;
        int iy = y + (size - s)/2;

        if (sel){
            Image* plate = common::getPlate();
            if (plate) plate->draw_scale(x-5, iy-5, s+10, s+10);
        }

        Image* round = e->getAppIcon();
        Image* icon = e->getIcon();
        if (round){
            common::drawAppIcon(round, x, iy, s);
        }
        else if (icon && icon != wait){
            // icon still being rounded, show it fitted for now
            int h = s * icon->getHeight() / max(1, icon->getWidth());
            icon->draw_scale(x, iy + (s-h)/2, s, h);
        }
        else {
            common::getImage(IMAGE_DIALOG)->draw_scale(x, iy, s, s);
        }

        string name = e->getAppLabel();
        if (name == "Recovery Menu" || name == "UMD Drive") name = TR(name);
        string label = fitLabel(name, cell_w-6);
        int tw = common::calcTextWidth(label.c_str(), SIZE_LITTLE, 0);
        common::printText(cx - tw/2, y + size + 15, label.c_str(), (sel)? WHITE : 0xFFE6E6E6, SIZE_LITTLE, 0, NULL, 0);
    }

    // page dots
    int pages = this->getPageCount();
    if (pages > 1 && focused){ // the dock shows its app name here instead
        int cur = this->index / GRID_PAGE;
        int w = pages*10 - 4;
        int x = (480 - w)/2;
        for (int p = 0; p < pages; p++){
            ya2d_draw_rect(x + p*10, 180, 6, 6, (p == cur)? WHITE : 0x70FFFFFF, 1);
        }
    }
}

void Menu::animStart(int direction){
    animating = direction;
    animState = 0.f;
    animDelay = false;
}

bool Menu::isAnimating(){
    return animating != 0;
}

bool Menu::empty(){
    bool ret = !this->entries->size();
    return ret;
}

void Menu::addEntry(Entry* e){
    this->entries->push_back(e);
}

Entry* Menu::getEntry(){
    return entries->at(this->index);
}

Entry* Menu::getEntry(int index){
    return entries->at(index);
}

void Menu::clearEntries(){
    for (int i=0; i<entries->size(); i++){
        Entry* entry = entries->at(i);
        entries->at(i) = NULL;
        delete entry;
    }
    this->entries->clear();
    this->index = 0;
    this->initLoad = false;
}

size_t Menu::getVectorSize(){
    size_t ret = this->entries->size();
    return ret;
}

vector<Entry*>* Menu::getVector(){
    return entries;
}

void Menu::moveUp(){
    if (animating || fastScrolling){
        fastScrolling = true;
        if (this->index <= 0){
            animating = 0;
            this->index = 0;
            this->stopFastScroll();
        }
        else{
            this->index--;
            common::playMenuSound();
        }
    }
    else if (this->index > 0){
        common::playMenuSound();
        this->stopFastScroll();
        animStart(1);
    }
    else
        this->stopFastScroll();
}

void Menu::moveDown(){
    if (animating || fastScrolling){
        fastScrolling = true;
        if (this->index >= this->getVectorSize()-1){
            animating = 0;
            this->index = this->getVectorSize()-1;
            this->stopFastScroll();
        }
        else{
            common::playMenuSound();
            this->index++;
        }
    }
    else if (this->index < this->getVectorSize()-1){
        common::playMenuSound();
        this->stopFastScroll();
        animStart(-1);
    }
    else
        this->stopFastScroll();
}

void Menu::stopFastScroll(){
    fastScrolling = false;
    animDelay = false;
}
