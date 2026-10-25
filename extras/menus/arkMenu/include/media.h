#ifndef MEDIA_H
#define MEDIA_H

#include <vector>
#include <string>
#include "system_entry.h"
#include "common.h"

/* Photos and Videos apps: an iOS style gallery of the PICTURE and VIDEO folders */

enum {
    MEDIA_PHOTO,
    MEDIA_VIDEO,
};

typedef struct {
    std::string path;
    std::string name;
    std::string thumb_path; // file the thumbnail comes from (the photo itself, or a video's .THM)
    std::string date;
    u64 size;
    Image* thumb;
    bool tried;
} MediaItem;

class MediaGallery : public SystemEntry{

    private:
        int kind;
        std::vector<MediaItem*> items;
        int index;
        bool scanned;
        bool scanning;

        SceUID thread;
        SceUID sema;
        bool running;
        bool paused;

        /* photo viewer and video card */
        bool viewing;
        bool view_loading;
        Image* view;
        bool slideshow;
        clock_t slide_time;
        clock_t view_time; // open zoom and browse slide
        int view_dir;      // 0 opening, 1 next, -1 previous
        float plate_x, plate_y;

        void scan();
        void scanDir(std::string dir, int depth);
        void clearItems();
        void openViewer(int i);
        void closeViewer();
        void drawViewer();
        void drawVideoCard();

        static int loader(SceSize args, void* argp);

    public:
        MediaGallery(int kind);
        ~MediaGallery();

        void draw();
        void control(Controller* pad);
        void pause();
        void resume();

        std::string getInfo();
        std::string getFooter(){ return ""; }
        void setInfo(std::string info){}
        void setFooter(std::string footer){}
        void setName(std::string name){}
        std::string getName(){ return (kind == MEDIA_PHOTO)? "Photos" : "Videos"; }
        Image* getIcon();
        bool isStillLoading(){ return false; }
        std::string getHints();
        void drawInfo();
};

#endif
