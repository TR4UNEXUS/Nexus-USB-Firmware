// SPDX-License-Identifier: GPL-2.0-or-later
#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include "storage_status.hpp"
#include "eject_view.hpp"
#include "drive_panel.hpp"
#include "nexus_ui.hpp"

namespace {
constexpr const char *EjectStatusPath="sdmc:/atmosphere/logs/usb_eject_status.txt";
constexpr const char *BlockedPath="sdmc:/atmosphere/logs/usb_eject_last_blocked.txt";
constexpr const char *CommandPath="sdmc:/atmosphere/logs/usb_eject_command.bin";
constexpr const char *CommandTempPath="sdmc:/atmosphere/logs/usb_eject_command.ovl.tmp";
uint64_t Now() { return armTicksToNs(armGetSystemTick())/1000000; }

bool ReadEject(nexus_eject::Snapshot& out) {
    FILE *f=std::fopen(EjectStatusPath,"rb");
    if(!f) return false;
    char data[512]{};
    const auto size=std::fread(data,1,sizeof(data),f);
    const bool ok=!std::ferror(f);
    std::fclose(f);
    return ok && nexus_eject::Parse(data,size,out);
}
const char *BlockReason(const nexus_eject::Snapshot &status) {
    // A report can survive a reboot or another attempt. Only show a reason
    // associated with the refusal that the firmware just acknowledged.
    FILE *file=std::fopen(BlockedPath,"rb");
    if(!file)return nullptr;
    char header[64]{},identity[192]{},decision[256]{},expected[96]{},check[64]{};
    const bool read=std::fgets(header,sizeof(header),file) && std::fgets(identity,sizeof(identity),file)
        && std::fgets(decision,sizeof(decision),file);
    std::fclose(file);
    std::snprintf(expected,sizeof(expected),"boot=%llu request=%llu ",static_cast<unsigned long long>(status.boot),
                  static_cast<unsigned long long>(status.ack));
    unsigned int result{};
    if(!read || std::strcmp(header,"NX1 safe eject admission v1\n") ||
       std::strncmp(identity,expected,std::strlen(expected)) ||
       std::sscanf(decision,"decision=refused check=%63s result=0x%x",check,&result)!=2 ||
       !result || result!=status.ack_result)return nullptr;
    if(!std::strcmp(check,"loader_binding_process_alive"))return "Close the running USB game.";
    if(!std::strcmp(check,"application_still_open"))return "Close the open game or application.";
    if(!std::strcmp(check,"album_process_does_not_match_cardsnx"))return "Close Album or its homebrew app.";
    if(!std::strcmp(check,"volume_io_lock_held"))return "USB is reading or writing. Try again.";
    if(!std::strcmp(check,"open_usb_handles"))return "A USB file or folder is still open.";
    if(!std::strcmp(check,"media_generation_mismatch"))return "The connected drive has changed.";
    if(!std::strncmp(check,"album_",6) || !std::strcmp(check,"application_process_query_failed") ||
       !std::strcmp(check,"loader_binding_process_query_failed"))return "Could not verify the running app.";
    return nullptr;
}
bool WriteFile(const char *path,const char *temp,const void *data,size_t size) {
    // Never replace another pending request or expose a partial record.
    if(access(path,F_OK)==0) return false;
    std::remove(temp);
    const int fd=open(temp,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0) return false;
    const bool wrote=write(fd,data,size)==static_cast<ssize_t>(size);
    const int sync=fsync(fd),closed=close(fd);
    if(!wrote || sync || closed || access(path,F_OK)==0 || std::rename(temp,path)!=0) {
        std::remove(temp);return false;
    }
    return true;
}
using nexus_ui::Text;

}

class UsbContentGui final : public tsl::Gui {
    storage_status::StatusData storage{};
    storage_status::StatusData drive_details{};
    bool have_details{}, storage_read_ok{};
    eject_view::Status eject;
    tsl::elm::ListItem *action{},*route{};
    uint64_t last_poll{},confirm_until{},confirm_boot{},confirm_generation{};
    uint64_t pending{},pending_boot{},pending_since{},pending_sample{};
    uint32_t pending_action{};
    bool route_pending{},route_usb{};
    uint64_t route_since{};
    std::string heading="Checking USB drive";
    std::string hint1="Please wait for the drive status.",hint2;

