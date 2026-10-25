#include <cstring>
#include <pspdisplay.h>
#include "entry.h"
#include "eboot.h"
#include "iso.h"
#include "sprites.h"
#include "system_mgr.h"
#include "music_player.h"
#include "pspav.h"
#include "pspav_wrapper.h"
#include "lang.h"
#include <cctype>

extern "C" int sceDisplaySetHoldMode(int);

int gameBootThread(SceSize _args, void *_argp){
    Sprites s;

    int w = 480/10;
    int h = 272/10;

    while (w < 480 || h < 272){
    
        common::clearScreen(CLEAR_COLOR);
        
        common::getImage(IMAGE_BG)->draw(0, 0);
    
        if (w+80<480) w+=80;
        else w=480;

        if (h+40<272) h+=40;
        else h=272;

        int x = (480-w)/2;
        int y = (272-h)/2;

        common::getImage(IMAGE_LOADING)->draw_scale(x, y, w, h);
        s.drawFadeout();
        common::flipScreen();
    }
    while (true){
        common::clearScreen(CLEAR_COLOR);
        common::getImage(IMAGE_LOADING)->draw(0, 0);
        bool ret = s.drawFadeout();
        common::flipScreen();
        if (!ret)
            break;
    }
    
    sceKernelExitDeleteThread(0);
    
    return 0;
}

Entry::Entry(){
    this->has_details = false;
    this->app_icon = NULL;
}

Entry::Entry(string path){
    this->has_details = false;
    this->app_icon = NULL;
}

void Entry::makeAppIcon(){
    if (app_label.empty()){
        SfoInfo info = this->getSfoInfo();
        info.title[sizeof(info.title)-1] = 0;
        app_label = (info.title[0])? info.title : name;
    }
    if (app_icon || icon0 == NULL || icon0 == common::getImage(IMAGE_WAITICON)) return;
    app_icon = common::makeAppIcon(icon0);
}

void Entry::loadDetails(){
    SfoInfo info = this->getSfoInfo();
    detail_title = (info.title[0])? info.title : name;
    detail_id = (info.gameid[0])? info.gameid : "-";

    string type = this->getType();
    if (type == "ISO"){
        string ext = common::getExtension(path);
        for (int i=0; i<(int)ext.size(); i++) ext[i] = toupper(ext[i]);
        detail_type = "PSP " + ext;
    }
    else if (type == "EBOOT"){
        string sub = this->getSubtype() ? this->getSubtype() : "";
        if (sub == "POPS") detail_type = "PS1 Game";
        else if (sub == "PSN") detail_type = "PSN Game";
        else if (sub == "UPDATER") detail_type = "Update";
        else detail_type = "Homebrew";
    }
    else detail_type = type;

    long size = common::fileSize(path);
    detail_size = (size > 0)? common::beautifySize(size) : "-";
    has_details = true;
}

void Entry::drawDetails(){
    static TextScroll title_scroll;
    const int x = 176, y = 58, w = 292, h = 140;

    // dim the game art so the card stays readable
    ya2d_draw_rect(0, 0, 480, 272, 0x70000000, 1);
    common::getImage(IMAGE_DIALOG)->draw_scale(x, y, w, h);

    title_scroll.w = w - 28;
    common::printText(x+14, y+26, detail_title.c_str(), WHITE, SIZE_BIG, 0, &title_scroll, 0);
    ya2d_draw_rect(x+12, y+36, w-24, 1, 0x40FFFFFF, 1);

    string shown_path = path;
    if (shown_path.size() > 34) shown_path = "..." + shown_path.substr(shown_path.size()-31);
    const char* labels[4] = {"Type", "Game ID", "Size", "Path"};
    string values[4] = {TR(detail_type), detail_id, detail_size, shown_path};
    int yoffset = y+58;
    for (int i=0; i<4; i++){
        common::printText(x+14, yoffset, labels[i], 0xFFA0A0A0, SIZE_LITTLE, 0, NULL, 1);
        common::printText(x+84, yoffset, values[i].c_str(), WHITE, SIZE_LITTLE, 0, NULL, 0);
        yoffset += 19;
    }

    common::drawHints("X:Play;O:Back");
}

void Entry::setName(string name){
    this->name = name;
}

string Entry::getName(){
    return this->name;
}

string Entry::getPath(){
    return this->path;
}

