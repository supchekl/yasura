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
    for (int i=0; i<GRID_PAGE; i++) this->pop[i] = 0;
    this->plate_x = this->plate_y = this->plate_s = 0;
    this->plate_valid = false;
    this->shown_page = 0;
    this->page_from = 0;
    this->page_off = 0;
    this->appear_time = 0;
    this->dot_x = -1;
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

void Menu::startAppear(){
    this->appear_time = clock();
    if (this->appear_time == 0) this->appear_time = 1;
    this->plate_valid = false;
}

static const int GRID_ICON = 52, GRID_CELL = 88, GRID_TOP = 26, GRID_ROW = 74;

void Menu::drawPage(int page, int xoff, bool focused, bool with_plate){
    const int left = (480 - GRID_COLS*GRID_CELL)/2;
    int start = page*GRID_PAGE;
    Image* wait = common::getImage(IMAGE_WAITICON);
    bool current = (page == this->index / GRID_PAGE);

    // the selection glass glides to the selected icon
    if (with_plate && focused && current){
        int k = this->index - start;
        int cx = left + (k % GRID_COLS)*GRID_CELL + GRID_CELL/2 + xoff;
        int y = GRID_TOP + (k / GRID_COLS)*GRID_ROW;
        float s = GRID_ICON + 8 + 10;
        float tx = cx - s/2, ty = y + GRID_ICON/2 - s/2;
        if (!plate_valid){ plate_x = tx; plate_y = ty; plate_s = s; plate_valid = true; }
        plate_x = common::approach(plate_x, tx, 0.35f);
        plate_y = common::approach(plate_y, ty, 0.35f);
        plate_s = common::approach(plate_s, s, 0.35f);
        Image* plate = common::getPlate();
        if (plate) plate->draw_scale((int)plate_x, (int)plate_y, (int)plate_s, (int)plate_s);
    }

    for (int k = 0; k < GRID_PAGE && start+k < (int)this->getVectorSize(); k++){
        Entry* e = this->getEntry(start+k);
        int col = k % GRID_COLS, row = k / GRID_COLS;
        int cx = left + col*GRID_CELL + GRID_CELL/2 + xoff;
        int y = GRID_TOP + row*GRID_ROW;
        bool sel = focused && (start+k == this->index);

        // springy grow of the selected icon
        float p = 0;
        if (current){
            pop[k] = common::approach(pop[k]*100, (sel)? 100 : 0, 0.3f) / 100.f;
            p = pop[k];
        }
        // staggered pop-in when the section appears
        float scale = 1.f;
        int alpha = 255;
        if (appear_time){
            clock_t t0 = appear_time + (clock_t)(k * 0.025f * CLOCKS_PER_SEC);
            float t = (clock() < t0)? 0 : common::since(t0, 0.35f);
            scale = 0.4f + 0.6f*common::easeBack(t);
            alpha = (int)(255*common::easeOut(t));
        }
        int s = (int)((GRID_ICON + 8*p) * scale);
        int x = cx - s/2;
        int iy = y + (GRID_ICON - s)/2;

        common::setAlpha(alpha);
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
        common::setAlpha(255);

        string name = e->getAppLabel();
        if (name == "Recovery Menu" || name == "UMD Drive") name = TR(name);
        string label = fitLabel(name, GRID_CELL-6);
        int tw = common::calcTextWidth(label.c_str(), SIZE_LITTLE, 0);
        common::printText(cx - tw/2, y + GRID_ICON + 15, label.c_str(), common::withAlpha((sel)? WHITE : 0xFFE6E6E6, alpha), SIZE_LITTLE, 0, NULL, 0);
    }
}

void Menu::drawGrid(bool focused){
    if (this->getVectorSize() == 0) return;
    this->checkIndex();

    // turning a page slides the old one out and the new one in
    int page = this->index / GRID_PAGE;
    if (page != shown_page){
        page_from = shown_page;
        page_off = (page > shown_page)? 480 : -480;
        shown_page = page;
        for (int i=0; i<GRID_PAGE; i++) pop[i] = 0;
        plate_valid = false;
    }
    page_off = common::approach(page_off, 0, 0.22f);
    int off = (int)page_off;
    if (off != 0){
        int dir = (page_off > 0)? 1 : -1;
        drawPage(page_from, off - dir*480, false, false);
    }
    drawPage(page, off, focused, off == 0);
    if (appear_time && common::since(appear_time, 0.35f + GRID_PAGE*0.025f) >= 1) appear_time = 0;

    // page dots, the current one is a wide pill that glides
    int pages = this->getPageCount();
    if (pages > 1 && focused){ // the dock shows its app name here instead
        int w = pages*10 + 6;
        int x = (480 - w)/2;
        float target = x + page*10;
        if (dot_x < 0) dot_x = target;
        dot_x = common::approach(dot_x, target, 0.3f);
        for (int p = 0; p < pages; p++){
            int px = x + p*10 + ((p > page)? 6 : 0);
            if (p != page) ya2d_draw_rect(px, 180, 6, 6, 0x70FFFFFF, 1);
        }
        ya2d_draw_rect((int)dot_x, 180, 12, 6, WHITE, 1);
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
