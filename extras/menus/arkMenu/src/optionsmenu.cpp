#include "optionsmenu.h"
#include "controller.h"
#include "lang.h"

OptionsMenu::OptionsMenu(char* description, int n_options, t_options_entry* entries){
    this->description = description;
    this->n_options = n_options;
    this->entries = entries;
    this->index = 0;
    this->w = max(min(this->maxString()+10, 320), 170);
    this->h = 42 + 18*n_options;
    this->x = (480-w)/2;
    this->y = (272-h)/2;
    this->scroll.w = this->w - 28;
}

OptionsMenu::~OptionsMenu(){
}

int OptionsMenu::maxString(){
    string cur_max = TR(description);
    for (int i=0; i<n_options; i++){
        string tmp = TR(entries[i].name);
        if (tmp.size() > cur_max.size()){
            cur_max = tmp;
        }
    }
    return common::calcTextWidth(cur_max.c_str(), SIZE_MEDIUM)+20;
}

void OptionsMenu::draw(){
    common::getImage(IMAGE_DIALOG)->draw_scale(x, y, w, h);
    
    int xoffset = x+14;
    int yoffset = y+20;
    
    // title and a hairline under it
    common::printText(xoffset, yoffset, description, WHITE, SIZE_MEDIUM);
    ya2d_draw_rect(x+10, y+28, w-20, 1, 0x40FFFFFF, 1);
    yoffset += 12;
    
    for (int i=0; i<n_options; i++){
        yoffset+=18;
        
        if (i == index){
            common::drawSelection(x+6, yoffset-13, w-12, 18);
            common::printText(xoffset, yoffset, entries[i].name, WHITE, SIZE_MEDIUM, 0, &scroll);
        }
        else {
            string s = TR(entries[i].name);
            int tw = common::calcTextWidth(s.c_str(), SIZE_LITTLE);
            if (tw > scroll.w){
                float cw = float(tw)/s.size();
                int nchars = scroll.w / cw;
                s = (s.substr(0, nchars-3)+"...");
            }
            common::printText(xoffset, yoffset, s.c_str(), LITEGRAY);
        }
    }
}
        
int OptionsMenu::control(){
    Controller pad;
    pad.flush();
    
    bool ret = true;
    
    while (true){
        pad.update();
        if (pad.up()){
            if (index > 0){
                index--;
                common::playMenuSound();
            }
        }
        else if (pad.down()){
            if (index < n_options-1){
                index++;
                common::playMenuSound();
            }
        }
        else if (pad.accept()){
            common::playMenuSound();
            ret = true;
            break;
        }
        else if (pad.decline()){
            common::playMenuSound();
            ret = false;
            break;
        }
    }
    pad.flush();
    return (ret)? entries[index].value : OPTIONS_CANCELLED;
}
