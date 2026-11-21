// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "storage_status.hpp"

namespace drive_panel {
struct Display {
    std::string status, name, detail, free, usage, access;
    double used_fraction{};
    bool has_capacity{};
};
inline Display Describe(const storage_status::StatusData& current,
                        const storage_status::StatusData& details, bool have_details, bool live) {
    Display d;
    d.status=live?(current.ready?(current.writable?"USB CONNECTED":"USB READ-ONLY"):"USB NOT READY")
                 :"WAITING FOR LIVE STATUS";
    d.name=have_details?storage_status::DeviceName(details):"USB drive";
    if(d.name=="Basic data partition" && (details.vendor[0] || details.product[0]))
        d.name=std::string(details.vendor)+" "+details.product;
    if(have_details) {
        std::string device=std::string(details.vendor)+" "+details.product;
        while(!device.empty() && device.front()==' ')device.erase(0,1);
        while(!device.empty() && device.back()==' ')device.pop_back();
        const std::string filesystem=std::strcmp(details.filesystem,"exfat")==0?"exFAT":details.filesystem;
        d.detail=(device.empty() || device==d.name)?filesystem:device+"  |  "+filesystem;
    } else d.detail="Reading drive information...";
    char free[32]{},used[32]{},total[32]{};
    d.has_capacity=have_details && details.has_free_space && details.has_total_space &&
                   details.total_space && details.free_space<=details.total_space;
    if(d.has_capacity) {
        storage_status::FormatBytes(free,sizeof(free),details.free_space);
        storage_status::FormatBytes(used,sizeof(used),details.total_space-details.free_space);
        storage_status::FormatBytes(total,sizeof(total),details.total_space);
        d.free=std::string(free)+" free";
        d.usage=std::string(used)+" used / "+total;
        d.used_fraction=double(details.total_space-details.free_space)/double(details.total_space);
    } else {
        d.free="Free space unavailable";
        d.usage="No capacity estimate";
    }
    d.access=live && current.ready?(current.writable?"Read + write available":"Read-only / no installs")
                                  :(have_details?"Last reported drive":"Waiting for drive details");
    return d;
}
}
