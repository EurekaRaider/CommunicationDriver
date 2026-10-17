#include "core.hpp"
#include <random>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <fstream>

namespace cd {
static std::string base64(const unsigned char* data,size_t size) {
 static const char digits[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 std::string out;unsigned value=0,bits=0;
 for(size_t i=0;i<size;++i) {value=(value<<8)|data[i];bits+=8;while(bits>=6) {bits-=6;out+=digits[(value>>bits)&63];}}
 if(bits) out+=digits[(value<<(6-bits))&63];return out;
}
struct SshBackend : Backend {
 asio::ip::tcp::socket socket;asio::ip::tcp::resolver resolver;
 LIBSSH2_SESSION* session=nullptr;LIBSSH2_CHANNEL* channel=nullptr;LIBSSH2_CHANNEL* stdio_channel=nullptr;LIBSSH2_SFTP* sftp=nullptr;LIBSSH2_SFTP_HANDLE* file=nullptr;
 bool stopped=false,waiting=false,write_eof=false,command_active=false;int phase=0,channel_phase=0,transfer_phase=0;
 bool stdio_started=false;
 uint64_t wait_revision=0;int wait_directions=0;
 Completion open_callback,command_callback,write_callback,transfer_callback;
 std::string command_text,write_data,source,destination;size_t write_offset=0;
 bool upload=false,overwrite=true;uint64_t transferred=0;
 std::function<void(uint64_t)> progress;
 std::shared_ptr<std::fstream> local;bool disk_busy=false;std::array<char,32768> file_buffer{};size_t file_size=0,file_offset=0;
 SshBackend(std::shared_ptr<Runtime> r,Config c):Backend(r,std::move(c)),socket(r->state->io),resolver(r->state->io) {
  static const int initialized=libssh2_init(0);if(initialized) throw Fault(CD_INTERNAL_ERROR,"libssh2 initialization failed",initialized);
 }
 ~SshBackend() override {close();}
 std::string session_shell() {
  if(!config.session_command.empty())return config.session_command;
  if(config.shell==CD_SHELL_CMD){std::random_device random;prompt_token="__CD_PROMPT_"+std::to_string(random())+std::to_string(random())+"__";return "cmd.exe /D /Q /K \"prompt "+prompt_token+"\"";}
  if(config.shell==CD_SHELL_PWSH||config.shell==CD_SHELL_WINDOWS_POWERSHELL)return std::string(config.shell==CD_SHELL_PWSH?"pwsh":"powershell.exe")+" -NoLogo -NoProfile -NonInteractive -Command -";
  return config.shell==CD_SHELL_BASH?"bash":config.shell==CD_SHELL_ZSH?"zsh":"sh";
 }
 std::string one_shot_command(const std::string& command) {
  if(config.shell==CD_SHELL_DEFAULT)return command;
  if(config.shell==CD_SHELL_CMD)return "cmd.exe /D /S /C \""+command+"\"";
  if(config.shell==CD_SHELL_PWSH||config.shell==CD_SHELL_WINDOWS_POWERSHELL){auto text=decode(command,CD_ENCODING_UTF8);std::string wide;for(size_t i=0;i<text.size();){unsigned char first=text[i++];uint32_t scalar=first;int remaining=first<0x80?0:first<0xe0?1:first<0xf0?2:3;if(remaining)scalar=first&((1<<(6-remaining))-1);while(remaining--)scalar=(scalar<<6)|(static_cast<unsigned char>(text[i++])&63);auto unit=[&](uint16_t value){wide+=char(value&255);wide+=char(value>>8);};if(scalar>65535){scalar-=65536;unit(0xd800+(scalar>>10));unit(0xdc00+(scalar&1023));}else unit(static_cast<uint16_t>(scalar));}return std::string(config.shell==CD_SHELL_PWSH?"pwsh":"powershell.exe")+" -NoLogo -NoProfile -NonInteractive -EncodedCommand "+base64(reinterpret_cast<const unsigned char*>(wide.data()),wide.size());}
  std::string quoted="'";for(char c:command)quoted+=c=='\''?"'\\''":std::string(1,c);quoted+="'";return session_shell()+" -c "+quoted;
 }
 std::string last_error() {char* message=nullptr;int len=0;if(session) libssh2_session_last_error(session,&message,&len,0);return message?std::string(message,len):"SSH error";}
 void report(cd_status_t status,int rc) {
  auto message=last_error();if(open_callback) {auto cb=std::exchange(open_callback,{});cb(status,message,rc);}
  else if(transfer_callback) {auto cb=std::exchange(transfer_callback,{});cb(status,message,rc);}
  else if(command_callback) {auto cb=std::exchange(command_callback,{});cb(status,message,rc);}
  else if(write_callback) {auto cb=std::exchange(write_callback,{});cb(status,message,rc);}
  else if(failure) failure(status,message,rc);
 }
 void wait_socket() {
  if(stopped)return;int dirs=libssh2_session_block_directions(session);if(!dirs)dirs=LIBSSH2_SESSION_BLOCK_INBOUND;
  if(waiting&&wait_directions==dirs)return;waiting=true;wait_directions=dirs;auto revision=++wait_revision;ErrorCode ignored;socket.cancel(ignored);
  auto self=std::static_pointer_cast<SshBackend>(shared_from_this());
  auto ready=[self,revision](ErrorCode ec){if(self->stopped||revision!=self->wait_revision)return;++self->wait_revision;self->waiting=false;ErrorCode ignored;self->socket.cancel(ignored);if(ec)self->report(CD_DISCONNECTED,ec.value());else self->drive();};
  if(dirs&LIBSSH2_SESSION_BLOCK_INBOUND)socket.async_wait(asio::ip::tcp::socket::wait_read,ready);
  if(dirs&LIBSSH2_SESSION_BLOCK_OUTBOUND)socket.async_wait(asio::ip::tcp::socket::wait_write,ready);
 }
 bool retry(int rc,cd_status_t status=CD_IO_ERROR) {if(rc==LIBSSH2_ERROR_EAGAIN) {wait_socket();return true;}if(rc<0) {report(status,rc);return true;}return false;}
 void open(Completion cb) override {
  open_callback=std::move(cb);auto self=std::static_pointer_cast<SshBackend>(shared_from_this());
  resolver.async_resolve(config.host,std::to_string(config.port),[self](ErrorCode ec,asio::ip::tcp::resolver::results_type eps){
   if(self->stopped)return;
   if(ec) {auto callback=std::exchange(self->open_callback,{});callback(CD_CONNECTION_FAILED,ec.message(),ec.value());return;}
   asio::async_connect(self->socket,eps,[self](ErrorCode e,const auto&){if(self->stopped)return;if(e) {auto callback=std::exchange(self->open_callback,{});callback(CD_CONNECTION_FAILED,e.message(),e.value());return;}self->socket.non_blocking(true);self->session=libssh2_session_init();if(!self->session) {self->report(CD_INTERNAL_ERROR,LIBSSH2_ERROR_ALLOC);return;}libssh2_session_set_blocking(self->session,0);self->phase=1;self->drive();});
  });
 }
 bool verify_key() {
  if(config.key_kind==CD_HOST_KEY_SHA256) {
   auto hash=reinterpret_cast<const unsigned char*>(libssh2_hostkey_hash(session,LIBSSH2_HOSTKEY_HASH_SHA256));
   auto expected=config.host_key;if(expected.starts_with("SHA256:")) expected.erase(0,7);while(!expected.empty()&&expected.back()=='=') expected.pop_back();
   return hash&&base64(hash,32)==expected;
  }
  auto known=libssh2_knownhost_init(session);if(!known) return false;
  bool accepted=false;size_t length=0;int type=0;auto key=libssh2_session_hostkey(session,&length,&type);
  if(key&&libssh2_knownhost_readfile(known,config.host_key.c_str(),LIBSSH2_KNOWNHOST_FILE_OPENSSH)>=0) {
   int kind=type==LIBSSH2_HOSTKEY_TYPE_RSA?LIBSSH2_KNOWNHOST_KEY_SSHRSA:type==LIBSSH2_HOSTKEY_TYPE_DSS?LIBSSH2_KNOWNHOST_KEY_SSHDSS:type==LIBSSH2_HOSTKEY_TYPE_ECDSA_256?LIBSSH2_KNOWNHOST_KEY_ECDSA_256:type==LIBSSH2_HOSTKEY_TYPE_ECDSA_384?LIBSSH2_KNOWNHOST_KEY_ECDSA_384:type==LIBSSH2_HOSTKEY_TYPE_ECDSA_521?LIBSSH2_KNOWNHOST_KEY_ECDSA_521:LIBSSH2_KNOWNHOST_KEY_ED25519;
   libssh2_knownhost* host=nullptr;accepted=libssh2_knownhost_checkp(known,config.host.c_str(),config.port,key,length,LIBSSH2_KNOWNHOST_TYPE_PLAIN|LIBSSH2_KNOWNHOST_KEYENC_RAW|kind,&host)==LIBSSH2_KNOWNHOST_CHECK_MATCH;
  }
  libssh2_knownhost_free(known);return accepted;
 }
 void drive() {
  if(stopped||!session) return;
  if(phase==1) {int rc=libssh2_session_handshake(session,socket.native_handle());if(retry(rc,CD_CONNECTION_FAILED)) return;phase=2;}
  if(phase==2) {if(!verify_key()) {auto cb=std::exchange(open_callback,{});cb(CD_HOST_KEY_REJECTED,"SSH host key did not match configured trust",0);return;}phase=3;}
  if(phase==3) {
   int rc=config.auth==CD_AUTH_PASSWORD?libssh2_userauth_password_ex(session,config.user.c_str(),static_cast<unsigned>(config.user.size()),config.password.c_str(),static_cast<unsigned>(config.password.size()),nullptr):libssh2_userauth_publickey_fromfile_ex(session,config.user.c_str(),static_cast<unsigned>(config.user.size()),nullptr,config.private_key.c_str(),config.passphrase.empty()?nullptr:config.passphrase.c_str());
   if(retry(rc,CD_AUTHENTICATION_FAILED)) return;phase=4;
   if(config.mode==CD_ONE_SHOT) {auto cb=std::exchange(open_callback,{});cb(CD_OK,{},0);return;}
   stdio_started=true;command_text=session_shell();
   channel_phase=1;
  }
  if(channel_phase==1) {channel=libssh2_channel_open_session(session);if(!channel) {retry(libssh2_session_last_errno(session));return;}channel_phase=2;}
  if(channel_phase==2) {int rc=libssh2_channel_exec(channel,command_text.c_str());if(retry(rc)) return;channel_phase=3;if(open_callback) {if(!prompt_token.empty())prompt_ready=std::exchange(open_callback,{});else{auto cb=std::exchange(open_callback,{});cb(CD_OK,{},0);}}if(command_callback) {auto cb=std::exchange(command_callback,{});cb(CD_OK,{},0);}}
  if(transfer_callback) {drive_transfer();if(stopped||(transfer_callback&&waiting&&!disk_busy)) return;}
  if(stdio_channel){for(int stream=0;stream<2;++stream){char buffer[8192];for(int budget=0;budget<8;++budget){auto n=libssh2_channel_read_ex(stdio_channel,stream?SSH_EXTENDED_DATA_STDERR:0,buffer,sizeof(buffer));if(n<=0)break;std::string data(buffer,n);if(stream==0&&!filter_prompt(data))continue;if(!data.empty())output(stream,std::move(data),false);if(stopped)return;}}}
  if(!channel||channel_phase!=3) return;
  if(write_callback) {
   if(write_eof) {int rc=libssh2_channel_send_eof(channel);if(retry(rc)) return;write_eof=false;auto cb=std::exchange(write_callback,{});cb(CD_OK,{},0);}
   else {while(write_offset<write_data.size()) {auto n=libssh2_channel_write_ex(channel,0,write_data.data()+write_offset,write_data.size()-write_offset);if(retry(static_cast<int>(n))) return;if(n==0) {wait_socket();return;}write_offset+=n;}auto cb=std::exchange(write_callback,{});write_data.clear();cb(CD_OK,{},0);}
  }
  bool read_data=false;
  for(int stream=0;stream<2;++stream) {
   char buffer[8192];
   for(int budget=0;budget<8;++budget) {auto n=libssh2_channel_read_ex(channel,stream?SSH_EXTENDED_DATA_STDERR:0,buffer,sizeof(buffer));if(n>0) {read_data=true;std::string data(buffer,n);if(stream==0&&!command_active&&!filter_prompt(data))continue;if(!data.empty())(command_active?command_output:output)(stream,std::move(data),false);if(stopped)return;}else if(n==LIBSSH2_ERROR_EAGAIN||n==0) break;else {report(CD_DISCONNECTED,static_cast<int>(n));return;}}
  }
  if(libssh2_channel_eof(channel)) {
   int rc=libssh2_channel_close(channel);if(retry(rc)) return;
   int code=libssh2_channel_get_exit_status(channel);std::optional<int> confirmed_code;
#if LIBSSH2_VERSION_NUM >= 0x010c00
   if(libssh2_channel_has_exit_status(channel)) confirmed_code=code;
#else
   // In 1.11, zero also means absent status. Preserve uncertainty instead of inventing success.
   if(code!=0) confirmed_code=code;
#endif
   auto emit=command_active?command_output:output;auto ended=command_active?command_exited:exited;
   libssh2_channel_free(channel);channel=std::exchange(stdio_channel,nullptr);channel_phase=channel?3:0;command_active=false;
   emit(0,{},true);emit(1,{},true);ended(confirmed_code);if(channel&&!stopped){auto self=std::static_pointer_cast<SshBackend>(shared_from_this());asio::post(runtime->state->io,[self]{self->drive();});}return;
  }
  // Reading stderr can pull stdout into libssh2 after stdout was visited.
  // Drain that buffered data without waiting for another socket readiness event.
  unsigned long buffered=0,stdio_buffered=0;
  libssh2_channel_window_read_ex(channel,&buffered,nullptr);
  if(stdio_channel)libssh2_channel_window_read_ex(stdio_channel,&stdio_buffered,nullptr);
  bool buffered_read=(buffered||stdio_buffered)&&!(libssh2_session_block_directions(session)&LIBSSH2_SESSION_BLOCK_OUTBOUND);
  if(read_data||buffered_read) {auto self=std::static_pointer_cast<SshBackend>(shared_from_this());asio::post(runtime->state->io,[self]{self->drive();});}
  else wait_socket();
 }
 bool command_channels() const override {return config.mode==CD_ONE_SHOT;}
 void command(std::string command,Completion cb) override {if(command_active) {cb(CD_BUSY,"SSH channel still active",0);return;}stdio_channel=std::exchange(channel,nullptr);command_text=one_shot_command(command);command_callback=std::move(cb);command_active=true;channel_phase=1;drive();}
 void ensure_stdio(Completion cb) override {if(channel){cb(CD_OK,{},0);return;}if(stdio_started){cb(CD_END_OF_STREAM,"SSH stdio channel ended; close/open to recreate it",0);return;}stdio_started=true;command_text=session_shell();open_callback=std::move(cb);channel_phase=1;drive();}
 void write(std::string data,Completion cb) override {if(!channel) {cb(CD_NOT_OPEN,"SSH stdio channel unavailable",0);return;}write_data=std::move(data);write_offset=0;write_callback=std::move(cb);drive();}
 void close_input(Completion cb) override {if(!channel) {cb(CD_NOT_OPEN,"SSH stdio channel unavailable",0);return;}write_eof=true;write_callback=std::move(cb);drive();}
 void transfer(bool is_upload,std::string from,std::string to,bool replace,std::function<void(uint64_t)> on_progress,Completion cb) override {
  transfer_partial_file.clear();
  upload=is_upload;source=std::move(from);destination=std::move(to);overwrite=replace;progress=std::move(on_progress);transfer_callback=std::move(cb);transferred=0;transfer_phase=1;file_size=file_offset=0;
  try {
   if(upload&&!std::filesystem::is_regular_file(utf8_path(source)))throw Fault(CD_INVALID_ARGUMENT,"Source must be a single regular file");
   if(!upload&&std::filesystem::is_directory(utf8_path(destination)))throw Fault(CD_INVALID_ARGUMENT,"Destination must be a single file");
   if(!upload&&!overwrite&&std::filesystem::exists(utf8_path(destination))) throw Fault(CD_PERMISSION_DENIED,"Destination already exists");
   transfer_partial_file=destination;
   local=std::make_shared<std::fstream>();disk_busy=true;auto self=std::static_pointer_cast<SshBackend>(shared_from_this());auto stream=local;
   runtime->file_job([stream,is_upload=upload,replace=overwrite,path=utf8_path(upload?source:destination)]{stream->open(path,std::ios::binary|(is_upload?std::ios::in:std::ios::out|(replace?std::ios::trunc:std::ios::noreplace)));if(!*stream)throw Fault(CD_IO_ERROR,"Cannot open local file");},[self](std::exception_ptr error){self->disk_busy=false;if(self->stopped)return;if(error){try{std::rethrow_exception(error);}catch(const Fault& f){auto cb=std::exchange(self->transfer_callback,{});cb(f.code,f.message,f.native);}catch(const std::exception& e){auto cb=std::exchange(self->transfer_callback,{});cb(CD_IO_ERROR,e.what(),0);}}else self->drive();});
  }catch(const Fault& f) {auto callback=std::exchange(transfer_callback,{});callback(f.code,f.message,f.native);}
 }
 void drive_transfer() {
  if(disk_busy)return;
  if(transfer_phase==1) {sftp=libssh2_sftp_init(session);if(!sftp) {retry(libssh2_session_last_errno(session));return;}transfer_phase=2;}
  if(transfer_phase==2) {
   auto remote=upload?destination:source;
   auto flags=upload?(LIBSSH2_FXF_WRITE|LIBSSH2_FXF_CREAT|(overwrite?LIBSSH2_FXF_TRUNC:LIBSSH2_FXF_EXCL)):LIBSSH2_FXF_READ;
   file=libssh2_sftp_open_ex(sftp,remote.c_str(),static_cast<unsigned>(remote.size()),flags,0644,LIBSSH2_SFTP_OPENFILE);
   if(!file) {retry(libssh2_session_last_errno(session));return;}transfer_phase=6;
  }
  if(transfer_phase==6){LIBSSH2_SFTP_ATTRIBUTES attributes{};int rc=libssh2_sftp_fstat_ex(file,&attributes,0);if(retry(rc))return;if((attributes.flags&LIBSSH2_SFTP_ATTR_PERMISSIONS)&&!LIBSSH2_SFTP_S_ISREG(attributes.permissions)){report(CD_NOT_SUPPORTED,LIBSSH2_ERROR_SFTP_PROTOCOL);return;}transfer_phase=3;}
  if(transfer_phase==3) {
   // Bound work per event, letting other connections and cancellation run between chunks.
   for(int budget=0;budget<8;++budget) {
    if(upload) {
     if(file_offset==file_size) {disk_busy=true;auto self=std::static_pointer_cast<SshBackend>(shared_from_this());auto stream=local;
      runtime->file_job([self,stream]{stream->read(self->file_buffer.data(),self->file_buffer.size());self->file_size=static_cast<size_t>(stream->gcount());self->file_offset=0;if(stream->bad())throw Fault(CD_IO_ERROR,"Local file read failed");},[self](std::exception_ptr error){self->disk_busy=false;if(self->stopped)return;if(error){self->report(CD_IO_ERROR,0);return;}if(self->file_size==0)self->transfer_phase=4;self->drive();});return;}
     auto n=libssh2_sftp_write(file,file_buffer.data()+file_offset,file_size-file_offset);if(n==LIBSSH2_ERROR_EAGAIN||n==0) {wait_socket();return;}if(n<0) {report(CD_IO_ERROR,static_cast<int>(n));return;}file_offset+=n;transferred+=n;
    }else {auto n=libssh2_sftp_read(file,file_buffer.data(),file_buffer.size());if(n==LIBSSH2_ERROR_EAGAIN) {wait_socket();return;}if(n<0) {report(CD_IO_ERROR,static_cast<int>(n));return;}if(n==0) {transfer_phase=4;break;}disk_busy=true;auto self=std::static_pointer_cast<SshBackend>(shared_from_this());auto stream=local;auto data=std::make_shared<std::string>(file_buffer.data(),n);
     runtime->file_job([stream,data]{stream->write(data->data(),data->size());if(!*stream)throw Fault(CD_IO_ERROR,"Local file write failed");},[self,data](std::exception_ptr error){self->disk_busy=false;if(self->stopped)return;if(error){self->report(CD_IO_ERROR,0);return;}self->transferred+=data->size();self->progress(self->transferred);self->drive();});return;}
    progress(transferred);
   }
   if(transfer_phase==3) {auto self=std::static_pointer_cast<SshBackend>(shared_from_this());asio::post(runtime->state->io,[self]{self->drive();});return;}
  }
  if(transfer_phase==4) {int rc=libssh2_sftp_close(file);if(retry(rc)) return;file=nullptr;transfer_phase=5;}
  if(transfer_phase==5) {int rc=libssh2_sftp_shutdown(sftp);if(retry(rc)) return;sftp=nullptr;disk_busy=true;auto self=std::static_pointer_cast<SshBackend>(shared_from_this());auto stream=local;
   runtime->file_job([stream]{stream->clear();stream->close();if(stream->fail())throw Fault(CD_IO_ERROR,"Local file close failed");},[self](std::exception_ptr error){self->disk_busy=false;if(self->stopped)return;if(error){self->report(CD_IO_ERROR,0);return;}self->local.reset();self->progress(self->transferred);auto cb=std::exchange(self->transfer_callback,{});self->transfer_phase=0;cb(CD_OK,{},0);});}
 }
 void close() override {
  if(stopped) return;stopped=true;resolver.cancel();ErrorCode e;socket.cancel(e);
  // Keep ownership of the descriptor while libssh2 frees channels: those calls
  // can still send packets. Closing it first lets another runtime reuse it.
  socket.shutdown(asio::ip::tcp::socket::shutdown_both,e);
  if(file) {libssh2_sftp_close(file);file=nullptr;}if(sftp) {libssh2_sftp_shutdown(sftp);sftp=nullptr;}
  if(channel) {libssh2_channel_free(channel);channel=nullptr;}if(stdio_channel){libssh2_channel_free(stdio_channel);stdio_channel=nullptr;}if(session) {libssh2_session_free(session);session=nullptr;}socket.close(e);local.reset();
 }
};
std::shared_ptr<Backend> make_ssh(std::shared_ptr<Runtime> r,Config c) {return std::make_shared<SshBackend>(std::move(r),std::move(c));}
}
