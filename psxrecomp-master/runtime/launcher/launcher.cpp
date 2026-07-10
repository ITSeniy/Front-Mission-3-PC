// launcher.cpp — SDL2/OpenGL launcher. No RmlUi. No FreeType. No dependencies
// beyond SDL2 + OpenGL 3.3 core + stb_image/png + stb_truetype (single headers).

#include "launcher.h"
#include "config_loader.h"
#include "disc_identity.h"

extern "C" {
#include "memcard.h"
#include "psx_keybinds.h"
}

#include "third_party/stb_image.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"

/* Do NOT define GL_GLEXT_PROTOTYPES: on Windows/MinGW modern GL entry points
 * are not in opengl32.lib. Load them via SDL_GL_GetProcAddress (same pattern as
 * gpu_gl_renderer.c) so PSX_LAUNCHER links on MSYS2 MinGW. */
#include <SDL.h>
#include <SDL_opengl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#  include <commdlg.h>
#endif

#ifndef GL_CLAMP_TO_EDGE
#  define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE_SWIZZLE_RGBA
#  define GL_TEXTURE_SWIZZLE_RGBA 0x8E46
#endif
#ifndef GL_ARRAY_BUFFER
#  define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_DYNAMIC_DRAW
#  define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_FRAGMENT_SHADER
#  define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#  define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_TEXTURE0
#  define GL_TEXTURE0 0x84C0
#endif

namespace fs = std::filesystem;
namespace {

// ---- modern GL via SDL (MinGW-safe) ----------------------------------------
using PFN_glCreateShader            = GLuint (APIENTRY*)(GLenum);
using PFN_glShaderSource            = void   (APIENTRY*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using PFN_glCompileShader           = void   (APIENTRY*)(GLuint);
using PFN_glCreateProgram           = GLuint (APIENTRY*)(void);
using PFN_glAttachShader            = void   (APIENTRY*)(GLuint, GLuint);
using PFN_glLinkProgram             = void   (APIENTRY*)(GLuint);
using PFN_glDeleteShader            = void   (APIENTRY*)(GLuint);
using PFN_glUseProgram              = void   (APIENTRY*)(GLuint);
using PFN_glGetUniformLocation      = GLint  (APIENTRY*)(GLuint, const GLchar*);
using PFN_glUniform1i               = void   (APIENTRY*)(GLint, GLint);
using PFN_glUniform4f               = void   (APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
using PFN_glUniformMatrix4fv        = void   (APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);
using PFN_glGenBuffers              = void   (APIENTRY*)(GLsizei, GLuint*);
using PFN_glBindBuffer              = void   (APIENTRY*)(GLenum, GLuint);
using PFN_glBufferData              = void   (APIENTRY*)(GLenum, GLsizeiptr, const void*, GLenum);
using PFN_glGenVertexArrays         = void   (APIENTRY*)(GLsizei, GLuint*);
using PFN_glBindVertexArray         = void   (APIENTRY*)(GLuint);
using PFN_glEnableVertexAttribArray = void   (APIENTRY*)(GLuint);
using PFN_glVertexAttribPointer     = void   (APIENTRY*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
using PFN_glActiveTexture           = void   (APIENTRY*)(GLenum);

static PFN_glCreateShader            p_glCreateShader;
static PFN_glShaderSource            p_glShaderSource;
static PFN_glCompileShader           p_glCompileShader;
static PFN_glCreateProgram           p_glCreateProgram;
static PFN_glAttachShader            p_glAttachShader;
static PFN_glLinkProgram             p_glLinkProgram;
static PFN_glDeleteShader            p_glDeleteShader;
static PFN_glUseProgram              p_glUseProgram;
static PFN_glGetUniformLocation      p_glGetUniformLocation;
static PFN_glUniform1i               p_glUniform1i;
static PFN_glUniform4f               p_glUniform4f;
static PFN_glUniformMatrix4fv        p_glUniformMatrix4fv;
static PFN_glGenBuffers              p_glGenBuffers;
static PFN_glBindBuffer              p_glBindBuffer;
static PFN_glBufferData              p_glBufferData;
static PFN_glGenVertexArrays         p_glGenVertexArrays;
static PFN_glBindVertexArray         p_glBindVertexArray;
static PFN_glEnableVertexAttribArray p_glEnableVertexAttribArray;
static PFN_glVertexAttribPointer     p_glVertexAttribPointer;
static PFN_glActiveTexture           p_glActiveTexture;
static int                           s_gl_modern_ok = 0;

static int gl_load_modern(void) {
    if (s_gl_modern_ok) return 1;
    int ok = 1;
#define LOAD(p, n) do { p = (decltype(p))SDL_GL_GetProcAddress(n); if (!p) ok = 0; } while (0)
    LOAD(p_glCreateShader,            "glCreateShader");
    LOAD(p_glShaderSource,            "glShaderSource");
    LOAD(p_glCompileShader,           "glCompileShader");
    LOAD(p_glCreateProgram,           "glCreateProgram");
    LOAD(p_glAttachShader,            "glAttachShader");
    LOAD(p_glLinkProgram,             "glLinkProgram");
    LOAD(p_glDeleteShader,            "glDeleteShader");
    LOAD(p_glUseProgram,              "glUseProgram");
    LOAD(p_glGetUniformLocation,      "glGetUniformLocation");
    LOAD(p_glUniform1i,               "glUniform1i");
    LOAD(p_glUniform4f,               "glUniform4f");
    LOAD(p_glUniformMatrix4fv,        "glUniformMatrix4fv");
    LOAD(p_glGenBuffers,              "glGenBuffers");
    LOAD(p_glBindBuffer,              "glBindBuffer");
    LOAD(p_glBufferData,              "glBufferData");
    LOAD(p_glGenVertexArrays,         "glGenVertexArrays");
    LOAD(p_glBindVertexArray,         "glBindVertexArray");
    LOAD(p_glEnableVertexAttribArray, "glEnableVertexAttribArray");
    LOAD(p_glVertexAttribPointer,     "glVertexAttribPointer");
    LOAD(p_glActiveTexture,           "glActiveTexture");
#undef LOAD
    s_gl_modern_ok = ok;
    if (!ok)
        std::fprintf(stderr, "launcher: failed to load modern GL via SDL_GL_GetProcAddress\n");
    return ok;
}

#define glCreateShader            p_glCreateShader
#define glShaderSource            p_glShaderSource
#define glCompileShader           p_glCompileShader
#define glCreateProgram           p_glCreateProgram
#define glAttachShader            p_glAttachShader
#define glLinkProgram             p_glLinkProgram
#define glDeleteShader            p_glDeleteShader
#define glUseProgram              p_glUseProgram
#define glGetUniformLocation      p_glGetUniformLocation
#define glUniform1i               p_glUniform1i
#define glUniform4f               p_glUniform4f
#define glUniformMatrix4fv        p_glUniformMatrix4fv
#define glGenBuffers              p_glGenBuffers
#define glBindBuffer              p_glBindBuffer
#define glBufferData              p_glBufferData
#define glGenVertexArrays         p_glGenVertexArrays
#define glBindVertexArray         p_glBindVertexArray
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glVertexAttribPointer     p_glVertexAttribPointer
#define glActiveTexture           p_glActiveTexture

// ---- math helpers -----------------------------------------------------------
struct Color { float r,g,b,a; constexpr Color(float R,float G,float B,float A=1):r(R),g(G),b(B),a(A){} constexpr bool operator==(const Color& o)const{return r==o.r&&g==o.g&&b==o.b&&a==o.a;} };
struct Vec2 { float x,y; };
static inline Color operator*(Color c, float s) { return {c.r*s,c.g*s,c.b*s,c.a}; }

// ---- theme ------------------------------------------------------------------
constexpr Color
    THEME_BG          {0.043f,0.055f,0.078f,1},
    THEME_PANEL       {0.067f,0.086f,0.122f,1},
    THEME_BORDER      {0.118f,0.153f,0.200f,1},
    THEME_BORDER_HL   {0.145f,0.184f,0.251f,1},
    THEME_TEXT        {0.902f,0.914f,0.937f,1},
    THEME_TEXT_DIM    {0.490f,0.459f,0.565f,1},
    THEME_TEXT_MUTED  {0.392f,0.431f,0.498f,1},
    THEME_ACCENT      {0.267f,0.576f,0.965f,1},
    THEME_ACCENT_HL   {0.400f,0.671f,0.992f,1},
    THEME_GREEN       {0.247f,0.725f,0.314f,1},
    THEME_RED         {0.973f,0.282f,0.282f,1},
    THEME_WARN        {0.824f,0.600f,0.133f,1},
    THEME_BTN_BG      {0.106f,0.137f,0.188f,1},
    THEME_BTN_HOVER   {0.157f,0.200f,0.259f,1},
    THEME_SEG_BG      {0.086f,0.114f,0.157f,1},
    THEME_SEG_ON      {0.145f,0.388f,0.922f,1},
    THEME_DROPDOWN_BG {0.051f,0.074f,0.110f,1};

// ---- GL 2D rendering helpers ------------------------------------------------
static GLuint s_vao=0, s_vbo=0, s_prog=0, s_white=0;
static GLint s_u_proj=-1, s_u_tex=-1, s_u_col=-1;
static float s_proj[16]; // orthographic matrix, row-major

static void gl_ortho(float l, float r, float b, float t) {
    std::memset(s_proj,0,sizeof(s_proj));
    s_proj[0]=2/(r-l); s_proj[5]=2/(t-b); s_proj[10]=-1; s_proj[12]=-(r+l)/(r-l); s_proj[13]=-(t+b)/(t-b); s_proj[15]=1;
}

static void gl_ensure_init() {
    if (s_prog) return;
    if (!gl_load_modern()) return;
    const char* vs="#version 330 core\nlayout(location=0)in vec2 aP;layout(location=1)in vec2 aU;uniform mat4 uP;out vec2 vU;void main(){gl_Position=uP*vec4(aP,0,1);vU=aU;}";
    const char* fs="#version 330 core\nin vec2 vU;uniform sampler2D uT;uniform vec4 uC;out vec4 oC;void main(){oC=texture(uT,vU)*uC;}";
    auto compile=[](GLuint t,const char*s){
        GLuint sh=glCreateShader(t); glShaderSource(sh,1,&s,nullptr); glCompileShader(sh); return sh;
    };
    GLuint v=compile(GL_VERTEX_SHADER,vs), f=compile(GL_FRAGMENT_SHADER,fs);
    s_prog=glCreateProgram(); glAttachShader(s_prog,v); glAttachShader(s_prog,f);
    glLinkProgram(s_prog); glDeleteShader(v); glDeleteShader(f);
    s_u_proj=glGetUniformLocation(s_prog,"uP"); s_u_tex=glGetUniformLocation(s_prog,"uT"); s_u_col=glGetUniformLocation(s_prog,"uC");
    glGenVertexArrays(1,&s_vao); glGenBuffers(1,&s_vbo);
    glBindVertexArray(s_vao); glBindBuffer(GL_ARRAY_BUFFER,s_vbo);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,16,(void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,16,(void*)8); glEnableVertexAttribArray(1);
    unsigned char wp[4]={255,255,255,255};
    glGenTextures(1,&s_white); glBindTexture(GL_TEXTURE_2D,s_white);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,wp);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
}

static void gl_draw_quad(float x,float y,float w,float h, GLuint tex, const Color& c,
                         float u0=0,float v0=0,float u1=1,float v1=1) {
    gl_ensure_init();
    if (!s_prog || !s_gl_modern_ok) return;
    struct V{float x,y,u,v;} verts[4]={{x,y+h,u0,v1},{x,y,u0,v0},{x+w,y+h,u1,v1},{x+w,y,u1,v0}};
    glUseProgram(s_prog);
    glUniformMatrix4fv(s_u_proj,1,GL_FALSE,s_proj);
    glUniform1i(s_u_tex,0); glUniform4f(s_u_col,c.r,c.g,c.b,c.a);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,tex);
    glBindBuffer(GL_ARRAY_BUFFER,s_vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_DYNAMIC_DRAW);
    glBindVertexArray(s_vao);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
}

static void gl_rect(float x,float y,float w,float h, const Color& c) { gl_draw_quad(x,y,w,h,s_white,c); }

// ---- stb_truetype font system -----------------------------------------------
struct Font {
    GLuint tex=0; int tw=512, th=512;
    stbtt_bakedchar cdata[96]{}; // ASCII 32..127
    float baseline=0;

    bool build(const unsigned char* ttf, float px) {
        tex=0; tw=th=512;
        auto* buf=(unsigned char*)std::calloc(tw*th,1);
        if (!buf) return false;
        int r=stbtt_BakeFontBitmap(ttf,0,px,buf,tw,th,32,96,cdata);
        if (r<=0) { std::free(buf); return false; }
        baseline=px*0.8f; // approximate ascender
        glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RED,tw,th,0,GL_RED,GL_UNSIGNED_BYTE,buf);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        GLint swz[]={GL_ONE,GL_ONE,GL_ONE,GL_RED};
        glTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_RGBA,swz);
        std::free(buf);
        return true;
    }

