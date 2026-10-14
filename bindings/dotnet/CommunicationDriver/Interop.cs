using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;
namespace CommunicationDriver {
internal sealed class NativeHandle:SafeHandleZeroOrMinusOneIsInvalid {
 internal readonly int Kind;internal NativeHandle(IntPtr value,int kind):base(true){Kind=kind;SetHandle(value);}
 protected override bool ReleaseHandle(){switch(Kind){case 0:Native.cd_driver_release(handle);break;case 1:Native.cd_connection_release(handle);break;case 2:Native.cd_operation_release(handle);break;case 3:Native.cd_result_release(handle);break;case 4:Native.cd_cancel_source_release(handle);break;}return true;}
}
internal sealed class HandleLease:IDisposable {
 internal NativeHandle Handle;private bool added;internal HandleLease(NativeHandle handle){Handle=handle;handle.DangerousAddRef(ref added);}internal IntPtr Pointer=>Handle.DangerousGetHandle();public void Dispose(){if(added){added=false;Handle.DangerousRelease();}}
}
internal sealed class Arena:IDisposable {
 private readonly List<IntPtr> memory=new List<IntPtr>();
 internal IntPtr Allocate(int size){var p=Marshal.AllocHGlobal(Math.Max(1,size));memory.Add(p);return p;}
 internal NBytesView Bytes(byte[] data){if(data==null)return default(NBytesView);var p=Allocate(data.Length);Marshal.Copy(data,0,p,data.Length);return new NBytesView {data=p,size=(ulong)data.Length};}
 internal unsafe NStringView Text(string s){if(s==null)return default(NStringView);int size=Interop.Utf8.GetByteCount(s);var p=Allocate(size);if(size!=0)fixed(char* chars=s)Interop.Utf8.GetBytes(chars,s.Length,(byte*)p,size);return new NStringView {data=p,size=(ulong)size};}
 internal IntPtr Struct<T>(T value) where T:struct {var p=Allocate(Marshal.SizeOf<T>());Marshal.StructureToPtr(value,p,false);return p;}
 internal IntPtr Array<T>(IList<T> values)where T:struct {int size=Marshal.SizeOf<T>();var p=Allocate(checked(size*values.Count));for(int i=0;i<values.Count;++i)Marshal.StructureToPtr(values[i],IntPtr.Add(p,i*size),false);return p;}
 internal NCheck Check(Check c){var n=Interop.Sized<NCheck>();if(c==null)return n;n.kind=c.Kind;n.is_text=c.Text!=null?1:0;n.text=Text(c.Text);n.bytes=Bytes(c.Bytes);return n;}
 internal NReceiveUntil Rule(ReceiveUntil r){var n=Interop.Sized<NReceiveUntil>();if(r==null)return n;n.kind=r.Kind;n.count=r.Count;n.idle_ms=r.IdleMs;n.is_text=r.Text!=null?1:0;n.text=Text(r.Text);n.bytes=Bytes(r.Data);n.include_delimiter=r.Include?1:0;return n;}
 internal NConnectionOptions Defaults(ConnectionOptions o){return new NConnectionOptions {struct_size=(uint)Marshal.SizeOf<NConnectionOptions>(),encoding=(int)o.Encoding,new_line=Text(o.NewLine),receive_until=Rule(o.ReceiveUntil),connect_timeout_ms=Interop.Milliseconds(o.ConnectTimeout),io_timeout_ms=Interop.Milliseconds(o.IoTimeout),execution_timeout_ms=Interop.Milliseconds(o.ExecutionTimeout),close_timeout_ms=Interop.Milliseconds(o.CloseTimeout),max_response_bytes=o.MaxResponseBytes};}
 internal IntPtr Options(CallOptions o,IntPtr cancel){NCallOptions n;Native.cd_call_options_init(out n);if(o!=null){n.timeout_ms=o.Timeout.HasValue?Interop.Milliseconds(o.Timeout.Value):0;n.encoding=o.Encoding.HasValue?(int)o.Encoding.Value:0;n.new_line=Text(o.NewLine);n.max_response_bytes=o.MaxResponseBytes;n.receive_until=Rule(o.ReceiveUntil);n.read_stream=(int)o.ReadStream;n.check_stream=(int)o.CheckStream;}n.cancellation=cancel;return Struct(n);}
 internal IntPtr Transfer(TransferOptions o,IntPtr cancel){NTransferOptions n;Native.cd_transfer_options_init(out n);if(o!=null){n.timeout_ms=o.Timeout.HasValue?Interop.Milliseconds(o.Timeout.Value):0;n.overwrite=o.Overwrite?1:0;}n.cancellation=cancel;return Struct(n);}
 public void Dispose(){foreach(var p in memory)Marshal.FreeHGlobal(p);memory.Clear();}
}
internal static class Interop {
 internal static readonly UTF8Encoding Utf8=new UTF8Encoding(false,true);
 private static class SizedValue<T> where T:struct {
  internal static readonly T Value=Create();
  private static T Create(){object boxed=default(T);typeof(T).GetField("struct_size",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic).SetValue(boxed,(uint)Marshal.SizeOf<T>());return (T)boxed;}
 }
 internal static T Sized<T>()where T:struct {return SizedValue<T>.Value;}
 internal static ulong Milliseconds(TimeSpan t){if(t.TotalMilliseconds<=0||t.TotalMilliseconds>long.MaxValue)throw new ArgumentOutOfRangeException(nameof(t));return checked((ulong)Math.Ceiling(t.TotalMilliseconds));}
 internal static byte[] Bytes(NBytesView v){if(v.size>int.MaxValue)throw new InvalidOperationException("Native result too large");var b=new byte[(int)v.size];if(b.Length>0)Marshal.Copy(v.data,b,0,b.Length);return b;}
 internal static unsafe string Text(NStringView v){if(v.size>int.MaxValue)throw new InvalidOperationException("Native result too large");return v.size==0?string.Empty:Utf8.GetString((byte*)v.data,(int)v.size);}
 internal static ErrorInfo Error(IntPtr h,int status=0){if(h==IntPtr.Zero)return new ErrorInfo {Code=(ErrorCode)status,Message=Marshal.PtrToStringAnsi(Native.cd_status_name(status)),PartialBytes=new byte[0]};var e=Sized<NErrorInfo>();Native.cd_result_get_error(h,ref e);return new ErrorInfo {Code=(ErrorCode)e.code,Message=Text(e.message),Backend=Text(e.backend),NativeCode=e.native_code,PartialText=Text(e.partial_text),PartialBytes=Bytes(e.partial_bytes),ConfirmedProgress=e.has_confirmed_progress!=0?(ulong?)e.confirmed_progress:null,PartialFile=Text(e.partial_file)};}
 internal static void Checked(int status,IntPtr result=default(IntPtr),object partial=null){if(status==0)return;var e=Error(result,status);if(status==6)throw new CommOperationCanceledException(e,partial);throw new CommException(e,partial);}
 internal static CheckResult CheckResult(IntPtr h){var c=Sized<NCheckResult>();Checked(Native.cd_result_get_check(h,ref c));if(c.has_value==0)return null;var groups=new List<string>();for(ulong i=0;i<c.group_count;++i)groups.Add(Text(Marshal.PtrToStructure<NStringView>(IntPtr.Add(c.groups,checked((int)i*Marshal.SizeOf<NStringView>())))));return new CheckResult {Passed=c.passed!=0,MatchedText=c.matched_text.data!=IntPtr.Zero?Text(c.matched_text):null,MatchedBytes=c.matched_bytes.data!=IntPtr.Zero?Bytes(c.matched_bytes):null,Groups=groups.AsReadOnly(),FailureReason=Text(c.failure_reason)};}
 internal static T Response<T>(IntPtr h,int stream){if(typeof(T)==typeof(string)){NStringView v;Checked(Native.cd_result_get_text(h,stream,out v));return (T)(object)Text(v);}NBytesView b;Checked(Native.cd_result_get_bytes(h,stream,out b));return (T)(object)Bytes(b);}
 internal static ExecutionResult<T> Execution<T>(IntPtr h,bool throwError=true){var info=Sized<NExecutionInfo>();Checked(Native.cd_result_get_execution_info(h,ref info));var result=new ExecutionResult<T> {Response=Response<T>(h,0),StandardError=Response<T>(h,1),Check=CheckResult(h),ExitCode=info.has_exit_code!=0?(int?)info.exit_code:null,CompletionReason=(CompletionReason)info.completion_reason,Elapsed=TimeSpan.FromTicks((long)(info.elapsed_ns/100))};if(throwError)Checked(Native.cd_result_get_status(h),h,result);return result;}
 internal static TransferResult Transfer(IntPtr h,bool throwError=true){var t=Sized<NTransferResult>();Checked(Native.cd_result_get_transfer(h,ref t));var result=new TransferResult {Source=Text(t.source),Destination=Text(t.destination),BytesTransferred=t.has_bytes_transferred!=0?(ulong?)t.bytes_transferred:null,Elapsed=TimeSpan.FromTicks((long)(t.elapsed_ns/100))};if(throwError)Checked(Native.cd_result_get_status(h),h,result);return result;}
 internal static BatchResult Batch(IntPtr h){Checked(Native.cd_result_get_status(h),h);var b=Sized<NBatchInfo>();Checked(Native.cd_result_get_batch_info(h,ref b));var items=new List<BatchItemResult>();for(ulong n=0;n<b.item_count;++n){var i=Sized<NBatchItemResult>();Checked(Native.cd_result_get_batch_item(h,n,ref i));var item=new BatchItemResult {Key=Text(i.key),State=(BatchItemState)i.state,Succeeded=i.succeeded!=0,Elapsed=TimeSpan.FromTicks((long)(i.elapsed_ns/100))};if(i.result!=IntPtr.Zero){var status=Native.cd_result_get_status(i.result);if(status!=0)item.Error=Error(i.result);switch(Native.cd_result_get_kind(i.result)){case 3:item.TextExecution=Execution<string>(i.result,false);break;case 4:item.ByteExecution=Execution<byte[]>(i.result,false);break;case 5:item.Transfer=Transfer(i.result,false);break;}}items.Add(item);}return new BatchResult {Items=items.AsReadOnly(),AllSucceeded=b.all_succeeded!=0,WasCancelled=b.was_cancelled!=0,Elapsed=TimeSpan.FromTicks((long)(b.elapsed_ns/100))};}
 internal delegate int Submit(Arena arena,IntPtr handle,IntPtr cancel,Native.Completion callback,IntPtr user,out IntPtr operation);
 private static readonly Native.Completion Callback=OnComplete;
 private interface IState {void Complete(IntPtr result);}
 private sealed class State<T>:IState {
  internal readonly TaskCompletionSource<T> Promise=new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
  internal Func<IntPtr,T> Convert;internal List<HandleLease> Leases;internal NativeHandle Cancel,Operation;internal CancellationTokenRegistration Registration;internal GCHandle Root;
  private readonly object gate=new object();private bool ready,done,finalized;private T value;private Exception error;
  public void Complete(IntPtr result){try{value=Convert(result);}catch(Exception e){error=e;}lock(gate){done=true;Finish();}}
  internal void Ready(NativeHandle operation){lock(gate){Operation=operation;ready=true;Finish();}}
  private void Finish(){if(!ready||!done||finalized)return;finalized=true;Registration.Dispose();Operation?.Dispose();Cancel.Dispose();foreach(var l in Leases)l.Dispose();Root.Free();if(error==null)Promise.TrySetResult(value);else Promise.TrySetException(error);}
  internal void Reject(Exception e){error=e;lock(gate){ready=done=true;Finish();}}
 }
 private static void OnComplete(IntPtr op,IntPtr result,IntPtr user){try{((IState)GCHandle.FromIntPtr(user).Target).Complete(result);}catch{ /* Never unwind a managed exception across the ABI. */ }}
 internal static Task<T> Start<T>(IList<NativeHandle> handles,CancellationToken token,Submit submit,Func<IntPtr,T> convert){
  var state=new State<T> {Convert=convert,Leases=new List<HandleLease>()};
  try {foreach(var h in handles)state.Leases.Add(new HandleLease(h));IntPtr cancel;Checked(Native.cd_cancel_source_create(out cancel));state.Cancel=new NativeHandle(cancel,4);state.Root=GCHandle.Alloc(state);state.Registration=token.Register(()=>Native.cd_cancel_source_request(cancel));
   IntPtr op;int status;using(var arena=new Arena())status=submit(arena,state.Leases[0].Pointer,cancel,Callback,GCHandle.ToIntPtr(state.Root),out op);
   if(status!=0)state.Reject(new CommException(Error(IntPtr.Zero,status),null));else state.Ready(new NativeHandle(op,2));
  }catch(Exception e){if(state.Cancel!=null&&state.Root.IsAllocated)state.Reject(e);else {foreach(var l in state.Leases)l.Dispose();state.Cancel?.Dispose();state.Promise.TrySetException(e);}}
  return state.Promise.Task;
 }
}
}