void Entry::setPath(string path){
    this->path = path;
}

Image* Entry::getIcon(){
    return this->icon0;
}

void* Entry::getIcon1(){
    return this->icon1;
}

int Entry::getIcon1Size(){
    return this->icon1_size;
}

Image* Entry::getPic0(){
    return this->pic0;
}

Image* Entry::getPic1(){
    return this->pic1;
}

void* Entry::getSnd(){
    return this->snd0;
}

int Entry::getSndSize(){
    return this->at3_size;
}

void Entry::freeIcon(){
    Image* round = this->app_icon;
    this->app_icon = NULL;
    if (round) delete round;
    register Image* aux = this->icon0;
    this->icon0 = common::getImage(IMAGE_WAITICON);
    if (aux && !common::isSharedImage(aux))
        delete aux;
}

void Entry::execute(bool isAutoboot){
    if (!isAutoboot) {
        char* last_game = common::getConf()->last_game;
        if (strcmp(last_game, this->path.c_str()) != 0 && name != "UMD Drive" && name != "Recovery Menu"){
            strcpy(last_game, this->path.c_str());
        }
        common::saveConf();
        this->gameBoot();
    }
    this->doExecute();
}

void Entry::gameBoot(){

    //if (common::getConf()->fast_gameboot && name != "Recovery Menu")
    if (common::getConf()->fast_gameboot) {
        MusicPlayer::fullStop();
        SystemMgr::pauseDraw();
        sceDisplaySetHoldMode(1);
        return;
    }

    MusicPlayer::fullStop();

    SystemMgr::pauseDraw();

    unsigned mp3_size;
    void* mp3_buffer = common::readFromPKG("BOOT.MP3", &mp3_size);
    
    SceUID boot_thread = sceKernelCreateThread("boot_thread", gameBootThread, 0x10, 0x8000, PSP_THREAD_ATTR_USER, NULL);
    
    sceKernelStartThread(boot_thread, 0, NULL);
    
    playMP3File(NULL, mp3_buffer, mp3_size);
    
    free(mp3_buffer);
    
    sceKernelWaitThreadEnd(boot_thread, NULL);

    sceDisplaySetHoldMode(1);
    
}

void Entry::drawBG(){
    if (this->pic1 != NULL){
        if (this->pic1->getWidth() == 480 && this->pic1->getHeight() == 272)
            this->pic1->draw(0, 0);
        else
            this->pic1->draw_scale(0, 0, 480, 272);
    }
    else common::drawScreen();
    if (has_details) drawDetails();
    else if (this->pic0 != NULL) this->pic0->draw(160, 85);
}

void Entry::freeTempData(){
    if (this->pic0 != NULL)
        delete this->pic0;
    if (this->pic1 != NULL)
        delete this->pic1;
    if (this->icon1 != NULL)
        free(this->icon1);
    if (this->snd0 != NULL)
        free(this->snd0);
}

bool Entry::isArchive(const char* path){
    return  common::getExtension(path) == "zip" ||
            common::getExtension(path) == "rar" ||
            common::getExtension(path) == "tar" ||
            common::getExtension(path) == "7z"   ;
}

bool Entry::isPRX(const char* path){
    return (common::getExtension(path) == "prx");
}

bool Entry::isARK(const char* path){
    return (common::getExtension(path) == "ark");
}

bool Entry::isTXT(const char* path){
    string ext = common::getExtension(path);
    return (ext == "txt" || ext == "cfg" || ext == "ini" || ext == "log" || ext == "py" || ext == "lua");
}