    void text(float x, float y, const char* s, const Color& c, float scale=1) const {
        if (!tex) return;
        while (*s) {
            if (*s>=32 && *s<128) {
                stbtt_aligned_quad q;
                float xx=x, yy=y;
                stbtt_GetBakedQuad(cdata,tw,th,*s-32,&xx,&yy,&q,1);
                float sx=q.x1-q.x0, sy=q.y1-q.y0;
                gl_draw_quad(x+(q.x0-x)*scale, y+(q.y0-y)*scale, sx*scale, sy*scale, tex, c, q.s0, q.t0, q.s1, q.t1);
                x+=(xx-x)*scale;
            } else if (*s=='\n') { x=0; y+=baseline*1.3f; }
            else { x+=8*scale; }
            s++;
        }
    }

    float width(const char* s, float scale=1) const {
        float x=0, y=0;
        while (*s) {
            if (*s>=32 && *s<128) { float xx=x,yy=y; stbtt_aligned_quad q; stbtt_GetBakedQuad(cdata,tw,th,*s-32,&xx,&yy,&q,1); x+=(xx-x)*scale; }
            else if (*s!='\n') x+=8*scale;
            s++;
        }
        return x;
    }
};

// ---- PNG texture loader -----------------------------------------------------
static GLuint load_png(const char* path, int* ow=nullptr, int* oh=nullptr) {
    int w=0,h=0,n=0; unsigned char* px=stbi_load(path,&w,&h,&n,4);
    if (!px) return 0;
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,px);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    stbi_image_free(px);
    if (ow) *ow=w; if (oh) *oh=h;
    return t;
}

// ---- UI state + interaction -------------------------------------------------
struct UI {
    int win_w=1280, win_h=960;
    float mx=0, my=0;
    bool mouse_down=false, mouse_pressed=false, mouse_released=false;
    bool keys[SDL_NUM_SCANCODES]{};
    int hot_id=0, active_id=0, next_id=1, last_hot=0;
    Font font;
    // preloaded images
    struct Img{GLuint t=0;int w=0,h=0;};
    Img logo, disc, pad_digital, pad_analog, memcard_img;
    Img check_on, check_off, caret;
    Img verdict_ok, verdict_warn, verdict_bad, verdict_none;
    // running text-input buffer
    char text_buf[256]={};

    float scale=1, scroll_y=0;
    float sz(float v) const { return v * scale; }
    float ts(float v) const { return v * (20.f/48.f) * scale; } // font-bake-adjusted text scale

    void rect(float x,float y,float w,float h,const Color& c){gl_rect(x,y,w,h,c);}
    void begin_frame(int w, int h) {
        win_w=w; win_h=h;
        scale = std::min((float)w/800.f, (float)h/600.f);
        if (scale < 0.4f) scale = 0.4f;
        if (scale > 3.0f) scale = 3.0f;
        last_hot=hot_id; hot_id=0; next_id=1;
        mouse_pressed=false; mouse_released=false;
        gl_ortho(0,(float)w,(float)h,0); // y-down
        gl_ensure_init();
    }

    int alloc_id() { return next_id++; }

    bool hot(int id, float x, float y, float w, float h) {
        if (mx>=x && mx<=x+w && my>=y && my<=y+h) { hot_id=id; return true; }
        return false;
    }

    bool click(int id) { return hot_id==id && mouse_released && active_id==id; }

    bool button(int id, float x, float y, float w, float h) {
        bool over=hot(id,x,y,w,h);
        bool was_active=(active_id==id);
        if (mouse_pressed && over) active_id=id;
        if (mouse_released && was_active) { active_id=0; return over; }
        if (mouse_released && active_id==id) active_id=0;
        Color bg=THEME_BTN_BG;
        if (was_active) bg=bg*1.3f;
        else if (over) bg=THEME_BTN_HOVER;
        gl_rect(x,y,w,h,bg);
        return false;
    }

    bool toggle(int id, float x, float y, float w, float h, bool on) {
        bool over=hot(id,x,y,w,h);
        if (mouse_pressed && over) active_id=id;
        bool clk= mouse_released && active_id==id && over;
        if (mouse_released && active_id==id) active_id=0;
        Color bg=on ? THEME_ACCENT : THEME_BORDER;
        if (over && !active_id) bg= bg==THEME_ACCENT ? THEME_ACCENT_HL : THEME_BORDER_HL;
        if (active_id==id) bg=on ? THEME_ACCENT_HL : THEME_BORDER_HL;
        gl_rect(x,y,w,h,bg);
        float knob_x=on ? x+w-h : x;
        gl_rect(knob_x+2,y+2,h-4,h-4,{0.902f,0.914f,0.937f,1});
        return clk;
    }

    int segmented(int base, float x, float y, float w, float h, int cnt, int sel) {
        int out=sel; float sw=w/cnt;
        for (int i=0;i<cnt;i++) {
            int id=base+i;
            float sx=x+sw*i;
            bool over=hot(id,sx,y,sw,h);
            if (mouse_pressed && over) active_id=id;
            bool clk=mouse_released && active_id==id && over;
            if (mouse_released && active_id==id) active_id=0;
            Color c=(i==sel)?THEME_SEG_ON:(over?THEME_BTN_HOVER:THEME_SEG_BG);
            gl_rect(sx,y,sw,h,c);
            if (clk) out=i;
            if (i<cnt-1) gl_rect(sx+sw-1,y,1,h,THEME_BORDER);
        }
        return out;
    }
};

// ---- font + image loading helper --------------------------------------------
static bool load_assets(UI& ui, const fs::path& assets_dir) {
    // fonts
    auto load_font=[&](const char* rel, float px)->bool{
        auto p=assets_dir/rel; std::error_code ec;
        if (!fs::exists(p,ec)) return false;
        FILE*f=std::fopen(p.generic_string().c_str(),"rb"); if(!f) return false;
        std::fseek(f,0,SEEK_END); long sz=std::ftell(f); std::fseek(f,0,SEEK_SET);
        auto* buf=(unsigned char*)std::malloc(sz);
        if (!buf) { std::fclose(f); return false; }
        std::fread(buf,1,sz,f); std::fclose(f);
        bool ok=ui.font.build(buf,px);
        std::free(buf);
        return ok;
    };
    if (!load_font("fonts/LatoLatin-Regular.ttf",48)) {
        if (!load_font("LatoLatin-Regular.ttf",48)) {
            std::fprintf(stderr,"launcher: no font found\n");
            return false;
        }
    }
    // images
    auto load_img=[&](UI::Img& img, const char* rel) {
        auto p=(assets_dir/rel).generic_string();
        img.t=load_png(p.c_str(),&img.w,&img.h);
    };
    load_img(ui.logo,         "img/logo.png");
    load_img(ui.disc,         "img/disc.png");
    load_img(ui.pad_digital,  "img/pad_digital.png");
    load_img(ui.pad_analog,   "img/pad_analog.png");
    load_img(ui.memcard_img,  "img/memcard.png");
    load_img(ui.check_on,     "img/check_on.png");
    load_img(ui.check_off,    "img/check_off.png");
    load_img(ui.caret,        "img/caret.png");
    load_img(ui.verdict_ok,   "img/verdict_ok.png");
    load_img(ui.verdict_warn, "img/verdict_warn.png");
    load_img(ui.verdict_bad,  "img/verdict_bad.png");
    load_img(ui.verdict_none, "img/verdict_none.png");
    return true;
}

// ---- business logic (ported verbatim from the original) ---------------------
struct LauncherModel {
    int  renderer=0, supersampling=1;
    bool antialiasing=true, auto_skip_fmv=false, turbo_loads=true, fullscreen=false;
    bool spu_hq=false, widescreen=false, ws_eligible=true, skip_launcher=false, show_skip_modal=false;
    int  texture_filter=0, crt=0, aspect_index=0, window_width=1280;
    int  p1_dev_index=1, p2_dev_index=0, p1_mode=0, p2_mode=0, deadzone_pct=37;
    bool allow_hybrid=true, mode_selectable=true, lock_device=false, ws_offered=true, lang_menu=false;
    int  lang_index=0, cfg_player=0;
    bool mc1_enabled=true, mc2_enabled=true, launch_requested=false, quit_requested=false;
    std::string bios_path, disc_path, view="dashboard";
    std::string p1_dev_label="Keyboard", p2_dev_label="None", p1_status, p2_status, p1_dot, p2_dot;
    std::string p1_options, p2_options, dd_open, lang_label;
    std::string mc1_path, mc2_path, mc1_name, mc2_name, mc1_size, mc2_size, mc1_used, mc2_used, mc1_foot, mc2_foot;
    std::string mc1_grid, mc2_grid;
    std::string disc_file, disc_region, disc_serial, verdict_title, verdict_detail, verdict_state="none";
    bool v_header=false, v_crc=false, v_verified=false;

