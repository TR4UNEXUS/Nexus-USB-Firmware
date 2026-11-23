namespace ams::ncm {Result CreateUsbRoutingBackup(){return CheckpointUsbRoutingBackup();}}
using namespace ams;using namespace ams::ncm;
static unsigned scenarios;
static void Reset(){fake::Reset();diagnostic={0x3344584e,1,0,0,0xffffffff,0,0,0,0};last_result=0;
 fake::saved[Marker]={reinterpret_cast<const u8*>(Owner),reinterpret_cast<const u8*>(Owner)+sizeof(Owner)};
 for(unsigned i=0;i<3;++i){u32 header[]{u32(i==0?0x54425355:i==1?0x43425355:0x44425355),1,1,0};auto&v=fake::sd[Paths[i]];v.resize(16+(i==1?40:16),1);std::memcpy(v.data(),header,16);}}
static void Good(){assert(R_SUCCEEDED(CheckpointUsbRoutingBackup()));assert(!fake::mounted&&fake::handles.empty());assert(diagnostic.stage==0);++scenarios;}
static void Bad(){assert(R_FAILED(CheckpointUsbRoutingBackup()));assert(!fake::mounted&&fake::handles.empty());assert(diagnostic.stage&&diagnostic.result);++scenarios;}
static void ValidSaved(){UsbRoutingBackupStatus status{};assert(R_SUCCEEDED(QueryUsbRoutingBackup(&status)));assert(status.flags==1);}
int main(){
 Reset();Good();ValidSaved();auto original=fake::saved;int commits=fake::commits;Good();assert(fake::saved==original&&fake::commits==commits);
 fake::sd[Paths[0]].back()=2;Good();assert(fake::saved.at(Archives[0])==original.at(Archives[0]));ValidSaved();
 auto two=fake::saved;fake::saved[Archives[1]][0]^=1;auto rejected=fake::saved;Good();assert(fake::saved.at(Archives[0])==original.at(Archives[0]));ValidSaved();
 Reset();fake::saved[Archives[0]]={1,2,3};fake::saved[Archives[1]]={4,5};auto broken=fake::saved;Good();assert(fake::saved.at("#nx-usb-backup:/maps-a.rejected.bin")==broken.at(Archives[0]));assert(fake::saved.at(Archives[1])==broken.at(Archives[1]));ValidSaved();
 // Inject one I/O failure at every production filesystem boundary, including
 // write, read-back, source recheck, commit and post-commit verification.
 int faults=0;
 for(int mode=0;mode<2;++mode){Reset();if(mode)fake::saved=broken;else {Good();fake::sd[Paths[0]].back()=2;}
  const auto base=fake::saved,source=fake::sd;fake::calls=0;Good();const int count=fake::calls;
  for(int n=1;n<=count;++n){Reset();fake::saved=base;fake::sd=source;fake::fail_at=n;const auto rc=CheckpointUsbRoutingBackup();fake::fail_at=0;
   assert(!fake::mounted&&fake::handles.empty());
   if(R_FAILED(rc))assert(diagnostic.result==rc.GetValue()&&diagnostic.stage);
   if(!fake::commits)assert(fake::saved==base);else ValidSaved();
   if(mode){assert(fake::saved.at(Archives[1])==base.at(Archives[1]));if(fake::commits)assert(fake::saved.at("#nx-usb-backup:/maps-a.rejected.bin")==base.at(Archives[0]));}
   else {assert(fake::saved.at(Archives[0])==base.at(Archives[0]));}
   ++faults;
  }
 }
 Reset();fake::saved=broken;fake::saved["#nx-usb-backup:/maps-a.rejected.bin"]={7};auto retained=fake::saved;Bad();assert(fake::saved==retained&&diagnostic.stage==7);
 Reset();fake::saved=two;fake::sd[Paths[0]].back()=3;fake::fail_path=Archives[1];retained=fake::saved;Bad();assert(fake::saved==retained&&diagnostic.result==0x456);
 Reset();fake::sd[Pending]={0};retained=fake::saved;Bad();assert(fake::saved==retained&&diagnostic.stage==1);
 Reset();fake::sd[Paths[1]][0]^=1;Bad();assert(diagnostic.stage==3&&diagnostic.index==1&&!fake::commits);
 Reset();fake::saved[Marker][0]^=1;retained=fake::saved;Bad();assert(diagnostic.stage==5&&fake::saved==retained);
 Reset();fake::saved.erase(Marker);fake::owner.owner_id=9;retained=fake::saved;Bad();assert(diagnostic.stage==5&&fake::saved==retained);
 Reset();fake::saved.erase(Marker);Good();ValidSaved(); // owned, empty interrupted creation
 Reset();fake::saved.erase(Marker);fake::saved["#nx-usb-backup:/foreign.bin"]={2};retained=fake::saved;Bad();assert(fake::saved==retained);
 Reset();retained=fake::saved;fake::on_flush=[](){fake::sd[Paths[0]].back()=5;};Bad();assert(diagnostic.stage==10&&fake::saved==retained&&!fake::commits);
 for(u32 stage:{13u,14u,15u}){Reset();RecordUsbRoutingBackupPreflightFailure(stage,Result(0x456));UsbRoutingBackupDiagnostic info{};assert(R_SUCCEEDED(QueryUsbRoutingBackupDiagnostic(&info)));assert(info.stage==stage&&info.result==0x456&&info.attempts==1&&!fake::commits);++scenarios;}
 std::printf("PASS: %u backup scenarios, %d injected filesystem failures; actual production transaction, archive fallback, ownership and ABI checks.\n",scenarios,faults);
}
