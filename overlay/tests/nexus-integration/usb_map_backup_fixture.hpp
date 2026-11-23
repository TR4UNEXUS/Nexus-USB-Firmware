// SPDX-License-Identifier: GPL-2.0-only
// Transactional fake filesystem; tests compile the complete production backup.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <functional>
#include <openssl/sha.h>
using u8=uint8_t;using u32=uint32_t;using u64=uint64_t;using s64=int64_t;
constexpr u64 operator""_MB(unsigned long long n){return n*1024*1024;}
namespace ams {struct Result {u32 value{};constexpr Result(u32 v=0):value(v){}constexpr u32 GetValue()const{return value;}};
constexpr Result ResultSuccess(){return 0;}
struct TimeSpan {static int FromSeconds(int n){return n;}};
namespace os{using SdkMutex=std::mutex;inline void SleepThread(int){}}
namespace crypto{inline void GenerateSha256(void*out,size_t n,const void*in,size_t size){assert(n==32);SHA256(static_cast<const u8*>(in),size,static_cast<u8*>(out));}
struct Sha256Generator{SHA256_CTX ctx;void Initialize(){SHA256_Init(&ctx);}void Update(const void*p,size_t n){SHA256_Update(&ctx,p,n);}void GetHash(void*p,size_t n){assert(n==32);SHA256_Final(static_cast<u8*>(p),&ctx);}};}
#define ERR(name,code) struct name:Result{constexpr name():Result(code){}static bool Includes(Result r){return r.value==code;}};
namespace ncm{ERR(ResultInvalidOperation,0x3fe805)}
namespace fs{
ERR(ResultPathNotFound,0x202) ERR(ResultTargetNotFound,0x1202) ERR(ResultInvalidArgument,0x2402)
using FileHandle=int;using DirectoryHandle=int;
struct SystemSaveDataId{u64 id;constexpr operator u64()const{return id;}};struct SaveDataId{u64 id;};
enum class SaveDataSpaceId{System};enum DirectoryEntryType{File};
enum{OpenMode_Read=1,OpenMode_Write=2,OpenDirectoryMode_All=3};enum class WriteOption{None,Flush};
struct SaveDataExtraData{u64 owner_id=0x0100000000000002ULL,available_size=16_MB,journal_size=16_MB;};
}}
using ams::Result;
#define R_SUCCEEDED(r) ((r).GetValue()==0)
#define R_FAILED(r) (!R_SUCCEEDED(r))
#define R_UNLESS(c,r) do{if(!(c))return (r);}while(0)
#define R_TRY(x) do{const Result rr=(x);if(R_FAILED(rr))return rr;}while(0)
#define R_RETURN(x) return (x)
#define R_SUCCEED() return ams::ResultSuccess()
#define AMS_UNUSED(x) (void)(x)
template<class F>struct Scope{F f;~Scope(){f();}};
struct AtExit{};template<class F>Scope<F>operator+(AtExit,F f){return {f};}
#define JOIN2(a,b) a##b
#define JOIN(a,b) JOIN2(a,b)
#define ON_SCOPE_EXIT auto JOIN(scope_,__LINE__)=AtExit{}+[&]()
namespace fake{
using Files=std::map<std::string,std::vector<u8>>;
inline Files saved,working,sd;inline std::map<int,std::string> handles;
inline bool mounted{},exists=true;inline int next_handle{},calls{},fail_at{},commits{};
inline std::string fail_path;inline ams::fs::SaveDataExtraData owner;
inline std::function<void()> on_commit,on_flush;
Result Event(const std::string&path=""){++calls;if((fail_at&&calls==fail_at)||(!fail_path.empty()&&path==fail_path))return 0x456;return 0;}
Files& Map(const std::string&p){return p.starts_with("#ncm-usb-map:")?sd:working;}
void Reset(){saved.clear();working.clear();sd.clear();handles.clear();mounted=false;exists=true;next_handle=calls=fail_at=commits=0;fail_path.clear();owner={};on_commit={};on_flush={};}
}
namespace ams::fs{
Result MountSystemSaveData(const char*,SaveDataSpaceId,SystemSaveDataId){R_TRY(fake::Event());R_UNLESS(fake::exists,ResultTargetNotFound());assert(!fake::mounted);fake::mounted=true;fake::working=fake::saved;return 0;}
Result CreateSystemSaveData(SaveDataSpaceId,SystemSaveDataId,u64,u64,u64,int){R_TRY(fake::Event());fake::exists=true;return 0;}
void Unmount(const char*){assert(fake::mounted);fake::mounted=false;fake::working.clear();assert(fake::handles.empty());}
Result GetEntryType(DirectoryEntryType*out,const char*p){R_TRY(fake::Event(p));R_UNLESS(fake::Map(p).contains(p),ResultPathNotFound());*out=File;return 0;}
Result OpenFile(FileHandle*out,const char*p,int){R_TRY(fake::Event(p));R_UNLESS(fake::Map(p).contains(p),ResultPathNotFound());*out=++fake::next_handle;fake::handles[*out]=p;return 0;}
void CloseFile(FileHandle f){assert(fake::handles.erase(f)==1);}
Result GetFileSize(s64*out,FileHandle f){const auto&p=fake::handles.at(f);R_TRY(fake::Event(p));*out=fake::Map(p).at(p).size();return 0;}
Result ReadFile(size_t*out,FileHandle f,u64 offset,void*buf,size_t n){const auto&p=fake::handles.at(f);R_TRY(fake::Event(p));auto&v=fake::Map(p).at(p);R_UNLESS(offset<=v.size(),ResultInvalidArgument());*out=std::min(n,v.size()-size_t(offset));std::memcpy(buf,v.data()+offset,*out);return 0;}
Result WriteFile(FileHandle f,u64 offset,const void*buf,size_t n,WriteOption){const auto&p=fake::handles.at(f);R_TRY(fake::Event(p));auto&v=fake::Map(p).at(p);R_UNLESS(offset<=v.size()&&n<=v.size()-offset,ResultInvalidArgument());std::memcpy(v.data()+offset,buf,n);return 0;}
Result FlushFile(FileHandle f){R_TRY(fake::Event(fake::handles.at(f)));if(fake::on_flush)fake::on_flush();return 0;}
Result DeleteFile(const char*p){R_TRY(fake::Event(p));R_UNLESS(fake::Map(p).erase(p),ResultPathNotFound());return 0;}
Result CreateFile(const char*p,u64 n){R_TRY(fake::Event(p));R_UNLESS(!fake::Map(p).contains(p),ResultInvalidArgument());fake::Map(p)[p].resize(n);return 0;}
Result RenameFile(const char*a,const char*b){R_TRY(fake::Event(a));auto&m=fake::Map(a);R_UNLESS(m.contains(a)&&!m.contains(b),ResultInvalidArgument());m[b]=m.at(a);m.erase(a);return 0;}
Result CommitSaveData(const char*){R_TRY(fake::Event());if(fake::on_commit)fake::on_commit();fake::saved=fake::working;++fake::commits;return 0;}
Result OpenDirectory(DirectoryHandle*out,const char*,int){R_TRY(fake::Event());*out=1;return 0;}
Result GetDirectoryEntryCount(s64*out,DirectoryHandle){R_TRY(fake::Event());*out=fake::working.size();return 0;}
void CloseDirectory(DirectoryHandle){}
namespace impl{Result ReadSaveDataFileSystemExtraData(SaveDataExtraData*out,SaveDataSpaceId,SaveDataId){R_TRY(fake::Event());*out=fake::owner;return 0;}}
}
enum{FsSaveDataSpaceId_System};struct FsSaveDataInfoReader{bool read{};};struct FsSaveDataInfo{u64 system_save_data_id=0x80584E5553420001ULL,save_data_id=1;};
Result fsOpenSaveDataInfoReader(FsSaveDataInfoReader*,int){return fake::Event();}
void fsSaveDataInfoReaderClose(FsSaveDataInfoReader*){}
Result fsSaveDataInfoReaderRead(FsSaveDataInfoReader*r,FsSaveDataInfo*out,int,s64*count){R_TRY(fake::Event());*count=r->read?0:1;r->read=true;*out={};return 0;}