    // display labels (computed from the values above)
    float uiscale = 1.0f;
    std::string renderer_label, crt_label, texfilter_label, aspect_label, winsize_label, uiscale_label;

    // keybind rebinding
    int scan_kind=0, scan_index=0;
    std::string scan_chip_id;
    bool rebuild_pending=false;
};

static const char* renderer_name(int v) { return v?"OpenGL":"Software"; }
static const char* texfilter_name(int v) { return v?"Bilinear":"Nearest"; }
static const char* crt_name(int v) {
    switch(v){case 1:return "CRT";case 2:return "Composite";case 3:return "Trinitron";default:return "Raw (off)";}
}
static const int kAspects[][2]={{4,3},{16,9},{21,9}};
static const int kNumAspects=3;
static const char* aspect_name(int i) {
    switch(i){case 1:return "16:9 (Widescreen)";case 2:return "21:9 (Ultrawide)";default:return "4:3 (Native)";}
}
static int aspect_index_for(int num,int den){for(int i=0;i<kNumAspects;i++)if(kAspects[i][0]==num&&kAspects[i][1]==den)return i;return 0;}
static const int kWinWidths[]={960,1280,1600,1920};
static const int kNumWinWidths=4;
static int winsize_index(int w){int b=1,bd=1<<30;for(int i=0;i<kNumWinWidths;i++){int d=w>kWinWidths[i]?w-kWinWidths[i]:kWinWidths[i]-w;if(d<bd){bd=d;b=i;}}return b;}
static std::string winsize_label_for(int w,int ai){return std::to_string(w)+" \xC3\x97 "+std::to_string(w*kAspects[ai][1]/kAspects[ai][0]);}
static void refresh_labels(LauncherModel& m){
    m.renderer_label=renderer_name(m.renderer); m.crt_label=crt_name(m.crt);
    m.texfilter_label=texfilter_name(m.texture_filter);
    if (!m.ws_offered && m.aspect_index != 0) m.aspect_index = 0;
    m.aspect_label=aspect_name(m.aspect_index); m.winsize_label=winsize_label_for(m.window_width,m.aspect_index); m.widescreen=(m.aspect_index==1); m.ws_eligible=m.ws_offered;
}
static std::string region_long(const std::string& r){
    if(r=="NTSC-U")return "NTSC-U (USA)"; if(r=="NTSC-J")return "NTSC-J (Japan)"; if(r=="PAL")return "PAL (Europe)"; return r;
}

// memcard helpers
static std::string fmt_mtime(long long secs){
    if(secs<=0)return{}; std::time_t t=(std::time_t)secs; std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv,&t);
#else
    localtime_r(&t,&tmv);
#endif
    char buf[32]; if(std::strftime(buf,sizeof(buf),"%b %d, %Y",&tmv)==0)return{}; return buf;
}
static std::string memcard_slot_path(const PSXRecompV4::UserSettings& io, int slot){
    const bool has=slot==0?io.has_memcard1_path:io.has_memcard2_path;
    const fs::path& p=slot==0?io.memcard1_path:io.memcard2_path;
    if(has&&!p.empty())return p.generic_string();
    fs::path dir=io.has_memcard_dir?io.memcard_dir:fs::path();
    if(dir.empty())return{}; return (dir/(std::string("card")+(slot==0?"1":"2")+".mcd")).generic_string();
}
static void refresh_memcard(LauncherModel& m, int slot,
                            const PSXRecompV4::UserSettings& io){
    std::string& path =slot==0?m.mc1_path :m.mc2_path;
    std::string& name =slot==0?m.mc1_name :m.mc2_name;
    std::string& size =slot==0?m.mc1_size :m.mc2_size;
    std::string& used =slot==0?m.mc1_used :m.mc2_used;
    std::string& foot =slot==0?m.mc1_foot :m.mc2_foot;
    std::string& grid =slot==0?m.mc1_grid :m.mc2_grid;
    auto build_grid=[](const uint8_t used[15]){std::string h;for(int i=0;i<15;i++)h+=used[i]?"X":".";return h;};
    const uint8_t empty15[15]={0}; grid=build_grid(empty15);
    name=path.empty()?"(no card)":fs::path(path).filename().generic_string();
    if(path.empty()){size=used="\xE2\x80\x94"; foot="No card configured."; return;}
    MemcardSummary s; memcard_summary_path(path.c_str(),&s); grid=build_grid(s.block_used);
    if(!s.exists){size="128 KB (15 blocks)"; used="0 / 15"; foot="New blank card \xE2\x80\x94 created on launch."; return;}
    if(!s.valid){size=used="\xE2\x80\x94"; foot="Not a valid memory-card image."; return;}
    size="128 KB (15 blocks)"; used=std::to_string(s.used_blocks)+" / 15";
    auto when=fmt_mtime(s.mtime); foot=when.empty()?"On-disk memory card.":"Last modified \xE2\x80\x94 "+when;
}

// device enumeration
struct DeviceOption{int kind=0;std::string guid,label;};
static std::vector<DeviceOption> enumerate_devices(){
    std::vector<DeviceOption> opts; opts.push_back({0,"","None"}); opts.push_back({1,"","Keyboard"});
    int n=SDL_NumJoysticks(); for(int i=0;i<n;i++){
        if(!SDL_IsGameController(i))continue;
        SDL_JoystickGUID g=SDL_JoystickGetDeviceGUID(i); char buf[40]={0}; SDL_JoystickGetGUIDString(g,buf,sizeof(buf));
        opts.push_back({2,buf,SDL_GameControllerNameForIndex(i)?:std::string("Controller")});
    } return opts;
}
static std::string device_string(const DeviceOption& o){return o.kind==0?"none":o.kind==1?"keyboard":o.guid;}
static int find_or_add_device_index(std::vector<DeviceOption>& opts, const std::string& dev){
    if(dev.empty()||dev=="none")return 0; if(dev=="keyboard")return 1;
    for(size_t i=0;i<opts.size();i++)if(opts[i].kind==2&&opts[i].guid==dev)return(int)i;
    opts.push_back({2,dev,"Saved controller (offline)"}); return(int)opts.size()-1;
}
static void refresh_player(LauncherModel& m, int player, const std::vector<DeviceOption>& opts){
    int& idx=player==0?m.p1_dev_index:m.p2_dev_index;
    std::string& label=player==0?m.p1_dev_label:m.p2_dev_label;
    std::string& status=player==0?m.p1_status:m.p2_status;
    std::string& dot=player==0?m.p1_dot:m.p2_dot;
    int mode=player==0?m.p1_mode:m.p2_mode;
    if(idx<0||idx>=(int)opts.size())idx=0;
    label=opts[idx].label;
    const char* type=mode==1?"DualShock (analog)":mode==2?"digital pad":"hybrid (auto analog/d-pad)";
    if(opts[idx].kind==0){status="No device \xE2\x80\x94 port empty"; dot="off";}
    else if(opts[idx].kind==1){status=std::string("Keyboard \xE2\x80\x94 ")+type; dot="";}
    else{status=opts[idx].label+" \xE2\x80\x94 "+type; dot="";}
}
static void refresh_language(LauncherModel& m, const std::vector<psx_launcher::GameInfo::Language>& langs){
    if(langs.empty())return; if(m.lang_index<0||m.lang_index>=(int)langs.size())m.lang_index=0; m.lang_label=langs[m.lang_index].label;
}
static int lang_index_for(const std::vector<psx_launcher::GameInfo::Language>& langs, const std::string& code){
    for(size_t i=0;i<langs.size();i++)if(langs[i].code==code)return(int)i; return 0;
}

// disc verification
static void refresh_disc_status(LauncherModel& m, const std::string& game_name,
                                 const std::string& expected_serial, uint32_t expected_crc, bool has_expected_crc){
    m.v_header=m.v_crc=m.v_verified=false; m.disc_region=m.disc_serial="\xE2\x80\x94"; m.disc_file="\xE2\x80\x94";
    if(m.disc_path.empty()){m.verdict_title="No disc selected"; m.verdict_detail="Choose a disc image to verify it against this build."; m.verdict_state="none"; return;}
    m.disc_file=fs::path(m.disc_path).filename().generic_string();
    auto id=PSXRecompV4::identify_disc(fs::path(m.disc_path),expected_serial,expected_crc,has_expected_crc,has_expected_crc);
    if(!id.opened){m.verdict_title="Disc not found"; m.verdict_detail="Could not open the image or its CUE-referenced BIN."; m.verdict_state="bad"; return;}
    if(!id.has_header){m.verdict_title="Not a PlayStation disc"; m.verdict_detail="No ISO9660 header at the expected sectors."; m.verdict_state="bad"; return;}
    m.v_header=true;
    if(!id.detected_serial.empty())m.disc_serial=id.detected_serial; else if(!expected_serial.empty())m.disc_serial=expected_serial;
    if(!id.region.empty())m.disc_region=region_long(id.region);
    bool serial_ok=!id.expected_serial_given||id.serial_matches;
    if(id.expected_crc_given&&id.crc_computed) m.v_crc=id.crc_matches; else m.v_crc=serial_ok&&id.expected_serial_given;
    m.v_verified=m.v_header&&serial_ok&&(!id.expected_crc_given||id.crc_matches);
    const std::string nm=game_name.empty()?"Disc":game_name;
    if(m.v_verified){m.verdict_title=nm+" disc verified"; m.verdict_detail="Correct disc image loaded. Ready to launch."; m.verdict_state="ok";}
    else if(!serial_ok){m.verdict_title="Wrong disc?"; m.verdict_detail="Serial does not match this build (expected "+expected_serial+")."; m.verdict_state="bad";}
    else if(id.expected_crc_given&&id.crc_computed&&!id.crc_matches){m.verdict_title="Disc image differs"; m.verdict_detail="Right game, but the image hash does not match the expected dump."; m.verdict_state="warn";}
    else{m.verdict_title="PlayStation disc"; m.verdict_detail="Recognised PlayStation disc. No reference hash configured."; m.verdict_state="ok"; m.v_verified=true;}
}

