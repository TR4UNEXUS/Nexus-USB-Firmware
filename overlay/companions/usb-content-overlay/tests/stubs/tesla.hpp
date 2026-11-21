#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <iostream>
#include <iomanip>
#include <atomic>
using u64=uint64_t;using u32=uint32_t;using s32=int32_t;using u16=uint16_t;
namespace ult {inline int activeHeaderHeight{};inline std::atomic<float>backWidth{},selectWidth{},halfGap{};}
constexpr u64 HidNpadButton_A=1,HidNpadButton_B=2;
struct HidTouchState{};struct HidAnalogStickState{};
inline uint64_t host_ms=1000;
inline uint64_t armGetSystemTick(){return host_ms*1000000;}
inline uint64_t armTicksToNs(uint64_t ticks){return ticks;}
namespace tsl {
struct Color {
    unsigned rgba;constexpr Color(unsigned v):rgba(v){}
    constexpr Color(unsigned r,unsigned g,unsigned b,unsigned a):rgba(r|(g<<4)|(b<<8)|(a<<12)){}
};
namespace gfx {
struct Renderer {
    bool record{};std::vector<std::string> labels;
    std::pair<float,float> getTextDimensions(const std::string& s,bool,int size){
        if(s=="\ue0e0" || s=="\ue0e1")return {float(size),float(size)};
        size_t count=0;for(unsigned char c:s)if((c&0xc0)!=0x80)++count;
        return {float(count*size/2),float(size)};
    }
    void drawString(const std::string&s,bool,int x,int y,int size,Color c){
        labels.push_back(s);
        if(record)std::cout<<"{\"op\":\"text\",\"x\":"<<x<<",\"y\":"<<y<<",\"size\":"<<size<<",\"color\":"<<c.rgba<<",\"text\":"<<std::quoted(s)<<"}\n";
    }
    void drawRoundedRect(int x,int y,int w,int h,int radius,Color c){
        if(record)std::cout<<"{\"op\":\"rect\",\"x\":"<<x<<",\"y\":"<<y<<",\"w\":"<<w<<",\"h\":"<<h<<",\"radius\":"<<radius<<",\"color\":"<<c.rgba<<"}\n";
    }
    void drawRect(int x,int y,int w,int h,Color c){drawRoundedRect(x,y,w,h,0,c);}
    void fillScreen(Color c){drawRect(0,0,448,720,c);}
    void drawLine(int x,int y,int x2,int y2,Color c){if(record)std::cout<<"{\"op\":\"line\",\"x\":"<<x<<",\"y\":"<<y<<",\"x2\":"<<x2<<",\"y2\":"<<y2<<",\"color\":"<<c.rgba<<"}\n";}
    void drawCircle(int x,int y,int radius,bool filled,Color c){if(record)std::cout<<"{\"op\":\"circle\",\"x\":"<<x<<",\"y\":"<<y<<",\"radius\":"<<radius<<",\"filled\":"<<(filled?"true":"false")<<",\"color\":"<<c.rgba<<"}\n";}
};
}
namespace elm {
struct Element {
    int x{},y{},w{},h{};bool m_focused{};
    virtual ~Element()=default;
    void setBoundaries(int a,int b,int c,int d){x=a;y=b;w=c;h=d;}
    int getX(){return x;}int getY(){return y;}int getWidth(){return w;}int getHeight(){return h;}
    void invalidate(){layout(x,y,w,h);}
    void frame(gfx::Renderer*r){draw(r);}
    virtual void draw(gfx::Renderer*){}
    virtual void drawHighlight(gfx::Renderer*){}
    virtual void drawFocusBackground(gfx::Renderer*){}
    virtual void layout(u16,u16,u16,u16){}
    virtual void setFocused(bool value){m_focused=value;}
};
struct ListItem:Element {
    inline static std::vector<ListItem*> created;
    std::string text,value;std::function<bool(u64)> click;
    ListItem(const std::string&t,const std::string&v="",bool=false):text(t),value(v){h=72;created.push_back(this);}
    void setText(const std::string&t){text=t;}void setValue(const std::string&v){value=v;}
    void setClickListener(std::function<bool(u64)> f){click=f;}
    const std::string&getText(){return text;}const std::string&getValue(){return value;}
};
struct List:Element {
    std::vector<Element*> items;
    ~List(){for(auto*e:items)delete e;}
    void addItem(Element*e,int height=0){if(height)e->h=height;items.push_back(e);}
    void layout(u16,u16,u16,u16)override {int top=y;for(auto*e:items){e->setBoundaries(x,top,w,e->getHeight());e->invalidate();top+=e->getHeight();}}
    void draw(gfx::Renderer*r)override {for(auto*e:items)e->frame(r);}
};
struct CustomDrawer:Element {
    inline static std::vector<CustomDrawer*> created;
    std::function<void(gfx::Renderer*,s32,s32,s32,s32)> render;
    template<class F>CustomDrawer(F f):render(f){created.push_back(this);}
    void draw(gfx::Renderer*r,s32 a,s32 b,s32 c,s32 d){render(r,a,b,c,d);}
    void draw(gfx::Renderer*r)override {render(r,x,y,w,h);}
};
struct HeaderOverlayFrame:Element {
    Element*m_contentElement{};HeaderOverlayFrame(const std::string&,const std::string&){}
    ~HeaderOverlayFrame(){delete m_contentElement;}void setContent(Element*e){m_contentElement=e;}
};
}
struct Gui {
    virtual ~Gui()=default;virtual elm::Element*createUI()=0;virtual void update(){}
    virtual bool handleInput(u64,u64,const HidTouchState&,HidAnalogStickState,HidAnalogStickState){return false;}
};
struct Overlay {
    virtual ~Overlay()=default;virtual void initServices(){};virtual void exitServices(){};
    virtual std::unique_ptr<Gui>loadInitialGui()=0;
    template<class T>std::unique_ptr<Gui>initially(){return std::make_unique<T>();}
};
template<class T>int loop(int,char**){return 0;}
}
