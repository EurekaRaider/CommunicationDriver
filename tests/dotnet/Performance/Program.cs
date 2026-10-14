using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using CommunicationDriver;

static class Program {
 static void Expect(bool value,string message){if(!value)throw new Exception(message);}
 static void Measure(string name,int count,Action action){
  for(int i=0;i<10;++i)action();GC.Collect();GC.WaitForPendingFinalizers();
  long allocated=GC.GetAllocatedBytesForCurrentThread();var watch=Stopwatch.StartNew();
  for(int i=0;i<count;++i)action();watch.Stop();allocated=GC.GetAllocatedBytesForCurrentThread()-allocated;
  Console.WriteLine($"METRIC {name} count={count} elapsed_us={watch.Elapsed.TotalMicroseconds:F0} allocated_bytes={allocated}");
 }
 static void Main(){
  var sized=Interop.Sized<NExecutionInfo>();Expect(sized.struct_size==Marshal.SizeOf<NExecutionInfo>(),"ABI struct size");sized.exit_code=123;Expect(Interop.Sized<NExecutionInfo>().exit_code==0,"fresh zeroed struct");
  foreach(var value in new[]{"","a\0b","你好😄",new string('x',256*1024)})using(var arena=new Arena()){var view=arena.Text(value);Expect(Interop.Text(view)==value,"UTF8 round trip including embedded NUL");}
  using(var arena=new Arena()){
   bool rejected=false;try{arena.Text("\ud800");}catch(EncoderFallbackException){rejected=true;}Expect(rejected,"invalid UTF16 rejected");
   var bytes=arena.Bytes(new byte[]{0xff});rejected=false;try{Interop.Text(new NStringView{data=bytes.data,size=bytes.size});}catch(DecoderFallbackException){rejected=true;}Expect(rejected,"invalid UTF8 rejected");
  }
  Expect(Interop.Text(default(NStringView))=="","empty null view");
  bool oversized=false;try{Interop.Text(new NStringView{data=IntPtr.Zero,size=(ulong)int.MaxValue+1});}catch(InvalidOperationException){oversized=true;}Expect(oversized,"oversized native text rejected before pointer access");
  using(var arena=new Arena()){Expect(arena.Text(null).data==IntPtr.Zero,"null input remains absent");var bytes=arena.Bytes(new byte[]{0xf0,0x9f});bool truncated=false;try{Interop.Text(new NStringView{data=bytes.data,size=bytes.size});}catch(DecoderFallbackException){truncated=true;}Expect(truncated,"truncated UTF8 rejected");}
  Parallel.For(0,1000,i=>{using(var arena=new Arena()){string value="并发😄"+i;Expect(Interop.Text(arena.Text(value))==value,"concurrent conversion");Expect(Interop.Sized<NExecutionInfo>().exit_code==0,"concurrent struct initialization");}});
  Measure("sized",1000000,()=>{var v=Interop.Sized<NExecutionInfo>();if(v.struct_size==0)throw new Exception();});
  string input=new string('x',256*1024);
  Measure("encode",128,()=>{using(var arena=new Arena()){var view=arena.Text(input);if(view.size!=(ulong)input.Length)throw new Exception();}});
  using(var arena=new Arena()){var view=arena.Text(input);Measure("decode",128,()=>{var text=Interop.Text(view);if(text.Length!=input.Length)throw new Exception();});}
  Console.WriteLine("PASS managed interop regression");
 }
}