// ---- file picker (unchanged) ------------------------------------------------
#if defined(_WIN32)
static std::string win_pick_file(SDL_Window*, const char* title, const char* filter){
    char buf[MAX_PATH]={0}; OPENFILENAMEA ofn={0}; ofn.lStructSize=sizeof(ofn); ofn.lpstrFilter=filter; ofn.lpstrFile=buf; ofn.nMaxFile=sizeof(buf); ofn.lpstrTitle=title; ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn)?std::string(buf):std::string();
}
static std::string win_pick_save_file(SDL_Window*, const char* title, const char* filter, const char* def_ext, const std::string& init){
    char buf[MAX_PATH]={0}; if(!init.empty()){std::snprintf(buf,sizeof(buf),"%s",init.c_str());for(char*p=buf;*p;++p)if(*p=='/')*p='\\';}
    OPENFILENAMEA ofn={0}; ofn.lStructSize=sizeof(ofn); ofn.lpstrFilter=filter; ofn.lpstrFile=buf; ofn.nMaxFile=sizeof(buf); ofn.lpstrTitle=title; ofn.lpstrDefExt=def_ext; ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    return GetSaveFileNameA(&ofn)?std::string(buf):std::string();
}
#else
#include <cstdio>
static std::string sh_squote(const std::string& s){std::string q="'";for(char c:s){if(c=='\'')q+="'\\''";else q+=c;}return q+"'";}
static std::string run_chooser(const std::string& cmd){
    std::string o; FILE* p=popen(cmd.c_str(),"r"); if(!p)return o; char buf[2048]; if(fgets(buf,sizeof(buf),p))o=buf; int rc=pclose(p);
    while(!o.empty()&&(o.back()=='\n'||o.back()=='\r'))o.pop_back(); if(rc!=0)o.clear(); return o;
}
static std::string win_pick_file(SDL_Window*, const char* title, const char*){
    std::string t=sh_squote(title?title:"Select file"); std::string r;
    if(!(r=run_chooser("command -v zenity >/dev/null 2>&1 && zenity --file-selection --title="+t+" 2>/dev/null")).empty())return r;
    if(!(r=run_chooser("command -v kdialog >/dev/null 2>&1 && kdialog --getopenfilename \"${HOME:-/}\" 2>/dev/null")).empty())return r;
    if(!(r=run_chooser("command -v qarma >/dev/null 2>&1 && qarma --file-selection --title="+t+" 2>/dev/null")).empty())return r;
    return run_chooser("command -v osascript >/dev/null 2>&1 && osascript -e 'POSIX path of (choose file)' 2>/dev/null");
}
static std::string win_pick_save_file(SDL_Window*, const char* title, const char*, const char*, const std::string& init){
    std::string t=sh_squote(title?title:"Save file"); std::string r;
    std::string z="command -v zenity >/dev/null 2>&1 && zenity --file-selection --save --confirm-overwrite --title="+t;
    if(!init.empty())z+=" --filename="+sh_squote(init);
    if(!(r=run_chooser(z+" 2>/dev/null")).empty())return r;
    std::string k="command -v kdialog >/dev/null 2>&1 && kdialog --getsavefilename ";
    k+=init.empty()?std::string("\"${HOME:-/}\""):sh_squote(init);
    return run_chooser(k+" 2>/dev/null");
}
#endif

// ---- view renderers ---------------------------------------------------------

