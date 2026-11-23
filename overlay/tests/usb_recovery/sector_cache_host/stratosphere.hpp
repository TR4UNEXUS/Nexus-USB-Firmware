/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
using u8=uint8_t;using u32=uint32_t;using u64=uint64_t;
namespace ams {
class Result { uint32_t value;public:constexpr Result(uint32_t v=0):value(v){} constexpr uint32_t GetValue()const{return value;} };
constexpr Result ResultSuccess(){return {};}
namespace os { using SdkMutex=std::mutex; }
namespace usb {
inline Result ResultAlreadyInitialized(){return Result(1);}
inline Result ResultNotInitialized(){return Result(2);}
inline Result ResultUnsupportedFilesystem(){return Result(3);}
inline Result ResultInvalidParameter(){return Result(4);}
namespace mass_storage {
struct Geometry {u32 block_size;u64 block_count;};
class Device {
public:
    Geometry geometry{};bool initialized{true};
    bool IsInitialized()const{return initialized;}
    const Geometry &GetGeometry()const{return geometry;}
    Result ReadBlocks(u64,u32,void*,size_t);
    Result WriteBlocks(u64,u32,const void*,size_t);
};
}
namespace volume {struct Partition {u64 first_lba;u64 sector_count;};}
}
}
#define R_SUCCEEDED(rc) ((rc).GetValue()==0)
#define R_FAILED(rc) (!R_SUCCEEDED(rc))
#define R_UNLESS(test,rc) do{if(!(test))return (rc);}while(0)
#define R_SUCCEED() return ams::ResultSuccess()
