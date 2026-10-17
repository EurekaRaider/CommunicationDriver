using CommunicationDriver;
using System.Security.Cryptography;
using System.Text;

static class Program {
 static int assertions;
 static void Expect(bool value,string reason){Interlocked.Increment(ref assertions);if(!value)throw new Exception(reason);}
 static string Q(string value)=>"'"+value.Replace("'","'\\''")+"'";
 static Connection Connect(Driver d,string adb,string serial,string session=null){var c=d.CreateAdb(new AdbOptions {ExecutablePath=adb,DeviceSerial=serial,SessionCommand=session});c.Open();return c;}
 static async Task Device(string adb,string serial,string root,string label){
  using var d=new Driver();using var c=Connect(d,adb,serial);using var observer=Connect(d,adb,serial);
  var remote="/data/local/tmp/cd-hardware-"+Guid.NewGuid().ToString("N");var local=Path.Combine(root,label,Guid.NewGuid().ToString("N"));Directory.CreateDirectory(local);
  try {
   Console.WriteLine("DEVICE_ROOT "+label+" "+remote);await c.ExecuteAsync("mkdir -p "+Q(remote));
   var version=await c.ExecuteAsync("getprop ro.build.version.incremental; dumpsys package android | grep -m 2 -E 'version(Name|Code)='");
   Expect(version.ExitCode==0&&version.Response.Contains("versionName="),"managed software/package version");
   var binary=await c.ExecuteAsync(Encoding.UTF8.GetBytes("printf 'A\\000B'"));Expect(binary.Response.SequenceEqual(new byte[]{65,0,66}),"managed binary response");
   var early=await c.ExecuteUntilAsync("printf READY; sleep 0.2; printf TAIL",Check.Contains("READY"));Expect(early.CompletionReason==CompletionReason.Matched,"managed early match");Expect((await c.ExecuteAsync("printf NEXT")).Response=="NEXT","managed output ownership");
   var queue=Enumerable.Range(0,64).Select(n=>c.ExecuteAsync("printf Q"+n)).ToArray();var replies=await Task.WhenAll(queue);for(int n=0;n<replies.Length;++n)Expect(replies[n].Response=="Q"+n,"managed FIFO");
   using(var logs=Connect(d,adb,serial,"logcat -b main -v raw -s CDHardwareManaged:I")){
    var token=label+"-"+Guid.NewGuid().ToString("N");var pending=logs.ReadUntilStringAsync(ReceiveUntil.Delimiter(token,true));
    await c.ExecuteAsync("log -p i -t CDHardwareManaged "+Q(token));var shortCommand=c.ExecuteAsync("getprop ro.build.version.release");
    Expect((await pending).Contains(token)&&!string.IsNullOrWhiteSpace((await shortCommand).Response),"managed logcat and short command overlap");
   }
   var source=Path.Combine(local,"source 空格'.bin");var pulled=Path.Combine(local,"pull.bin");var bytes=RandomNumberGenerator.GetBytes(8*1024*1024);await File.WriteAllBytesAsync(source,bytes);
   var upload=await c.UploadAsync(source,remote+"/file 空格'.bin");var download=await c.DownloadAsync(remote+"/file 空格'.bin",pulled);
   Expect(upload.BytesTransferred==(ulong)bytes.Length&&download.BytesTransferred==(ulong)bytes.Length,"managed transfer sizes");Expect(SHA256.HashData(await File.ReadAllBytesAsync(pulled)).SequenceEqual(SHA256.HashData(bytes)),"managed SHA256 roundtrip");
   // Observe a nonempty destination before cancelling, so this exercises an active USB transfer.
   var large=Path.Combine(local,"cancel-source.bin");using(var file=File.Create(large)){for(int n=0;n<16;++n){RandomNumberGenerator.Fill(bytes);await file.WriteAsync(bytes);}}
   using(var cancellation=new CancellationTokenSource()){
    var target=remote+"/cancel.bin";var transfer=c.UploadAsync(large,target,cancellationToken:cancellation.Token);bool started=false;
    for(int n=0;n<300&&!transfer.IsCompleted;++n){var probe=await observer.ExecuteAsync("test -s "+Q(target));if(probe.ExitCode==0){started=true;break;}}
    Expect(started&&!transfer.IsCompleted,"observe active push before cancellation");cancellation.Cancel();
    try{await transfer;throw new Exception("active transfer ignored cancellation");}catch(CommOperationCanceledException e){Expect(e.Error.Code==ErrorCode.Cancelled&&e.Error.PartialFile==target,"active push reports cancellation and partial path");}
    await c.CloseAsync();await c.OpenAsync();Expect((await c.ExecuteAsync("printf RECOVERED")).Response=="RECOVERED","managed recovery after transfer cancellation");
   }
   // Pull cancellation also waits until real file data reaches the host.
   await c.UploadAsync(large,remote+"/large.bin");var partial=Path.Combine(local,"partial.bin");
   using(var cancellation=new CancellationTokenSource()){
    var transfer=c.DownloadAsync(remote+"/large.bin",partial,cancellationToken:cancellation.Token);bool started=false;
    for(int n=0;n<1000&&!transfer.IsCompleted;++n){if(File.Exists(partial)&&new FileInfo(partial).Length>0){started=true;break;}await Task.Delay(1);}
    Expect(started&&!transfer.IsCompleted,"observe active pull before cancellation");cancellation.Cancel();
    try{await transfer;throw new Exception("active pull ignored cancellation");}catch(CommOperationCanceledException e){Expect(e.Error.Code==ErrorCode.Cancelled&&e.Error.PartialFile==partial,"active pull reports partial path");}
    await c.CloseAsync();long length=new FileInfo(partial).Length;await c.OpenAsync();await c.ExecuteAsync("printf RECOVERED");Expect(length==new FileInfo(partial).Length&&length<new FileInfo(large).Length,"partial pull stable after close");
   }
   using(var cancellation=new CancellationTokenSource(100)){
    try{await c.ExecuteAsync("printf PART; sleep 1",cancellationToken:cancellation.Token);throw new Exception("managed cancellation ignored");}catch(CommOperationCanceledException e){Expect(e.Error.Code==ErrorCode.Cancelled,"managed command cancellation");}
   }
   await c.CloseAsync();await c.OpenAsync();var closing=Enumerable.Range(0,16).Select(_=>c.CloseAsync()).ToArray();await Task.WhenAll(closing);await c.OpenAsync();Expect((await c.ExecuteAsync("printf END")).Response=="END","managed concurrent close and reopen");
   Console.WriteLine("PASS "+label+" managed ADB, active push/pull cancellation, SHA256 and concurrent lifecycle");
  } finally {await c.CloseAsync();var cleanup=await observer.ExecuteAsync("rm -rf "+Q(remote)+"; test ! -e "+Q(remote));Expect(cleanup.ExitCode==0,"managed temporary directory cleanup");}
 }
 static async Task<int> Main(string[] args){try{var serials=args.Skip(2).Where(x=>x!="--allow-single-device").ToArray();if(serials.Length<(args.Contains("--allow-single-device")?1:2))throw new Exception("Use: <adb> <local-root> <serial>... [--allow-single-device]");await Task.WhenAll(serials.Select((serial,n)=>Device(args[0],serial,args[1],"device-"+(n+1))));Console.WriteLine("PASS managed assertions="+assertions);return 0;}catch(Exception e){Console.Error.WriteLine("FAIL "+e);return 1;}}
}
