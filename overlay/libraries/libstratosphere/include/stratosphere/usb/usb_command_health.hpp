/* Copyright (c) Nexus contributors. SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <atomic>
#include <cstdint>
namespace ams::usb::command_health {
enum class Phase : uint32_t { Startup=1, Wake=2, Live=3 };
inline std::atomic<bool> media_ready{false};
// Count completed command outcomes. SCSI NOT READY during discovery is not a
// runtime read/write failure. Classification never changes retries or timing.
inline std::atomic<uint64_t> startup_ready{0}, startup_other{0}, wake{0};
inline std::atomic<uint64_t> live_read{0}, live_write{0}, live_control{0};
inline std::atomic<uint64_t> first_live_command{0}, first_live_ns{0};
inline bool IsRead(uint8_t op){return op==0x28 || op==0x88;}
inline bool IsWrite(uint8_t op){return op==0x2a || op==0x8a;}
inline void Record(Phase phase,uint8_t op,uint32_t result,uint64_t now){
 if(!result)return;
 if(phase==Phase::Startup){
   if(op==0 && result==(140u|(214u<<9)))++startup_ready;
   else ++startup_other;
 }else if(phase==Phase::Wake)++wake;
 else {
   if(IsRead(op))++live_read;else if(IsWrite(op))++live_write;else ++live_control;
   // Command execution is serialized by the storage lock. Publish the packed
   // result last so the status thread never sees a partial first-event record.
   if(!first_live_command.load(std::memory_order_acquire)){
     first_live_ns.store(now,std::memory_order_relaxed);
     first_live_command.store((uint64_t(op)<<32)|result,std::memory_order_release);
   }
 }
}
}