bool Entry::isIMG(const char* path){
    string ext = common::getExtension(path);
    return (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp");
}

bool Entry::isMusic(const char* path){
    string ext = common::getExtension(path);
    return (ext == "mp3");
}

bool Entry::isVideo(const char* path){
    string ext = common::getExtension(path);
    return (ext == "pmf" || ext == "mps");
}

Entry::~Entry(){
    if (app_icon) delete app_icon;
}

bool Entry::cmpEntriesForSort (Entry* i, Entry* j) {
    return (strcasecmp(i->getName().c_str(), j->getName().c_str())<0);
}

bool Entry::getSfoParam(unsigned char* sfo_buffer, int buf_size, char* param_name, unsigned char* var, int* var_size){
    SFOHeader *header = (SFOHeader *)sfo_buffer;
    SFODir *entries = (SFODir *)(sfo_buffer + sizeof(SFOHeader));
    bool res = false;
    int i;
    if (buf_size < (int)sizeof(SFOHeader) || header->nitems > (buf_size - sizeof(SFOHeader)) / sizeof(SFODir)
            || header->fields_table_offs >= buf_size || header->values_table_offs >= buf_size)
        return false; // not a valid PARAM.SFO
    for (i = 0; i < header->nitems; i++) {
        if (header->fields_table_offs + entries[i].field_offs >= buf_size) break;
        u32 val = header->values_table_offs + entries[i].val_offs;
        if (val >= buf_size) continue;
        if (strcmp((char*)sfo_buffer + header->fields_table_offs + entries[i].field_offs, param_name) == 0) {
            int n = (val + *var_size > buf_size)? buf_size - val : *var_size;
            memcpy(var, sfo_buffer + val, n);
            res = true;
            break;
        }
    }
    return res;
}

void Entry::animAppear(){
    for (int i=480; i>=0; i-=40){
        common::clearScreen(CLEAR_COLOR);
        SystemMgr::drawScreen();
        Image* pic1 = this->getPic1();
        if (pic1 != NULL){
            if (pic1->getWidth() == 480 && pic1->getHeight() == 272)
                pic1->draw(i, 0);
            else
                pic1->draw_scale(i, 0, 480, 272);
        }
        Image* pic0 = this->getPic0();
        if (pic0 != NULL && !has_details) pic0->draw(i+160, 85);
        this->getIcon()->draw(i+20, 92);
        common::flipScreen();
    }
}

void Entry::animDisappear(){
    for (int i=0; i<=480; i+=40){
        common::clearScreen(CLEAR_COLOR);
        common::drawScreen();
        SystemMgr::drawScreen();
        Image* pic1 = this->getPic1();
        if (pic1 != NULL){
            if (pic1->getWidth() == 480 && pic1->getHeight() == 272)
                pic1->draw(i, 0);
            else
                pic1->draw_scale(i, 0, 480, 272);
        }
        Image* pic0 = this->getPic0();
        if (pic0 != NULL && !has_details) pic0->draw(i+160, 85);
        this->getIcon()->draw(i+20, 92);
        common::flipScreen();
    }
}

static int loading_data;

int load_thread(int argc, void* argp){
    Entry* e = (Entry*)(*(void**)argp);
    e->loadAVMedia();
    loading_data = false;
    sceKernelExitDeleteThread(0);
    return 0;
}

bool Entry::pmfPrompt(){

    bool ret;
    
    SystemMgr::pauseDraw();
    
    if (!has_details) loadDetails();
    animAppear();

    loading_data = true;

    Entry* entry = this;

    int thd = sceKernelCreateThread("gamedata_thread", (SceKernelThreadEntry)&load_thread, 0x10, 0x10000, PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU, NULL);
    sceKernelStartThread(thd, sizeof(entry), &entry);

    float angle = 1.0;
    Image* img = common::getImage(IMAGE_WAITICON);
    while (loading_data){
        common::clearScreen(CLEAR_COLOR);
        entry->drawBG();
        entry->getIcon()->draw(20, 92);
        img->draw_rotate((480-img->getWidth())/2, (272-img->getHeight())/2, angle);
        angle+=0.2;
        common::flipScreen();
    }
    
    bool pmfPlayback = entry->getIcon1() != NULL || entry->getSnd() != NULL;
        
    if (pmfPlayback && !MusicPlayer::isPlaying()){
        if (sceUtilityLoadModule(PSP_MODULE_AV_PLAYER)>=0){
            PSPAVEntry ave = convertEntry(entry);
            ret = pspavPlayGamePMF(&ave, &av_callbacks, 20, 92);
            sceUtilityUnloadModule(PSP_MODULE_AV_PLAYER);
        }
    }
    else{
        Controller control;
    
        while (true){
            common::clearScreen(CLEAR_COLOR);
            entry->drawBG();
            entry->getIcon()->draw(20, 92);
            common::flipScreen();
            control.update(1);
            if (control.accept()){
                ret = true;
                break;
            }
            else if (control.decline()){
                ret = false;
                break;
            }
        }
    }
    if (!ret){
        common::playMenuSound();
        animDisappear();
    }
    SystemMgr::resumeDraw();
    return ret;
}