static float render_dashboard(UI& ui, LauncherModel& m,
                              const std::vector<DeviceOption>& dev_opts,
                              SDL_Window* window,
                              const std::string& game_name,
                              const std::string& expected_serial,
                              uint32_t expected_crc, bool has_expected_crc,
                              const std::vector<psx_launcher::GameInfo::Language>& langs)
{
    const float s = ui.scale;
    const float W=(float)ui.win_w, H=(float)ui.win_h;
    const float pad=ui.sz(22), gap=ui.sz(13);
    float y=pad+ui.sz(46)+gap; // skip top bar (drawn by main loop)
    float mgn=ui.sz(18);

    // account for scroll
    y += ui.scroll_y;

    // ---- disc verification panel ----
    float disc_h=ui.sz(220), dp_y=y, dp_x=pad, dp_w=W-2*pad;
    ui.rect(dp_x,dp_y,dp_w,disc_h,THEME_PANEL);
    ui.rect(dp_x,dp_y,dp_w,1,THEME_BORDER);
    float art_s=ui.sz(180);
    if (ui.disc.t) gl_draw_quad(dp_x+mgn,dp_y+(disc_h-art_s)/2,art_s,art_s,ui.disc.t,{1,1,1,1});
    float di_x=dp_x+mgn+art_s+ui.sz(20);
    ui.font.text(di_x,dp_y+ui.sz(16),"DISC VERIFICATION",THEME_ACCENT,ui.ts(0.6f));
    float title_sz=ui.ts(1.0f);
    ui.font.text(di_x,dp_y+ui.sz(36),game_name.c_str(),THEME_TEXT,title_sz);
    ui.font.text(di_x,dp_y+ui.sz(36)+ui.font.baseline*title_sz+ui.sz(2),"PlayStation disc image",THEME_TEXT_DIM,ui.ts(0.7f));
    // metadata
    float meta_y=dp_y+ui.sz(36)+ui.font.baseline*title_sz+ui.sz(2)+ui.sz(16);
    auto meta=[&](const char* k, const std::string& v, float& cx){
        ui.font.text(cx,meta_y,k,THEME_TEXT_MUTED,ui.ts(0.55f));
        ui.font.text(cx,meta_y+ui.sz(12),v.c_str(),THEME_TEXT,ui.ts(0.7f));
        cx+=std::max(ui.font.width(k,ui.ts(0.55f)),ui.font.width(v.c_str(),ui.ts(0.7f)))+ui.sz(40);
    };
    float cx=di_x; meta("FILE",m.disc_file,cx); meta("REGION",m.disc_region,cx); meta("SERIAL",m.disc_serial,cx);
    // verification checks
    float chk_y=meta_y+ui.sz(12)+ui.sz(18);
    ui.font.text(di_x,chk_y,"VERIFICATION RESULTS",THEME_ACCENT,ui.ts(0.55f));
    auto check=[&](const char* lbl, bool on, float& cx2){
        auto& img=on?ui.check_on:ui.check_off;
        float chk_sz=ui.sz(16);
        if (img.t) gl_draw_quad(cx2,chk_y+ui.sz(14),chk_sz,chk_sz,img.t,{1,1,1,1});
        ui.font.text(cx2+ui.sz(20),chk_y+ui.sz(14),lbl,THEME_TEXT_DIM,ui.ts(0.6f));
        cx2+=ui.sz(20)+ui.font.width(lbl,ui.ts(0.6f))+ui.sz(24);
    };
    float chk_x=di_x; check("Header Match",m.v_header,chk_x); check("CRC / Hash Match",m.v_crc,chk_x); check("Disc Verified",m.v_verified,chk_x);
    // verdict column
    float vc_w=ui.sz(260);
    float vc_x=dp_x+dp_w-vc_w-mgn;
    ui.rect(vc_x,dp_y+ui.sz(12),1,disc_h-ui.sz(24),THEME_BORDER);
    auto vimg=[&]()->UI::Img{
        if (m.verdict_state=="ok") return ui.verdict_ok;
        if (m.verdict_state=="warn") return ui.verdict_warn;
        if (m.verdict_state=="bad") return ui.verdict_bad;
        return ui.verdict_none;
    }();
    float vimg_sz=ui.sz(50);
    if (vimg.t) gl_draw_quad(vc_x+ui.sz(20),dp_y+(disc_h-vimg_sz)/2,vimg_sz,vimg_sz,vimg.t,{1,1,1,1});
    ui.font.text(vc_x+vimg_sz+ui.sz(30),dp_y+disc_h/2-ui.sz(18),m.verdict_title.c_str(),THEME_TEXT,ui.ts(0.8f));
    ui.font.text(vc_x+vimg_sz+ui.sz(30),dp_y+disc_h/2+ui.sz(2),m.verdict_detail.c_str(),THEME_TEXT_MUTED,ui.ts(0.55f));
    // change ISO button
    float iso_bw=ui.sz(100), iso_bh=ui.sz(30);
    float iso_x=vc_x-iso_bw-mgn+vc_w;
    if (ui.button(ui.alloc_id(),iso_x,dp_y+ui.sz(12),iso_bw,iso_bh)) {
        std::string p=win_pick_file(window,"Select disc image","Disc image (*.cue;*.bin;*.iso)\0*.cue;*.bin;*.iso\0All files (*.*)\0*.*\0\0");
        if (!p.empty()){ m.disc_path=fs::path(p).generic_string(); refresh_disc_status(m,game_name,expected_serial,expected_crc,has_expected_crc); }
    } float lbw=ui.font.width("Change ISO",ui.ts(0.6f));
    ui.font.text(iso_x+(iso_bw-lbw)/2,dp_y+ui.sz(12)+(iso_bh-ui.font.baseline*ui.ts(0.6f))/2,"Change ISO",THEME_TEXT,ui.ts(0.6f));
    y=dp_y+disc_h+gap;

    // ---- player cards (stacked vertically) ----
    float cw=W-2*pad, ch=ui.sz(190);
    for (int pl=0;pl<2;pl++){
        float px=pad, py=y + pl*(ch+gap);
        ui.rect(px,py,cw,ch,THEME_PANEL); ui.rect(px,py,cw,1,THEME_BORDER);
        ui.font.text(px+mgn,py+ui.sz(12),pl==0?"PLAYER 1":"PLAYER 2",THEME_ACCENT,ui.ts(0.55f));
        int& mode=pl==0?m.p1_mode:m.p2_mode;
        if (m.mode_selectable) {
            const char* seg_opts[]={"Hybrid","Analog","D-Pad"};
            int n=m.allow_hybrid?3:2;
            float seg_w=ui.sz(200), seg_h=ui.sz(24), seg_x=px+cw-mgn-seg_w, seg_y=py+ui.sz(10);
            float sw=seg_w/n;
            for (int i=0;i<n;i++){
                int id=ui.alloc_id(); float sx=seg_x+sw*i;
                bool over=ui.hot(id,sx,seg_y,sw,seg_h);
                if (ui.mouse_pressed&&over) ui.active_id=id;
                bool clk=ui.mouse_released&&ui.active_id==id&&over;
                if (ui.mouse_released&&ui.active_id==id) ui.active_id=0;
                Color bgc=(i==mode)?THEME_SEG_ON:(over?THEME_BTN_HOVER:THEME_SEG_BG);
                ui.rect(sx,seg_y,sw,seg_h,bgc);
                float tw2=ui.font.width(seg_opts[i],ui.ts(0.5f));
                ui.font.text(sx+(sw-tw2)/2,seg_y+(seg_h-ui.font.baseline*ui.ts(0.5f))/2,seg_opts[i],THEME_TEXT,ui.ts(0.5f));
                if (clk) mode=i;
                if (i<n-1) ui.rect(sx+sw-1,seg_y,1,seg_h,THEME_BORDER);
            }
        }
        auto& art=mode==2?ui.pad_digital:ui.pad_analog;
        if (art.t){
            float as=ch-ui.sz(40), ar=(float)art.h/(float)art.w;
            gl_draw_quad(px+mgn,py+ui.sz(40)+(ch-ui.sz(40)-as*ar)/2,as*ar,as,art.t,{1,1,1,1});
        }
        float ddx=px+mgn+(ui.pad_analog.t?ui.sz(140):ui.sz(20)), ddy=py+ui.sz(40), ddw=cw-ddx-mgn, ddh=ui.sz(30);
        int& dev_idx=pl==0?m.p1_dev_index:m.p2_dev_index;
        std::string& dev_label=pl==0?m.p1_dev_label:m.p2_dev_label;
        std::string& status=pl==0?m.p1_status:m.p2_status;
        std::string& dot=pl==0?m.p1_dot:m.p2_dot;
        std::string& dd_open_key=pl==0?m.dd_open:m.dd_open;
        const char* ddk=pl==0?"p1":"p2";
        bool dd_hot=false;
        ui.font.text(ddx,ddy-ui.sz(16),"Device",THEME_TEXT_MUTED,ui.ts(0.5f));
        if (!m.lock_device) {
            dd_hot=ui.hot(ui.alloc_id(),ddx,ddy,ddw,ddh);
            ui.rect(ddx,ddy,ddw,ddh,dd_hot?THEME_BTN_HOVER:THEME_SEG_BG);
            ui.font.text(ddx+ui.sz(8),ddy+(ddh-ui.font.baseline*ui.ts(0.65f))/2,dev_label.c_str(),THEME_TEXT,ui.ts(0.65f));
            float caret_sz=ui.sz(13);
            if (ui.caret.t) gl_draw_quad(ddx+ddw-ui.sz(22),ddy+(ddh-caret_sz)/2,caret_sz,caret_sz,ui.caret.t,{1,1,1,1});
            if (ui.mouse_pressed&&dd_hot) {
                if (dd_open_key==ddk) dd_open_key.clear(); else dd_open_key=ddk;
            }
        }
        if (dd_open_key==ddk) {
            float dpnl_y=ddy+ddh+ui.sz(2), dpnl_h=dev_opts.size()*ui.sz(28)+ui.sz(4);
            bool dd_panel_over = ui.mx >= ddx && ui.mx <= ddx+ddw && ui.my >= dpnl_y && ui.my <= dpnl_y+dpnl_h;
            ui.rect(ddx,dpnl_y,ddw,dpnl_h,THEME_DROPDOWN_BG);
            ui.rect(ddx,dpnl_y,ddw,1,THEME_BORDER);
            float row_h_dd=ui.sz(28);
            for (size_t i=0;i<dev_opts.size();i++){
                int id=ui.alloc_id(); float oy=dpnl_y+ui.sz(4)+i*row_h_dd;
                bool oh=ui.hot(id,ddx,oy,ddw,row_h_dd);
                if (oh) ui.rect(ddx,oy,ddw,row_h_dd,THEME_BTN_HOVER);
                ui.font.text(ddx+ui.sz(8),oy+(row_h_dd-ui.font.baseline*ui.ts(0.6f))/2,dev_opts[i].label.c_str(),THEME_TEXT,ui.ts(0.6f));
                if (ui.mouse_released&&oh){
                    dev_idx=(int)i; dd_open_key.clear();
                    refresh_player(m,pl,dev_opts);
                }
                if (ui.mouse_pressed&&oh) ui.active_id=id;
            }
            // close only on outside click (not over button, not over panel)
            if (ui.mouse_pressed && !dd_hot && !dd_panel_over) dd_open_key.clear();
        }
        float sty=ddy+ddh+ui.sz(8);
        float dot_sz=ui.sz(9);
        if (!dot.empty()) ui.rect(ddx,sty+ui.sz(4),dot_sz,dot_sz,THEME_TEXT_MUTED);
        else ui.rect(ddx,sty+ui.sz(4),dot_sz,dot_sz,THEME_GREEN);
        ui.font.text(ddx+ui.sz(16),sty,status.c_str(),THEME_TEXT_DIM,ui.ts(0.55f));
    }
    float player_end = y + 2*(ch+gap);

    // ---- memory cards (stacked vertically) ----
    float mc_y = player_end, mc_h=ui.sz(190);
    for (int mc=0;mc<2;mc++){
        float px=pad, py=mc_y + mc*(mc_h+gap);
        ui.rect(px,py,cw,mc_h,THEME_PANEL); ui.rect(px,py,cw,1,THEME_BORDER);
        bool& enabled=mc==0?m.mc1_enabled:m.mc2_enabled;
        std::string& mcname=mc==0?m.mc1_name:m.mc2_name;
        std::string& mcsize=mc==0?m.mc1_size:m.mc2_size;
        std::string& mcused=mc==0?m.mc1_used:m.mc2_used;
        std::string& mcgrid=mc==0?m.mc1_grid:m.mc2_grid;
        std::string& mcfoot=mc==0?m.mc1_foot:m.mc2_foot;
        std::string& mcpath=mc==0?m.mc1_path:m.mc2_path;
        ui.font.text(px+mgn,py+ui.sz(12),mc==0?"MEMORY CARD 1":"MEMORY CARD 2",THEME_ACCENT,ui.ts(0.55f));
        float tog_w=ui.sz(42), tog_h=ui.sz(22);
        float tog_x=px+cw-ui.sz(60), tog_y=py+ui.sz(10);
        if (ui.toggle(ui.alloc_id(),tog_x,tog_y,tog_w,tog_h,enabled)) enabled=!enabled;
        ui.font.text(tog_x-ui.sz(48),tog_y+(tog_h-ui.font.baseline*ui.ts(0.55f))/2,"Enabled",THEME_TEXT_DIM,ui.ts(0.55f));
        float mc_art_w=ui.sz(80), mc_art_h=ui.sz(90);
        if (ui.memcard_img.t) gl_draw_quad(px+mgn,py+ui.sz(38),mc_art_w,mc_art_h,ui.memcard_img.t,{1,1,1,1});
        float lx=px+ui.sz(112), ly=py+ui.sz(36);
        ui.font.text(lx,ly,mcname.c_str(),THEME_TEXT,ui.ts(0.8f));
        float btn_sz_h=ui.sz(22), btn_sz_w=ui.sz(60);
        if (ui.button(ui.alloc_id(),lx,ly+ui.sz(24),btn_sz_w,btn_sz_h)) {
            std::string p=win_pick_file(window,"Select memory-card image","Memory card (*.mcd;*.mc;*.mcr)\0*.mcd;*.mc;*.mcr\0All files (*.*)\0*.*\0\0");
            if (!p.empty()){mcpath=fs::path(p).generic_string(); }
        } ui.font.text(lx+ui.sz(10),ly+ui.sz(24)+(btn_sz_h-ui.font.baseline*ui.ts(0.55f))/2,"Browse",THEME_TEXT,ui.ts(0.55f));
        if (ui.button(ui.alloc_id(),lx+btn_sz_w+ui.sz(6),ly+ui.sz(24),btn_sz_w,btn_sz_h)) {
            std::string p=win_pick_save_file(window,"Create new memory card","Memory card (*.mcd)\0*.mcd\0All files (*.*)\0*.*\0\0","mcd",mcpath);
            if (!p.empty()&&memcard_format_file(p.c_str())==0) mcpath=fs::path(p).generic_string();
        } ui.font.text(lx+btn_sz_w+ui.sz(16),ly+ui.sz(24)+(btn_sz_h-ui.font.baseline*ui.ts(0.55f))/2,"New",THEME_TEXT,ui.ts(0.55f));
        ui.font.text(lx,ly+ui.sz(54),mcsize.c_str(),THEME_TEXT,ui.ts(0.65f));
        ui.font.text(lx,ly+ui.sz(68),mcused.c_str(),THEME_TEXT,ui.ts(0.65f));
        float gx=lx, gy=ly+ui.sz(84);
        for (size_t i=0;i<mcgrid.size();i++){
            bool filled=mcgrid[i]=='X';
            float bs=ui.sz(13), bw=ui.sz(15), bh=ui.sz(16);
            ui.rect(gx+i*bw,gy,bs,bh,filled?THEME_ACCENT:THEME_BORDER);
        }
        ui.font.text(lx,gy+ui.sz(20),mcfoot.c_str(),THEME_TEXT_MUTED,ui.ts(0.5f));
    }
    float mc_end = mc_y + 2*(mc_h+gap);

    return mc_end + gap;
}

} // namespace

