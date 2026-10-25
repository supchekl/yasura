/* Photos and Videos apps */

#include <cstdio>
#include <algorithm>
#include <pspiofilemgr.h>
#include "media.h"
#include "system_mgr.h"
#include "lang.h"

#define MEDIA_COLS 6
#define MEDIA_ROWS 3
#define MEDIA_PAGE (MEDIA_COLS*MEDIA_ROWS)
#define TILE 64
#define TILE_GAP 6
#define MAX_PIXELS (1024*1024) // bigger pictures don't fit in memory once decoded
#define SLIDE_SECONDS 4

static const char* photo_dirs[] = {"ms0:/PICTURE", "ms0:/PSP/PHOTO", "ef0:/PICTURE"};
static const char* video_dirs[] = {"ms0:/VIDEO", "ms0:/MP_ROOT/100MNV01", "ms0:/MP_ROOT/101ANV01", "ef0:/VIDEO"};

static bool isPhoto(const std::string& ext){
    return ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp";
}

static bool isVideo(const std::string& ext){
    return ext == "mp4" || ext == "m4v" || ext == "avi" || ext == "mp4v";
}

static bool cmpItems(MediaItem* a, MediaItem* b){
    return strcasecmp(a->name.c_str(), b->name.c_str()) < 0;
}

static u32 be32(const u8* p){ return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]; }

/* Read the picture size from the file header so huge photos can be skipped before decoding */
static bool pictureSize(const std::string& path, int* w, int* h){
    SceUID fd = sceIoOpen(path.c_str(), PSP_O_RDONLY, 0777);
    if (fd < 0) return false;
    const int n = 32*1024;
    u8* buf = (u8*)malloc(n);
    if (!buf){ sceIoClose(fd); return false; }
    int len = sceIoRead(fd, buf, n);
    sceIoClose(fd);
    bool ok = false;
    if (len > 24 && buf[0] == 0x89 && buf[1] == 'P'){ // PNG, IHDR comes first
        *w = be32(buf+16); *h = be32(buf+20); ok = true;
    }
    else if (len > 26 && buf[0] == 'B' && buf[1] == 'M'){
        *w = buf[18]|(buf[19]<<8); *h = buf[22]|(buf[23]<<8); ok = true;
    }
    else if (len > 4 && buf[0] == 0xFF && buf[1] == 0xD8){ // JPEG, walk the markers to a SOF
        int i = 2;
        while (i+9 < len){
            if (buf[i] != 0xFF){ i++; continue; }
            u8 m = buf[i+1];
            int seg = (buf[i+2]<<8)|buf[i+3];
            if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC){
                *h = (buf[i+5]<<8)|buf[i+6];
                *w = (buf[i+7]<<8)|buf[i+8];
                ok = true;
                break;
            }
            i += 2 + seg;
        }
    }
    free(buf);
    return ok;
}

