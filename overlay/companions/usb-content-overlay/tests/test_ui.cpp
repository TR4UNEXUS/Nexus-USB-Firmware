#define main overlay_main
#include "main.cpp"
#undef main
#include <cassert>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <algorithm>
void Status(unsigned phase=0,unsigned stage=0,unsigned result=0,uint64_t request=0,uint64_t ack=0,unsigned ack_result=0,unsigned handles=0) {
    std::ofstream f("sdmc:/atmosphere/logs/usb_eject_status.txt");
    f<<"NXEJ1 123 "<<host_ms<<" 7 "<<request<<" "<<ack<<" "<<ack_result<<" "<<phase<<" "<<stage<<" "<<result<<" "<<handles<<" "<<(phase==0?1:0)<<"\n";
}
int main() {
    std::filesystem::create_directories("sdmc:/atmosphere/logs");
    std::ofstream("sdmc:/atmosphere/logs/usb_storage_status.txt")<<"state=Ready\nready=1\nwritable=1\ninstall_route=0\npartition_name=NEXUS GAMES\nvendor=Samsung\nproduct=Portable SSD\nfilesystem=exfat\nfree_space=161061273600\ntotal_space=536870912000\n";
    UsbContentGui gui;auto *frame=gui.createUI();auto*action=tsl::elm::ListItem::created[1];
    assert(tsl::elm::ListItem::created.size()==3);
    assert(tsl::elm::ListItem::created[0]->text=="Install route");
    assert(tsl::elm::ListItem::created[2]->text=="Refresh status");
    assert(action->text=="Safe eject");
    const auto panel=[](){tsl::gfx::Renderer renderer;tsl::elm::CustomDrawer::created[0]->draw(&renderer,24,104,400,264);return renderer.labels;};
    auto labels=panel();
    // Drive details must appear even with NO eject-status file, on the first frame.
    assert(std::find(labels.begin(),labels.end(),"NEXUS GAMES")!=labels.end());
    assert(std::find(labels.begin(),labels.end(),"150.0 GB")!=labels.end());
    assert(std::find(labels.begin(),labels.end(),"350.0 GB used")!=labels.end());
    assert(std::find(labels.begin(),labels.end(),"500.0 GB")!=labels.end());
    assert(std::find(labels.begin(),labels.end(),"USB connected")!=labels.end());
    Status();
    action->click(HidNpadButton_A);assert(!std::filesystem::exists(CommandPath)); // no live heartbeat yet
    host_ms+=1000;Status();gui.update();
    if(std::getenv("NEXUS_RENDER_PANEL")) {
        tsl::gfx::Renderer renderer;renderer.record=true;
        frame->layout(0,0,448,720);tsl::elm::ListItem::created[0]->setFocused(true);
        frame->draw(&renderer);
    }
    action->click(HidNpadButton_A);assert(action->text=="Eject USB now?");
    assert(!std::filesystem::exists(CommandPath)); // confirmation is required
    assert(gui.handleInput(HidNpadButton_B,0,{},{},{}));assert(action->text=="Safe eject");
    action->click(HidNpadButton_A);action->click(HidNpadButton_A);
    assert(std::filesystem::file_size(CommandPath)==48);assert(!std::filesystem::exists(CommandTempPath));
    nexus_eject::Request r{};std::ifstream(CommandPath,std::ios::binary).read(reinterpret_cast<char*>(&r),sizeof(r));
    assert(nexus_eject::Valid(r,123,host_ms));assert(r.action==nexus_eject::Eject && r.generation==7);
    std::filesystem::remove(CommandPath);host_ms+=1000;Status(0,0,0,0,r.nonce,0x402);gui.update();
    assert(action->text=="Safe eject"); // refusal must never show reconnect/safe
    action->click(HidNpadButton_A);action->click(HidNpadButton_A);
    std::ifstream(CommandPath,std::ios::binary).read(reinterpret_cast<char*>(&r),sizeof(r));
    assert(r.action==nexus_eject::Eject);std::filesystem::remove(CommandPath);
    host_ms+=1000;Status(2,4,0,r.nonce,r.nonce);gui.update();assert(action->value=="Please wait");
    host_ms+=1000;Status(3,7,0,r.nonce,r.nonce);gui.update();assert(action->text=="Reconnect USB");
    host_ms+=4000;gui.update();assert(action->text=="Safe eject" && action->value=="Waiting");
    labels=panel();assert(std::find(labels.begin(),labels.end(),"150.0 GB")!=labels.end());
    action->click(HidNpadButton_A);assert(!std::filesystem::exists(CommandPath));
    host_ms+=1000;Status(3,7,0,r.nonce,r.nonce);gui.update();
    host_ms+=1000;Status(3,7,0,r.nonce,r.nonce);gui.update();assert(action->text=="Reconnect USB");
    action->click(HidNpadButton_A);
    std::ifstream(CommandPath,std::ios::binary).read(reinterpret_cast<char*>(&r),sizeof(r));
    assert(r.action==nexus_eject::Resume);assert(nexus_eject::Valid(r,123,host_ms));
    std::filesystem::remove(CommandPath);host_ms+=1000;Status(0,0,0,0,r.nonce);gui.update();
    assert(action->text=="Safe eject");
    host_ms+=1000;Status(0,0,0,0,r.nonce,0,1);gui.update();
    assert(action->value=="Check use");
    action->click(HidNpadButton_A);assert(!std::filesystem::exists(CommandPath));assert(action->value=="Confirm");
    action->click(HidNpadButton_A);assert(std::filesystem::exists(CommandPath));
    std::ifstream(CommandPath,std::ios::binary).read(reinterpret_cast<char*>(&r),sizeof(r));
    std::filesystem::remove(CommandPath);
    host_ms+=1000;Status(0,0,0,0,r.nonce,0x402,1);gui.update();
    assert(action->text=="Safe eject" && action->value=="Check use");
    std::ofstream("sdmc:/atmosphere/logs/usb_storage_status.txt")<<"state=Ready\nready=1\nwritable=1\npartition_name=NEXUS GAMES\nfree_space=0\ntotal_space=536870912000\n";
    host_ms+=1000;gui.update();labels=panel();
    assert(std::find(labels.begin(),labels.end(),"0 B")!=labels.end()); // full drive is not N/A
    std::filesystem::remove("sdmc:/atmosphere/logs/usb_storage_status.txt");
    host_ms+=1000;gui.update();labels=panel();
    assert(std::find(labels.begin(),labels.end(),"Last reported")!=labels.end());
    assert(std::find(labels.begin(),labels.end(),"0 B")!=labels.end());
    // A consumed request can expire without an ack. A missing or stale
    // heartbeat must never enable retry or claim the drive was released.
    host_ms+=1000;Status();gui.update();
    action->click(HidNpadButton_A);action->click(HidNpadButton_A);
    std::ifstream(CommandPath,std::ios::binary).read(reinterpret_cast<char*>(&r),sizeof(r));
    host_ms+=6100;gui.update();assert(action->value=="Waiting");
    Status();action->click(HidNpadButton_A); // first new heartbeat after the gap
    host_ms+=500;Status();gui.update();action->click(HidNpadButton_A);
    assert(action->value=="Please wait"); // command file is still present
    std::filesystem::remove(CommandPath);host_ms+=1000;Status();gui.update();
    action->click(HidNpadButton_A);assert(action->value=="Confirm");
    assert(!std::filesystem::exists(CommandPath)); // no automatic retry
    nexus_eject::Snapshot refused{};refused.boot=123;refused.ack=r.nonce;refused.ack_result=0x402;
    std::ofstream(BlockedPath)<<"NX1 safe eject admission v1\nboot=123 request="<<r.nonce
        <<" uptime_ms=5000 requested_generation=7\ndecision=refused check=open_usb_handles result=0x00000402 query_result=0x0\n";
    assert(std::string(BlockReason(refused))=="A USB file or folder is still open.");
    refused.ack++;assert(!BlockReason(refused));refused.ack--;refused.boot++;assert(!BlockReason(refused));
    refused.boot--;refused.ack_result++;assert(!BlockReason(refused));
    delete frame;
    std::cout<<"PASS: actual overlay callbacks, confirmation/cancel, atomic firmware request, busy/refused/flush/wait/safe/stale/reconnect flow\n";
}