    void Hint(const char *a,const char *b="") {hint1=a;hint2=b;}
    void Poll() {
        const auto now=Now();last_poll=now;
        const bool was_fresh=eject.Fresh(now);
        nexus_eject::Snapshot next{};
        if(ReadEject(next)) eject.Observe(next,now);else eject.Missing();
        if(!was_fresh && eject.Fresh(now))Hint("Close games and transfers before ejecting.");
        storage_status::StatusData info{};
        storage_read_ok=storage_status::ReadStatus(&info);
        if(storage_read_ok) {
            storage=info;
            // Keep the drive panel independent of the eject heartbeat. Also
            // retain the last reported details while eject/reconnect completes.
            if(info.ready || (!have_details && info.total_space)) {
                drive_details=info;have_details=true;
            }
        }
        if(pending && eject.Fresh(now)) {
            if(eject.value.boot!=pending_boot) {
                pending=0;Hint("USB restarted. Check the drive again.");
            } else if(eject.value.ack==pending) {
                if(eject.value.ack_result) {
                    const char *why=pending_action==nexus_eject::Eject ? BlockReason(eject.value) : nullptr;
                    pending=0;Hint(why ? why : "The USB request was refused.","Keep USB connected and try again.");
                } else if((pending_action==nexus_eject::Eject && (eject.Safe(now) || eject.value.phase==4)) ||
                          (pending_action==nexus_eject::Resume && eject.value.phase==0)) pending=0;
            } else if((eject.value.phase==0 || eject.value.phase==3) &&
                      eject.value.sample_ms>pending_sample && eject.value.sample_ms-pending_sample>6000 &&
                      access(CommandPath,F_OK)!=0 && errno==ENOENT) {
                // A live firmware sample is past the request's validity window
                // and its command file is gone. A retry requires a fresh press.
                pending=0;Hint("The request expired.","Keep USB connected and try again.");
            }
        }
        if(route_pending && eject.Fresh(now)) {
            if(storage.has_install_route && storage.install_route==route_usb) {
                route_pending=false;Hint("Install target updated.");
            } else if(now-route_since>6000) {
                route_pending=false;Hint("Install target was not confirmed.","Check the drive and try again.");
            }
        }
        Refresh(now);
    }
    void Refresh(uint64_t now) {
        action->setText("Safe eject");action->setValue("");
        route->setValue(route_pending?"Waiting...":(storage.install_route?"USB drive":"microSD"));
        if(!eject.Fresh(now)) {
            heading="Checking USB drive";action->setValue("Waiting");
            Hint("Safe eject is waiting for confirmation.","Keep the drive connected.");return;
        }
        if(eject.Safe(now)) {
            heading="Safe to unplug";action->setText("Reconnect USB");
            Hint("You can now unplug the USB drive.","To use it again, choose Reconnect USB.");
        } else if(eject.value.phase==4) {
            heading="Could not safely eject";action->setValue("Stopped");
            Hint("Keep the USB drive connected.","Power off fully before unplugging.");
        } else if(eject.value.phase==1 || eject.value.phase==2) {
            heading="Safely ejecting USB";action->setValue("Please wait");
            Hint("Finishing writes and releasing USB.","Keep the drive connected.");
        } else if(eject.value.ready) {
            heading="USB connected";
            if(eject.value.handles)action->setValue("Check use");
        } else {
            heading="USB not ready";action->setValue("Unavailable");
        }
        if(pending) {
            action->setValue("Please wait");
            if(pending_action==nexus_eject::Resume) {
                heading="Reconnecting USB";Hint("Keep the drive connected.");
            } else if(now-pending_since>6000)Hint("Still waiting for confirmation.","Keep the drive connected.");
        } else if(confirm_until && now<confirm_until && eject.CanEject(now) &&
                  confirm_boot==eject.value.boot && confirm_generation==eject.value.generation) {
            action->setText("Eject USB now?");action->setValue("Confirm");
            Hint("Press A to safely eject the USB drive.","Press B to cancel.");
        } else confirm_until=0;
    }
    bool Activate(u64 keys) {
        if(!(keys&HidNpadButton_A))return false;
        Poll();const auto now=Now();
        if(pending || route_pending)return true;
        uint32_t operation=nexus_eject::Eject;
        if(eject.CanResume(now))operation=nexus_eject::Resume;
        else if(!eject.CanEject(now)) {
            Hint("Close games, apps and transfers first.","Wait until USB is ready, then retry.");return true;
        } else if(!confirm_until || now>=confirm_until) {
            confirm_until=now+10000;confirm_boot=eject.value.boot;confirm_generation=eject.value.generation;
            Refresh(now);return true;
        }
        confirm_until=0;
        const auto nonce=static_cast<uint64_t>(armGetSystemTick())|1ULL;
        const nexus_eject::Request request{nexus_eject::Magic,eject.value.boot,eject.value.sample_ms,
                                           eject.value.generation,nonce,operation,0};
        if(!WriteFile(CommandPath,CommandTempPath,&request,sizeof(request))) {
            Hint("Could not send the request.","Keep USB connected and try again.");return true;
        }
        pending=nonce;pending_boot=eject.value.boot;pending_since=now;pending_sample=request.sample_ms;pending_action=operation;
        Hint("Waiting for USB to finish safely.","Keep the drive connected.");Refresh(now);return true;
    }
public:
    tsl::elm::Element *createUI() override {
        auto *list=new tsl::elm::List();
        auto *info=new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *r,s32 x,s32 y,s32 w,s32) {
            auto display=drive_panel::Describe(storage,drive_details,have_details,
                                               storage_read_ok && storage_status::IsRecent(storage));
            if(eject.Fresh(Now()) && (eject.value.phase!=0 || pending)) {
                display.status=heading;
                display.access="Last reported drive";
            }
            nexus_ui::Drive(r,x,y,w,display);
        });list->addItem(info,264);
        route=new nexus_ui::MenuItem(nexus_ui::Action::Route,"Install route","microSD");
        route->setClickListener([this](u64 keys) {
            if(!(keys&HidNpadButton_A))return false;
            Poll();const auto now=Now();
            if(pending || route_pending || !eject.Fresh(now) || eject.value.phase!=0)return true;
            const bool usb=!storage.install_route;
            if(usb && (!eject.value.ready || !storage.ready || !storage.writable)) {
                Hint("USB is not ready for installs.");return true;
            }
            confirm_until=0;
            const char *data=usb?"usb\n":"sd\n";
            if(WriteFile(storage_status::RouteCommandPath,storage_status::RouteCommandTempPath,data,std::strlen(data))) {
                route_pending=true;route_usb=usb;route_since=now;Hint("Updating install target...");
            } else Hint("Could not change the install target.");
            Refresh(now);return true;
        });list->addItem(route);
        action=new nexus_ui::MenuItem(nexus_ui::Action::Eject,"Safe eject","Waiting");
        action->setClickListener([this](u64 keys){return Activate(keys);});list->addItem(action);
        auto *refresh=new nexus_ui::MenuItem(nexus_ui::Action::Refresh,"Refresh status");
        refresh->setClickListener([this](u64 keys) {
            if(!(keys&HidNpadButton_A))return false;
            Poll();return true;
        });list->addItem(refresh);
        auto *help=new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *r,s32 x,s32 y,s32 w,s32) {
            Text(r,hint1,x+4,y+20,w-8,13,nexus_ui::Secondary);
            Text(r,hint2,x+4,y+40,w-8,13,nexus_ui::Secondary);
        });list->addItem(help,48);
        auto *frame=new nexus_ui::Frame();
        frame->setContent(list);
        Hint("Close games and transfers before ejecting.");Poll();return frame;
    }
    void update() override {const auto now=Now();if(now<last_poll || now-last_poll>=300)Poll();}
    bool handleInput(u64 keysDown,u64,const HidTouchState&,HidAnalogStickState,HidAnalogStickState) override {
        if((keysDown&HidNpadButton_B) && confirm_until) {
            confirm_until=0;Hint("Eject cancelled. USB is still connected.");Refresh(Now());return true;
        }return false;
    }
};
class UsbContentOverlay final : public tsl::Overlay {
public:
    void initServices() override {}
    void exitServices() override {}
    std::unique_ptr<tsl::Gui> loadInitialGui() override {return initially<UsbContentGui>();}
};
int main(int argc,char **argv) {return tsl::loop<UsbContentOverlay>(argc,argv);}