static int nextPow2(int v){
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

/* Load a picture into RAM, NULL when it can't be read or would not fit */
static Image* loadPicture(const std::string& path){
    int w = 0, h = 0;
    if (!pictureSize(path, &w, &h)) return NULL;
    if (w <= 0 || h <= 0 || nextPow2(w)*nextPow2(h) > MAX_PIXELS) return NULL;

    std::string ext = common::getExtension(path);
    Image* img = NULL;
    if (ext == "thm"){
        // video thumbnails are JPEGs with another extension
        SceUID fd = sceIoOpen(path.c_str(), PSP_O_RDONLY, 0777);
        if (fd < 0) return NULL;
        SceOff size = sceIoLseek(fd, 0, PSP_SEEK_END);
        sceIoLseek(fd, 0, PSP_SEEK_SET);
        if (size > 0 && size < 512*1024){
            void* buf = malloc(size);
            if (buf){
                sceIoRead(fd, buf, size);
                if (((u8*)buf)[0] == 0xFF && ((u8*)buf)[1] == 0xD8)
                    img = new Image(buf, (unsigned long)size, YA2D_PLACE_RAM);
                free(buf);
            }
        }
        sceIoClose(fd);
    }
    else {
        img = new Image(path, YA2D_PLACE_RAM);
    }
    if (img && (img->is_system_image || img->getTexture() == NULL)){
        delete img; // a failed load points at NOICON's texture, which the destructor leaves alone
        return NULL;
    }
    if (img){
        // the JPEG decoder leaves alpha at zero, photos are always opaque
        ya2d_texture* t = img->getTexture();
        u32* px = (u32*)t->data;
        for (int i = 0; i < t->stride/4 * t->height; i++) px[i] |= 0xFF000000;
        ya2d_flush_texture(t);
    }
    return img;
}

static void freePicture(Image* img){
    if (img) delete img;
}

MediaGallery::MediaGallery(int kind){
    this->kind = kind;
    index = 0;
    scanned = false;
    scanning = false;
    thread = -1;
    sema = sceKernelCreateSema("media_sema", 0, 1, 1, NULL);
    running = false;
    paused = true;
    viewing = false;
    view_loading = false;
    view = NULL;
    slideshow = false;
    slide_time = 0;
    view_time = 0;
    view_dir = 0;
    plate_x = plate_y = -1;
}

MediaGallery::~MediaGallery(){
    running = false;
    if (thread >= 0){
        sceKernelWaitThreadEnd(thread, NULL);
        sceKernelDeleteThread(thread);
    }
    closeViewer();
    clearItems();
    sceKernelDeleteSema(sema);
}

void MediaGallery::clearItems(){
    for (int i=0; i<(int)items.size(); i++){
        if (items[i]->thumb) delete items[i]->thumb;
        delete items[i];
    }
    items.clear();
}

void MediaGallery::scanDir(std::string dir, int depth){
    SceUID d = sceIoDopen(dir.c_str());
    if (d < 0) return;
    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(d, &ent) > 0){
        std::string name = ent.d_name;
        if (name.empty() || name[0] == '.') continue;
        std::string path = dir + "/" + name;
        if (FIO_S_ISDIR(ent.d_stat.st_mode)){
            if (depth < 1) scanDir(path, depth+1);
            continue;
        }
        std::string ext = common::getExtension(name);
        bool want = (kind == MEDIA_PHOTO)? isPhoto(ext) : isVideo(ext);
        if (!want) continue;

        MediaItem* it = new MediaItem;
        it->path = path;
        it->name = name;
        it->size = ent.d_stat.st_size;
        it->thumb = NULL;
        it->tried = false;
        char date[16];
        snprintf(date, sizeof(date), "%02d.%02d.%04d", ent.d_stat.st_mtime.day, ent.d_stat.st_mtime.month, ent.d_stat.st_mtime.year);
        it->date = date;
        if (kind == MEDIA_PHOTO){
            it->thumb_path = path;
        }
        else {
            // the PSP keeps video thumbnails next to the video as NAME.THM
            std::string base = path.substr(0, path.find_last_of('.'));
            SceIoStat st;
            if (sceIoGetstat((base + ".THM").c_str(), &st) >= 0) it->thumb_path = base + ".THM";
            else if (sceIoGetstat((base + ".thm").c_str(), &st) >= 0) it->thumb_path = base + ".thm";
        }
        items.push_back(it);
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(d);
}

void MediaGallery::scan(){
    scanning = true;
    std::vector<MediaItem*> old;
    sceKernelWaitSema(sema, 1, NULL);
    SystemMgr::pauseDraw();
    old.swap(items);
    SystemMgr::resumeDraw();

    const char** dirs = (kind == MEDIA_PHOTO)? photo_dirs : video_dirs;
    int ndirs = (kind == MEDIA_PHOTO)? sizeof(photo_dirs)/sizeof(char*) : sizeof(video_dirs)/sizeof(char*);
    std::vector<MediaItem*> found;
    items.swap(found);
    for (int i=0; i<ndirs; i++) scanDir(dirs[i], 0);
    std::sort(items.begin(), items.end(), cmpItems);
    sceKernelSignalSema(sema, 1);

    for (int i=0; i<(int)old.size(); i++){
        if (old[i]->thumb) delete old[i]->thumb;
        delete old[i];
    }
    if (index >= (int)items.size()) index = max(0, (int)items.size()-1);
    scanned = true;
    scanning = false;
}

/* Background thread: keeps thumbnails for the visible page and its neighbours */
int MediaGallery::loader(SceSize args, void* argp){
    MediaGallery* self = *(MediaGallery**)argp;
    while (self->running){
        if (self->paused || !self->scanned){
            sceKernelDelayThread(50000);
            continue;
        }
        sceKernelWaitSema(self->sema, 1, NULL);
        int n = self->items.size();
        int page = (self->index / MEDIA_PAGE) * MEDIA_PAGE;
        int lo = max(0, page - MEDIA_PAGE), hi = min(n, page + 2*MEDIA_PAGE);
        MediaItem* todo = NULL;
        // current page first, then the next one
        for (int i=page; i<hi && !todo; i++)
            if (!self->items[i]->tried) todo = self->items[i];
        for (int i=lo; i<page && !todo; i++)
            if (!self->items[i]->tried) todo = self->items[i];
        // free what scrolled far away
        for (int i=0; i<n; i++){
            if ((i < lo || i >= hi) && self->items[i]->thumb){
                Image* t = self->items[i]->thumb;
                self->items[i]->thumb = NULL;
                self->items[i]->tried = false;
                delete t;
            }
        }
        if (todo){
            todo->tried = true;
            Image* pic = (todo->thumb_path.empty())? NULL : loadPicture(todo->thumb_path);
            if (pic){
                todo->thumb = common::fitImage(pic, TILE, TILE, true);
                freePicture(pic);
            }
        }
        sceKernelSignalSema(self->sema, 1);
        sceKernelDelayThread((todo)? 0 : 50000);
    }
    sceKernelExitThread(0);
    return 0;
}

void MediaGallery::pause(){
    paused = true;
    if (viewing) closeViewer();
}

void MediaGallery::resume(){
    if (thread < 0){
        running = true;
        thread = sceKernelCreateThread("media_thread", MediaGallery::loader, 0x12, 0x10000, PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU, NULL);
        MediaGallery* self = this;
        sceKernelStartThread(thread, sizeof(self), &self);
    }
    if (!scanned || common::mediaChanged(true)) scan(); // a screenshot may have landed in PICTURE
    paused = false;
}

Image* MediaGallery::getIcon(){
    Image* img = common::getThemeImage((kind == MEDIA_PHOTO)? THEME_PHOTOS : THEME_VIDEO);
    return (img)? img : common::getImage(IMAGE_BROWSER);
}

std::string MediaGallery::getInfo(){
    return getName();
}

std::string MediaGallery::getHints(){
    if (viewing){
        if (kind == MEDIA_VIDEO) return "O:Back";
        return "LR:Browse;S:Set as wallpaper;ST:Slideshow;O:Back";
    }
    if (items.empty()) return "SE:Refresh;T:Menu";
    return "DP:Choose;X:Open;SE:Refresh;T:Menu";
}

void MediaGallery::drawInfo(){
    // title and count on the left, details of the selected item next to it
    std::string title = TR(getName());
    if (scanning){
        common::printText(8, 14, title.c_str(), WHITE, SIZE_MEDIUM, 0, NULL, 0);
        return;
    }
    char count[16];
    snprintf(count, sizeof(count), "  %d", (int)items.size());
    title += count;
    common::printText(8, 14, title.c_str(), WHITE, SIZE_MEDIUM, 0, NULL, 0);
    if (!scanning && !items.empty() && index < (int)items.size() && !viewing){
        MediaItem* it = items[index];
        std::string info = common::beautifySize(it->size) + "  ·  " + it->date;
        int x = 16 + common::calcTextWidth(title.c_str(), SIZE_MEDIUM, 0);
        common::printText(x, 14, info.c_str(), 0xFFCCCCCC, SIZE_LITTLE, 0, NULL, 0);
    }
}

void MediaGallery::draw(){
    if (viewing){
        if (kind == MEDIA_PHOTO) drawViewer();
        else drawVideoCard();
        return;
    }

    if (!scanned || scanning){
        Image* img = common::getImage(IMAGE_WAITICON);
        img->draw((480-img->getWidth())/2, (272-img->getHeight())/2);
        return;
    }

    if (items.empty()){
        std::string a = TR((kind == MEDIA_PHOTO)? "No photos yet" : "No videos yet");
        std::string b = TR((kind == MEDIA_PHOTO)? "Copy pictures to the PICTURE folder" : "Copy MP4 videos to the VIDEO folder");
        Image* icon = getIcon();
        icon->draw_scale(240-36, 60, 72, 72);
        common::printText(240 - common::calcTextWidth(a.c_str(), SIZE_BIG, 0)/2, 160, a.c_str(), WHITE, SIZE_BIG, 0, NULL, 0);
        common::printText(240 - common::calcTextWidth(b.c_str(), SIZE_LITTLE, 0)/2, 182, b.c_str(), 0xFFDDDDDD, SIZE_LITTLE, 0, NULL, 0);
        return;
    }

    int left = (480 - (MEDIA_COLS*(TILE+TILE_GAP) - TILE_GAP))/2;
    int top = 28;
    int page = (index / MEDIA_PAGE) * MEDIA_PAGE;
    Image* cc = common::getThemeImage(THEME_CC);
    for (int k=0; k<MEDIA_PAGE && page+k < (int)items.size(); k++){
        MediaItem* it = items[page+k];
        int x = left + (k % MEDIA_COLS)*(TILE+TILE_GAP);
        int y = top + (k / MEDIA_COLS)*(TILE+TILE_GAP);
        bool sel = (page+k == index);
        int s = (sel)? TILE+6 : TILE;
        int dx = x - (s-TILE)/2, dy = y - (s-TILE)/2;
        if (sel){
            // the highlight glides between tiles instead of jumping
            if (plate_x < 0){ plate_x = dx-4; plate_y = dy-4; }
            plate_x = common::approach(plate_x, dx-4);
            plate_y = common::approach(plate_y, dy-4);
            Image* plate = common::getPlate();
            if (plate) plate->draw_scale((int)plate_x, (int)plate_y, s+8, s+8);
        }
        if (it->thumb){
            common::drawSub(it->thumb, 0, 0, it->thumb->getWidth(), it->thumb->getHeight(), dx, dy, s, s);
        }
        else {
            common::getImage(IMAGE_DIALOG)->draw_scale(dx, dy, s, s);
            if (kind == MEDIA_VIDEO || it->tried){
                Image* icon = getIcon();
                icon->draw_scale(dx + s/4, dy + s/4, s/2, s/2);
            }
        }
        if (kind == MEDIA_VIDEO && cc){
            // play badge, glyph 0 of the control center strip
            int b = 18;
            ya2d_draw_rect(dx+3, dy+s-b-3, b, b, 0x80000000, 1);
            common::drawSub(cc, 0, 0, 32, 32, dx+3, dy+s-b-3, b, b);
        }
    }

    int pages = (items.size() + MEDIA_PAGE - 1) / MEDIA_PAGE;
    if (pages > 1){
        int cur = index / MEDIA_PAGE;
        int w = pages*10 - 4;
        int x = (480 - w)/2;
        for (int p=0; p<pages && p<30; p++)
            ya2d_draw_rect(x + p*10, 238, 6, 6, (p == cur)? WHITE : 0x70FFFFFF, 1);
    }
}

void MediaGallery::openViewer(int i){
    if (i < 0 || i >= (int)items.size()) return;
    index = i;
    if (!viewing) view_dir = 0;
    viewing = true;
    view_time = clock();
    if (kind == MEDIA_VIDEO) return; // the video card only needs the thumbnail
    view_loading = true;
    Image* pic = loadPicture(items[i]->path);
    Image* fit = NULL;
    if (pic){
        ya2d_texture* t = pic->getTexture();
        // small pictures are shown as they are, anything else is fitted to the screen
        if (t->width <= 480 && t->height <= 272) fit = pic;
        else {
            fit = common::fitImage(pic, 480, 272, false);
            freePicture(pic);
        }
    }
    else common::notify("This picture is too big to open", GLYPH_IMAGE);
    Image* old = view;
    SystemMgr::pauseDraw();
    view = fit;
    view_loading = false;
    SystemMgr::resumeDraw();
    if (old) freePicture(old);
    slide_time = clock();
    view_time = clock();
}

void MediaGallery::closeViewer(){
    Image* old = view;
    SystemMgr::pauseDraw();
    viewing = false;
    slideshow = false;
    view = NULL;
    SystemMgr::resumeDraw();
    if (old) freePicture(old);
}

void MediaGallery::drawViewer(){
    ya2d_draw_rect(0, 0, 480, 272, BLACK, 1);
    if (view_loading || view == NULL){
        if (view_loading){
            Image* img = common::getImage(IMAGE_WAITICON);
            img->draw((480-img->getWidth())/2, (272-img->getHeight())/2);
        }
        return;
    }
    int w = view->getWidth(), h = view->getHeight();
    // fit small pictures too, keeping the aspect ratio
    float s = min(480.f/w, 272.f/h);
    if (s > 1.f && w > 240) s = 1.f;
    // opening zooms the photo up from the grid, browsing slides it in from the side
    float e = common::easeOut(common::since(view_time, 0.28f));
    int ox = 0;
    if (view_dir == 0) s *= 0.8f + 0.2f*e;
    else ox = (int)(view_dir * 80 * (1-e));
    int dw = (int)(w*s), dh = (int)(h*s);
    common::setAlpha((int)(255*e));
    common::drawSub(view, 0, 0, w, h, (480-dw)/2 + ox, (272-dh)/2, dw, dh);
    common::setAlpha(255);
    if (slideshow){
        std::string t = TR("Slideshow");
        common::printText(472 - common::calcTextWidth(t.c_str(), SIZE_LITTLE, 0), 14, t.c_str(), WHITE, SIZE_LITTLE, 0, NULL, 0);
    }
}

void MediaGallery::drawVideoCard(){
    MediaItem* it = items[index];
    float e = common::easeOut(common::since(view_time, 0.28f));
    ya2d_draw_rect(0, 0, 480, 272, common::withAlpha(0x90000000, (int)(255*e)), 1);
    int x = 40, y = 40 + (int)(30*(1-e)), w = 400, h = 180;
    common::getImage(IMAGE_DIALOG)->draw_scale(x, y, w, h);
    int tx = x + 14, ty = y + 14;
    if (it->thumb) common::drawSub(it->thumb, 0, 0, it->thumb->getWidth(), it->thumb->getHeight(), tx, ty, 96, 96);
    else getIcon()->draw_scale(tx + 12, ty + 12, 72, 72);
    int lx = tx + 110;
    scroll.w = w - 130;
    common::printText(lx, ty + 16, it->name.c_str(), WHITE, SIZE_BIG, 0, &scroll, 0);
    common::printText(lx, ty + 40, (TR("Size") + ":  " + common::beautifySize(it->size)).c_str(), 0xFFDDDDDD, SIZE_LITTLE, 0, NULL, 0);
    common::printText(lx, ty + 56, (TR("Date") + ":  " + it->date).c_str(), 0xFFDDDDDD, SIZE_LITTLE, 0, NULL, 0);
    scroll.w = 270;
    std::string dir = it->path.substr(0, it->path.find_last_of('/')+1);
    common::printText(lx, ty + 72, dir.c_str(), 0xFFAAAAAA, SIZE_LITTLE, 0, NULL, 0);
    // the launcher has no video decoder, tell how to watch it
    const char* lines[] = {
        "Videos play in the standard PSP player (XMB).",
        "Turn off Autoboot Launcher in CFW Settings,",
        "restart the PSP and open Video in the XMB.",
    };
    for (int i=0; i<3; i++)
        common::printText(tx, ty + 122 + i*15, TR(lines[i]).c_str(), WHITE, SIZE_LITTLE, 0, NULL, 0);
}

void MediaGallery::control(Controller* pad){
    if (viewing){
        if (pad->decline()){
            common::playMenuSound();
            closeViewer();
            return;
        }
        if (kind == MEDIA_VIDEO) return;
        bool next = pad->right() || pad->RT();
        bool prev = pad->left() || pad->LT();
        if (slideshow && (clock() - slide_time) > SLIDE_SECONDS*CLOCKS_PER_SEC) next = true;
        if (next || prev){
            int n = items.size();
            int i = (index + (next? 1 : n-1)) % n;
            view_dir = (next)? 1 : -1;
            if (!slideshow) common::playMenuSound();
            openViewer(i);
        }
        else if (pad->square() && view){
            common::playMenuSound();
            common::notify(common::setPhotoWallpaper(view)? "Wallpaper set" : "Could not save the wallpaper", GLYPH_IMAGE);
        }
        else if (pad->start()){
            slideshow = !slideshow;
            slide_time = clock();
            common::playMenuSound();
        }
        return;
    }

    if (pad->select()){
        common::playMenuSound();
        scan();
        return;
    }
    if (items.empty()) return;

    int n = items.size();
    int moved = index;
    if (pad->right()) moved = min(n-1, index+1);
    else if (pad->left()) moved = max(0, index-1);
    else if (pad->down()){ if (index + MEDIA_COLS < n) moved = index + MEDIA_COLS; else moved = n-1; }
    else if (pad->up()) moved = max(0, index - MEDIA_COLS);
    else if (pad->LT()) moved = max(0, index - MEDIA_PAGE);
    else if (pad->RT()) moved = min(n-1, index + MEDIA_PAGE);
    else if (pad->accept()){
        common::playMenuSound();
        openViewer(index);
        return;
    }
    if (moved != index){
        index = moved;
        scroll.tmp = scroll.x;
        common::playMenuSound();
        sceKernelDelayThread(90000); // held buttons repeat every frame
    }
}
