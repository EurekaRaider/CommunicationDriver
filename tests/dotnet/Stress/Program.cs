using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Net;
using System.Net.Sockets;
using System.Text;
using CommunicationDriver;

static class Program {
 static readonly string Ready=RuntimeInformation.IsOSPlatform(OSPlatform.Windows)?"echo|set /p=READY":"printf READY";
 static void Expect(bool value,string message){if(!value)throw new Exception(message);}
 static async Task Bounded(Task task){await task.WaitAsync(TimeSpan.FromSeconds(15));}
 static async Task Observe(Task task){try{await task;}catch(CommException e){Expect(e.Error.Code!=ErrorCode.InternalError,"native internal error during cancellation");}catch(CommOperationCanceledException){} }
 static async Task ReadOptionsSnapshot(){
  using var server=new TcpListener(IPAddress.Loopback,0);server.Start();var accepted=server.AcceptTcpClientAsync();
  using var driver=new Driver();using var connection=driver.CreateTcp(new TcpOptions {Host="127.0.0.1",Port=(ushort)((IPEndPoint)server.LocalEndpoint).Port});await Bounded(connection.OpenAsync());using var peer=await accepted;
  for(int kind=0;kind<7;++kind){
   var options=new CallOptions();Task<string> pending=kind switch {
    0=>Text(connection.ReadBytesAsync(options)),1=>connection.ReadStringAsync(options),2=>connection.ReadLineAsync(options),
    3=>Text(connection.ReadExactlyBytesAsync(1,options)),4=>connection.ReadExactlyStringAsync(1,options),
    5=>Text(connection.ReadUntilBytesAsync(ReceiveUntil.Line(),options)),_=>connection.ReadUntilStringAsync(ReceiveUntil.Line(),options)
   };
   options.ReadStream=StreamKind.StandardError;
   await peer.GetStream().WriteAsync(Encoding.UTF8.GetBytes(kind==2||kind>=5?"X\n":"X"));
   await Bounded(pending);Expect(await pending=="X","read stream must use submitted options snapshot: "+kind);
  }
  var head=connection.ReadBytesAsync(new CallOptions {Timeout=TimeSpan.FromMinutes(1)});
  using var cancelled=new CancellationTokenSource();cancelled.Cancel();
  var queued=connection.ReadBytesAsync(cancellationToken:cancelled.Token);
  try{await Bounded(queued);throw new Exception("pre-cancelled managed read succeeded");}catch(CommOperationCanceledException){}
  Expect(!head.IsCompleted,"pre-cancelled managed read leaves active read pending");await connection.CloseAsync();try{await head;}catch(CommException e){Expect(e.Error.Code==ErrorCode.Closed,"active read closed");}
  Console.WriteLine("PASS managed read options snapshot and pre-cancelled queue");
 }
 static async Task<string> Text(Task<byte[]> task)=>Encoding.UTF8.GetString(await task);
 static async Task Storm(){
  using var driver=new Driver();using var connection=driver.CreateCli(new CliOptions());await connection.OpenAsync();
  using var cancel=new CancellationTokenSource();
  var producers=Enumerable.Range(0,8).Select(_=>Task.Run(async()=>{
   var tasks=new List<Task>();
   for(int i=0;i<500;++i){tasks.Add(Observe(connection.ExecuteAsync(Ready,cancellationToken:cancel.Token)));if(i==100)cancel.Cancel();}
   await Task.WhenAll(tasks);
  })).ToArray();
  GC.Collect();GC.WaitForPendingFinalizers();await Bounded(Task.WhenAll(producers));
  await connection.CloseAsync();await connection.OpenAsync();
  Expect((await connection.ExecuteAsync(Ready)).Response=="READY","recovery after managed cancellation storm");
  await Bounded(Task.WhenAll(Enumerable.Range(0,128).Select(_=>connection.CloseAsync())));
 }
 [MethodImpl(MethodImplOptions.NoInlining)]
 static WeakReference[] Churn(){
  var driver=new Driver();var connection=driver.CreateCli(new CliOptions());connection.Open();
  var refs=new[]{new WeakReference(driver),new WeakReference(connection)};connection.Dispose();driver.Dispose();return refs;
 }
 static async Task Main(string[] args){
  var expected=Environment.GetEnvironmentVariable("CD_EXPECT_POINTER_BYTES");if(expected!=null)Expect(IntPtr.Size==int.Parse(expected),"managed stress process bitness");
  await ReadOptionsSnapshot();if(args.Length==1&&args[0]=="--regressions-only")return;
  int rounds=args.Length==0?10:int.Parse(args[0]);var begin=Stopwatch.StartNew();
  await Storm();GC.Collect();GC.WaitForPendingFinalizers();long baseline=GC.GetTotalMemory(true);
  var weak=new List<WeakReference>();
  for(int i=0;i<rounds;++i){await Storm();weak.AddRange(Churn());}
  GC.Collect();GC.WaitForPendingFinalizers();GC.Collect();
  Expect(weak.All(reference=>!reference.IsAlive),"disposed wrappers must not remain rooted");
  Console.WriteLine($"PASS managed_stress operations={(rounds+1)*4000} concurrent_closes={(rounds+1)*128} collected_wrappers={weak.Count} baseline_gc_bytes={baseline} final_gc_bytes={GC.GetTotalMemory(true)} elapsed_ms={begin.ElapsedMilliseconds}");
 }
}
