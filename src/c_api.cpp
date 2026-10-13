#include "core.hpp"
#include <set>
#include <cstring>

using namespace cd;
namespace {
template<class T> void retain(T* h) {if(h) h->refs.fetch_add(1,std::memory_order_relaxed);}
template<class T> void release(T* h) {if(h&&h->refs.fetch_sub(1,std::memory_order_acq_rel)==1) delete h;}
void require(bool c,const char* m) {if(!c) throw Fault(CD_INVALID_ARGUMENT,m);}
void text_argument(const std::string& s,const char* message,bool nonempty=true) {require((!nonempty||!s.empty())&&s.find('\0')==std::string::npos,message);decode(s,CD_ENCODING_UTF8);}
cd_status_t exception_result(cd_result_t** out) noexcept {
 std::shared_ptr<Result> r;try {throw;}catch(const Fault& f) {r=error_result(f);}catch(const std::exception& e) {r=error_result(Fault(CD_INTERNAL_ERROR,e.what()));}catch(...) {r=error_result(Fault(CD_INTERNAL_ERROR,"Unknown native exception"));}
 auto status=r->status;if(out) {try {*out=new cd_result(std::move(r));}catch(...) {*out=nullptr;}}return status;
}
cd_status_t make_connection(cd_driver_t* driver,Config config,cd_connection_t** out) {
 require(driver&&out,"Driver and output handle required");*out=nullptr;
 require(config.mode==CD_PERSISTENT||config.mode==CD_ONE_SHOT,"Invalid execution mode");
 require(config.shell>=CD_SHELL_DEFAULT&&config.shell<=CD_SHELL_ZSH,"Invalid shell");
 if(config.backend==3||config.backend==4) {if(!config.session_command.empty()&&config.mode==CD_ONE_SHOT) throw Fault(CD_INVALID_ARGUMENT,"SessionCommand requires Persistent mode");}
 auto c=std::make_shared<Connection>(driver->value,std::move(config));driver->value->register_connection(c);*out=new cd_connection(c);return CD_OK;
}
cd_status_t accept(std::shared_ptr<Operation> op,cd_completion_fn cb,void* user,cd_operation_t** out) {
 require(out,"Operation output required");*out=nullptr;auto handle=new cd_operation(op);retain(handle);
 op->callback=[handle,cb,user](std::shared_ptr<Result> result) {
  cd_result* r=nullptr;
  try {r=new cd_result(std::move(result));if(cb) cb(handle,r,user);}catch(...) { /* Foreign callbacks must not unwind into the event loop. */ }
  release(r);release(handle);
 };
 *out=handle;return CD_OK;
}
std::shared_ptr<Request> request(cd_connection_t* c,Action action,bool text,const cd_call_options_t* options) {
 require(c,"Connection required");auto& defaults=c->value->config.defaults;
 uint64_t timeout=action==Action::Open?defaults.connect_ms:action==Action::Close?defaults.close_ms:(action==Action::Execute||action==Action::ExecuteUntil)?defaults.execution_ms:defaults.io_ms;
 return std::make_shared<Request>(action,text,Options(defaults,options,timeout));
}
cd_status_t submit(cd_connection_t* c,std::shared_ptr<Request> r,cd_completion_fn cb,void* user,cd_operation_t** out) {
 auto op=std::make_shared<Operation>(c->value->runtime);r->operation=op;accept(op,cb,user,out);c->value->submit(std::move(r));return CD_OK;
}
template<class F> cd_status_t synchronous(cd_connection_t* c,cd_result_t** result,F&& start) {
 if(result) *result=nullptr;if(c&&current_io==c->value->runtime->state.get()) {if(result) *result=new cd_result(error_result(Fault(CD_INVALID_CONTEXT,"Synchronous wait inside completion context; use Async")));return CD_INVALID_CONTEXT;}
 require(result,"Result output required");cd_operation_t* op=nullptr;auto status=start(&op);if(status!=CD_OK) {*result=new cd_result(error_result(Fault(status,cd_status_name(status))));return status;}
 status=cd_operation_wait(op,CD_INFINITE,result);release(op);return status==CD_OK?cd_result_get_status(*result):status;
}
struct Batch : std::enable_shared_from_this<Batch> {
 struct Item {std::shared_ptr<Connection> connection;std::shared_ptr<Request> request;std::string key;};
 std::shared_ptr<Runtime> runtime;std::shared_ptr<Operation> operation;std::shared_ptr<Result> result=std::make_shared<Result>();
 std::vector<Item> items;size_t next_index=0,running=0,done=0,max=32;Clock::time_point start=Clock::now();
 std::vector<std::shared_ptr<Operation>> admitted;
 void schedule() {
  if(operation->cancelled) {
   result->was_cancelled=true;
   while(next_index<items.size()) {result->items[next_index].state=CD_BATCH_NOT_STARTED;++next_index;++done;}
   for(auto& op:admitted) if(op&&!op->completed()) op->cancel();
  }
  while(!operation->cancelled&&running<max&&next_index<items.size()) {
   size_t index=next_index++;auto& item=items[index];auto req=item.request;req->started=Clock::now();req->operation=std::make_shared<Operation>(runtime);admitted[index]=req->operation;
   auto self=shared_from_this();req->operation->callback=[self,index](std::shared_ptr<Result> r){
    auto& entry=self->result->items[index];entry.result=std::move(r);entry.state=entry.result->status==CD_OK?CD_BATCH_COMPLETED:entry.result->status==CD_TIMEOUT?CD_BATCH_TIMED_OUT:entry.result->status==CD_CANCELLED?CD_BATCH_CANCELLED:CD_BATCH_FAILED;
    --self->running;++self->done;self->schedule();
   };
   ++running;item.connection->submit(req);
  }
  if(done==items.size()) {result->elapsed_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();operation->complete(result);}
 }
};
}
cd_driver::~cd_driver() {value->close([]{});}
cd_connection::~cd_connection() {auto c=value;asio::post(c->runtime->state->io,[c]{c->force_close();});}
cd_result::cd_result(std::shared_ptr<Result> r):CdHandle(std::move(r)) {
 for(auto& g:value->groups) group_views.push_back(view(g));for(auto& item:value->items) item_handles.push_back(item.result?new cd_result(item.result):nullptr);
}
cd_result::~cd_result() {for(auto* item:item_handles) release(item);}
namespace cd {std::shared_ptr<Cancellation> cancellation(cd_cancel_source_t* c) {return c?c->value:nullptr;}}
extern "C" {
uint32_t CD_CALL cd_abi_version(void) {return CD_ABI_VERSION;}
const char* CD_CALL cd_status_name(cd_status_t s) {static const char* names[]={"OK","InvalidArgument","NotSupported","NotOpen","Closed","Timeout","Cancelled","ConnectionFailed","Disconnected","EndOfStream","AuthenticationFailed","HostKeyRejected","DeviceUnavailable","PermissionDenied","IOError","ProtocolError","EncodingError","LimitExceeded","Busy","NeedsRecovery","InvalidContext","InternalError"};return s>=0&&s<=CD_INTERNAL_ERROR?names[s]:"Unknown";}
void CD_CALL cd_connection_options_init(cd_connection_options_t* o) {if(!o)return;*o={};o->struct_size=sizeof(*o);o->encoding=CD_ENCODING_UTF8;o->new_line=CD_SV("\n");o->receive_until.struct_size=sizeof(cd_receive_until_t);o->connect_timeout_ms=10000;o->io_timeout_ms=5000;o->execution_timeout_ms=30000;o->close_timeout_ms=10000;o->max_response_bytes=1048576;}
#define INIT_BEGIN(name) void CD_CALL cd_##name##_options_init(cd_##name##_options_t* o) {if(!o)return;*o={};o->struct_size=sizeof(*o)
#define INIT_DEFAULTS(name,extra) INIT_BEGIN(name);cd_connection_options_init(&o->defaults);extra;}
INIT_DEFAULTS(tcp, )
INIT_DEFAULTS(udp, )
INIT_DEFAULTS(serial, o->baud_rate=115200;o->data_bits=8)
INIT_DEFAULTS(ssh, o->port=22)
INIT_DEFAULTS(adb, o->server_host=CD_SV("127.0.0.1");o->server_port=5037)
INIT_DEFAULTS(cli, )
INIT_BEGIN(call);o->receive_until.struct_size=sizeof(cd_receive_until_t);}
INIT_BEGIN(transfer);o->overwrite=1;}
INIT_BEGIN(batch);o->max_concurrency=32;}
#undef INIT_BEGIN
#undef INIT_DEFAULTS
static cd_check_t check_value(int kind,bool text,cd_string_view_t s,cd_bytes_view_t b) {return {sizeof(cd_check_t),kind,text,0,s,b};}
cd_check_t CD_CALL cd_check_contains_string(cd_string_view_t v) {return check_value(CD_CHECK_CONTAINS,true,v,{});}
cd_check_t CD_CALL cd_check_contains_bytes(cd_bytes_view_t v) {return check_value(CD_CHECK_CONTAINS,false,{},v);}
cd_check_t CD_CALL cd_check_equals_string(cd_string_view_t v) {return check_value(CD_CHECK_EQUALS,true,v,{});}
cd_check_t CD_CALL cd_check_equals_bytes(cd_bytes_view_t v) {return check_value(CD_CHECK_EQUALS,false,{},v);}
cd_check_t CD_CALL cd_check_regex(cd_string_view_t v) {return check_value(CD_CHECK_REGEX,true,v,{});}
static cd_receive_until_t rule_value(int kind) {cd_receive_until_t r{};r.struct_size=sizeof(r);r.kind=kind;return r;}
cd_receive_until_t CD_CALL cd_receive_until_bytes(uint64_t count) {auto r=rule_value(CD_RECEIVE_BYTES);r.count=count;return r;}
cd_receive_until_t CD_CALL cd_receive_until_characters(uint64_t count) {auto r=rule_value(CD_RECEIVE_CHARACTERS);r.count=count;return r;}
cd_receive_until_t CD_CALL cd_receive_until_delimiter_string(cd_string_view_t v,int32_t include) {auto r=rule_value(CD_RECEIVE_DELIMITER);r.is_text=1;r.text=v;r.include_delimiter=include;return r;}
cd_receive_until_t CD_CALL cd_receive_until_delimiter_bytes(cd_bytes_view_t v,int32_t include) {auto r=rule_value(CD_RECEIVE_DELIMITER);r.bytes=v;r.include_delimiter=include;return r;}
cd_receive_until_t CD_CALL cd_receive_until_line(cd_string_view_t v) {return cd_receive_until_delimiter_string(v.size?v:CD_SV("\n"),0);}
cd_receive_until_t CD_CALL cd_receive_until_pattern(cd_string_view_t v) {auto r=rule_value(CD_RECEIVE_PATTERN);r.is_text=1;r.text=v;return r;}
cd_receive_until_t CD_CALL cd_receive_until_idle(uint64_t interval) {auto r=rule_value(CD_RECEIVE_IDLE);r.idle_ms=interval;return r;}
cd_receive_until_t CD_CALL cd_receive_until_message(void) {return rule_value(CD_RECEIVE_MESSAGE);}
cd_receive_until_t CD_CALL cd_receive_until_available(void) {return rule_value(CD_RECEIVE_AVAILABLE);}
cd_status_t CD_CALL cd_driver_create(cd_driver_t** out) {try {require(out,"Driver output required");*out=new cd_driver(std::make_shared<Runtime>());return CD_OK;}catch(...) {return exception_result(nullptr);}}
#define CREATE_HEAD(name) cd_status_t CD_CALL cd_create_##name(cd_driver_t* d,const cd_##name##_options_t* o,cd_connection_t** out,cd_result_t** error) {if(error)*error=nullptr;if(out)*out=nullptr;try {require(o&&o->struct_size>=sizeof(*o),"Invalid creation options");Config c(o->defaults)
#define CREATE_END return make_connection(d,std::move(c),out);}catch(...) {return exception_result(error);}}
CREATE_HEAD(tcp);c.backend=0;c.host=copy(o->host);c.port=o->port;text_argument(c.host,"Host required");require(c.port,"Port required");CREATE_END
CREATE_HEAD(udp);c.backend=1;c.host=copy(o->remote_host);c.port=o->remote_port;c.local_host=copy(o->local_host);c.local_port=o->local_port;text_argument(c.host,"RemoteHost required");require(c.port,"RemotePort required");text_argument(c.local_host,"Invalid LocalHost",false);CREATE_END
CREATE_HEAD(serial);c.backend=2;c.port_name=copy(o->port_name);c.baud=o->baud_rate;c.bits=o->data_bits;c.parity=o->parity;c.stop=o->stop_bits;c.flow=o->flow_control;text_argument(c.port_name,"PortName required");require(c.baud&&c.bits>=5&&c.bits<=8,"Invalid serial settings");require(c.parity>=0&&c.parity<=2&&c.stop>=0&&c.stop<=2&&c.flow>=0&&c.flow<=2,"Invalid serial setting enum");CREATE_END
CREATE_HEAD(ssh);c.backend=3;c.host=copy(o->host);c.port=o->port;c.user=copy(o->user_name);c.password=copy(o->password);c.private_key=copy(o->private_key_path);c.passphrase=copy(o->passphrase);c.host_key=copy(o->host_key);c.auth=o->auth;c.key_kind=o->host_key_kind;c.shell=o->shell;c.mode=o->execution_mode;c.session_command=copy(o->session_command);text_argument(c.host,"Host required");text_argument(c.user,"UserName required");text_argument(c.host_key,"HostKey trust required");require(c.port&&c.auth>=0&&c.auth<=1&&c.key_kind>=0&&c.key_kind<=1,"Invalid SSH options");if(c.auth==CD_AUTH_PRIVATE_KEY) text_argument(c.private_key,"PrivateKey path required");CREATE_END
CREATE_HEAD(adb);c.backend=4;c.device=copy(o->device_serial);c.executable=copy(o->executable_path);c.server_host=copy(o->server_host);c.server_port=o->server_port;c.mode=o->execution_mode;c.session_command=copy(o->session_command);text_argument(c.device,"DeviceSerial required");text_argument(c.server_host,"ServerHost required");require(c.server_port,"ServerPort required");text_argument(c.executable,"Invalid executable",false);CREATE_END
CREATE_HEAD(cli);c.backend=5;c.shell=o->shell;c.mode=o->execution_mode;c.executable=copy(o->executable_path);c.working_directory=copy(o->working_directory);require(o->arguments||o->argument_count==0,"Arguments pointer required");require(o->environment||o->environment_count==0,"Environment pointer required");for(uint64_t i=0;i<o->argument_count;++i) {auto arg=copy(o->arguments[i]);text_argument(arg,"Invalid argument",false);c.arguments.push_back(std::move(arg));}for(uint64_t i=0;i<o->environment_count;++i) {auto k=copy(o->environment[i].key);auto v=copy(o->environment[i].value);text_argument(k,"Invalid environment key");text_argument(v,"Invalid environment value",false);require(k.find('=')==std::string::npos,"Environment key cannot contain =");c.environment[k]=v;}text_argument(c.executable,"Invalid executable",false);text_argument(c.working_directory,"Invalid working directory",false);CREATE_END
#undef CREATE_HEAD
#undef CREATE_END
cd_state_t CD_CALL cd_connection_get_state(cd_connection_t* c) {return c?c->value->state.load():CD_STATE_CLOSED;}
cd_capabilities_t CD_CALL cd_connection_get_capabilities(cd_connection_t* c) {return c?c->value->capabilities:0;}
cd_status_t CD_CALL cd_driver_close_async(cd_driver_t* d,cd_completion_fn cb,void* user,cd_operation_t** out) {try {require(d,"Driver required");auto op=std::make_shared<Operation>(d->value);accept(op,cb,user,out);d->value->close([op]{op->complete(std::make_shared<Result>());});return CD_OK;}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_driver_close(cd_driver_t* d,cd_result_t** out) {try {require(d&&out,"Driver and result required");*out=nullptr;if(current_io==d->value->state.get()) {*out=new cd_result(error_result(Fault(CD_INVALID_CONTEXT,"Use CloseAsync in a completion callback")));return CD_INVALID_CONTEXT;}cd_operation_t* op=nullptr;auto s=cd_driver_close_async(d,nullptr,nullptr,&op);if(s!=CD_OK)return s;s=cd_operation_wait(op,CD_INFINITE,out);release(op);return s;}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_open_async(cd_connection_t* c, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::Open,false,options); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_open(cd_connection_t* c, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_open_async(c,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_close_async(cd_connection_t* c, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::Close,false,options); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_close(cd_connection_t* c, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_close_async(c,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_close_input_async(cd_connection_t* c, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::CloseInput,false,options); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_close_input(cd_connection_t* c, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_close_input_async(c,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_read_bytes_async(cd_connection_t* c, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::Read,false,options);r->options.rule=Rule(cd_receive_until_available()); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_read_bytes(cd_connection_t* c, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_read_bytes_async(c,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_read_string_async(cd_connection_t* c, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::Read,true,options);r->options.rule=Rule(cd_receive_until_available()); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_read_string(cd_connection_t* c, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_read_string_async(c,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_read_line_async(cd_connection_t* c, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::Read,true,options);r->options.rule=Rule(cd_receive_until_line(view(r->options.newline))); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_read_line(cd_connection_t* c, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_read_line_async(c,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_read_exactly_bytes_async(cd_connection_t* c, uint64_t count, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::Read,false,options);r->options.rule=Rule(cd_receive_until_bytes(count)); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_read_exactly_bytes(cd_connection_t* c, uint64_t count, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_read_exactly_bytes_async(c, count,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_read_exactly_string_async(cd_connection_t* c, uint64_t count, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::Read,true,options);r->options.rule=Rule(cd_receive_until_characters(count)); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_read_exactly_string(cd_connection_t* c, uint64_t count, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_read_exactly_string_async(c, count,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_read_until_bytes_async(cd_connection_t* c, const cd_receive_until_t* until, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {require(until,"ReceiveUntil required");auto r=request(c,Action::Read,false,options);r->options.rule=Rule(*until);require(r->options.rule.kind!=CD_RECEIVE_NONE,"ReceiveUntil required"); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_read_until_bytes(cd_connection_t* c, const cd_receive_until_t* until, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_read_until_bytes_async(c, until,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_read_until_string_async(cd_connection_t* c, const cd_receive_until_t* until, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {require(until,"ReceiveUntil required");auto r=request(c,Action::Read,true,options);r->options.rule=Rule(*until);require(r->options.rule.kind!=CD_RECEIVE_NONE,"ReceiveUntil required"); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_read_until_string(cd_connection_t* c, const cd_receive_until_t* until, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_read_until_string_async(c, until,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_write_bytes_async(cd_connection_t* c, cd_bytes_view_t input, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::Write,false,options);r->input=copy(input); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_write_bytes(cd_connection_t* c, cd_bytes_view_t input, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_write_bytes_async(c, input,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_write_string_async(cd_connection_t* c, cd_string_view_t input, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::Write,true,options);r->input=encode(copy(input),r->options.encoding); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_write_string(cd_connection_t* c, cd_string_view_t input, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_write_string_async(c, input,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_write_line_async(cd_connection_t* c, cd_string_view_t input, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::Write,true,options);r->input=encode(copy(input)+r->options.newline,r->options.encoding); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_write_line(cd_connection_t* c, cd_string_view_t input, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_write_line_async(c, input,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_execute_string_async(cd_connection_t* c, cd_string_view_t input, const cd_check_t* check, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::Execute,true,options);r->input=encode(copy(input),r->options.encoding);r->check=Check(check); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_execute_string(cd_connection_t* c, cd_string_view_t input, const cd_check_t* check, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_execute_string_async(c, input, check,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_execute_bytes_async(cd_connection_t* c, cd_bytes_view_t input, const cd_check_t* check, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::Execute,false,options);r->input=copy(input);r->check=Check(check); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_execute_bytes(cd_connection_t* c, cd_bytes_view_t input, const cd_check_t* check, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_execute_bytes_async(c, input, check,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_execute_until_string_async(cd_connection_t* c, cd_string_view_t input, const cd_check_t* check, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::ExecuteUntil,true,options);r->input=encode(copy(input),r->options.encoding);r->check=Check(check);require(r->check.kind==CD_CHECK_CONTAINS||r->check.kind==CD_CHECK_REGEX,"ExecuteUntil requires Contains or Regex"); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_execute_until_string(cd_connection_t* c, cd_string_view_t input, const cd_check_t* check, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_execute_until_string_async(c, input, check,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_execute_until_bytes_async(cd_connection_t* c, cd_bytes_view_t input, const cd_check_t* check, const cd_call_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::ExecuteUntil,false,options);r->input=copy(input);r->check=Check(check);require(r->check.kind==CD_CHECK_CONTAINS||r->check.kind==CD_CHECK_REGEX,"ExecuteUntil requires Contains or Regex"); return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_execute_until_bytes(cd_connection_t* c, cd_bytes_view_t input, const cd_check_t* check, const cd_call_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_execute_until_bytes_async(c, input, check,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_upload_async(cd_connection_t* c,cd_string_view_t source,cd_string_view_t destination,const cd_transfer_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::Upload,false,nullptr);r->source=copy(source);r->destination=copy(destination);text_argument(r->source,"Source required");text_argument(r->destination,"Destination required");r->options.timeout=c->value->config.defaults.execution_ms;if(options) {require(options->struct_size>=sizeof(*options),"Invalid TransferOptions");r->overwrite=options->overwrite!=0;if(options->timeout_ms)r->options.timeout=options->timeout_ms;r->options.cancellation=cancellation(options->cancellation);}return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_upload(cd_connection_t* c,cd_string_view_t source,cd_string_view_t destination,const cd_transfer_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_upload_async(c,source,destination,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_download_async(cd_connection_t* c,cd_string_view_t source,cd_string_view_t destination,const cd_transfer_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {if(out)*out=nullptr;try {auto r=request(c,Action::Download,false,nullptr);r->source=copy(source);r->destination=copy(destination);text_argument(r->source,"Source required");text_argument(r->destination,"Destination required");r->options.timeout=c->value->config.defaults.execution_ms;if(options) {require(options->struct_size>=sizeof(*options),"Invalid TransferOptions");r->overwrite=options->overwrite!=0;if(options->timeout_ms)r->options.timeout=options->timeout_ms;r->options.cancellation=cancellation(options->cancellation);}return submit(c,std::move(r),cb,user,out);}catch(...) {return exception_result(nullptr);}}
cd_status_t CD_CALL cd_download(cd_connection_t* c,cd_string_view_t source,cd_string_view_t destination,const cd_transfer_options_t* options,cd_result_t** out) {try {return synchronous(c,out,[&](cd_operation_t** op){return cd_download_async(c,source,destination,options,nullptr,nullptr,op);});}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_batch_run_async(cd_driver_t* d,const cd_batch_item_t* input,uint64_t count,const cd_batch_options_t* options,cd_completion_fn cb,void* user,cd_operation_t** out) {
 if(out)*out=nullptr;try {
  require(d&&(input||count==0)&&count<=SIZE_MAX,"Invalid batch input");auto batch=std::make_shared<Batch>();batch->runtime=d->value;batch->operation=std::make_shared<Operation>(d->value);batch->result->kind=CD_RESULT_BATCH;
  std::shared_ptr<Cancellation> cancel;if(options) {require(options->struct_size>=sizeof(*options)&&options->max_concurrency>0,"Invalid BatchOptions");batch->max=options->max_concurrency;cancel=cancellation(options->cancellation);}
  std::set<std::string> keys;
  for(uint64_t i=0;i<count;++i) {
   auto& item=input[i];require(item.struct_size>=sizeof(item)&&item.connection,"Invalid batch item");auto key=copy(item.key);require(!key.empty()&&keys.insert(key).second,"Batch keys must be nonempty and unique");require(item.connection->value->runtime==d->value,"Connections must belong to the same Driver");require(item.connection->value->state==CD_STATE_OPEN,"Batch connections must already be open");
   require(item.kind>=CD_BATCH_EXECUTE_STRING&&item.kind<=CD_BATCH_DOWNLOAD,"Invalid batch action");bool text=item.kind==CD_BATCH_EXECUTE_STRING||item.kind==CD_BATCH_EXECUTE_UNTIL_STRING;
   Action action=item.kind==CD_BATCH_UPLOAD?Action::Upload:item.kind==CD_BATCH_DOWNLOAD?Action::Download:item.kind==CD_BATCH_EXECUTE_UNTIL_STRING||item.kind==CD_BATCH_EXECUTE_UNTIL_BYTES?Action::ExecuteUntil:Action::Execute;
   auto req=request(item.connection,action,text,item.options);req->check=Check(item.check);
   if(action==Action::ExecuteUntil) require(req->check.kind==CD_CHECK_CONTAINS||req->check.kind==CD_CHECK_REGEX,"ExecuteUntil requires Contains or Regex");
   if(action==Action::Upload||action==Action::Download) {req->source=copy(item.source);req->destination=copy(item.destination);text_argument(req->source,"Source required");text_argument(req->destination,"Destination required");if(auto o=item.transfer_options) {require(o->struct_size>=sizeof(*o),"Invalid TransferOptions");req->overwrite=o->overwrite!=0;if(o->timeout_ms) req->options.timeout=o->timeout_ms;req->options.cancellation=cancellation(o->cancellation);}}
   else req->input=text?encode(copy(item.command),req->options.encoding):copy(item.bytes);
   batch->items.push_back({item.connection->value,req,key});batch->result->items.push_back({key,CD_BATCH_NOT_STARTED,{}});
  }
  batch->admitted.resize(static_cast<size_t>(count));accept(batch->operation,cb,user,out);
  auto weak=std::weak_ptr<Batch>(batch);{std::lock_guard lock(batch->operation->mutex);batch->operation->cancel_hook=[weak]{if(auto b=weak.lock()) asio::post(b->runtime->state->io,[b]{b->schedule();});};}
  if(cancel) cancel->subscribe(batch->operation);
  asio::post(d->value->state->io,[batch]{batch->schedule();});return CD_OK;
 }catch(...) {return exception_result(nullptr);}
}
cd_status_t CD_CALL cd_batch_run(cd_driver_t* d,const cd_batch_item_t* items,uint64_t count,const cd_batch_options_t* options,cd_result_t** out) {
 try {require(d&&out,"Driver and result required");*out=nullptr;if(current_io==d->value->state.get()) throw Fault(CD_INVALID_CONTEXT,"Use Batch.RunAsync in completion context");cd_operation_t* op=nullptr;auto s=cd_batch_run_async(d,items,count,options,nullptr,nullptr,&op);if(s!=CD_OK)return s;s=cd_operation_wait(op,CD_INFINITE,out);release(op);return s==CD_OK?cd_result_get_status(*out):s;}catch(...) {return exception_result(out);}
}
cd_status_t CD_CALL cd_response_checker_is_match_string(cd_string_view_t response,const cd_check_t* check,cd_result_t** out) {if(out)*out=nullptr;try {require(out&&check,"Check and result required");auto r=std::make_shared<Result>();r->kind=CD_RESULT_CHECK;r->raw[0]=copy(response);r->evaluate(Check(check),0,CD_ENCODING_UTF8,true);*out=new cd_result(r);return CD_OK;}catch(...) {return exception_result(out);}}
cd_status_t CD_CALL cd_response_checker_is_match_bytes(cd_bytes_view_t response,const cd_check_t* check,cd_result_t** out) {return cd_response_checker_is_match_string({reinterpret_cast<const char*>(response.data),response.size},check,out);}
int32_t CD_CALL cd_operation_is_completed(cd_operation_t* o) {return o&&o->value->completed();}
cd_status_t CD_CALL cd_operation_wait(cd_operation_t* o,uint64_t timeout,cd_result_t** out) {
 if(out)*out=nullptr;try {require(o&&out,"Operation and result required");auto op=o->value;if(current_io==op->runtime->state.get()) return CD_INVALID_CONTEXT;std::unique_lock lock(op->mutex);if(timeout==CD_INFINITE) op->cv.wait(lock,[&]{return bool(op->result);});else if(!op->cv.wait_for(lock,std::chrono::milliseconds(timeout),[&]{return bool(op->result);})) return CD_TIMEOUT;*out=new cd_result(op->result);return CD_OK;}catch(...) {return exception_result(out);}
}
void CD_CALL cd_operation_cancel(cd_operation_t* o) {if(o) o->value->cancel();}
cd_status_t CD_CALL cd_cancel_source_create(cd_cancel_source_t** out) {try {require(out,"Output required");*out=new cd_cancel_source(std::make_shared<Cancellation>());return CD_OK;}catch(...) {return exception_result(nullptr);}}
void CD_CALL cd_cancel_source_request(cd_cancel_source_t* c) {if(c)c->value->request();}
int32_t CD_CALL cd_cancel_source_is_requested(cd_cancel_source_t* c) {return c&&c->value->requested;}
#define LIFETIME(name) void CD_CALL cd_##name##_retain(cd_##name##_t* h) {retain(h);} void CD_CALL cd_##name##_release(cd_##name##_t* h) {release(h);}
LIFETIME(driver) LIFETIME(connection) LIFETIME(operation) LIFETIME(result) LIFETIME(cancel_source)
#undef LIFETIME
cd_status_t CD_CALL cd_result_get_status(const cd_result_t* r) {return r?r->value->status:CD_INVALID_ARGUMENT;}
cd_result_kind_t CD_CALL cd_result_get_kind(const cd_result_t* r) {return r?r->value->kind:CD_RESULT_VOID;}
#define GET_START(type) if(!h||!out||out->struct_size<sizeof(type)) return CD_INVALID_ARGUMENT;auto& r=*h->value;*out={};out->struct_size=sizeof(type)
cd_status_t CD_CALL cd_result_get_error(const cd_result_t* h,cd_error_info_t* out) {GET_START(cd_error_info_t);out->code=r.status;out->message=view(r.message);out->backend=view(r.backend);out->native_code=r.native;out->partial_text=view(r.text[0]);out->partial_bytes=bytes_view(r.raw[0]);out->confirmed_progress=r.progress.value_or(0);out->has_confirmed_progress=r.progress.has_value();out->partial_file=view(r.partial_file);return CD_OK;}
cd_status_t CD_CALL cd_result_get_text(const cd_result_t* h,cd_stream_t stream,cd_string_view_t* out) {if(!h||!out||stream<0||stream>1)return CD_INVALID_ARGUMENT;*out=view(h->value->text[stream]);return CD_OK;}
cd_status_t CD_CALL cd_result_get_bytes(const cd_result_t* h,cd_stream_t stream,cd_bytes_view_t* out) {if(!h||!out||stream<0||stream>1)return CD_INVALID_ARGUMENT;*out=bytes_view(h->value->raw[stream]);return CD_OK;}
cd_status_t CD_CALL cd_result_get_check(const cd_result_t* h,cd_check_result_t* out) {GET_START(cd_check_result_t);out->has_value=r.check_present;out->passed=r.passed;if(r.matched_is_text)out->matched_text=view(r.matched);else out->matched_bytes=bytes_view(r.matched);out->groups=h->group_views.data();out->group_count=h->group_views.size();out->failure_reason=view(r.failure);return CD_OK;}
cd_status_t CD_CALL cd_result_get_execution_info(const cd_result_t* h,cd_execution_info_t* out) {GET_START(cd_execution_info_t);out->completion_reason=r.completion;out->has_exit_code=r.exit_code.has_value();out->exit_code=r.exit_code.value_or(0);out->elapsed_ns=r.elapsed_ns;return CD_OK;}
cd_status_t CD_CALL cd_result_get_transfer(const cd_result_t* h,cd_transfer_result_t* out) {GET_START(cd_transfer_result_t);if(r.kind!=CD_RESULT_TRANSFER)return CD_NOT_SUPPORTED;out->source=view(r.source);out->destination=view(r.destination);out->bytes_transferred=r.progress.value_or(0);out->has_bytes_transferred=r.progress.has_value();out->elapsed_ns=r.elapsed_ns;return CD_OK;}
cd_status_t CD_CALL cd_result_get_batch_info(const cd_result_t* h,cd_batch_info_t* out) {GET_START(cd_batch_info_t);if(r.kind!=CD_RESULT_BATCH)return CD_NOT_SUPPORTED;out->all_succeeded=std::all_of(r.items.begin(),r.items.end(),[](const BatchEntry& e){return e.result&&e.result->succeeded();});out->was_cancelled=r.was_cancelled;out->item_count=r.items.size();out->elapsed_ns=r.elapsed_ns;return CD_OK;}
cd_status_t CD_CALL cd_result_get_batch_item(const cd_result_t* h,uint64_t index,cd_batch_item_result_t* out) {GET_START(cd_batch_item_result_t);if(index>=r.items.size())return CD_INVALID_ARGUMENT;auto& entry=r.items[static_cast<size_t>(index)];out->key=view(entry.key);out->state=entry.state;out->result=h->item_handles[static_cast<size_t>(index)];out->succeeded=entry.result&&entry.result->succeeded();out->elapsed_ns=entry.result?entry.result->elapsed_ns:0;return CD_OK;}
#undef GET_START
}
