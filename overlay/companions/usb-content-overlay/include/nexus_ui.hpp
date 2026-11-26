// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <tesla.hpp>
#include "drive_panel.hpp"

namespace nexus_ui {
using Renderer=tsl::gfx::Renderer;
using Color=tsl::Color;
constexpr Color Background(0,1,2,15),Surface(1,2,3,15),Border(2,3,5,15);
constexpr Color White(15,15,15,15),Secondary(11,12,14,15),Blue(5,10,15,15),Purple(10,7,15,15);

inline void Text(Renderer *r,const std::string& text,int x,int y,int width,int size,Color color=White) {
    std::string fitted=text;
    if(r->getTextDimensions(fitted,false,size).first>width) {
        do {
            if(fitted.empty())break;
            size_t pos=fitted.size()-1;
            while(pos && (static_cast<unsigned char>(fitted[pos])&0xC0)==0x80)--pos;
            fitted.resize(pos);
        } while(r->getTextDimensions(fitted+"...",false,size).first>width);
        fitted+="...";
    }
    r->drawString(fitted,false,x,y,size,color);
}
inline void RightText(Renderer*r,const std::string&text,int right,int y,int size,Color color=White) {
    r->drawString(text,false,right-static_cast<int>(r->getTextDimensions(text,false,size).first),y,size,color);
}
inline void Box(Renderer*r,int x,int y,int w,int h,Color outline,Color fill,int radius=10) {
    r->drawRoundedRect(x,y,w,h,radius,outline);
    r->drawRoundedRect(x+1,y+1,w-2,h-2,radius-1,fill);
}
inline void Header(Renderer*r) {
    Text(r,"NEXUS",24,43,300,25);
    Text(r,"USB Content",24,70,300,16,Secondary);
    Box(r,370,22,54,25,Border,Surface,7);
    Text(r,"USB",384,39,35,12);
    RightText(r,APP_VERSION,424,70,12,Secondary);
    r->drawRect(24,89,248,2,Blue);
    r->drawRect(272,89,152,2,Purple);
}
inline void Footer(Renderer*r) {
    r->drawRect(24,651,400,1,Border);
    Text(r,"\uE0E1",30,689,30,23);
    Text(r,"Back",60,687,80,17);
    Text(r,"\uE0E0",140,689,30,23);
    Text(r,"Select",170,687,110,17);
    RightText(r,"NEXUS USB",424,686,11,Secondary);
}
inline void Drive(Renderer*r,int x,int y,int w,const drive_panel::Display&d) {
    Box(r,x,y,w,250,Border,Surface,12);
    const bool connected=d.status=="USB CONNECTED" || d.status=="USB READ-ONLY";
    r->drawCircle(x+22,y+25,3,true,connected?Blue:Purple);
    const std::string status=d.status=="USB CONNECTED"?"USB connected":
                             d.status=="USB READ-ONLY"?"USB read-only":
                             d.status=="USB NOT READY"?"USB not ready":
                             d.status=="WAITING FOR LIVE STATUS"?"Waiting for drive status":d.status;
    Text(r,status,x+34,y+30,w-54,14);
    Text(r,d.name,x+20,y+70,w-40,24);
    Text(r,d.detail,x+20,y+96,w-40,14,Secondary);
    r->drawRect(x+20,y+114,w-40,1,Border);
    Text(r,"FREE SPACE",x+20,y+140,170,11,Secondary);
    Text(r,"CAPACITY",x+w/2+10,y+140,w/2-30,11,Secondary);
    std::string free="Unavailable",total="Unavailable";
    if(d.has_capacity) {
        free=d.free.substr(0,d.free.size()-5);
        const auto slash=d.usage.find(" / ");
        if(slash!=std::string::npos)total=d.usage.substr(slash+3);
    }
    Text(r,free,x+20,y+174,w/2-20,d.has_capacity?29:20);
    Text(r,total,x+w/2+10,y+174,w/2-30,d.has_capacity?25:20);
    const int width=w-40,used=d.has_capacity?static_cast<int>(d.used_fraction*width):0;
    r->drawRoundedRect(x+20,y+195,width,7,3,Border);
    if(used>0)r->drawRoundedRect(x+20,y+195,used,7,3,Blue);
    if(used>8)r->drawRoundedRect(x+20+used-8,y+195,8,7,3,Purple);
    std::string used_label=d.has_capacity?d.usage.substr(0,d.usage.find(" / ")):d.usage;
    Text(r,used_label,x+20,y+227,w-90,13,Secondary);
    if(d.has_capacity) {
        char percentage[16]{};std::snprintf(percentage,sizeof(percentage),"%.0f%%",d.used_fraction*100);
        RightText(r,percentage,x+w-20,y+227,13,Secondary);
    }
    // A stale/disconnected snapshot keeps its details with an explicit label.
    if(d.access=="Last reported drive")Text(r,"Last reported",x+w-126,y+30,108,12,Secondary);
    else if(connected)RightText(r,d.status=="USB READ-ONLY"?"Read-only":"Read + write",x+w-20,y+30,12,Secondary);
}

enum class Action { Route,Eject,Refresh };
inline void Icon(Renderer*r,Action kind,int x,int y) {
    const Color c=kind==Action::Eject?Purple:Blue;
    if(kind==Action::Route) {
        r->drawLine(x,y-5,x+17,y-5,c);r->drawLine(x+13,y-9,x+17,y-5,c);r->drawLine(x+13,y-1,x+17,y-5,c);
        r->drawLine(x,y+5,x+17,y+5,c);r->drawLine(x+4,y+1,x,y+5,c);r->drawLine(x+4,y+9,x,y+5,c);
    } else if(kind==Action::Eject) {
        r->drawLine(x,y+2,x+9,y-8,c);r->drawLine(x+9,y-8,x+18,y+2,c);r->drawLine(x,y+2,x+18,y+2,c);
        r->drawRect(x,y+7,19,3,c);
    } else {
        r->drawCircle(x+9,y,8,false,c);
        r->drawRect(x+12,y-11,9,8,Surface);
        r->drawLine(x+17,y-8,x+17,y-1,c);r->drawLine(x+10,y-1,x+17,y-1,c);
    }
}
inline void ActionRow(Renderer*r,int x,int y,int w,Action kind,const std::string&label,const std::string&value,bool focused) {
    Box(r,x,y+1,w,64,focused?Blue:Border,focused?Color(2,3,5,15):Surface,9);
    if(focused)r->drawRoundedRect(x+1,y+13,3,40,1,Purple);
    Icon(r,kind,x+20,y+31);
    const int value_width=value.empty()?0:static_cast<int>(r->getTextDimensions(value,false,13).first)+20;
    Text(r,label,x+56,y+27,w-80-value_width,19);
    const char*subtitle=kind==Action::Route?"Destination for new installs":kind==Action::Refresh?"Update drive information":
                         label=="Reconnect USB"?"Use the connected drive again":"Finish writes before disconnecting";
    Text(r,subtitle,x+56,y+48,w-76,12,Secondary);
    if(!value.empty())RightText(r,value,x+w-18,y+27,13);
}
class MenuItem final:public tsl::elm::ListItem {
    Action kind;
public:
    MenuItem(Action type,const std::string&label,const std::string&value=""):
        tsl::elm::ListItem(label,value),kind(type) {}
    void layout(u16,u16,u16,u16) override {setBoundaries(getX(),getY(),getWidth(),72);}
    void drawFocusBackground(Renderer*) override {}
    void drawHighlight(Renderer*) override {}
    void draw(Renderer*r) override {ActionRow(r,getX(),getY(),getWidth(),kind,getText(),getValue(),m_focused);}
};
class Frame final:public tsl::elm::HeaderOverlayFrame {
public:
    Frame():HeaderOverlayFrame("NEXUS","USB Content") {
        ult::activeHeaderHeight=104;
        ult::backWidth.store(110);ult::selectWidth.store(120);ult::halfGap.store(0);
    }
    void layout(u16 x,u16 y,u16 w,u16 h) override {
        setBoundaries(x,y,w,h);
        if(m_contentElement) {
            m_contentElement->setBoundaries(x+24,y+104,w-48,h-185);
            m_contentElement->invalidate();
        }
    }
    void draw(Renderer*r) override {
        r->fillScreen(Background);Header(r);
        if(m_contentElement)m_contentElement->frame(r);
        Footer(r);
    }
};
}
