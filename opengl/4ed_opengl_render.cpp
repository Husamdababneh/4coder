/*
 * Mr. 4th Dimention - Allen Webster
 *
 * 10.11.2017
 *
 * OpenGL render implementation
 *
 */

// TOP

// TODO(allen): If we don't actually need this then burn down 4ed_opengl_funcs.h
// Declare function types and function pointers
//#define GL_FUNC(N,R,P) typedef R (N##_Function) P; N##_Function *P = 0;
//#include "4ed_opengl_funcs.h"
#include "4ed_opengl_defines.h"

// OpenGL 2.1 implementation

internal GLuint
private_texture_initialize(GLint tex_width, GLint tex_height, u32 *pixels){
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, tex_width, tex_height, 0, GL_ALPHA, GL_UNSIGNED_INT, pixels);
    return(tex);
}

inline void
private_draw_bind_texture(Render_Target *t, i32 texid){
    if (t->bound_texture != texid){
        glBindTexture(GL_TEXTURE_2D, texid);
        t->bound_texture = texid;
    }
}

inline void
private_draw_set_color(Render_Target *t, u32 color){
    if (t->color != color){
        t->color = color;
        Vec4 c = unpack_color4(color);
        glColor4f(c.r, c.g, c.b, c.a);
    }
}

internal void
interpret_render_buffer(System_Functions *system, Render_Target *t){
    // HACK(allen): Probably should use a different partition that can be resized, but whatevs for now scro.
    Partition *part = &t->buffer;
    
    local_persist b32 first_opengl_call = true;
    if (first_opengl_call){
        first_opengl_call = false;
        
        char *vendor   = (char *)glGetString(GL_VENDOR);
        char *renderer = (char *)glGetString(GL_RENDERER);
        char *version  = (char *)glGetString(GL_VERSION);
        
        LOGF("GL_VENDOR: %s\n", vendor);
        LOGF("GL_RENDERER: %s\n", renderer);
        LOGF("GL_VERSION: %s\n", version);
        
        // TODO(allen): Get this up and running for dev mode again.
#if (defined(BUILD_X64) && 0) || (defined(BUILD_X86) && 0)
        // NOTE(casey): This slows down GL but puts error messages to
        // the debug console immediately whenever you do something wrong
        
        void CALL_CONVENTION gl_dbg(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char *message, const void *userParam);
        
        glDebugMessageCallback_type *glDebugMessageCallback = 
            (glDebugMessageCallback_type *)win32_load_gl_always("glDebugMessageCallback", module);
        glDebugMessageControl_type *glDebugMessageControl = 
            (glDebugMessageControl_type *)win32_load_gl_always("glDebugMessageControl", module);
        if(glDebugMessageCallback != 0 && glDebugMessageControl != 0)
        {
            glDebugMessageCallback(opengl_debug_callback, 0);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_TRUE);
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        }
#endif
        
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_SCISSOR_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    
    i32 width = t->width;
    i32 height = t->height;
    
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glScissor(0, 0, width, height);
    glClearColor(1.f, 0.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    t->bound_texture = 0;
    
    glColor4f(0.f, 0.f, 0.f, 0.f);
    t->color = 0;
    
    u8 *start = (u8*)t->buffer.base;
    u8 *end = (u8*)t->buffer.base + t->buffer.pos;
    Render_Command_Header *header = 0;
    for (u8 *p = start; p < end; p += header->size){
        header = (Render_Command_Header*)p;
        
        i32 type = header->type;
        switch (type){
            case RenCom_Rectangle:
            {
                Render_Command_Rectangle *rectangle = (Render_Command_Rectangle*)header;
                f32_Rect r = rectangle->rect;
                private_draw_set_color(t, rectangle->color);
                private_draw_bind_texture(t, 0);
                glBegin(GL_QUADS);
                {
                    glVertex2f(r.x0, r.y0);
                    glVertex2f(r.x0, r.y1);
                    glVertex2f(r.x1, r.y1);
                    glVertex2f(r.x1, r.y0);
                }
                glEnd();
            }break;
            
            case RenCom_Outline:
            {
                Render_Command_Rectangle *rectangle = (Render_Command_Rectangle*)header;
                f32_Rect r = get_inner_rect(rectangle->rect, .5f);
                private_draw_set_color(t, rectangle->color);
                private_draw_bind_texture(t, 0);
                glBegin(GL_LINE_STRIP);
                {
                    glVertex2f(r.x0, r.y0);
                    glVertex2f(r.x1, r.y0);
                    glVertex2f(r.x1, r.y1);
                    glVertex2f(r.x0, r.y1);
                    glVertex2f(r.x0, r.y0);
                }
                glEnd();
            }break;
            
            case RenCom_Glyph:
            {
                Render_Command_Glyph *glyph = (Render_Command_Glyph*)header;
                Render_Font *font = system->font.get_render_data_by_id(glyph->font_id);
                if (font == 0){
                    break;
                }
                
                // HACK(allen): Super stupid... gotta fucking cleanup the font loading fiasco system.
                Glyph_Data g = font_get_glyph(system, font, glyph->codepoint);
                if (g.tex == 0){
                    if (g.has_gpu_setup){
                        break;
                    }
                    else{
                        u32 page_number = (glyph->codepoint/ITEM_PER_FONT_PAGE);
                        Glyph_Page *page = font_get_or_make_page(system, font, page_number);
                        
                        Temp_Memory temp = begin_temp_memory(part);
                        i32 tex_width = 0;
                        i32 tex_height = 0;
                        u32 *pixels = font_load_page_pixels(part, font, page, page_number, &tex_width, &tex_height);
                        page->has_gpu_setup = true;
                        page->gpu_tex = private_texture_initialize(tex_width, tex_height, pixels);
                        end_temp_memory(temp);
                        
                        g = font_get_glyph(system, font, glyph->codepoint);
                        if (g.tex == 0){
                            break;
                        }
                    }
                }
                
                f32 x = glyph->pos.x;
                f32 y = glyph->pos.y;
                
                f32_Rect xy = {0};
                xy.x0 = x + g.bounds.xoff;
                xy.y0 = y + g.bounds.yoff;
                xy.x1 = x + g.bounds.xoff2;
                xy.y1 = y + g.bounds.yoff2;
                
                // TODO(allen): Why aren't these baked in???
                f32 unit_u = 1.f/g.tex_width;
                f32 unit_v = 1.f/g.tex_height;
                
                f32_Rect uv = {0};
                uv.x0 = g.bounds.x0*unit_u;
                uv.y0 = g.bounds.y0*unit_v;
                uv.x1 = g.bounds.x1*unit_u;
                uv.y1 = g.bounds.y1*unit_v;
                
                private_draw_set_color(t, glyph->color);
                private_draw_bind_texture(t, g.tex);
                glBegin(GL_QUADS);
                {
                    glTexCoord2f(uv.x0, uv.y1); glVertex2f(xy.x0, xy.y1);
                    glTexCoord2f(uv.x1, uv.y1); glVertex2f(xy.x1, xy.y1);
                    glTexCoord2f(uv.x1, uv.y0); glVertex2f(xy.x1, xy.y0);
                    glTexCoord2f(uv.x0, uv.y0); glVertex2f(xy.x0, xy.y0);
                }
                glEnd();
            }break;
            
            case RenCom_ChangeClip:
            {
                Render_Command_Change_Clip *clip = (Render_Command_Change_Clip*)header;
                i32_Rect box = clip->box;
                glScissor(box.x0, height - box.y1, box.x1 - box.x0, box.y1 - box.y0);
            }break;
        }
    }
    
    glFlush();
}

// BOTTOM
