#include "ya2d++.h"
#include <string>
#include <common.h>
#include <pspgu.h>
#include <pspgum.h>

u32 Image::tint = 0xFFFFFFFF;

Image::Image(){
    this->is_system_image = false;
    this->slice = 0;
}

Image::Image(ya2d_texture* tex){
    if (tex == NULL){
        this->texture = common::getImage(IMAGE_NOICON)->texture;
        this->is_system_image = true;
        this->slice = 0;
    }
    else {
        this->texture = tex;
        this->is_system_image = false;
    this->slice = 0;
    }
}

Image::Image(string filename, int place){
    this->is_system_image = false;
    this->slice = 0;
    string ext = common::getExtension(filename);
    if (ext == "png"){
        this->texture = ya2d_load_PNG_file_offset(filename.c_str(), place, 0);
    }
    else if (ext == "jpg" || ext == "jpeg"){
        this->texture = ya2d_load_JPEG_file(filename.c_str(), place);
    }
    else if (ext == "bmp"){
        this->texture = ya2d_load_BMP_file(filename.c_str(), place);
    }
    if (this->texture == NULL){
        this->texture = common::getImage(IMAGE_NOICON)->texture;
        this->is_system_image = true;
    }
}

Image::Image(void* buffer, int place){
    this->is_system_image = false;
    this->slice = 0;
    u32 magic = *(u32*)buffer;
    if (magic == PNG_MAGIC)
        this->texture = ya2d_load_PNG_buffer(buffer, place);
    else if (magic == BMP_MAGIC)
        this->texture = ya2d_load_BMP_buffer(buffer, place);
}

Image::Image(void* buffer, unsigned long buffer_size, int place){
    this->is_system_image = false;
    this->slice = 0;
    if ( (*(u32*)buffer & 0x0000FFFF) == JPG_MAGIC)
        this->texture = ya2d_load_JPEG_buffer(buffer, buffer_size, place);
}

Image::Image(string filename, int place, SceOff offset){
    this->is_system_image = false;
    this->slice = 0;
    this->texture = ya2d_load_PNG_file_offset(filename.c_str(), place, offset);
}

Image::~Image(){
    if (!common::isSharedImage(this))
        ya2d_free_texture(this->texture);
}

ya2d_texture* Image::getTexture(){
    return this->texture;
}

int Image::getWidth(){
    if (texture)
        return texture->width;
    return 1;
}

int Image::getHeight(){
    if (texture)
        return texture->height;
    return 1;
}

void Image::swizzle(){
    ya2d_swizzle_texture(this->texture);
    flush();
}

void Image::flush(){
    ya2d_flush_texture(this->texture);
}

void Image::draw(int x, int y){
    ya2d_draw_texture(this->texture, x, y);
}

void Image::draw_centered(int x, int y){
    ya2d_draw_texture_centered(this->texture, x, y);
}

void Image::draw_hotspot(int x, int y, int center_x, int center_y){
    ya2d_draw_texture_hotspot(this->texture, x, y, center_x, center_y);
}

void Image::draw_scale(int x, int y, float scale_x, float scale_y){
    ya2d_draw_texture_scale(this->texture, x, y, scale_x, scale_y);
}

void Image::draw_scale(int x, int y, int newWidth, int newHeight){
    if (slice > 0){
        draw_slice(x, y, newWidth, newHeight);
        return;
    }
    float scale_x = (float)newWidth/(float)getWidth();
    float scale_y = (float)newHeight/(float)getHeight();
    ya2d_draw_texture_scale(this->texture, x, y, scale_x, scale_y);
}

// Draw the texture stretched to newWidth x newHeight keeping its corners intact,
// so rounded panels don't get smeared when scaled to bars or popups.
void Image::draw_slice(int x, int y, int newWidth, int newHeight){
    if (!texture) return;
    int tw = texture->width, th = texture->height;
    int sb = slice;
    if (sb*2 > tw) sb = tw/2;
    if (sb*2 > th) sb = th/2;
    int db = sb;
    if (db*2 > newWidth) db = newWidth/2;
    if (db*2 > newHeight) db = newHeight/2;

    short su[4] = {0, (short)sb, (short)(tw-sb), (short)tw};
    short sv[4] = {0, (short)sb, (short)(th-sb), (short)th};
    short dx[4] = {(short)x, (short)(x+db), (short)(x+newWidth-db), (short)(x+newWidth)};
    short dy[4] = {(short)y, (short)(y+db), (short)(y+newHeight-db), (short)(y+newHeight)};

    ya2d_set_texture(texture);
    if (tint != 0xFFFFFFFF){
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        sceGuColor(tint);
    }
    struct ya2d_vertex_2s3s *v = (struct ya2d_vertex_2s3s*)sceGuGetMemory(18 * sizeof(struct ya2d_vertex_2s3s));
    int n = 0;
    for (int j=0; j<3; j++){
        for (int i=0; i<3; i++){
            v[n].u = su[i];   v[n].v = sv[j];   v[n].x = dx[i];   v[n].y = dy[j];   v[n].z = 0; n++;
            v[n].u = su[i+1]; v[n].v = sv[j+1]; v[n].x = dx[i+1]; v[n].y = dy[j+1]; v[n].z = 0; n++;
        }
    }
    sceGumDrawArray(GU_SPRITES, GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D, 18, 0, v);
}

void Image::draw_rotate(int x, int y, float angle){
    ya2d_draw_texture_rotate(this->texture, x+getWidth()/2, y+getHeight()/2, angle);
}

void Image::draw_rotate_hotspot(int x, int y, float angle, int center_x, int center_y){
    ya2d_draw_texture_rotate_hotspot(this->texture, x, y, angle, center_x, center_y);
}

bool Image::operator==(Image* other){
    return ((u32)this == (u32)other) || (this->texture == other->texture);
}