namespace psx_launcher {

Result run(SDL_Window* window, void* gl_context,
           PSXRecompV4::UserSettings& io,
           const GameInfo& game, const char* assets_dir)
{
    (void)gl_context; // caller passed it in, we just render with it

    psx_keybinds_init(assets_dir);

    const std::string expected_serial = game.expected_serial ? game.expected_serial : "";
    const uint32_t    expected_crc    = game.expected_crc;
    const bool        has_expected_crc = game.has_expected_crc;
    const std::string game_name_s = game.name ? game.name : "";
    const std::string game_id_s   = game.expected_serial ? game.expected_serial : "";

    UI ui;
    const fs::path assets = assets_dir ? fs::path(assets_dir) : fs::current_path();
    if (!load_assets(ui, assets)) {
        std::fprintf(stderr, "launcher: asset loading failed\n");
        return Result::Unavailable;
    }

    // ---- seed model from io ----
    LauncherModel m;
    m.renderer       = io.renderer;
    m.supersampling  = io.supersampling;
    m.antialiasing   = io.antialiasing;
    m.texture_filter = io.texture_filter;
    m.crt            = io.screen_kind;
    m.auto_skip_fmv  = io.auto_skip_fmv;
    m.turbo_loads    = io.turbo_loads;
    m.fullscreen     = io.fullscreen;
    m.skip_launcher  = io.skip_launcher;
    m.spu_hq         = io.spu_hq;
    m.aspect_index   = io.has_aspect_ratio ? aspect_index_for(io.aspect_num, io.aspect_den) : 0;
    m.window_width   = kWinWidths[winsize_index(io.has_window_width ? io.window_width : 1280)];
    if (io.has_bios_path) m.bios_path = io.bios_path.generic_string();
    if (io.has_disc_path) m.disc_path = io.disc_path.generic_string();
    refresh_labels(m);
    refresh_disc_status(m, game_name_s, expected_serial, expected_crc, has_expected_crc);

    if (io.has_memcard1_enabled) m.mc1_enabled = io.memcard1_enabled;
    if (io.has_memcard2_enabled) m.mc2_enabled = io.memcard2_enabled;
    m.mc1_path = memcard_slot_path(io, 0);
    m.mc2_path = memcard_slot_path(io, 1);
    refresh_memcard(m, 0, io);
    refresh_memcard(m, 1, io);

    std::vector<DeviceOption> dev_opts = enumerate_devices();
    m.p1_mode = io.has_p1_mode ? io.p1_mode : 0;
    m.p2_mode = io.has_p2_mode ? io.p2_mode : 0;
    m.allow_hybrid = game.allow_hybrid;
    if (!m.allow_hybrid) {
        if (m.p1_mode == 0) m.p1_mode = 1;
        if (m.p2_mode == 0) m.p2_mode = 1;
    }
    m.mode_selectable = !game.lock_mode;
    if (game.lock_mode) { m.p1_mode = game.locked_mode; m.p2_mode = game.locked_mode; }
    m.lock_device = game.lock_device;
    m.ws_offered  = game.ws_offered;
    m.deadzone_pct = io.has_deadzone ? (io.deadzone * 100 / 32767) : 37;
    m.uiscale = 1.0f; // TODO: persist uiscale in UserSettings
    m.p1_dev_index = io.has_p1_device ? find_or_add_device_index(dev_opts, io.p1_device) : (dev_opts.size()>2?2:1);
    m.p2_dev_index = io.has_p2_device ? find_or_add_device_index(dev_opts, io.p2_device) : 0;
    refresh_player(m, 0, dev_opts);
    refresh_player(m, 1, dev_opts);

    m.lang_menu = !game.languages.empty();
    if (m.lang_menu) { m.lang_index = lang_index_for(game.languages, io.language); refresh_language(m, game.languages); }

    // GL setup happens in ui.begin_frame which calls gl_ensure_init

    // ---- main loop ----
    Result result = Result::Quit;
    bool running = true;
    int win_w = 0, win_h = 0;
    SDL_GL_GetDrawableSize(window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) { win_w = 1280; win_h = 960; }

    // For tracking mouse press/release
    bool prev_mouse = false;

    while (running) {
        SDL_GL_GetDrawableSize(window, &win_w, &win_h);
        if (win_w <= 0 || win_h <= 0) { win_w = 1280; win_h = 960; }

        ui.begin_frame(win_w, win_h);
        ui.scale *= m.uiscale;

        // ---- input ----
        { int mx,my; SDL_GetMouseState(&mx,&my); ui.mx=(float)mx; ui.my=(float)my; }
        ui.mouse_down = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        ui.mouse_pressed = ui.mouse_down && !prev_mouse;
        ui.mouse_released = !ui.mouse_down && prev_mouse;
        prev_mouse = ui.mouse_down;
        std::memset(ui.keys, 0, sizeof(ui.keys));

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: m.quit_requested = true; break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.scancode < SDL_NUM_SCANCODES) ui.keys[ev.key.keysym.scancode] = true;
                // keybind scan capture
                if (m.scan_kind) {
                    if (ev.key.keysym.sym == SDLK_ESCAPE) { m.scan_kind = 0; m.rebuild_pending = true; }
                    else {
                        SDL_Scancode sc = ev.key.keysym.scancode;
                        for (int pl = 1; pl <= 2; pl++)
                            for (int b = 0; b < psx_keybinds_button_count(); b++)
                                if (psx_keybinds_get_button(pl, b) == sc &&
                                    !(pl == m.cfg_player + 1 && b == m.scan_index))
                                    psx_keybinds_set_button(pl, b, SDL_SCANCODE_UNKNOWN);
                        psx_keybinds_set_button(m.cfg_player + 1, m.scan_index, sc);
                        psx_keybinds_save();
                        m.scan_kind = 0;
                        m.rebuild_pending = true;
                    }
                }
                break;
            case SDL_MOUSEWHEEL:
                ui.scroll_y += ev.wheel.y * ui.sz(30);
                break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    SDL_GL_GetDrawableSize(window, &win_w, &win_h);
                break;
            default: break;
            }
        }

        // ---- quit / launch signals ----
        if (m.launch_requested) { result = Result::Launch; running = false; }
        if (m.quit_requested)   { result = Result::Quit;   running = false; }
        if (m.scan_kind) {
            // if scanning for a keybind, don't process normal UI
            // just keep rendering the background
        }

        // ---- full render every frame (even scan mode) ----
        // We need to implement the view switching and draw everything here.
        // Since we can't use closures/event-bindings like RmlUi, we inline
        // the view drawing into the loop using switch on m.view.

        // Clear
        glViewport(0, 0, win_w, win_h);
        glClearColor(THEME_BG.r, THEME_BG.g, THEME_BG.b, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        gl_ensure_init();

        const float s = ui.scale;
        const float W = (float)win_w, H = (float)win_h;
        const float pad = ui.sz(22);

        // ---- background ----
        ui.rect(0, 0, W, H, THEME_BG);

        // ---- common top bar ----
        float logo_sz = ui.sz(46);
        if (ui.logo.t) gl_draw_quad(pad, pad, logo_sz, logo_sz, ui.logo.t, {1,1,1,1});
        float tb_tx = pad + logo_sz + ui.sz(16);
        ui.font.text(tb_tx, pad + (logo_sz - ui.font.baseline*ui.ts(1.2f))/2, (game_name_s + " Recomp").c_str(), THEME_TEXT, ui.ts(1.2f));
        ui.font.text(tb_tx, pad + logo_sz - ui.sz(16), (game_name_s + " launcher").c_str(), THEME_TEXT_DIM, ui.ts(0.7f));

        // top-right buttons
        float tb_btn_h = ui.sz(30);
        float tb_btn_y = pad + (logo_sz - tb_btn_h)/2;
        float bw_back = ui.font.width("Back", ui.ts(0.6f)) + ui.sz(24);
        float bw_ctl  = ui.font.width("Controls", ui.ts(0.6f)) + ui.sz(24);
        float bw_set  = ui.font.width("Settings", ui.ts(0.6f)) + ui.sz(24);
        float bw_launch = ui.font.width("LAUNCH", ui.ts(0.8f)) + ui.sz(40);

        if (m.view == "dashboard") {
            // Controls button
            float cx = W - pad - bw_launch - ui.sz(16) - bw_set - ui.sz(8) - bw_ctl;
            if (ui.button(ui.alloc_id(), cx, tb_btn_y, bw_ctl, tb_btn_h)) m.view = "controls";
            ui.font.text(cx + (bw_ctl - ui.font.width("Controls", ui.ts(0.6f)))/2,
                         tb_btn_y + (tb_btn_h - ui.font.baseline*ui.ts(0.6f))/2,
                         "Controls", THEME_TEXT, ui.ts(0.6f));
            // Settings button
            cx = W - pad - bw_launch - ui.sz(16) - bw_set;
            if (ui.button(ui.alloc_id(), cx, tb_btn_y, bw_set, tb_btn_h)) m.view = "settings";
            ui.font.text(cx + (bw_set - ui.font.width("Settings", ui.ts(0.6f)))/2,
                         tb_btn_y + (tb_btn_h - ui.font.baseline*ui.ts(0.6f))/2,
                         "Settings", THEME_TEXT, ui.ts(0.6f));
        } else {
            // Back button
            float cx = W - pad - bw_back;
            if (ui.button(ui.alloc_id(), cx, tb_btn_y, bw_back, tb_btn_h)) m.view = "dashboard";
            ui.font.text(cx + (bw_back - ui.font.width("Back", ui.ts(0.6f)))/2,
                         tb_btn_y + (tb_btn_h - ui.font.baseline*ui.ts(0.6f))/2,
                         "Back", THEME_TEXT, ui.ts(0.6f));
        }

        float y = pad + logo_sz + ui.sz(13);

        // ---- main content based on view ----
        if (m.view == "dashboard") {
            // scrollable content: from y to above footer
            float disc_h = ui.sz(220), card_h = ui.sz(190), mc_h = ui.sz(190);
            float gap_s = ui.sz(13);
            float base_h = disc_h + gap_s + 2*(card_h+gap_s) + 2*(mc_h+gap_s) + gap_s;
            float dash_top = y, dash_bot = H - pad - ui.sz(20) - ui.sz(5);
            float avail = dash_bot - dash_top;
            // clamp scroll
            float max_s = avail - base_h;
            if (max_s > 0) max_s = 0;
            if (ui.scroll_y > 0) ui.scroll_y = 0;
            if (ui.scroll_y < max_s) ui.scroll_y = max_s;
            // clip
            if (avail > 0) {
                glEnable(GL_SCISSOR_TEST);
                glScissor(0, win_h-(int)dash_bot, win_w, (int)avail);
            }

            render_dashboard(ui, m, dev_opts, window, game_name_s, expected_serial, expected_crc, has_expected_crc, game.languages);

            if (avail > 0) glDisable(GL_SCISSOR_TEST);

            // footer
            float footer_y = H - pad - ui.sz(20);
            ui.rect(0, footer_y, W, 1, THEME_BORDER);
            float skip_y = footer_y + ui.sz(10);
            float tog_sz = ui.sz(42), tog_h = ui.sz(22);
            if (ui.toggle(ui.alloc_id(), pad, skip_y, tog_sz, tog_h, m.skip_launcher)) {
                if (m.skip_launcher) m.skip_launcher = false;
                else m.show_skip_modal = true;
            }
            ui.font.text(pad + ui.sz(50), skip_y + (tog_h - ui.font.baseline*ui.ts(0.6f))/2, "Skip launcher", THEME_TEXT_DIM, ui.ts(0.6f));
            ui.font.text(W/2 - ui.sz(50), skip_y + (tog_h - ui.font.baseline*ui.ts(0.55f))/2, "Recompiler ready", THEME_GREEN, ui.ts(0.55f));
            float l_x = W - pad - bw_launch;
            float btn_h = ui.sz(30);
            if (ui.button(ui.alloc_id(), l_x, skip_y, bw_launch, btn_h)) m.launch_requested = true;
            ui.font.text(l_x + (bw_launch - ui.font.width("LAUNCH", ui.ts(0.8f)))/2,
                         skip_y + (btn_h - ui.font.baseline*ui.ts(0.8f))/2,
                         "LAUNCH", THEME_TEXT, ui.ts(0.8f));

            // skip launcher confirm modal
            if (m.show_skip_modal) {
                ui.rect(0, 0, W, H, {0,0,0,0.8f});
                float mw = ui.sz(460), mh = ui.sz(160), mx = (W-mw)/2, my = (H-mh)/2;
                ui.rect(mx, my, mw, mh, THEME_PANEL);
                ui.rect(mx, my, mw, 1, THEME_BORDER);
                float mpad = ui.sz(24);
                ui.font.text(mx+mpad, my+ui.sz(22), "Skip the launcher on boot?", THEME_TEXT, ui.ts(0.9f));
                ui.font.text(mx+mpad, my+ui.sz(52), "The launcher will no longer appear. Run with --launcher to get it back.", THEME_TEXT_DIM, ui.ts(0.6f));
                float btn_w = ui.sz(100), bbtn_h = ui.sz(30);
                if (ui.button(ui.alloc_id(), mx+mw-mpad-btn_w-ui.sz(10)-btn_w, my+mh-mpad-bbtn_h, btn_w, bbtn_h)) m.show_skip_modal = false;
                ui.font.text(mx+mw-mpad-btn_w-ui.sz(10)-btn_w + (btn_w - ui.font.width("Cancel",ui.ts(0.6f)))/2,
                             my+mh-mpad-bbtn_h + (bbtn_h - ui.font.baseline*ui.ts(0.6f))/2, "Cancel", THEME_TEXT, ui.ts(0.6f));
                if (ui.button(ui.alloc_id(), mx+mw-mpad-btn_w, my+mh-mpad-bbtn_h, btn_w, bbtn_h)) { m.skip_launcher = true; m.show_skip_modal = false; }
                ui.font.text(mx+mw-mpad-btn_w + (btn_w - ui.font.width("Confirm",ui.ts(0.6f)))/2,
                             my+mh-mpad-bbtn_h + (bbtn_h - ui.font.baseline*ui.ts(0.6f))/2, "Confirm", THEME_TEXT, ui.ts(0.6f));
            }
        }
        else if (m.view == "settings") {
            // ---- SETTINGS VIEW (scrollable) ----
            float gap_s = ui.sz(13);
            const float col_w = (W - 2*pad - gap_s) / 2;
            float sx = pad, sy = y;
            float mgn = ui.sz(18), btn_pad = ui.sz(24);

            // estimate total height to know scroll range
            float rrh = ui.sz(28);
            int num_video = 11; // window/renderer/ss/aa/texfilter/crt/fmv/turbo/full/ws/uiscale
            float vh = ui.sz(40) + num_video * rrh + ui.sz(10);
            float rh_col = vh; // left column
            float panel_h = ui.sz(60), ph = ui.sz(28);
            float rh_right = (m.lang_menu ? ui.sz(68) : 0) + ui.sz(68) + ui.sz(68);
            float sys_h = ui.sz(110);
            float total_h = std::max(vh, rh_right) + gap_s + sys_h;
            float avail_h = H - sy - pad;
            float settings_top = sy, settings_bot = sy + total_h;
            if (settings_bot > H - pad) settings_bot = H - pad;
            // clip
            float clip_h = H - sy - pad;
            if (clip_h > 0) {
                glEnable(GL_SCISSOR_TEST);
                glScissor(0, win_h-(int)(sy+clip_h), win_w, (int)clip_h);
            }

            float soff = ui.scroll_y;
            // keep scroll in bounds
            if (soff > 0) soff = 0;
            float max_soff = -(total_h - clip_h);
            if (max_soff > 0) max_soff = 0;
            if (soff < max_soff) soff = max_soff;
            ui.scroll_y = soff;

            sy += soff;

            // left column: VIDEO
            ui.rect(sx, sy, col_w, vh, THEME_PANEL); ui.rect(sx, sy, col_w, 1, THEME_BORDER);
            ui.font.text(sx+mgn, sy+ui.sz(14), "VIDEO", THEME_ACCENT, ui.ts(0.6f));
            auto setting = [&](const char* label, const std::string& value, int id,
                               std::function<void()> onclick) {
                float ry = sy + ui.sz(40) + id*rrh;
                if (onclick) {
                    float bw = ui.font.width(value.c_str(), ui.ts(0.65f)) + btn_pad;
                    float bx = sx+col_w-mgn-bw;
                    ui.font.text(sx+mgn, ry + (rrh - ui.font.baseline*ui.ts(0.55f))/2, label, THEME_TEXT_DIM, ui.ts(0.55f));
                    if (ui.button(ui.alloc_id(), bx, ry, bw, rrh)) onclick();
                    ui.font.text(bx + (bw - ui.font.width(value.c_str(), ui.ts(0.65f)))/2,
                                 ry + (rrh - ui.font.baseline*ui.ts(0.65f))/2,
                                 value.c_str(), THEME_TEXT, ui.ts(0.65f));
                } else {
                    ui.font.text(sx+mgn, ry + (rrh - ui.font.baseline*ui.ts(0.55f))/2, label, THEME_TEXT_DIM, ui.ts(0.55f));
                }
            };
            int ri=0;
            setting("Window size", m.winsize_label, ri++, [&](){
                int i=(winsize_index(m.window_width)+1)%kNumWinWidths; m.window_width=kWinWidths[i]; refresh_labels(m);
            });
            setting("Renderer", m.renderer_label, ri++, [&](){ m.renderer^=1; refresh_labels(m); });
            setting("Supersampling", std::to_string(m.supersampling)+"x", ri++, [&](){ m.supersampling=(m.supersampling%4)+1; });
            setting("Antialiasing", m.antialiasing?"On":"Off", ri++, [&](){ m.antialiasing=!m.antialiasing; });
            setting("Texture filter", m.texfilter_label, ri++, [&](){ m.texture_filter^=1; refresh_labels(m); });
            setting("Screen model", m.crt_label, ri++, [&](){ m.crt=(m.crt+1)%4; refresh_labels(m); });
            setting("Skip FMVs", m.auto_skip_fmv?"On":"Off", ri++, [&](){ m.auto_skip_fmv=!m.auto_skip_fmv; });
            setting("Turbo loads", m.turbo_loads?"On":"Off", ri++, [&](){ m.turbo_loads=!m.turbo_loads; });
            setting("Fullscreen", m.fullscreen?"On":"Off", ri++, [&](){ m.fullscreen=!m.fullscreen; });
            if (m.ws_offered)
                setting("Widescreen", m.widescreen?"On":"Off", ri++, [&](){ m.aspect_index=(m.aspect_index==1)?0:1; refresh_labels(m); });
            // UI scale cycling: 0.5 0.75 1.0 1.25 1.5 1.75 2.0
            static const float kScales[] = {0.5f,0.75f,1.0f,1.25f,1.5f,1.75f,2.0f};
            static const int kNumScales = 7;
            m.uiscale_label = std::to_string((int)(m.uiscale*100))+"%";
            setting("UI scale", m.uiscale_label, ri++, [&](){
                int idx = 0;
                for (int i=0;i<kNumScales;i++) { if (m.uiscale <= kScales[i] + 0.01f) { idx = i; break; } }
                m.uiscale = kScales[(idx+1)%kNumScales];
            });

            // right column: panels
            float rcx = sx + col_w + gap_s;
            // LOCALIZATION
            if (m.lang_menu) {
                ui.rect(rcx, sy, col_w, panel_h, THEME_PANEL); ui.rect(rcx, sy, col_w, 1, THEME_BORDER);
                ui.font.text(rcx+mgn, sy+ui.sz(14), "LOCALIZATION", THEME_ACCENT, ui.ts(0.6f));
                float bw=ui.font.width(m.lang_label.c_str(), ui.ts(0.65f))+btn_pad;
                if (ui.button(ui.alloc_id(), rcx+col_w-mgn-bw, sy+ui.sz(40), bw, ph)) {
                    m.lang_index=(m.lang_index+1)%(int)game.languages.size(); refresh_language(m,game.languages);
                }
                ui.font.text(rcx+mgn, sy+ui.sz(40) + (ph-ui.font.baseline*ui.ts(0.55f))/2, "Language", THEME_TEXT_DIM, ui.ts(0.55f));
                ui.font.text(rcx+col_w-mgn-bw + (bw-ui.font.width(m.lang_label.c_str(), ui.ts(0.65f)))/2,
                             sy+ui.sz(40) + (ph-ui.font.baseline*ui.ts(0.65f))/2, m.lang_label.c_str(), THEME_TEXT, ui.ts(0.65f));
            }
            // AUDIO
            float ay = sy + (m.lang_menu?ui.sz(68):0);
            ui.rect(rcx, ay, col_w, panel_h, THEME_PANEL); ui.rect(rcx, ay, col_w, 1, THEME_BORDER);
            ui.font.text(rcx+mgn, ay+ui.sz(14), "AUDIO", THEME_ACCENT, ui.ts(0.6f));
            { float bw=ui.font.width(m.spu_hq?"On":"Off", ui.ts(0.65f))+btn_pad;
              if (ui.button(ui.alloc_id(), rcx+col_w-mgn-bw, ay+ui.sz(40), bw, ph)) m.spu_hq=!m.spu_hq;
              ui.font.text(rcx+mgn, ay+ui.sz(40)+(ph-ui.font.baseline*ui.ts(0.55f))/2, "SPU high-quality (float shadow)", THEME_TEXT_DIM, ui.ts(0.55f));
              ui.font.text(rcx+col_w-mgn-bw + (bw-ui.font.width(m.spu_hq?"On":"Off", ui.ts(0.65f)))/2,
                           ay+ui.sz(40)+(ph-ui.font.baseline*ui.ts(0.65f))/2, m.spu_hq?"On":"Off", THEME_TEXT, ui.ts(0.65f)); }
            // CONTROLLER
            float cy = ay + ui.sz(68);
            ui.rect(rcx, cy, col_w, panel_h, THEME_PANEL); ui.rect(rcx, cy, col_w, 1, THEME_BORDER);
            ui.font.text(rcx+mgn, cy+ui.sz(14), "CONTROLLER", THEME_ACCENT, ui.ts(0.6f));
            { std::string dz=std::to_string(m.deadzone_pct)+"%";
              float bw=ui.font.width(dz.c_str(), ui.ts(0.65f))+btn_pad;
              if (ui.button(ui.alloc_id(), rcx+col_w-mgn-bw, cy+ui.sz(40), bw, ph)){
                  m.deadzone_pct+=5; if(m.deadzone_pct>50)m.deadzone_pct=0; }
              ui.font.text(rcx+mgn, cy+ui.sz(40)+(ph-ui.font.baseline*ui.ts(0.55f))/2, "Analog stick deadzone", THEME_TEXT_DIM, ui.ts(0.55f));
              ui.font.text(rcx+col_w-mgn-bw + (bw-ui.font.width(dz.c_str(), ui.ts(0.65f)))/2,
                           cy+ui.sz(40)+(ph-ui.font.baseline*ui.ts(0.65f))/2, dz.c_str(), THEME_TEXT, ui.ts(0.65f)); }
            // SYSTEM (full width below columns)
            float sys_h_u = ui.sz(110);
            float sys_y = sy + std::max(vh, cy+ui.sz(68)) + gap_s;
            ui.rect(pad, sys_y, W-2*pad, sys_h_u, THEME_PANEL); ui.rect(pad, sys_y, W-2*pad, 1, THEME_BORDER);
            ui.font.text(pad+mgn, sys_y+ui.sz(14), "SYSTEM", THEME_ACCENT, ui.ts(0.6f));
            // BIOS row
            float bsy = sys_y + ui.sz(40);
            ui.font.text(pad+mgn, bsy+(ph-ui.font.baseline*ui.ts(0.55f))/2, "BIOS", THEME_TEXT_DIM, ui.ts(0.55f));
            float bbw=ui.sz(60);
            if (ui.button(ui.alloc_id(), pad+col_w, bsy, bbw, ph)) {
                std::string p=win_pick_file(window,"Select PlayStation BIOS","BIOS image (*.bin;*.rom)\0*.bin;*.rom\0All files (*.*)\0*.*\0\0");
                if (!p.empty()) m.bios_path=fs::path(p).generic_string();
            } ui.font.text(pad+col_w+(bbw-ui.font.width("Browse", ui.ts(0.55f)))/2, bsy+(ph-ui.font.baseline*ui.ts(0.55f))/2, "Browse", THEME_TEXT, ui.ts(0.55f));
            std::string bp=m.bios_path.empty()?"(not set)":m.bios_path;
            ui.font.text(pad+col_w+bbw+ui.sz(10), bsy+(ph-ui.font.baseline*ui.ts(0.55f))/2, bp.c_str(), m.bios_path.empty()?THEME_RED:THEME_TEXT_MUTED, ui.ts(0.55f));
            // Disc row
            float dsy=bsy+ui.sz(32);
            ui.font.text(pad+mgn, dsy+(ph-ui.font.baseline*ui.ts(0.55f))/2, "Disc", THEME_TEXT_DIM, ui.ts(0.55f));
            if (ui.button(ui.alloc_id(), pad+col_w, dsy, bbw, ph)) {
                std::string p=win_pick_file(window,"Select disc image","Disc image (*.cue;*.bin;*.iso)\0*.cue;*.bin;*.iso\0All files (*.*)\0*.*\0\0");
                if (!p.empty()){ m.disc_path=fs::path(p).generic_string(); refresh_disc_status(m,game_name_s,expected_serial,expected_crc,has_expected_crc); }
            } ui.font.text(pad+col_w+(bbw-ui.font.width("Browse", ui.ts(0.55f)))/2, dsy+(ph-ui.font.baseline*ui.ts(0.55f))/2, "Browse", THEME_TEXT, ui.ts(0.55f));
            std::string dp=m.disc_path.empty()?"(not set)":m.disc_path;
            ui.font.text(pad+col_w+bbw+ui.sz(10), dsy+(ph-ui.font.baseline*ui.ts(0.55f))/2, dp.c_str(), m.disc_path.empty()?THEME_RED:THEME_TEXT_MUTED, ui.ts(0.55f));

            if (clip_h > 0) glDisable(GL_SCISSOR_TEST);
        }
        else if (m.view == "controls") {
            // ---- CONTROLS VIEW ----
            float ctrl_h = H - y - pad - ui.sz(20);
            ui.rect(pad, y, W-2*pad, ctrl_h, THEME_PANEL); ui.rect(pad, y, W-2*pad, 1, THEME_BORDER);
            ui.font.text(pad+ui.sz(18), y+ui.sz(14), "KEYBOARD CONTROLS", THEME_ACCENT, ui.ts(0.6f));
            // player selector
            float sel_h = ui.sz(24);
            float sel_y = y + ui.sz(40);
            ui.font.text(pad+ui.sz(18), sel_y + (sel_h - ui.font.baseline*ui.ts(0.55f))/2, "Player", THEME_TEXT_DIM, ui.ts(0.55f));
            float seg_w = ui.sz(80), seg_x = pad+ui.sz(80);
            for (int p=0;p<2;p++){
                float sx=seg_x+seg_w*p;
                int id=ui.alloc_id();
                bool over=ui.hot(id,sx,sel_y,seg_w,sel_h);
                if (ui.mouse_pressed&&over) ui.active_id=id;
                bool clk=ui.mouse_released&&ui.active_id==id&&over;
                if (ui.mouse_released&&ui.active_id==id) ui.active_id=0;
                Color bgc=(m.cfg_player==p)?THEME_SEG_ON:(over?THEME_BTN_HOVER:THEME_SEG_BG);
                ui.rect(sx,sel_y,seg_w,sel_h,bgc);
                ui.font.text(sx+(seg_w-ui.font.width(p==0?"1":"2", ui.ts(0.6f)))/2,
                             sel_y+(sel_h-ui.font.baseline*ui.ts(0.6f))/2, p==0?"1":"2", THEME_TEXT, ui.ts(0.6f));
                if (clk){ m.cfg_player=p; m.rebuild_pending=true; }
                if (p==0) ui.rect(sx+seg_w-1,sel_y,1,sel_h,THEME_BORDER);
            }
            // reset button
            float rw=ui.sz(120);
            if (ui.button(ui.alloc_id(), W-pad-ui.sz(18)-rw, sel_y, rw, sel_h)) {
                psx_keybinds_reset_player(m.cfg_player+1); psx_keybinds_save(); m.rebuild_pending=true;
            }
            ui.font.text(W-pad-ui.sz(18)-rw+(rw-ui.font.width("Reset to defaults", ui.ts(0.55f)))/2,
                         sel_y+(sel_h-ui.font.baseline*ui.ts(0.55f))/2, "Reset to defaults", THEME_TEXT, ui.ts(0.55f));
            // keybind list (scrollable)
            float lb_y = sel_y + sel_h + ui.sz(12);
            float row_h_kb = ui.sz(26), chip_w = ui.sz(120), chip_x = pad+ui.sz(150);
            int n = psx_keybinds_button_count();
            // apply scroll offset and clip (scissor in window-pixel coords)
            float content_top = lb_y, content_bot = y + ctrl_h;
            if (content_bot > content_top) {
                glEnable(GL_SCISSOR_TEST);
                glScissor((int)pad, win_h-(int)content_bot, (int)(W-2*pad), (int)(content_bot-content_top));
            }
            float ry_base = lb_y + ui.scroll_y;
            for (int i=0;i<n;i++){
                float ry = ry_base + i*row_h_kb;
                if (ry + row_h_kb < content_top) continue;
                if (ry > content_bot) break;
                const char* lbl = psx_keybinds_button_label(i);
                ui.font.text(pad+ui.sz(22), ry + (row_h_kb - ui.font.baseline*ui.ts(0.5f))/2, lbl, THEME_TEXT, ui.ts(0.5f));
                SDL_Scancode sc = psx_keybinds_get_button(m.cfg_player+1, i);
                const char* key_name = (sc != SDL_SCANCODE_UNKNOWN) ? SDL_GetScancodeName(sc) : "None";
                if (m.scan_kind && m.scan_index == i) {
                    (void)key_name;
                    if (ui.button(ui.alloc_id(), chip_x, ry, chip_w, row_h_kb)) { }
                    ui.font.text(chip_x + (chip_w - ui.font.width("Press a key...", ui.ts(0.5f)))/2,
                                 ry + (row_h_kb - ui.font.baseline*ui.ts(0.5f))/2, "Press a key...", THEME_WARN, ui.ts(0.5f));
                } else {
                    if (ui.button(ui.alloc_id(), chip_x, ry, chip_w, row_h_kb)) {
                        m.scan_kind = 1; m.scan_index = i;
                    }
                    float tw3 = ui.font.width(key_name, ui.ts(0.5f));
                    ui.font.text(chip_x + (chip_w - tw3)/2,
                                 ry + (row_h_kb - ui.font.baseline*ui.ts(0.5f))/2,
                                 key_name, THEME_TEXT, ui.ts(0.5f));
                }
            }
            if (content_bot > content_top) glDisable(GL_SCISSOR_TEST);
            // clamp scroll to prevent overscroll
            float total_h = n * row_h_kb;
            float visible_h = content_bot - content_top;
            if (total_h > visible_h) {
                if (ui.scroll_y > 0) ui.scroll_y = 0;
                if (ui.scroll_y < -(total_h - visible_h)) ui.scroll_y = -(total_h - visible_h);
            } else ui.scroll_y = 0;
        }

        gl_ensure_init();
        SDL_GL_SwapWindow(window);
    }

    // ---- Commit choices on launch ----
    if (result == Result::Launch) {
        io.renderer = m.renderer;             io.has_renderer = true;
        io.supersampling = m.supersampling;   io.has_supersampling = true;
        io.antialiasing = m.antialiasing;     io.has_antialiasing = true;
        io.texture_filter = m.texture_filter; io.has_texture_filter = true;
        io.screen_kind = m.crt;               io.has_screen_kind = true;
        io.auto_skip_fmv = m.auto_skip_fmv;   io.has_auto_skip_fmv = true;
        io.turbo_loads = m.turbo_loads;       io.has_turbo_loads = true;
        io.fullscreen = m.fullscreen;         io.has_fullscreen = true;
        io.skip_launcher = m.skip_launcher;   io.has_skip_launcher = true;
        io.spu_hq = m.spu_hq;                io.has_spu_hq = true;
        io.aspect_num = kAspects[m.aspect_index][0];
        io.aspect_den = kAspects[m.aspect_index][1];
        io.has_aspect_ratio = true;
        io.window_width = m.window_width;     io.has_window_width = true;
        if (!m.bios_path.empty()) { io.bios_path = fs::path(m.bios_path); io.has_bios_path = true; }
        if (!m.disc_path.empty()) { io.disc_path = fs::path(m.disc_path); io.has_disc_path = true; }
        io.memcard1_enabled = m.mc1_enabled; io.has_memcard1_enabled = true;
        io.memcard2_enabled = m.mc2_enabled; io.has_memcard2_enabled = true;
        if (!m.mc1_path.empty()) { io.memcard1_path = fs::path(m.mc1_path); io.has_memcard1_path = true; }
        if (!m.mc2_path.empty()) { io.memcard2_path = fs::path(m.mc2_path); io.has_memcard2_path = true; }
        const int i1=m.p1_dev_index>0&&m.p1_dev_index<(int)dev_opts.size()?m.p1_dev_index:0;
        const int i2=m.p2_dev_index>0&&m.p2_dev_index<(int)dev_opts.size()?m.p2_dev_index:0;
        io.p1_device = device_string(dev_opts[i1]); io.has_p1_device = true;
        io.p2_device = device_string(dev_opts[i2]); io.has_p2_device = true;
        io.p1_mode = m.p1_mode; io.has_p1_mode = true;
        io.p2_mode = m.p2_mode; io.has_p2_mode = true;
        io.deadzone = m.deadzone_pct * 32767 / 100; io.has_deadzone = true;
        if (m.lang_menu && m.lang_index >= 0 && m.lang_index < (int)game.languages.size()) {
            io.language = game.languages[m.lang_index].code;
            io.has_language = true;
        }
        // io.uiscale = m.uiscale; io.has_uiscale = true;
    }

    return result;
}

} // namespace psx_launcher
